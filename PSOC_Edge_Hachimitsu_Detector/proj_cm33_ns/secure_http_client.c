/*******************************************************************************
* File Name: secure_http_client.c
*******************************************************************************/

#include "cy_result.h"
#include "cy_syslib.h"
#include "cybsp.h"
#include "cy_secure_sockets.h"
#include "cy_tls.h"
#include "cy_wcm.h"
#include "cy_wcm_error.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "secure_http_client.h"
#include "cy_http_client_api.h"
#include "secure_keys.h"
#include "lwip/ip_addr.h"

#include <FreeRTOS.h>
#include <task.h>
#include "retarget_io_init.h"

#include "shared_memory.h"
#include "service.h"

#define LAST_INDEX                      (1U)
#define MEMSET_VAL                      (0U)
#define UART_RESULT_SUCCESS             (1U)
#define APP_SDIO_INTERRUPT_PRIORITY     (7U)
#define APP_HOST_WAKE_INTERRUPT_PRIORITY (2U)
#define APP_SDIO_FREQUENCY_HZ           (25000000U)
#define SDHC_SDIO_64BYTES_BLOCK         (64U)
#define INITIAL_VALUE                   (0U)

static cy_awsport_ssl_credentials_t security_config;
static cy_awsport_server_info_t server_info;
static uint8_t http_get_buffer[HTTP_GET_BUFFER_LENGTH];
static cy_wcm_ip_address_t ip_addr;
static cy_http_client_t https_client;
static mtb_hal_sdio_t sdio_instance;
static cy_stc_sd_host_context_t sdhc_host_context;
static cy_wcm_config_t wcm_config;

static volatile bool server_is_connected = false;
static bool wcm_initialized = false;
static bool http_client_initialized = false;
static bool http_client_created = false;
static volatile bool wifi_reconnect_requested = false;
static TaskHandle_t https_client_task_handle = NULL;

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)
static cy_stc_syspm_callback_params_t sdcardDSParams =
{
    .context = &sdhc_host_context,
    .base = CYBSP_WIFI_SDIO_HW
};

static cy_stc_syspm_callback_t sdhcDeepSleepCallbackHandler =
{
    .callback = Cy_SD_Host_DeepSleepCallback,
    .skipMode = SYSPM_SKIP_MODE,
    .type = CY_SYSPM_DEEPSLEEP,
    .callbackParams = &sdcardDSParams,
    .prevItm = NULL,
    .nextItm = NULL,
    .order = SYSPM_CALLBACK_ORDER
};
#endif

static void disconnect_callback_handler(cy_http_client_t handle,
                                        cy_http_client_disconn_type_t type,
                                        void *args);
static cy_rslt_t send_http_request(cy_http_client_t handle,
                                   cy_http_client_method_t method,
                                   const char *path,
                                   const char *request_body,
                                   cy_http_client_response_t *response);
static cy_rslt_t send_http_request_with_payload(cy_http_client_t handle,
                                                cy_http_client_method_t method,
                                                const char *path,
                                                const char *content_type,
                                                const uint8_t *payload,
                                                uint32_t payload_len,
                                                cy_http_client_response_t *response);
static cy_rslt_t configure_https_client(void);
static cy_rslt_t wifi_connect(void);
static cy_rslt_t connect_to_server(void);
static void cleanup_http_client(void);
static cy_rslt_t ensure_connection_ready(void);
static void wifi_event_callback(cy_wcm_event_t event, cy_wcm_event_data_t *event_data);
static void request_reconnect(const char *reason);

static void sdio_interrupt_handler(void)
{
    mtb_hal_sdio_process_interrupt(&sdio_instance);
}

static void host_wake_interrupt_handler(void)
{
    mtb_hal_gpio_process_interrupt(&wcm_config.wifi_host_wake_pin);
}

