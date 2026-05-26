/*******************************************************************************
* File Name        : main.c
*
* Description      : This source file contains the main routine for
*                    application running on CM55 CPU.
*
* Related Document : See README.md
*
********************************************************************************
* (c) 2025-2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "retarget_io_init.h"
#include "vg_lite.h"
#include "vg_lite_platform.h"

#include "FreeRTOS.h"
#include "task.h"
#include "cyabs_rtos.h"
#include "cyabs_rtos_impl.h"
#include "cy_time.h"

#include "lvgl.h"
#include <stdio.h>

#if defined(MTB_DISPLAY_WS7P0DSI_RPI)
#include "mtb_disp_ws7p0dsi_drv.h"
#elif defined(MTB_DISPLAY_EK79007AD3)
#include "mtb_display_ek79007ad3.h"
#elif defined(MTB_DISPLAY_W4P3INCH_RPI)
#include "mtb_disp_dsi_waveshare_4p3.h"
#endif

#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "display_i2c_config.h"
#include "audio.h"
#include "global_constants.h"
#include "shared_memory.h"
#include <stdbool.h>

/*******************************************************************************
* Macros
*******************************************************************************/
#define GPU_INT_PRIORITY                    (3U)
#define DC_INT_PRIORITY                     (3U)

#define GFX_TASK_NAME                       ("CM55 Gfx Task")
/* stack size in words */
#define GFX_TASK_STACK_SIZE                 (configMINIMAL_STACK_SIZE * 16)

#define GFX_TASK_PRIORITY                   (configMAX_PRIORITIES - 1)

#define AUDIO_TASK_NAME                     ("CM55 Audio Task")
#define AUDIO_TASK_STACK_SIZE               (configMINIMAL_STACK_SIZE * 16)
#define AUDIO_TASK_PRIORITY                 (configMAX_PRIORITIES - 2)

#define DISPLAY_STABILIZATION_DELAY_MS      (1000U)
#define DISPLAY_INIT_RETRY_COUNT            (3U)
#define DISPLAY_INIT_RETRY_DELAY_MS         (500U)
#define STATUS_TIMER_PERIOD_MS              (1000U)
#define CM55_GFX_START_DELAY_MS             (3000U)
#define W4P3_DISPLAY_I2C_TARGET_HZ          (100000U)

#define APP_BUFFER_COUNT                    (2U)
/* 64 KB */
#define DEFAULT_GPU_CMD_BUFFER_SIZE         ((64U) * (1024U))


#define GPU_TESSELLATION_BUFFER_SIZE        ((MY_DISP_VER_RES) * 128U)

#define VGLITE_HEAP_SIZE                    (((DEFAULT_GPU_CMD_BUFFER_SIZE) * \
                                              (APP_BUFFER_COUNT)) + \
                                             ((GPU_TESSELLATION_BUFFER_SIZE) * \
                                              (APP_BUFFER_COUNT)))

#define GPU_MEM_BASE                        (0x0U)
#define I2C_CONTROLLER_IRQ_PRIORITY         (2UL)
#define VG_PARAMS_POS                       (0UL)

/* Enabling or disabling a MCWDT requires a wait time of upto 2 CLK_LF cycles
 * to come into effect. This wait time value will depend on the actual CLK_LF
 * frequency set by the BSP.
 */
#define LPTIMER_1_WAIT_TIME_USEC            (62U)

/* Define the LPTimer interrupt priority number. '1' implies highest priority.*/
#define APP_LPTIMER_INTERRUPT_PRIORITY      (1U)

#if LV_USE_DEMO_BENCHMARK
#define BENCHMARK_DONE_CHECK_PERIOD_MS      (500U)
#endif

#if ( configGENERATE_RUN_TIME_STATS == 1 )
#define TCPWM_TIMER_INT_PRIORITY            (1U)
#endif

/*******************************************************************************
* Global Variables
*******************************************************************************/
/* Heap memory for VGLite to allocate memory for buffers, command, and
 * tessellation buffers
 */
CY_SECTION(".cy_gpu_buf") uint8_t contiguous_mem[VGLITE_HEAP_SIZE] = {0xFF};

volatile void *vglite_heap_base = &contiguous_mem;

TaskHandle_t rtos_cm55_gfx_task_handle = NULL;
TaskHandle_t rtos_cm55_audio_task_handle = NULL;
static bool gfxss_initialized = false;
static bool display_i2c_initialized = false;
static lv_obj_t *status_body_label = NULL;
static uint32_t status_elapsed_seconds = 0U;

