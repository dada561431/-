/*******************************************************************************
* File Name: secure_http_client.h
*
* Description: This file contains configuration parameters for the secure HTTP
* client along with the HTML page that the client will host.
*
* Related Document: See README.md
********************************************************************************
* (c) 2024-2025, Infineon Technologies AG, or an affiliate of Infineon Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is owned by
* Infineon Technologies AG or one of its affiliates ("Infineon") and is protected
* by and subject to worldwide patent protection, worldwide copyright laws, and
* international treaty provisions. Therefore, you may use this Software only as
* provided in the license agreement accompanying the software package from which
* you obtained this Software. If no license agreement applies, then any use,
* reproduction, modification, translation, or compilation of this Software is
* prohibited without the express written permission of Infineon.
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING,
* BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF THIRD-PARTY RIGHTS AND
* IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A SPECIFIC USE/PURPOSE OR
* MERCHANTABILITY. Infineon reserves the right to make changes to the Software
* without notice. You are responsible for properly designing, programming, and
* testing the functionality and safety of your intended application of the
* Software, as well as complying with any legal requirements related to its
* use. Infineon does not guarantee that the Software will be free from intrusion,
* data theft or loss, or other breaches ("Security Breaches"), and Infineon
* shall have no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any application
* where a failure of the Product or any consequences of the use thereof can
* reasonably be expected to result in personal injury.
*******************************************************************************/

/*******************************************************************************
* Include guard
*******************************************************************************/
#ifndef SECURE_HTTP_CLIENT_H_
#define SECURE_HTTP_CLIENT_H_

/* FreeRTOS header file */
#include "FreeRTOS.h"
#include <stdint.h>
#include <task.h>
#include "cy_http_client_api.h"
#include "cy_wcm.h"
#include "cybsp.h"
#include "cy_network_mw_core.h"

/*******************************************************************************
* Macros
*******************************************************************************/
#define TEST_INFO( x )                           (printf x)

/* Wi-Fi Credentials: Modify WIFI_SSID and WIFI_PASSWORD to match your Wi-Fi
 * network Credentials.
 */
#define WIFI_SSID                                "wzh"
#define WIFI_PASSWORD                            "001234567"

/* Security type of the Wi-Fi access point. See 'cy_wcm_security_t' structure
 * in "cy_wcm.h" for more details.
 */
#define WIFI_SECURITY_TYPE                       CY_WCM_SECURITY_WPA2_AES_PSK
#define MAX_WIFI_RETRY_COUNT                     (3U)
#define APP_INFO(x)                do { printf("Info: "); printf x; } while(0);
#define ERR_INFO(x)                do { printf("Error: "); printf x; } while(0);
#define PRINT_AND_ASSERT(result, msg, args...)   \
                                     do                                 \
                                     {                                  \
                                         if (CY_RSLT_SUCCESS != result) \
                                         {                              \
                                             ERR_INFO((msg, ## args));  \
                                             CY_ASSERT(0);              \
                                         }                              \
                                     } while(0);
#define HTTPS_PORT                               8080
#define HTTPS_SERVER_HOST                        "192.168.118.186"
#define TRANSPORT_SEND_RECV_TIMEOUT_MS           (10000U)
#define HTTP_GET_BUFFER_LENGTH                   (2048U)

/* Wi-Fi re-connection time interval in milliseconds */
#define WIFI_CONN_RETRY_INTERVAL_MSEC            (1000U)
#define HTTP_CONNECT_RETRY_COUNT                 (5U)
#define HTTP_REQUEST_RETRY_COUNT                 (2U)
#define HTTP_RECONNECT_RETRY_DELAY_MSEC          (5000U)

/*End Range until where the data is expected. Set Set this to -1 if requested
 * range is all bytes from the starting
 */
#define HTTP_REQUEST_RANGE_END                   (-1)

/* Start Range from where the server should return. */
#define HTTP_REQUEST_RANGE_START                 (0U)

/* Number of headers in the header list */
#define NUM_HTTP_HEADERS                         (1U)

/* Length of the request header. */
#define HTTP_REQUEST_HEADER_LEN                  (0U)

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
// extern cy_rslt_t boot_http_task();
void https_client_task(void *arg);
cy_rslt_t fetch_https_client_method(cy_http_client_method_t method, const char * path,
                                    const char * req_body, cy_http_client_response_t * resp_body);
cy_rslt_t fetch_https_client_binary_method(cy_http_client_method_t method,
                                           const char *path,
                                           const char *content_type,
                                           const uint8_t *payload,
                                           uint32_t payload_len,
                                           cy_http_client_response_t *resp_body);

#endif /* SECURE_HTTP_CLIENT_H_ */


/* [] END OF FILE */