static void app_sdio_init(void)
{
    cy_rslt_t result;
    mtb_hal_sdio_cfg_t sdio_hal_cfg;
    cy_stc_sysint_t sdio_intr_cfg =
    {
        .intrSrc = CYBSP_WIFI_SDIO_IRQ,
        .intrPriority = APP_SDIO_INTERRUPT_PRIORITY
    };
    cy_stc_sysint_t host_wake_intr_cfg =
    {
        .intrSrc = CYBSP_WIFI_HOST_WAKE_IRQ,
        .intrPriority = APP_HOST_WAKE_INTERRUPT_PRIORITY
    };

    printf("[WIFI] SDIO_INIT_BEGIN\n");
    cy_en_sysint_status_t interrupt_init_status = Cy_SysInt_Init(&sdio_intr_cfg,
                                                                 sdio_interrupt_handler);
    if (CY_SYSINT_SUCCESS != interrupt_init_status)
    {
        handle_app_error();
    }
    NVIC_EnableIRQ(CYBSP_WIFI_SDIO_IRQ);
    printf("[WIFI] SDIO_IRQ_READY\n");

    result = mtb_hal_sdio_setup(&sdio_instance,
                                &CYBSP_WIFI_SDIO_sdio_hal_config,
                                NULL,
                                &sdhc_host_context);
    if (CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }
    printf("[WIFI] SDIO_HAL_READY\n");

    Cy_SD_Host_Enable(CYBSP_WIFI_SDIO_HW);
    Cy_SD_Host_Init(CYBSP_WIFI_SDIO_HW,
                    CYBSP_WIFI_SDIO_sdio_hal_config.host_config,
                    &sdhc_host_context);
    Cy_SD_Host_SetHostBusWidth(CYBSP_WIFI_SDIO_HW, CY_SD_HOST_BUS_WIDTH_4_BIT);
    sdio_hal_cfg.frequencyhal_hz = APP_SDIO_FREQUENCY_HZ;
    sdio_hal_cfg.block_size = SDHC_SDIO_64BYTES_BLOCK;
    mtb_hal_sdio_configure(&sdio_instance, &sdio_hal_cfg);
    printf("[WIFI] SDIO_HOST_READY\n");

    mtb_hal_gpio_setup(&wcm_config.wifi_wl_pin,
                       CYBSP_WIFI_WL_REG_ON_PORT_NUM,
                       CYBSP_WIFI_WL_REG_ON_PIN);
    mtb_hal_gpio_setup(&wcm_config.wifi_host_wake_pin,
                       CYBSP_WIFI_HOST_WAKE_PORT_NUM,
                       CYBSP_WIFI_HOST_WAKE_PIN);
    printf("[WIFI] SDIO_GPIO_READY\n");

    cy_en_sysint_status_t interrupt_init_status_host_wake =
        Cy_SysInt_Init(&host_wake_intr_cfg, host_wake_interrupt_handler);
    if (CY_SYSINT_SUCCESS != interrupt_init_status_host_wake)
    {
        handle_app_error();
    }
    NVIC_EnableIRQ(CYBSP_WIFI_HOST_WAKE_IRQ);
    printf("[WIFI] SDIO_INIT_DONE\n");
}

static void print_ip_address(const cy_wcm_ip_address_t *address)
{
    if (address == NULL)
    {
        printf("[WIFI] IP=UNKNOWN\n");
        return;
    }

    if (CY_WCM_IP_VER_V4 == address->version)
    {
        printf("[WIFI] IP=%s\n", ip4addr_ntoa((const ip4_addr_t *)&address->ip.v4));
    }
    else if (CY_WCM_IP_VER_V6 == address->version)
    {
        printf("[WIFI] IP=%s\n", ip6addr_ntoa((const ip6_addr_t *)&address->ip.v6));
    }
    else
    {
        printf("[WIFI] IP=UNKNOWN\n");
    }
}

static void request_reconnect(const char *reason)
{
    if (reason != NULL)
    {
        printf("[WIFI] RECONNECT_REQUESTED: %s\n", reason);
    }

    if (https_client_task_handle != NULL)
    {
        xTaskNotifyGive(https_client_task_handle);
    }
}

static void wifi_event_callback(cy_wcm_event_t event, cy_wcm_event_data_t *event_data)
{
    switch (event)
    {
        case CY_WCM_EVENT_CONNECTED:
            printf("[WIFI] CONNECTED\n");
            wifi_reconnect_requested = false;
            request_reconnect("Wi-Fi connected");
            break;

        case CY_WCM_EVENT_RECONNECTED:
            printf("[WIFI] RECONNECTED\n");
            server_is_connected = false;
            wifi_reconnect_requested = false;
            request_reconnect("Wi-Fi reconnected");
            break;

        case CY_WCM_EVENT_IP_CHANGED:
            printf("[WIFI] IP_CHANGED\n");
            if (event_data != NULL)
            {
                print_ip_address(&event_data->ip_addr);
            }
            break;

        case CY_WCM_EVENT_DISCONNECTED:
            printf("[WIFI] DISCONNECTED\n");
            server_is_connected = false;
            wifi_reconnect_requested = true;
            request_reconnect("Wi-Fi disconnected");
            break;

        case CY_WCM_EVENT_CONNECT_FAILED:
            printf("[WIFI] CONNECT_FAILED\n");
            wifi_reconnect_requested = true;
            request_reconnect("Wi-Fi connect failed");
            break;

        default:
            break;
    }
}