/* DC IRQ Config */
cy_stc_sysint_t dc_irq_cfg =
{
    .intrSrc      = GFXSS_DC_IRQ,
    .intrPriority = DC_INT_PRIORITY
};

/* GPU IRQ Config */
cy_stc_sysint_t gpu_irq_cfg =
{
    .intrSrc      = GFXSS_GPU_IRQ,
    .intrPriority = GPU_INT_PRIORITY
};

cy_stc_scb_i2c_context_t disp_touch_i2c_controller_context;

cy_stc_sysint_t disp_touch_i2c_controller_irq_cfg =
{
    .intrSrc      = DISPLAY_I2C_CONTROLLER_IRQ,
    .intrPriority = I2C_CONTROLLER_IRQ_PRIORITY,
};

#if defined(MTB_DISPLAY_EK79007AD3)
mtb_display_ek79007ad3_pin_config_t ek79007ad3_pin_cfg =
{
    .reset_port = CYBSP_DISP_RST_PORT,
    .reset_pin  = CYBSP_DISP_RST_PIN,
};
#endif

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
static void cm55_audio_task(void *arg);
static void start_audio_task(void);
static void configure_display_i2c_clock(void);
static void suspend_gfx_task_after_failure(const char *reason);

static void update_hachimitsu_status_text(void)
{
    if (status_body_label != NULL)
    {
        lv_label_set_text_fmt(status_body_label,
                              "LVGL running on CM55\n"
                              "Audio detector active\n"
                              "Uptime: %lu s",
                              (unsigned long)status_elapsed_seconds);
    }
}

static void hachimitsu_status_timer_cb(lv_timer_t *timer)
{
    CY_UNUSED_PARAMETER(timer);

    status_elapsed_seconds++;
    update_hachimitsu_status_text();
}

