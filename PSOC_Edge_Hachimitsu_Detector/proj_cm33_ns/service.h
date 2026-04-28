/*
 * service.h
 *
 *  Created on: 2025年12月13日
 *      Author: 14838
 */

#ifndef SERVICE_H_
#define SERVICE_H_

#include "cy_http_client_api.h"
#include <stdint.h>


void send_hachimitsu_log();
void sync_rtc();
extern cy_rslt_t fetch_https_client_method(cy_http_client_method_t method, const char * path,
                                           const char * req_body, cy_http_client_response_t * resp_body);
extern cy_rslt_t fetch_https_client_binary_method(cy_http_client_method_t method,
                                                  const char *path,
                                                  const char *content_type,
                                                  const uint8_t *payload,
                                                  uint32_t payload_len,
                                                  cy_http_client_response_t *resp_body);

#endif /* SERVICE_H_ */