static cy_rslt_t wifi_connect(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    uint32_t retry_count;
    cy_wcm_connect_params_t connect_param =
    {
        .ap_credentials = {{INITIAL_VALUE}},
        .BSSID = {INITIAL_VALUE},
        .static_ip_settings = NULL,
        .band = (cy_wcm_wifi_band_t)INITIAL_VALUE,
        .itwt_profile = CY_WCM_ITWT_PROFILE_NONE
    };

    if (wcm_initialized && !wifi_reconnect_requested && (cy_wcm_is_connected_to_ap() != 0U))
    {
        printf("[WIFI] ALREADY_CONNECTED\n");
        return CY_RSLT_SUCCESS;
    }

    if (!wcm_initialized)
    {
#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)
        Cy_SysPm_RegisterCallback(&sdhcDeepSleepCallbackHandler);
#endif
        app_sdio_init();
        wcm_config.interface = CY_WCM_INTERFACE_TYPE_STA;
        wcm_config.wifi_interface_instance = &sdio_instance;

        printf("[WIFI] WCM_INIT_BEGIN\n");
        result = cy_wcm_init(&wcm_config);
        printf("[WIFI] WCM_INIT_DONE status=0x%08lX\n", (unsigned long)result);
        if (CY_RSLT_SUCCESS != result)
        {
            printf("[HTTP_TASK] Wi-Fi Connection Manager initialization failed!\n");
            handle_app_error();
        }

        wcm_initialized = true;
        cy_wcm_register_event_callback(wifi_event_callback);
        APP_INFO(("Wi-Fi initialization is successful\n"));
        printf("[WIFI] INIT_OK\n");
    }

    memcpy(&connect_param.ap_credentials.SSID, WIFI_SSID, sizeof(WIFI_SSID));
    memcpy(&connect_param.ap_credentials.password, WIFI_PASSWORD, sizeof(WIFI_PASSWORD));
    connect_param.ap_credentials.security = WIFI_SECURITY_TYPE;
    APP_INFO(("Join to AP: %s\n", connect_param.ap_credentials.SSID));
    printf("[WIFI] CONNECTING SSID=%s\n", connect_param.ap_credentials.SSID);

    for (retry_count = INITIAL_VALUE; retry_count < MAX_WIFI_RETRY_COUNT; retry_count++)
    {
        printf("[WIFI] CONNECT_ATTEMPT=%lu/%u\n",
               (unsigned long)(retry_count + 1U),
               MAX_WIFI_RETRY_COUNT);
        result = cy_wcm_connect_ap(&connect_param, &ip_addr);
        if (CY_RSLT_SUCCESS == result)
        {
            APP_INFO(("Successfully joined Wi-Fi network %s\n", connect_param.ap_credentials.SSID));
            printf("[WIFI] CONNECTED SSID=%s\n", connect_param.ap_credentials.SSID);
            print_ip_address(&ip_addr);
            wifi_reconnect_requested = false;
            return CY_RSLT_SUCCESS;
        }

        ERR_INFO(("Failed to join Wi-Fi network. Retrying...\n"));
        printf("[WIFI] CONNECT_RETRY\n");
        vTaskDelay(pdMS_TO_TICKS(WIFI_CONN_RETRY_INTERVAL_MSEC));
    }

    return result;
}

static void disconnect_callback_handler(cy_http_client_t handle,
                                        cy_http_client_disconn_type_t type,
                                        void *args)
{
    bool wifi_connected;

    (void)args;
    printf("[HTTP_TASK] Application Disconnect callback triggered for handle = %p type=%d\n",
           handle,
           type);
    (void)handle;
    printf("[HTTP_TASK] Deferring disconnect cleanup to reconnect task\n");
    printf("[HTTP_TASK] Application Disconnect!\n");

    server_is_connected = false;
    wifi_connected = (wcm_initialized && (cy_wcm_is_connected_to_ap() != 0U));
    if (type == CY_HTTP_CLIENT_DISCONN_TYPE_NETWORK_DOWN)
    {
        if (!wifi_connected)
        {
            wifi_reconnect_requested = true;
            request_reconnect("HTTP network down and Wi-Fi disconnected");
        }
        else
        {
            wifi_reconnect_requested = false;
            request_reconnect("HTTP network down but Wi-Fi still connected");
        }
    }
    else
    {
        wifi_reconnect_requested = false;
        request_reconnect("HTTP disconnected");
    }
}