/*******************************************************************************
* Function Name: create_hachimitsu_status_screen
********************************************************************************
* Summary:
*  Creates a small LVGL screen so the display path can be verified without
*  depending on optional LVGL demo assets.
*
*******************************************************************************/
static void create_hachimitsu_status_screen(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101820), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Hachimitsu Detector");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF8E16C), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -42);

    status_body_label = lv_label_create(scr);
    status_elapsed_seconds = 0U;
    update_hachimitsu_status_text();
    lv_obj_set_style_text_color(status_body_label, lv_color_hex(0xE8EEF2), LV_PART_MAIN);
    lv_obj_set_style_text_align(status_body_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(status_body_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_align(status_body_label, LV_ALIGN_CENTER, 0, 24);

    //(void)lv_timer_create(hachimitsu_status_timer_cb,STATUS_TIMER_PERIOD_MS,NULL);
}

/*******************************************************************************
* Function Name: start_audio_task
********************************************************************************
* Summary:
*  Starts the CM55 audio detector task once. The graphics task calls this only
*  after the display path has settled, so panel probing does not starve audio.
*
*******************************************************************************/
static void start_audio_task(void)
{
    if (NULL == rtos_cm55_audio_task_handle)
    {
        BaseType_t audio_task_return = xTaskCreate(cm55_audio_task,
                                                   AUDIO_TASK_NAME,
                                                   AUDIO_TASK_STACK_SIZE,
                                                   NULL,
                                                   AUDIO_TASK_PRIORITY,
                                                   &rtos_cm55_audio_task_handle);

        if (pdPASS != audio_task_return)
        {
            printf("Error: Failed to create CM55 audio task.\r\n");
            handle_app_error();
        }
    }
}

/*******************************************************************************
* Function Name: configure_display_i2c_clock
********************************************************************************
* Summary:
*  The Waveshare 4.3-inch DSI panel controller supports 100 kHz I2C. The base
*  BSP config uses the same SCB0 controller at a faster rate, so adjust only
*  the display clock divider before initializing the panel bus.
*
*******************************************************************************/
static void configure_display_i2c_clock(void)
{
#if defined(MTB_DISPLAY_W4P3INCH_RPI)
    uint32_t current_divider;
    uint32_t current_pclk_hz;
    uint32_t source_hz;
    uint32_t target_pclk_hz;
    uint32_t new_divider_plus_one;
    uint32_t new_divider;
    uint32_t new_pclk_hz;
    uint32_t oversample =
        (uint32_t)DISPLAY_I2C_CONTROLLER_config.lowPhaseDutyCycle +
        (uint32_t)DISPLAY_I2C_CONTROLLER_config.highPhaseDutyCycle;

    if (0U == oversample)
    {
        oversample = 1U;
    }

    current_divider =
        Cy_SysClk_PeriPclkGetDivider((en_clk_dst_t)DISPLAY_I2C_CONTROLLER_CLK_DIV_GRP_NUM,
                                     DISPLAY_I2C_CONTROLLER_CLK_DIV_HW,
                                     DISPLAY_I2C_CONTROLLER_CLK_DIV_NUM);
    current_pclk_hz =
        Cy_SysClk_PeriPclkGetFrequency((en_clk_dst_t)DISPLAY_I2C_CONTROLLER_CLK_DIV_GRP_NUM,
                                       DISPLAY_I2C_CONTROLLER_CLK_DIV_HW,
                                       DISPLAY_I2C_CONTROLLER_CLK_DIV_NUM);

    source_hz = current_pclk_hz * (current_divider + 1U);
    target_pclk_hz = W4P3_DISPLAY_I2C_TARGET_HZ * oversample;
    new_divider_plus_one = (source_hz + target_pclk_hz - 1U) / target_pclk_hz;
    if (0U == new_divider_plus_one)
    {
        new_divider_plus_one = 1U;
    }
    new_divider = new_divider_plus_one - 1U;

    Cy_SysClk_PeriPclkDisableDivider((en_clk_dst_t)DISPLAY_I2C_CONTROLLER_CLK_DIV_GRP_NUM,
                                     DISPLAY_I2C_CONTROLLER_CLK_DIV_HW,
                                     DISPLAY_I2C_CONTROLLER_CLK_DIV_NUM);
    (void)Cy_SysClk_PeriPclkSetDivider((en_clk_dst_t)DISPLAY_I2C_CONTROLLER_CLK_DIV_GRP_NUM,
                                       DISPLAY_I2C_CONTROLLER_CLK_DIV_HW,
                                       DISPLAY_I2C_CONTROLLER_CLK_DIV_NUM,
                                       new_divider);
    Cy_SysClk_PeriPclkEnableDivider((en_clk_dst_t)DISPLAY_I2C_CONTROLLER_CLK_DIV_GRP_NUM,
                                    DISPLAY_I2C_CONTROLLER_CLK_DIV_HW,
                                    DISPLAY_I2C_CONTROLLER_CLK_DIV_NUM);

    new_pclk_hz =
        Cy_SysClk_PeriPclkGetFrequency((en_clk_dst_t)DISPLAY_I2C_CONTROLLER_CLK_DIV_GRP_NUM,
                                       DISPLAY_I2C_CONTROLLER_CLK_DIV_HW,
                                       DISPLAY_I2C_CONTROLLER_CLK_DIV_NUM);
    printf("[GFX] Display I2C clock pclk %lu -> %lu Hz, bus ~= %lu Hz\r\n",
           (unsigned long)current_pclk_hz,
           (unsigned long)new_pclk_hz,
           (unsigned long)(new_pclk_hz / oversample));
#endif
}

/*******************************************************************************
* Function Name: shutdown_gfx_resources
********************************************************************************
* Summary:
*  Turns off display-side peripherals when LVGL cannot finish initialization.
*
*******************************************************************************/
static void shutdown_gfx_resources(void)
{
    if (display_i2c_initialized)
    {
        NVIC_DisableIRQ(disp_touch_i2c_controller_irq_cfg.intrSrc);
        Cy_SCB_I2C_Disable(DISPLAY_I2C_CONTROLLER_HW,
                           &disp_touch_i2c_controller_context);
        display_i2c_initialized = false;
    }

    if (gfxss_initialized)
    {
        NVIC_DisableIRQ(GFXSS_DC_IRQ);
        NVIC_DisableIRQ(GFXSS_GPU_IRQ);
        Cy_GFXSS_Disable_GPU_Interrupt(GFXSS);
        Cy_GFXSS_Clear_DC_Interrupt(GFXSS, &gfx_context);
        Cy_GFXSS_Clear_GPU_Interrupt(GFXSS, &gfx_context);
        (void)Cy_GFXSS_DeInit(GFXSS, &gfx_context);
        gfxss_initialized = false;
    }
}

/*******************************************************************************
* Function Name: suspend_gfx_task_after_failure
********************************************************************************
* Summary:
*  Keeps CM55 alive when the optional display path cannot initialize. This lets
*  audio detection continue and leaves enough diagnostics on UART to fix the
*  display wiring/configuration.
*
*******************************************************************************/
static void suspend_gfx_task_after_failure(const char *reason)
{
    printf("[GFX] %s\r\n", reason);
    shutdown_gfx_resources();
    printf("[GFX] LVGL display task suspended after cleanup; audio task will continue.\r\n");
    start_audio_task();

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

/* LPTimer HAL object */
static mtb_hal_lptimer_t lptimer_obj;

/* RTC HAL object */
static mtb_hal_rtc_t rtc_obj;

#if (configGENERATE_RUN_TIME_STATS == 1)
/*******************************************************************************
* Function Name: setup_run_time_stats_timer
********************************************************************************
* Summary:
*  This function configuresTCPWM 0 GRP 0 Counter 0 as the timer source for
*  FreeRTOS runtime statistics.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
void setup_run_time_stats_timer(void)
{
    /* Initialze TCPWM block with required timer configuration */
    if (CY_TCPWM_SUCCESS != Cy_TCPWM_Counter_Init(CYBSP_GENERAL_PURPOSE_TIMER_HW,
                                                CYBSP_GENERAL_PURPOSE_TIMER_NUM,
                                           &CYBSP_GENERAL_PURPOSE_TIMER_config))
    {
        handle_app_error();
    }

    /* Enable the initialized counter */
    Cy_TCPWM_Counter_Enable(CYBSP_GENERAL_PURPOSE_TIMER_HW,
                            CYBSP_GENERAL_PURPOSE_TIMER_NUM);

    /* Start the counter */
    Cy_TCPWM_TriggerStart_Single(CYBSP_GENERAL_PURPOSE_TIMER_HW,
                                 CYBSP_GENERAL_PURPOSE_TIMER_NUM);
}


/*******************************************************************************
* Function Name: get_run_time_counter_value
********************************************************************************
* Summary:
*  Function to fetch run time counter value. This will be used by FreeRTOS for
*  run time statistics calculation.
*
* Parameters:
*  void
*
* Return:
*  uint32_t: TCPWM 0 GRP 0 Counter 0 value
*
*******************************************************************************/
uint32_t get_run_time_counter_value(void)
{
   return (Cy_TCPWM_Counter_GetCounter(CYBSP_GENERAL_PURPOSE_TIMER_HW,
                                       CYBSP_GENERAL_PURPOSE_TIMER_NUM));
}


/*******************************************************************************
* Function Name: calculate_idle_percentage
********************************************************************************
* Summary:
*  Function to calculate CPU idle percentage. This function is used by LVGL to
*  showcase CPU usage.
*
* Parameters:
*  void
*
* Return:
*  uint32_t: CPU idle percentage
*
*******************************************************************************/
uint32_t calculate_idle_percentage(void)
{
    static uint32_t previousIdleTime = 0;
    static TickType_t previousTick = 0;
    uint32_t time_diff = 0;
    uint32_t idle_percent = 0;

    uint32_t currentIdleTime = ulTaskGetIdleRunTimeCounter();
    TickType_t currentTick = portGET_RUN_TIME_COUNTER_VALUE();

    time_diff = currentTick - previousTick;

    if((currentIdleTime >= previousIdleTime) && (currentTick > previousTick))
    {
        idle_percent = ((currentIdleTime - previousIdleTime) * 100)/time_diff;
    }

    previousIdleTime = ulTaskGetIdleRunTimeCounter();
    previousTick = portGET_RUN_TIME_COUNTER_VALUE();

    return idle_percent;
}
#endif /* #if configGENERATE_RUN_TIME_STATS == 1 */

/*******************************************************************************
* Function Name: lptimer_interrupt_handler
********************************************************************************
* Summary:
*  Interrupt handler function for LPTimer instance.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void lptimer_interrupt_handler(void)
{
    mtb_hal_lptimer_process_interrupt(&lptimer_obj);
}


/*******************************************************************************
* Function Name: setup_tickless_idle_timer
********************************************************************************
* Summary:
*  1. This function first configures and initializes an interrupt for LPTimer.
*  2. Then it initializes the LPTimer HAL object to be used in the RTOS
*     tickless idle mode implementation to allow the device enter deep sleep
*     when idle task runs. LPTIMER_1 instance is configured for CM55 CPU.
*  3. It then passes the LPTimer object to abstraction RTOS library that
*     implements tickless idle mode
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void setup_tickless_idle_timer(void)
{
    /* Interrupt configuration structure for LPTimer */
    cy_stc_sysint_t lptimer_intr_cfg =
    {
        .intrSrc = CYBSP_CM55_LPTIMER_1_IRQ,
        .intrPriority = APP_LPTIMER_INTERRUPT_PRIORITY
    };

    /* Initialize the LPTimer interrupt and specify the interrupt handler. */
    cy_en_sysint_status_t interrupt_init_status =
                                    Cy_SysInt_Init(&lptimer_intr_cfg,
                                                    lptimer_interrupt_handler);

    /* LPTimer interrupt initialization failed. Stop program execution. */
    if(CY_SYSINT_SUCCESS != interrupt_init_status)
    {
        handle_app_error();
    }

    /* Enable NVIC interrupt. */
    NVIC_EnableIRQ(lptimer_intr_cfg.intrSrc);

    /* Initialize the MCWDT block */
    cy_en_mcwdt_status_t mcwdt_init_status =
                                    Cy_MCWDT_Init(CYBSP_CM55_LPTIMER_1_HW,
                                                &CYBSP_CM55_LPTIMER_1_config);

    /* MCWDT initialization failed. Stop program execution. */
    if(CY_MCWDT_SUCCESS != mcwdt_init_status)
    {
        handle_app_error();
    }

    /* Enable MCWDT instance */
    Cy_MCWDT_Enable(CYBSP_CM55_LPTIMER_1_HW,
                    CY_MCWDT_CTR_Msk,
                    LPTIMER_1_WAIT_TIME_USEC);

    /* Setup LPTimer using the HAL object and desired configuration as defined
     * in the device configurator. */
    cy_rslt_t result = mtb_hal_lptimer_setup(&lptimer_obj,
                                            &CYBSP_CM55_LPTIMER_1_hal_config);

    /* LPTimer setup failed. Stop program execution. */
    if(CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }

    /* Pass the LPTimer object to abstraction RTOS library that implements
     * tickless idle mode
     */
    cyabs_rtos_set_lptimer(&lptimer_obj);
}


