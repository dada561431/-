/******************************************************************************
* File Name:   audio.c
*
* Description: This file implements the interface with the PDM, as
*              well as the PDM ISR to feed the audio processing block.
*
* Related Document: See README.md
*
*
********************************************************************************
* (c) 2024-2025, Infineon Technologies AG, or an affiliate of Infineon
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
#include "LED.h"
#include "cybsp.h"
#include "cy_pdl.h"
#include <math.h>
#include "audio.h"

/******************************************************************************
 * 鐚彨浠诲姟闇€瑕佸紩鍏ョ殑澶存枃浠? *****************************************************************************/
#include "global_constants.h"
#include "arm_math.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
// #include "ipc_communication.h"
// #include "ipc_pipe.h"
#include "shared_memory.h"

/******************************************************************************
 * Macros
 *****************************************************************************/
/* PDM PCM interrupt priority */
#define PDM_PCM_ISR_PRIORITY                    (2u)

/* Channel Index */
#define PDM_CHANNEL                             (3u)

/* PDM PCM hardware FIFO size */
#define HW_FIFO_SIZE                            (64u)

/* Rx FIFO trigger level/threshold configured by user */
#define RX_FIFO_TRIG_LEVEL                      (HW_FIFO_SIZE/2)

/* Total number of interrupts to get the FRAME_SIZE number of samples*/
#define NUMBER_INTERRUPTS_FOR_FRAME             (FRAME_SIZE/RX_FIFO_TRIG_LEVEL)

/* Specifies the dynamic range in bits.
 * PCM word length, see the A/D specific documentation for valid ranges. */
#define AUIDO_BITS_PER_SAMPLE                  16

/* Converts given audio sample into range [-1,1] */
#define SAMPLE_NORMALIZE(sample)                (((float) (sample)) / (float) (1 << (AUIDO_BITS_PER_SAMPLE - 1)))

#define PDM_PCM_GAIN                            (CY_PDM_PCM_SEL_GAIN_5DB)

/******************************************************************************
 * Global Variables
 *****************************************************************************/
/* Set up one buffer for data collection and one for processing */
static int16_t audio_buffer0[FRAME_SIZE] = {0};
static int16_t audio_buffer1[FRAME_SIZE] = {0};
static int16_t* active_rx_buffer;
static int16_t* full_rx_buffer;

/* PDM PCM interrupt configuration parameters */
static const cy_stc_sysint_t PDM_IRQ_cfg =
{
    .intrSrc = (IRQn_Type)CYBSP_PDM_CHANNEL_3_IRQ,
    .intrPriority = PDM_PCM_ISR_PRIORITY
};

/* Flag to check if the data from PDM/PCM block is ready for processing. */
static volatile bool pdm_pcm_flag;

/*******************************************************************************
* Local Function Prototypes
*******************************************************************************/
static void pdm_pcm_event_handler(void);

/*******************************************************************************
* Function Definitions
*******************************************************************************/

/*******************************************************************************
* Function Name: pdm_init
********************************************************************************
* Summary:
*  A function used to initialize and configure the PDM. Sets up an interrupt
*  to trigger when the PDM FIFO level passes the trigger level.
*
* Parameters:
*  None
*
* Return:
*  The status of the initialization.
*
*******************************************************************************/
static float audio_window[WINDOW_SIZE_SAMPLES]; // Sliding PCM window
static int window_filled = 0;                   // Number of valid samples in the window
static uint32_t report_cooldown_windows_remaining = 0;
static uint32_t image_capture_cooldown_windows_remaining = 0;
static uint32_t throttled_detection_counter = 0;
static volatile uint32_t high_confidence_upload_count = 0U;
static ipc_msg_t detection_msg;

static void fill_ipc_audio_payload(ipc_msg_t *msg)
{
    uint32_t sample_count = (uint32_t)WINDOW_SIZE_SAMPLES;

    if (msg == NULL)
    {
        return;
    }

    if (sample_count > IPC_MSG_AUDIO_SAMPLE_COUNT)
    {
        sample_count = IPC_MSG_AUDIO_SAMPLE_COUNT;
    }

    msg->audio_sample_count = sample_count;
    msg->audio_sample_rate = IPC_AUDIO_SAMPLE_RATE;
    msg->audio_duration_ms = (sample_count * 1000U) / IPC_AUDIO_SAMPLE_RATE;
    msg->audio_bits_per_sample = IPC_AUDIO_BITS_PER_SAMPLE;
    msg->audio_channel_count = IPC_AUDIO_CHANNEL_COUNT;

    for (uint32_t i = 0; i < sample_count; i++)
    {
        float sample = audio_window[i];

        if (sample > 32767.0f)
        {
            sample = 32767.0f;
        }
        else if (sample < -32768.0f)
        {
            sample = -32768.0f;
        }

        msg->audio_samples[i] = (int16_t)sample;
    }
}