static cy_rslt_t send_http_request(cy_http_client_t handle,
                                   cy_http_client_method_t method,
                                   const char *path,
                                   const char *request_body,
                                   cy_http_client_response_t *response)
{
    if ((request_body == NULL) || (*request_body == '\0'))
    {
        request_body = "{}";
    }

    return send_http_request_with_payload(handle,
                                          method,
                                          path,
                                          "application/json",
                                          (const uint8_t *)request_body,
                                          (uint32_t)strlen(request_body),
                                          response);
}

static cy_rslt_t send_http_request_with_payload(cy_http_client_t handle,
                                                cy_http_client_method_t method,
                                                const char *path,
                                                const char *content_type,
                                                const uint8_t *payload,
                                                uint32_t payload_len,
                                                cy_http_client_response_t *response)
{
    cy_http_client_request_header_t request;
    cy_http_client_header_t header;
    cy_rslt_t http_status;

    if ((content_type == NULL) || (*content_type == '\0'))
    {
        content_type = "application/octet-stream";
    }

    request.buffer = http_get_buffer;
    request.buffer_len = HTTP_GET_BUFFER_LENGTH;
    request.headers_len = HTTP_REQUEST_HEADER_LEN;
    request.method = method;
    request.range_end = HTTP_REQUEST_RANGE_END;
    request.range_start = HTTP_REQUEST_RANGE_START;
    request.resource_path = path;

    header.field = "Content-Type";
    header.field_len = sizeof("Content-Type") - LAST_INDEX;
    header.value = content_type;
    header.value_len = strlen(content_type);

    http_status = cy_http_client_write_header(handle, &request, &header, NUM_HTTP_HEADERS);
    if (CY_RSLT_SUCCESS != http_status)
    {
        printf("[HTTP_TASK] Write Header ----------- Fail\n");
        return http_status;
    }

    http_status = cy_http_client_send(handle,
                                      &request,
                                      (uint8_t *)payload,
                                      payload_len,
                                      response);
    if (CY_RSLT_SUCCESS != http_status)
    {
        printf("[HTTP_TASK] Failed to send HTTP method=%d Error=%ld\r\n",
               request.method,
               (unsigned long)http_status);
    }

    return http_status;
}

static cy_rslt_t configure_https_client(void)
{
    cy_rslt_t result;
    cy_http_disconnect_callback_t http_cb;

    (void)memset(&security_config, MEMSET_VAL, sizeof(security_config));
    (void)memset(&server_info, MEMSET_VAL, sizeof(server_info));

    security_config.client_cert = (const char *)&keyCLIENT_CERTIFICATE_PEM;
    security_config.client_cert_size = sizeof(keyCLIENT_CERTIFICATE_PEM);
    security_config.private_key = (const char *)&keyCLIENT_PRIVATE_KEY_PEM;
    security_config.private_key_size = sizeof(keyCLIENT_PRIVATE_KEY_PEM);
    security_config.root_ca = (const char *)&keySERVER_ROOTCA_PEM;
    security_config.root_ca_size = sizeof(keySERVER_ROOTCA_PEM);
    server_info.host_name = HTTPS_SERVER_HOST;
    server_info.port = HTTPS_PORT;

    if (!http_client_initialized)
    {
        result = cy_http_client_init();
        if (CY_RSLT_SUCCESS != result)
        {
            ERR_INFO(("Failed to initialize http client.\n"));
            return result;
        }
        http_client_initialized = true;
    }

    http_cb = disconnect_callback_handler;
    result = cy_http_client_create(&security_config, &server_info, http_cb, NULL, &https_client);
    if (CY_RSLT_SUCCESS != result)
    {
        ERR_INFO(("Failed to create http client.\n"));
    }
    else
    {
        http_client_created = true;
    }

    return result;
}

static void cleanup_http_client(void)
{
    if (http_client_created)
    {
        cy_http_client_disconnect(https_client);
        cy_http_client_delete(https_client);
        https_client = NULL;
        http_client_created = false;
    }

    if (http_client_initialized)
    {
        cy_http_client_deinit();
        http_client_initialized = false;
    }

    server_is_connected = false;
}

