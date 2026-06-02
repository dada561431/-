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

static uint32_t next_index(uint32_t index)
{
    return (index + 1U) % IPC_MSG_QUEUE_LEN;
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
}

void shared_mem_set_clock(uint8_t hour, uint8_t min, uint8_t sec)
{
    uint32_t sequence;

    if (shared_mem_p == NULL)
    {
        return;
    }

    sequence = shared_mem_p->clock_sequence + 1U;
    shared_mem_p->clock_sequence = sequence;
    __DMB();
    shared_mem_p->clock_hour = hour;
    shared_mem_p->clock_min = min;
    shared_mem_p->clock_sec = sec;
    shared_mem_p->clock_valid = 1U;
    __DMB();
    shared_mem_p->clock_sequence = sequence + 1U;
}

bool shared_mem_get_clock(uint8_t *hour, uint8_t *min, uint8_t *sec)
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
    if (next_write_index == read_index)
    {
        return false;
    }

    shared_mem_p->queue[write_index] = *msg;
    __DMB();
    shared_mem_p->write_index = next_write_index;
    return true;
}