/*******************************************************************************
* Function Name: dc_irq_handler
********************************************************************************
* Summary:
*  Display Controller interrupt handler which gets invoked when the DC finishes
*  utilizing the current frame buffer.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void dc_irq_handler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    Cy_GFXSS_Clear_DC_Interrupt(GFXSS, &gfx_context);

    /* Notify the cm55_gfx_task */
    xTaskNotifyFromISR(rtos_cm55_gfx_task_handle, 1, eSetValueWithOverwrite,
                       &xHigherPriorityTaskWoken);

    /* Perform a context switch if a higher-priority task was woken */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}


/*******************************************************************************
* Function Name: gpu_irq_handler
********************************************************************************
* Summary:
*  GPU interrupt handler which gets invoked when the GPU finishes composing
*  a frame.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void gpu_irq_handler(void)
{
    Cy_GFXSS_Clear_GPU_Interrupt(GFXSS, &gfx_context);
    vg_lite_IRQHandler();
}


/*******************************************************************************
* Function Name: disp_touch_i2c_controller_interrupt
********************************************************************************
* Summary:
*  I2C controller ISR which invokes Cy_SCB_I2C_Interrupt to perform I2C transfer
*  as controller.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void disp_touch_i2c_controller_interrupt(void)
{
    Cy_SCB_I2C_Interrupt(DISPLAY_I2C_CONTROLLER_HW,
                         &disp_touch_i2c_controller_context);
}


#if LV_USE_DEMO_BENCHMARK
/*******************************************************************************
* Function Name: check_benchmark_done_cb
********************************************************************************
* Summary:
*  This timer callback polls for the completion of the LVGL benchmark.
*
*  Once all test cases finish, the benchmark demo replaces the screen content
*  with a results table. When the table is detected as the first child of the
*  active screen, the callback re-enables touch input (which was disabled
*  during the benchmark to reduce CPU load) and deletes the one-shot timer.
*
*  Touch input is intentionally disabled while the benchmark is running
*  because the hardware touch controller operates in polling mode, which
*  would add unnecessary CPU overhead.
*
* Parameters:
*  lv_timer_t *t: Pointer to the LVGL timer that triggered this callback
*
* Return:
*  void
*
*******************************************************************************/
static void check_benchmark_done_cb(lv_timer_t *t)
{
    lv_obj_t *scr = lv_screen_active();

    /* The benchmark summary screen places a table as the first child of the
     * active screen once all test cases have completed. */
    if ((lv_obj_get_child_count(scr) > 0) &&
        (lv_obj_check_type(lv_obj_get_child(scr, 0), &lv_table_class)))
    {
        /* Benchmark is done - re-enable touch so the user can scroll results */
        lv_port_indev_init();
        lv_timer_delete(t);
    }
}
#endif