static cy_rslt_t connect_to_server(void)
{
    cy_rslt_t result;
    uint32_t i;

    cleanup_http_client();
    result = configure_https_client();
    if (CY_RSLT_SUCCESS != result)
    {
        return result;
    }

    for (i = 0U; i < HTTP_CONNECT_RETRY_COUNT; i++)
    {
        result = cy_http_client_connect(https_client,
                                        TRANSPORT_SEND_RECV_TIMEOUT_MS,
                                        TRANSPORT_SEND_RECV_TIMEOUT_MS);
        if (CY_RSLT_SUCCESS == result)
        {
            server_is_connected = true;
            return CY_RSLT_SUCCESS;
        }

        ERR_INFO(("Failed to connect to the http server. Retry %lu times...\n",
                  (unsigned long)(i + 1U)));
        vTaskDelay(pdMS_TO_TICKS(WIFI_CONN_RETRY_INTERVAL_MSEC));
    }

    ERR_INFO(("Failed to connect to the http server.\n"));
    cleanup_http_client();
    return result;
}

static cy_rslt_t ensure_connection_ready(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    if ((!wcm_initialized) || wifi_reconnect_requested || (cy_wcm_is_connected_to_ap() == 0U))
    {
        cleanup_http_client();
        printf("[WIFI] RECONNECTING_TO_AP\n");
        result = wifi_connect();
        if (CY_RSLT_SUCCESS != result)
        {
            return result;
        }
    }

    if (!server_is_connected)
    {
        printf("[HTTP_TASK] RECONNECTING_TO_SERVER\n");
        result = connect_to_server();
        if (CY_RSLT_SUCCESS != result)
        {
            return result;
        }
    }

    return CY_RSLT_SUCCESS;
}

void https_client_task(void *arg)
{
    cy_rslt_t result;
    (void)arg;

    https_client_task_handle = xTaskGetCurrentTaskHandle();
    result = ensure_connection_ready();
    PRINT_AND_ASSERT(result, "Initial network connection failed.\n");

    printf("Successfully connected to http server\r\n");
    sync_rtc();

    while (true)
    {
        result = ensure_connection_ready();
        if (CY_RSLT_SUCCESS != result)
        {
            ERR_INFO(("Network is unavailable, retrying later.\n"));
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(HTTP_RECONNECT_RETRY_DELAY_MSEC));
            continue;
        }

        send_hachimitsu_log();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
    }
}

cy_rslt_t fetch_https_client_method(cy_http_client_method_t method, const char *path,
                                    const char *req_body, cy_http_client_response_t *resp)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    uint32_t attempt;

    for (attempt = 0U; attempt < HTTP_REQUEST_RETRY_COUNT; attempt++)
    {
        result = ensure_connection_ready();
        if (CY_RSLT_SUCCESS != result)
        {
            ERR_INFO(("Failed to establish network before HTTP request.\n"));
            continue;
        }

        result = send_http_request(https_client, method, path, req_body, resp);
        if (CY_RSLT_SUCCESS == result)
        {
            printf("ResponseBody: %s\r\n", resp->body);
            return CY_RSLT_SUCCESS;
        }

        cleanup_http_client();
    }

    ERR_INFO(("Failed to send the http request after retries.\n"));
    return result;
}

cy_rslt_t fetch_https_client_binary_method(cy_http_client_method_t method,
                                           const char *path,
                                           const char *content_type,
                                           const uint8_t *payload,
                                           uint32_t payload_len,
                                           cy_http_client_response_t *resp)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    uint32_t attempt;

    if ((payload == NULL) || (payload_len == 0U))
    {
        printf("[HTTP_TASK] Binary request payload is empty\n");
        return CY_RSLT_TYPE_ERROR;
    }

    for (attempt = 0U; attempt < HTTP_REQUEST_RETRY_COUNT; attempt++)
    {
        result = ensure_connection_ready();
        if (CY_RSLT_SUCCESS != result)
        {
            ERR_INFO(("Failed to establish network before binary HTTP request.\n"));
            continue;
        }

        result = send_http_request_with_payload(https_client,
                                                method,
                                                path,
                                                content_type,
                                                payload,
                                                payload_len,
                                                resp);
        if (CY_RSLT_SUCCESS == result)
        {
            printf("ResponseBody: %s\r\n", resp->body);
            return CY_RSLT_SUCCESS;
        }

        cleanup_http_client();
    }

    ERR_INFO(("Failed to send the binary http request after retries.\n"));
    return result;
}

/* [] END OF FILE */
