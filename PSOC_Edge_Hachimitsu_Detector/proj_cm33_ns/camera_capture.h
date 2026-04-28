#ifndef CAMERA_CAPTURE_H_
#define CAMERA_CAPTURE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cy_result.h"
#include "model/entity.h"
#include "shared_memory.h"

cy_rslt_t camera_capture_init(void);
bool camera_capture_fill_meow_payload(AddMeowDto *dto, const ipc_msg_t *msg);
bool camera_capture_get_yuyv_snapshot(const ipc_msg_t *msg,
                                      const uint8_t **out_frame,
                                      size_t *out_frame_len,
                                      uint16_t *out_width,
                                      uint16_t *out_height,
                                      uint16_t *out_source_stride,
                                      bool *out_reused_frame);
void camera_capture_release_yuyv_snapshot(void);

#endif /* CAMERA_CAPTURE_H_ */
