/*
 * shared_memory.h
 *
 *  Created on: 2025骞?2鏈?2鏃?
 *      Author: 14838
 */

#ifndef SHARED_INCLUDE_SHARED_MEMORY_H_
#define SHARED_INCLUDE_SHARED_MEMORY_H_

#include <stdbool.h>
#include <stdint.h>

#define IPC_MSG_QUEUE_LEN  (64U)

typedef struct {
    float       confidence;
    int64_t     timestamp;
    uint8_t     request_snapshot;
} ipc_msg_t;

void shared_mem_init();
bool get_msg(ipc_msg_t ** msg);
bool write_msg(ipc_msg_t * msg);

#endif /* SHARED_INCLUDE_SHARED_MEMORY_H_ */

