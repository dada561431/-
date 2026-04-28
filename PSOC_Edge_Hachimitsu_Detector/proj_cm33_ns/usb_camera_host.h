#ifndef USB_CAMERA_HOST_H_
#define USB_CAMERA_HOST_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cy_result.h"

#define USB_CAMERA_FRAME_WIDTH        (320U)
#define USB_CAMERA_FRAME_HEIGHT       (240U)
#define USB_CAMERA_FRAME_BYTES        (USB_CAMERA_FRAME_WIDTH * USB_CAMERA_FRAME_HEIGHT * 2U)
#define USB_CAMERA_SNAPSHOT_WIDTH     (128U)
#define USB_CAMERA_SNAPSHOT_HEIGHT    (96U)
#define USB_CAMERA_SNAPSHOT_BMP_BYTES (54U + (USB_CAMERA_SNAPSHOT_WIDTH * USB_CAMERA_SNAPSHOT_HEIGHT * 3U))

cy_rslt_t usb_camera_host_init(void);
bool usb_camera_host_request_snapshot(uint32_t timeout_ms);
bool usb_camera_host_is_stream_active(void);
bool usb_camera_host_get_snapshot_bmp(uint8_t *out_bmp,
                                      size_t out_bmp_size,
                                      size_t *out_bmp_len,
                                      uint16_t *out_width,
                                      uint16_t *out_height);
bool usb_camera_host_get_latest_yuyv(uint8_t *out_frame,
                                     size_t out_frame_size,
                                     size_t *out_frame_len,
                                     uint16_t *out_width,
                                     uint16_t *out_height,
                                     uint16_t *out_source_stride);
bool usb_camera_host_borrow_latest_yuyv(const uint8_t **out_frame,
                                        size_t *out_frame_len,
                                        uint16_t *out_width,
                                        uint16_t *out_height,
                                        uint16_t *out_source_stride);
void usb_camera_host_release_latest_yuyv(void);

#endif /* USB_CAMERA_HOST_H_ */
