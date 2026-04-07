/*
 * shared_memory.c
 */
#include "shared_memory.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cybsp.h>

#define SHARED_MEM_DATA_ADDR     (CYMEM_CM33_0_m55_allocatable_shared_START + CYMEM_CM33_0_m55_allocatable_shared_SIZE - (sizeof(shared_mem_t)))

typedef struct {
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
    shared_mem_p = (shared_mem_t *) SHARED_MEM_DATA_ADDR;
    memset(shared_mem_p, 0, sizeof(shared_mem_t));
    printf("init_shared_addr_at: %p\n", (void *) shared_mem_p);
}

bool get_msg(ipc_msg_t **msg)
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

    *msg = (ipc_msg_t *) malloc(sizeof(ipc_msg_t));
    if (*msg == NULL)
    {
        return false;
    }

    __DMB();
    **msg = shared_mem_p->queue[read_index];
    __DMB();
    shared_mem_p->read_index = next_index(read_index);
    return true;
}

bool write_msg(ipc_msg_t *msg)
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