/*******************************************************************************
* Function Name: cm55_gfx_task
********************************************************************************
* Summary:
*   This is the FreeRTOS task callback function.
*   It initializes:
*       - GFX subsystem.
*       - Configure the DC, GPU interrupts.
*       - Initialize I2C interface to be used for touch as well as 7, 4.3-inch 
*         display drivers.
*       - Initializes the display panel selected through Makefile component and
*         vglite driver.
*       - Allocates vglite memory.
*       - Configures LVGL, low level display and touch driver.
*       - Finally invokes the UI application.
*
* Parameters:
*  void *arg: Pointer to the argument passed to the task (not used)
*
* Return:
*  void
*
*******************************************************************************/
static void cm55_gfx_task(void *arg)
{
    CY_UNUSED_PARAMETER(arg);

    uint32_t time_till_next = 0;

    cy_en_sysint_status_t sysint_status = CY_SYSINT_SUCCESS;
    cy_en_gfx_status_t gfx_status = CY_GFX_SUCCESS;
    vg_lite_error_t vglite_status = VG_LITE_SUCCESS;

#if defined(MTB_DISPLAY_WS7P0DSI_RPI)
    cy_rslt_t status = CY_RSLT_SUCCESS;
#elif defined(MTB_DISPLAY_EK79007AD3)
    cy_en_mipidsi_status_t mipi_status = CY_MIPIDSI_SUCCESS;
#endif

    cy_en_scb_i2c_status_t i2c_result = CY_SCB_I2C_SUCCESS;

    vTaskDelay(pdMS_TO_TICKS(CM55_GFX_START_DELAY_MS));
    printf("[GFX] Starting LVGL display initialization.\r\n");

    /* GFXSS init */
    /* MIPI-DSI Display specific configs */
#if defined(MTB_DISPLAY_WS7P0DSI_RPI)
    GFXSS_config.mipi_dsi_cfg = &mtb_disp_ws7p0dsi_dsi_config;
#elif defined(MTB_DISPLAY_EK79007AD3)
    GFXSS_config.mipi_dsi_cfg = &mtb_display_ek79007ad3_mipidsi_config;
#elif defined(MTB_DISPLAY_W4P3INCH_RPI)
    GFXSS_config.mipi_dsi_cfg = &mtb_disp_waveshare_4p3_dsi_config;
#endif

    GFXSS_config.dc_cfg->gfx_layer_config->width  = MY_DISP_HOR_RES;
    GFXSS_config.dc_cfg->gfx_layer_config->height = MY_DISP_VER_RES;
    GFXSS_config.dc_cfg->display_width            = MY_DISP_HOR_RES;
    GFXSS_config.dc_cfg->display_height           = MY_DISP_VER_RES; 

    /* Set frame buffer address to the GFXSS configuration structure */
    GFXSS_config.dc_cfg->gfx_layer_config->buffer_address    = frame_buffer1;
    GFXSS_config.dc_cfg->gfx_layer_config->uv_buffer_address = frame_buffer1;

    /* Initialize Graphics subsystem as per the configuration */
    gfx_status = Cy_GFXSS_Init(GFXSS, &GFXSS_config, &gfx_context);

    if (CY_GFX_SUCCESS == gfx_status)
    {
        gfxss_initialized = true;

        /* Initialize GFXSS DC interrupt */
        sysint_status = Cy_SysInt_Init(&dc_irq_cfg, dc_irq_handler);

        if (CY_SYSINT_SUCCESS != sysint_status)
        {
            printf("Error in registering DC interrupt: %d\r\n", sysint_status);
            suspend_gfx_task_after_failure("Failed to register display-controller interrupt.");
        }

        /* Enable GFX DC interrupt in NVIC. */
        NVIC_EnableIRQ(GFXSS_DC_IRQ);

        /* Initialize GFX GPU interrupt */
        sysint_status = Cy_SysInt_Init(&gpu_irq_cfg, gpu_irq_handler);

        if (CY_SYSINT_SUCCESS != sysint_status)
        {
            printf("Error in registering GPU interrupt: %d\r\n", sysint_status);
            suspend_gfx_task_after_failure("Failed to register GPU interrupt.");
        }

        /* Enable GPU interrupt */
        Cy_GFXSS_Enable_GPU_Interrupt(GFXSS);

        /* Enable GFX GPU interrupt in NVIC. */
        NVIC_EnableIRQ(GFXSS_GPU_IRQ);

        configure_display_i2c_clock();

        /* Initialize the I2C in controller mode. */
        i2c_result = Cy_SCB_I2C_Init(DISPLAY_I2C_CONTROLLER_HW,
                                     &DISPLAY_I2C_CONTROLLER_config,
                                     &disp_touch_i2c_controller_context);

        if (CY_SCB_I2C_SUCCESS != i2c_result)
        {
            printf("I2C controller initialization failed with status = 0x%08lX\r\n",
                   (unsigned long)i2c_result);
            suspend_gfx_task_after_failure("Display/touch I2C initialization failed.");
        }

        /* Initialize the I2C interrupt */
        sysint_status = Cy_SysInt_Init(&disp_touch_i2c_controller_irq_cfg,
                                       &disp_touch_i2c_controller_interrupt);

        if (CY_SYSINT_SUCCESS != sysint_status)
        {
            printf("I2C controller interrupt initialization failed\r\n");
            suspend_gfx_task_after_failure("Display/touch I2C interrupt initialization failed.");
        }

        /* Enable the I2C interrupts. */
        NVIC_EnableIRQ(disp_touch_i2c_controller_irq_cfg.intrSrc);

        /* Enable the I2C */
        Cy_SCB_I2C_Enable(DISPLAY_I2C_CONTROLLER_HW);
        display_i2c_initialized = true;

        vTaskDelay(pdMS_TO_TICKS(DISPLAY_STABILIZATION_DELAY_MS));

#if defined(MTB_DISPLAY_WS7P0DSI_RPI)
        /* Initialize the RPI display */
        status = mtb_disp_ws7p0dsi_panel_init(DISPLAY_I2C_CONTROLLER_HW,
                                              &disp_touch_i2c_controller_context);

        if (CY_RSLT_SUCCESS != status)
        {
            printf("Waveshare 7-Inch R-Pi display init failed with status = %u\r\n", (unsigned int) status);
            suspend_gfx_task_after_failure("Waveshare 7-inch display initialization failed.");
        }

#elif defined(MTB_DISPLAY_EK79007AD3)
        /* Initialize the WF101JTYAHMNB0 display driver. */
        mipi_status = mtb_display_ek79007ad3_init(GFXSS_GFXSS_MIPIDSI,
                                                  &ek79007ad3_pin_cfg);

        if (CY_MIPIDSI_SUCCESS != mipi_status)
        {
            printf("WF101JTYAHMNB0 10-inch display init failed with status = %d\r\n", mipi_status);
            suspend_gfx_task_after_failure("WF101JTYAHMNB0 display initialization failed.");
        }

#elif defined(MTB_DISPLAY_W4P3INCH_RPI)

        for (uint32_t attempt = 1U; attempt <= DISPLAY_INIT_RETRY_COUNT; attempt++)
        {
            /* Initialize the Waveshare 4.3-Inch display */
            i2c_result = mtb_disp_waveshare_4p3_init(DISPLAY_I2C_CONTROLLER_HW,
                                                      &disp_touch_i2c_controller_context);

            if (CY_SCB_I2C_SUCCESS == i2c_result)
            {
                break;
            }

            printf("Waveshare 4.3-Inch display init attempt %lu/%lu failed "
                   "with status = 0x%08lX (%lu)\r\n",
                   (unsigned long)attempt,
                   (unsigned long)DISPLAY_INIT_RETRY_COUNT,
                   (unsigned long)i2c_result,
                   (unsigned long)i2c_result);

            if (attempt < DISPLAY_INIT_RETRY_COUNT)
            {
                vTaskDelay(pdMS_TO_TICKS(DISPLAY_INIT_RETRY_DELAY_MS));
            }
        }

        if (CY_SCB_I2C_SUCCESS != i2c_result)
        {
            suspend_gfx_task_after_failure("Waveshare 4.3-inch display initialization failed.");
        }
#endif
        /* Allocate memory for VGLite from the vglite_heap_base */
        vg_module_parameters_t vg_params;
        vg_params.register_mem_base = (uint32_t)GFXSS_GFXSS_GPU_GCNANO;
        vg_params.gpu_mem_base[VG_PARAMS_POS] = GPU_MEM_BASE;
        vg_params.contiguous_mem_base[VG_PARAMS_POS] = vglite_heap_base;
        vg_params.contiguous_mem_size[VG_PARAMS_POS] = VGLITE_HEAP_SIZE;

        /* Initialize VGlite memory. */
        vg_lite_init_mem(&vg_params);

        /* Initialize the memory and data structures needed for VGLite draw/blit
         * functions
         */
        vglite_status = vg_lite_init((MY_DISP_HOR_RES),
                             (MY_DISP_VER_RES));

        if (VG_LITE_SUCCESS == vglite_status)
        {
            /* Initialize LVGL library */
            lv_init();
            lv_port_disp_init();

            /* Initialize touch input and draw the application status screen. */
            lv_port_indev_init();
            create_hachimitsu_status_screen();
            start_audio_task();
        }
        else
        {
            printf("vg_lite_init failed, status: %d\r\n", vglite_status);

            /* Deallocate all the resources and free up all the memory */
            vg_lite_close();
            suspend_gfx_task_after_failure("VG-Lite initialization failed.");
        }
    }
    else
    {
        printf("Graphics subsystem init failed, status: %d\r\n", gfx_status);
        suspend_gfx_task_after_failure("Graphics subsystem initialization failed.");
    }

    for (;;)
    {
        /* LVGL's timer handler function, to be called periodically to handle
         * LVGL tasks.
         */
        time_till_next = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(time_till_next));
    }
}