cy_rslt_t pdm_init(void)
{
    cy_rslt_t result;

    /* 鍒濆鍖栭煶棰戠紦鍐插尯 */
    memset(audio_buffer0, 0, FRAME_SIZE * sizeof(int16_t));
    memset(audio_buffer1, 0, FRAME_SIZE * sizeof(int16_t));
    active_rx_buffer = audio_buffer0;
    full_rx_buffer = audio_buffer1;
    pdm_pcm_flag = false;
    window_filled = 0;
    report_cooldown_windows_remaining = 0;
    image_capture_cooldown_windows_remaining = 0;
    throttled_detection_counter = 0;
    high_confidence_upload_count = 0U;
    memset(audio_window, 0, sizeof(audio_window));

    /* 鍒濆鍖?PDM PCM 纭欢 */
    result = Cy_PDM_PCM_Init(CYBSP_PDM_HW, &CYBSP_PDM_config);
    if (result != CY_PDM_PCM_SUCCESS)
        return result;

    Cy_PDM_PCM_Channel_Enable(CYBSP_PDM_HW, PDM_CHANNEL); 
    Cy_PDM_PCM_Channel_Init(CYBSP_PDM_HW, &channel_3_config, (uint8_t)PDM_CHANNEL);
    Cy_PDM_PCM_SetGain(CYBSP_PDM_HW, PDM_CHANNEL, PDM_PCM_GAIN);

    /* 娓呴櫎骞跺惎鐢ㄤ腑鏂?*/
    Cy_PDM_PCM_Channel_ClearInterrupt(CYBSP_PDM_HW, PDM_CHANNEL, CY_PDM_PCM_INTR_MASK);
    Cy_PDM_PCM_Channel_SetInterruptMask(CYBSP_PDM_HW, PDM_CHANNEL, CY_PDM_PCM_INTR_MASK);
    Cy_SysInt_Init(&PDM_IRQ_cfg, &pdm_pcm_event_handler);
    NVIC_ClearPendingIRQ(PDM_IRQ_cfg.intrSrc);
    NVIC_EnableIRQ(PDM_IRQ_cfg.intrSrc);

    /* 鍒濆鍖栫尗鍙娴嬫ā鍨?*/
    if (IMAI_init() != 0)
        return CY_RSLT_TYPE_ERROR;

    Cy_PDM_PCM_Activate_Channel(CYBSP_PDM_HW, PDM_CHANNEL);

    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
* Function Name: pdm_pcm_event_handler
********************************************************************************
* Summary:
*  PDM/PCM ISR handler. Check the interrupt status and clears it.
*  Fills a buffer and then swaps that buffer with an empty one.
*  Once a buffer is full, a flag is set which is used in main.
*
* Parameters:
*  None
*
* Return:
*  None
*
*******************************************************************************/
static uint32_t overrun_counter = 0;
static void pdm_pcm_event_handler(void)
{
    /* Used to track how full the buffer is */
    static uint16_t frame_counter = 0;

    /* Check the interrupt status */
    uint32_t intr_status = Cy_PDM_PCM_Channel_GetInterruptStatusMasked(CYBSP_PDM_HW, PDM_CHANNEL);
    if(CY_PDM_PCM_INTR_RX_TRIGGER & intr_status)
    {
        /* Move data from the PDM fifo and place it in a buffer */
        for(uint32_t index=0; index < RX_FIFO_TRIG_LEVEL; index++)
        {
            int32_t data = (int32_t)Cy_PDM_PCM_Channel_ReadFifo(CYBSP_PDM_HW, PDM_CHANNEL);
            active_rx_buffer[frame_counter * RX_FIFO_TRIG_LEVEL + index] = (int16_t)(data);
        }
        Cy_PDM_PCM_Channel_ClearInterrupt(CYBSP_PDM_HW, PDM_CHANNEL, CY_PDM_PCM_INTR_RX_TRIGGER);
        frame_counter++;
    }
    /* Check if the buffer is full */
    if((NUMBER_INTERRUPTS_FOR_FRAME) <= frame_counter)
    {
		// 鍒ゆ柇娑堣垂鑰呮槸鍚﹀凡娑堣垂
		if (pdm_pcm_flag) {
	        // 涓婁竴甯ц繕娌″鐞嗗畬锛屽張閲囬泦鍒版柊甯?=> 娑堣垂婊炲悗
	        overrun_counter++;
	        printf("[Hachimitsu Detector] WARNING! Audio overrun: consumer too slow! count=%lu\n", (unsigned long) overrun_counter);
	    }

        /* Flip the active and the next rx buffers */
        int16_t* temp = active_rx_buffer;
        active_rx_buffer = full_rx_buffer;
        full_rx_buffer = temp;

        /* Set the PDM_PCM flag as true, signaling there is data ready for use */
        pdm_pcm_flag = true;
        frame_counter = 0;
    }

    if((CY_PDM_PCM_INTR_RX_FIR_OVERFLOW | CY_PDM_PCM_INTR_RX_OVERFLOW|
            CY_PDM_PCM_INTR_RX_IF_OVERFLOW | CY_PDM_PCM_INTR_RX_UNDERFLOW) & intr_status)
    {
        Cy_PDM_PCM_Channel_ClearInterrupt(CYBSP_PDM_HW, PDM_CHANNEL, CY_PDM_PCM_INTR_MASK);
    }
}

/*******************************************************************************
* Function Name: pdm_data_process
********************************************************************************
* Summary:
*  This function feeds the data to the DEEPCRAFT pre-processor and returns the
*  processed results.
*
* Parameters:
*  None
*
* Return:
*  CY_RSLT_SUCCESS if successful, otherwise an error code indicating failure.
*
*******************************************************************************/
static uint32_t shift_counter = 0;

cy_rslt_t pdm_data_process(void)
{
    static float new_samples[FRAME_SIZE];
    float max_val = 1e-6f;
    float model_output[MODEL_OUTPUT_LEN];
    static float model_input[MODEL_INPUT_LEN];
    float confidence;

    if (!pdm_pcm_flag)
    {
        return PDM_PCM_DATA_NOT_READY;
    }

    pdm_pcm_flag = false;

    for (int i = 0; i < FRAME_SIZE; i++)
    {
        new_samples[i] = full_rx_buffer[i];
    }

    if (window_filled < WINDOW_SIZE_SAMPLES)
    {
        int copy_len = FRAME_SIZE;
        if (window_filled + copy_len > WINDOW_SIZE_SAMPLES)
        {
            copy_len = WINDOW_SIZE_SAMPLES - window_filled;
        }

        memcpy(&audio_window[window_filled], new_samples, copy_len * sizeof(float));
        window_filled += copy_len;

        if (window_filled < WINDOW_SIZE_SAMPLES)
        {
            return CY_RSLT_SUCCESS;
        }

        return CY_RSLT_SUCCESS;
    }

    memmove(audio_window,
            &audio_window[FRAME_SIZE],
            (WINDOW_SIZE_SAMPLES - FRAME_SIZE) * sizeof(float));
    memcpy(&audio_window[WINDOW_SIZE_SAMPLES - FRAME_SIZE],
           new_samples,
           FRAME_SIZE * sizeof(float));
    window_filled = WINDOW_SIZE_SAMPLES;

    shift_counter += FRAME_SIZE;
    if (shift_counter < STEP_SIZE_SAMPLES)
    {
        return CY_RSLT_SUCCESS;
    }
    shift_counter = 0;

    flip_led2();

    for (int i = 0; i < WINDOW_SIZE_SAMPLES; i++)
    {
        if (fabsf(audio_window[i]) > max_val)
        {
            max_val = fabsf(audio_window[i]);
        }
    }

    for (int i = 0; i < WINDOW_SIZE_SAMPLES; i++)
    {
        model_input[i] = audio_window[i] / max_val;
    }
    for (int i = WINDOW_SIZE_SAMPLES; i < MODEL_INPUT_LEN; i++)
    {
        model_input[i] = 0.0f;
    }
    IMAI_compute(model_input, model_output);

    confidence = model_output[0];

    if (report_cooldown_windows_remaining > 0u)
    {
        report_cooldown_windows_remaining--;
    }
    if (image_capture_cooldown_windows_remaining > 0u)
    {
        image_capture_cooldown_windows_remaining--;
    }

    if (confidence > OUTPUT_THRESHOLD_SCORE)
    {
        set_led1(true);

        if (report_cooldown_windows_remaining > 0u)
        {
            throttled_detection_counter++;
            if (throttled_detection_counter == 1u)
            {
                printf("[Hachimitsu Detector] Detection throttled, waiting %.1fs before next upload.\n",
                       DETECTION_REPORT_COOLDOWN_SEC);
            }
        }
        else
        {
            ipc_msg_t *msg = &detection_msg;

            memset(msg, 0, sizeof(*msg));
            msg->confidence = confidence;
            msg->timestamp = 0;
            msg->request_snapshot = (image_capture_cooldown_windows_remaining == 0u) ? 1U : 0U;
            fill_ipc_audio_payload(msg);

            printf("[Hachimitsu Detector] Detected hachimitsu voice, confidence=%.2f\n", confidence);
            if (!write_msg(msg))
            {
                printf("[Hachimitsu Detector] Msg queued failed, CM33 sender is busy.\n");
            }
            else
            {
                high_confidence_upload_count++;
                if (msg->request_snapshot != 0U)
                {
                    image_capture_cooldown_windows_remaining = IMAGE_CAPTURE_COOLDOWN_WINDOWS;
                }
            }

            report_cooldown_windows_remaining = DETECTION_REPORT_COOLDOWN_WINDOWS;
            throttled_detection_counter = 0u;
        }
    }
    else
    {
        set_led1(false);
        throttled_detection_counter = 0u;
    }

    return CY_RSLT_SUCCESS;
}

uint32_t audio_get_high_confidence_upload_count(void)
{
    return high_confidence_upload_count;
}
/* [] END OF FILE */



