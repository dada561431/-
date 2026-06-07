/*
 * shared_memory.c
 */
#include "shared_memory.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <cybsp.h>

#if defined(CYMEM_CM33_0_m33_m55_shared_START) && defined(CYMEM_CM33_0_m33_m55_shared_SIZE)
#define SHARED_MEM_REGION_START  (CYMEM_CM33_0_m33_m55_shared_START)
#define SHARED_MEM_REGION_SIZE   (CYMEM_CM33_0_m33_m55_shared_SIZE)
#elif defined(CYMEM_CM55_0_m33_m55_shared_START) && defined(CYMEM_CM55_0_m33_m55_shared_SIZE)
#define SHARED_MEM_REGION_START  (CYMEM_CM55_0_m33_m55_shared_START)
#define SHARED_MEM_REGION_SIZE   (CYMEM_CM55_0_m33_m55_shared_SIZE)
#else
#define SHARED_MEM_REGION_START  (CYMEM_CM33_0_m55_allocatable_shared_START)
#define SHARED_MEM_REGION_SIZE   (CYMEM_CM33_0_m55_allocatable_shared_SIZE)
#endif

typedef struct {
    volatile uint32_t clock_sequence;
    volatile uint8_t clock_hour;
    volatile uint8_t clock_min;
    volatile uint8_t clock_sec;
    volatile uint8_t clock_valid;
    volatile uint32_t write_index;
    volatile uint32_t read_index;
    ipc_msg_t queue[IPC_MSG_QUEUE_LEN];
} shared_mem_t;

static shared_mem_t *shared_mem_p;

#define CLOCK_SEQUENCE_COUNTER_MASK       (0xFFU)
#define CLOCK_SEQUENCE_BUSY_FLAG          (1U)
#define CLOCK_SEQUENCE_DATE_SHIFT         (8U)
#define CLOCK_SEQUENCE_YEAR_BASE          (2000U)
#define CLOCK_SEQUENCE_YEAR_BITS          (7U)
#define CLOCK_SEQUENCE_YEAR_MAX           \
    (CLOCK_SEQUENCE_YEAR_BASE + ((1U << CLOCK_SEQUENCE_YEAR_BITS) - 1U))
#define CLOCK_SEQUENCE_YEAR_SHIFT         (9U)
#define CLOCK_SEQUENCE_MONTH_SHIFT        (5U)
#define CLOCK_SEQUENCE_MONTH_MASK         (0x0FU)
#define CLOCK_SEQUENCE_DAY_MASK           (0x1FU)

static uint32_t next_index(uint32_t index)
{
    return (index + 1U) % IPC_MSG_QUEUE_LEN;
}

static uint32_t encode_clock_date(uint16_t year, uint8_t month, uint8_t day)
{
    uint32_t year_offset;

    if ((year < CLOCK_SEQUENCE_YEAR_BASE) ||
        (year > CLOCK_SEQUENCE_YEAR_MAX) ||
        (month < 1U) ||
        (month > 12U) ||
        (day < 1U) ||
        (day > 31U))
    {
        return 0U;
    }

    year_offset = (uint32_t)year - CLOCK_SEQUENCE_YEAR_BASE;
    return ((year_offset << CLOCK_SEQUENCE_YEAR_SHIFT) |
            ((uint32_t)month << CLOCK_SEQUENCE_MONTH_SHIFT) |
            (uint32_t)day);
}

static void decode_clock_date(uint32_t sequence, uint16_t *year,
                              uint8_t *month, uint8_t *day)
{
    uint32_t date_code = sequence >> CLOCK_SEQUENCE_DATE_SHIFT;
    uint32_t year_offset = date_code >> CLOCK_SEQUENCE_YEAR_SHIFT;
    uint32_t local_month = (date_code >> CLOCK_SEQUENCE_MONTH_SHIFT) &
                           CLOCK_SEQUENCE_MONTH_MASK;
    uint32_t local_day = date_code & CLOCK_SEQUENCE_DAY_MASK;

    if ((year_offset >= (1U << CLOCK_SEQUENCE_YEAR_BITS)) ||
        (local_month < 1U) ||
        (local_month > 12U) ||
        (local_day < 1U) ||
        (local_day > 31U))
    {
        if (year != NULL)
        {
            *year = 0U;
        }
        if (month != NULL)
        {
            *month = 0U;
        }
        if (day != NULL)
        {
            *day = 0U;
        }
        return;
    }

    if (year != NULL)
    {
        *year = (uint16_t)(CLOCK_SEQUENCE_YEAR_BASE + year_offset);
    }
    if (month != NULL)
    {
        *month = (uint8_t)local_month;
    }
    if (day != NULL)
    {
        *day = (uint8_t)local_day;
    }
}

static uint32_t next_clock_counter(uint32_t sequence)
{
    return ((sequence & CLOCK_SEQUENCE_COUNTER_MASK) + 2U) &
           (CLOCK_SEQUENCE_COUNTER_MASK & ~CLOCK_SEQUENCE_BUSY_FLAG);
}

static bool queue_indices_valid(uint32_t read_index, uint32_t write_index)
{
    return ((read_index < IPC_MSG_QUEUE_LEN) &&
            (write_index < IPC_MSG_QUEUE_LEN));
}

static void reset_queue_indices(void)
{
    shared_mem_p->read_index = 0U;
    shared_mem_p->write_index = 0U;
    __DMB();
}