/*******************************************************************************
* Function Name: cm55_audio_task
********************************************************************************
* Summary:
*  Initializes the shared memory channel and audio detector, then continuously
*  processes PDM frames on CM55.
*
*******************************************************************************/
static void cm55_audio_task(void *arg)
{
    CY_UNUSED_PARAMETER(arg);

    cy_rslt_t result = CY_RSLT_SUCCESS;

    shared_mem_init();

    result = pdm_init();
    if(CY_RSLT_SUCCESS != result)
    {
        printf("Audio initialization failed, status: %lu\r\n",
               (unsigned long) result);
        handle_app_error();
    }

    printf("========== Audio detector initialized ==========\r\n");
    printf("SAMPLE_RATE: %d\r\n", SAMPLE_RATE);
    printf("STEP_SIZE_SEC: %f\r\n", STEP_SIZE_SEC);
    printf("OUTPUT_THRESHOLD_SCORE: %f\r\n", OUTPUT_THRESHOLD_SCORE);
    printf("WINDOW_SIZE: %f\r\n", WINDOW_SIZE);
    printf("WINDOW_SIZE_SAMPLES: %d\r\n", WINDOW_SIZE_SAMPLES);
    printf("STEP_SIZE_SAMPLES: %d\r\n\r\n", STEP_SIZE_SAMPLES);

    for(;;)
    {
        if(PDM_PCM_DATA_NOT_READY == pdm_data_process())
        {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

/*******************************************************************************
* Function Name: setup_clib_support
********************************************************************************
* Summary:
*    1. This function configures and initializes the Real-Time Clock (RTC)).
*    2. It then initializes the RTC HAL object to enable CLIB support library
*       to work with the provided Real-Time Clock (RTC) module.
*
* Parameters:
*  void
*
* Return:
*  void
*
*******************************************************************************/
static void setup_clib_support(void)
{
    /* RTC Initialization is done in CM33 non-secure project */

    /* Initialize the ModusToolbox CLIB support library */
    mtb_clib_support_init(&rtc_obj);
}


/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
*  This is the main function for CM55 non-secure application.
*    1. It initializes the device and board peripherals.
*    2. It sets up the LPtimer instance for CM55 CPU and initializes debug UART.
*    3. It creates the FreeRTOS application task 'cm55_gfx_task'.
*    4. It starts the RTOS task scheduler.
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result              = CY_RSLT_SUCCESS;
    BaseType_t gfx_task_return    = pdFAIL;

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    if (CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }

    /* Setup CLIB support library. */
    setup_clib_support();
    /* Setup the LPTimer instance for CM55 */
    setup_tickless_idle_timer();

    /* Initialize retarget-io middleware */
    init_retarget_io();

    /* Enable global interrupts */
    __enable_irq();

    /* Create the FreeRTOS Task */
    gfx_task_return = xTaskCreate(cm55_gfx_task, GFX_TASK_NAME,
                                  GFX_TASK_STACK_SIZE, NULL,
                                  GFX_TASK_PRIORITY,
                                  &rtos_cm55_gfx_task_handle);

    if (pdPASS == gfx_task_return)
    {
        printf("****************** "
               "PSOC Edge MCU: Hachimitsu LVGL + Audio "
               "****************** \r\n\n");

        /* Start the RTOS Scheduler */
        vTaskStartScheduler();

        /* Should never get here! */
        handle_app_error();
    }
    else
    {
        printf("Error: Failed to create CM55 RTOS tasks.\r\n");
        handle_app_error();
    }
}


/* [] END OF FILE */
