#ifndef CAMERA_CAPTURE_H_
#define CAMERA_CAPTURE_H_

#include <stdbool.h>

#include "cy_result.h"
#include "model/entity.h"
#include "shared_memory.h"

cy_rslt_t camera_capture_init(void);
bool camera_capture_fill_meow_payload(AddMeowDto *dto, const ipc_msg_t *msg);

#endif /* CAMERA_CAPTURE_H_ */