void shared_mem_init(void)
{
    if (sizeof(shared_mem_t) > SHARED_MEM_REGION_SIZE)
    {
        shared_mem_p = NULL;
        printf("shared_mem_t too large: %" PRIu32 " > %" PRIu32 "\n",
               (uint32_t)sizeof(shared_mem_t),
               (uint32_t)SHARED_MEM_REGION_SIZE);
        return;
    }

    shared_mem_p = (shared_mem_t *)((uintptr_t)SHARED_MEM_REGION_START +
                                    (uintptr_t)SHARED_MEM_REGION_SIZE -
                                    sizeof(shared_mem_t));
#if defined(ML_DEEPCRAFT_CM33)
    memset(shared_mem_p, 0, sizeof(shared_mem_t));
#endif
    printf("init_shared_addr_at: %p, bytes=%" PRIu32 "\n",
           (void *) shared_mem_p,
           (uint32_t)sizeof(shared_mem_t));

    if (!queue_indices_valid(shared_mem_p->read_index,
                             shared_mem_p->write_index))
    {
        reset_queue_indices();
    }
}

void shared_mem_set_clock(uint8_t hour, uint8_t min, uint8_t sec)
{
    uint32_t current_sequence;
    uint32_t sequence_base;
    uint32_t sequence;

    if (shared_mem_p == NULL)
    {
        return;
    }

    current_sequence = shared_mem_p->clock_sequence;
    sequence_base = (current_sequence & ~CLOCK_SEQUENCE_COUNTER_MASK) |
                    next_clock_counter(current_sequence);
    sequence = sequence_base | CLOCK_SEQUENCE_BUSY_FLAG;
    shared_mem_p->clock_sequence = sequence;
    __DMB();
    shared_mem_p->clock_hour = hour;
    shared_mem_p->clock_min = min;
    shared_mem_p->clock_sec = sec;
    shared_mem_p->clock_valid = 1U;
    __DMB();
    shared_mem_p->clock_sequence = sequence_base;
}

void shared_mem_set_datetime(uint16_t year, uint8_t month, uint8_t day,
                             uint8_t hour, uint8_t min, uint8_t sec)
{
    uint32_t current_sequence;
    uint32_t date_code;
    uint32_t date_bits;
    uint32_t sequence_base;
    uint32_t sequence;

    if (shared_mem_p == NULL)
    {
        return;
    }

    current_sequence = shared_mem_p->clock_sequence;
    date_code = encode_clock_date(year, month, day);
    if (date_code != 0U)
    {
        date_bits = date_code << CLOCK_SEQUENCE_DATE_SHIFT;
    }
    else
    {
        date_bits = current_sequence & ~CLOCK_SEQUENCE_COUNTER_MASK;
    }

    sequence_base = date_bits | next_clock_counter(current_sequence);
    sequence = sequence_base | CLOCK_SEQUENCE_BUSY_FLAG;
    shared_mem_p->clock_sequence = sequence;
    __DMB();
    shared_mem_p->clock_hour = hour;
    shared_mem_p->clock_min = min;
    shared_mem_p->clock_sec = sec;
    shared_mem_p->clock_valid = 1U;
    __DMB();
    shared_mem_p->clock_sequence = sequence_base;
}

bool shared_mem_get_clock(uint8_t *hour, uint8_t *min, uint8_t *sec)
{
    return shared_mem_get_datetime(NULL, NULL, NULL, hour, min, sec);
}

bool shared_mem_get_datetime(uint16_t *year, uint8_t *month, uint8_t *day,
                             uint8_t *hour, uint8_t *min, uint8_t *sec)
{
    uint32_t sequence_before;
    uint32_t sequence_after;
    uint8_t local_hour;
    uint8_t local_min;
    uint8_t local_sec;

    if ((shared_mem_p == NULL) ||
        (hour == NULL) ||
        (min == NULL) ||
        (sec == NULL) ||
        (shared_mem_p->clock_valid == 0U))
    {
        return false;
    }

    sequence_before = shared_mem_p->clock_sequence;
    if ((sequence_before & 1U) != 0U)
    {
        return false;
    }

    __DMB();
    local_hour = shared_mem_p->clock_hour;
    local_min = shared_mem_p->clock_min;
    local_sec = shared_mem_p->clock_sec;
    __DMB();
    sequence_after = shared_mem_p->clock_sequence;

    if ((sequence_before != sequence_after) || ((sequence_after & 1U) != 0U))
    {
        return false;
    }

    decode_clock_date(sequence_after, year, month, day);
    *hour = local_hour;
    *min = local_min;
    *sec = local_sec;
    return true;
}

bool get_msg(ipc_msg_t *msg)
{
    uint32_t read_index;
    uint32_t write_index;

    if ((shared_mem_p == NULL) || (msg == NULL))
    {
        return false;
    }

    read_index = shared_mem_p->read_index;
    write_index = shared_mem_p->write_index;
    if (!queue_indices_valid(read_index, write_index))
    {
        reset_queue_indices();
        return false;
    }

    if (read_index == write_index)
    {
        return false;
    }

    __DMB();
    *msg = shared_mem_p->queue[read_index];
    __DMB();
    shared_mem_p->read_index = next_index(read_index);
    return true;
}

bool write_msg(const ipc_msg_t *msg)
{
    uint32_t write_index;
    uint32_t next_write_index;
    uint32_t read_index;

    if ((shared_mem_p == NULL) || (msg == NULL))
    {
        return false;
    }

    write_index = shared_mem_p->write_index;
    next_write_index = next_index(write_index);
    read_index = shared_mem_p->read_index;
    if (!queue_indices_valid(read_index, write_index))
    {
        reset_queue_indices();
        write_index = 0U;
        next_write_index = next_index(write_index);
        read_index = 0U;
    }

    if (next_write_index == read_index)
    {
        return false;
    }

    shared_mem_p->queue[write_index] = *msg;
    __DMB();
    shared_mem_p->write_index = next_write_index;
    return true;
}
