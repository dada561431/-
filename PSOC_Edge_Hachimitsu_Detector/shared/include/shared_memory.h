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

#define IPC_MSG_QUEUE_LEN                 (8U)
#define IPC_AUDIO_SAMPLE_RATE             (16000U)
#define IPC_AUDIO_BITS_PER_SAMPLE         (16U)
#define IPC_AUDIO_CHANNEL_COUNT           (1U)
#define IPC_AUDIO_WINDOW_MS               (832U)
#define IPC_MSG_AUDIO_SAMPLE_COUNT        ((IPC_AUDIO_SAMPLE_RATE * IPC_AUDIO_WINDOW_MS) / 1000U)

typedef struct {
    int64_t     timestamp;
    float       confidence;
    uint32_t    audio_sample_count;
    uint32_t    audio_sample_rate;
    uint32_t    audio_duration_ms;
    uint16_t    audio_bits_per_sample;
    uint8_t     audio_channel_count;
    uint8_t     request_snapshot;
    int16_t     audio_samples[IPC_MSG_AUDIO_SAMPLE_COUNT];
} ipc_msg_t;

void shared_mem_init(void);
void shared_mem_set_clock(uint8_t hour, uint8_t min, uint8_t sec);
bool shared_mem_get_clock(uint8_t *hour, uint8_t *min, uint8_t *sec);
bool get_msg(ipc_msg_t * msg);
bool write_msg(const ipc_msg_t * msg);

#endif /* SHARED_INCLUDE_SHARED_MEMORY_H_ */
