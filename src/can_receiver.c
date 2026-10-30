/**
 * CAN Receiver - Silent Frame Collection
 * Fills temp_data[13] array - no printing during collection
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "mxc_device.h"
#include "mxc_delay.h"
#include "mxc_errors.h"
#include "can.h"
#include "gpio.h"

#define CAN_IDX         1U
#define CAN_BITRATE     500000U
#define CAN_DLC         8U

/* Temperature data array - THIS IS WHAT WE FILL */
static uint16_t temp_data[13];
static uint8_t temp_data_valid[13];  /* Track which channels are filled */

int can_init(void)
{
    int err;

    printf("[CAN]  Powering up CAN1...\n");
    err = MXC_CAN_PowerControl(CAN_IDX, MXC_CAN_PWR_CTRL_FULL);
    if (err != E_NO_ERROR) {
        printf("[CAN]  ERROR: MXC_CAN_PowerControl failed (%d)\n", err);
        return err;
    }

    printf("[CAN]  Initializing CAN1...\n");
    MXC_GPIO_SetConfigLock(MXC_GPIO_CONFIG_UNLOCKED);
    err = MXC_CAN_Init(CAN_IDX, MXC_CAN_OBJ_CFG_TXRX, NULL, NULL);
    MXC_GPIO_SetConfigLock(MXC_GPIO_CONFIG_LOCKED);
    
    if (err != E_NO_ERROR) {
        printf("[CAN]  ERROR: MXC_CAN_Init failed (%d)\n", err);
        return err;
    }

    printf("[CAN]  Setting bitrate to 500 kbps...\n");
    err = MXC_CAN_SetBitRate(CAN_IDX,
                              MXC_CAN_BITRATE_SEL_NOMINAL,
                              CAN_BITRATE,
                              MXC_CAN_BIT_SEGMENTS(12, 2, 1));
    if (err != E_NO_ERROR) {
        printf("[CAN]  ERROR: MXC_CAN_SetBitRate failed (%d)\n", err);
        return err;
    }

    int actual_br = MXC_CAN_GetBitRate(CAN_IDX, MXC_CAN_BITRATE_SEL_NOMINAL);
    printf("[CAN]  Bitrate: %d bps\n", actual_br);

    printf("[CAN]  Configuring CAN object...\n");
    err = MXC_CAN_ObjectConfigure(CAN_IDX, MXC_CAN_OBJ_CFG_TXRX);
    if (err != E_NO_ERROR) {
        printf("[CAN]  ERROR: MXC_CAN_ObjectConfigure failed (%d)\n", err);
        return err;
    }

    printf("[CAN]  Setting open filter...\n");
    err = MXC_CAN_ObjectSetFilter(CAN_IDX,
                                   MXC_CAN_FILT_CFG_MASK_ADD | 
                                   MXC_CAN_FILT_CFG_SINGLE_STD_ID,
                                   0x000, 0x000);
    if (err != E_NO_ERROR) {
        printf("[CAN]  ERROR: MXC_CAN_ObjectSetFilter failed (%d)\n", err);
        return err;
    }

    printf("[CAN]  Entering NORMAL mode...\n");
    err = MXC_CAN_SetMode(CAN_IDX, MXC_CAN_MODE_NORMAL);
    if (err != E_NO_ERROR) {
        printf("[CAN]  ERROR: MXC_CAN_SetMode failed (%d)\n", err);
        return err;
    }

    printf("[CAN]  ✓ Ready\n\n");
    
    memset(temp_data, 0, sizeof(temp_data));
    memset(temp_data_valid, 0, sizeof(temp_data_valid));
    
    return E_NO_ERROR;
}

/**
 * Silent polling - just fills temp_data[13] array
 * NO PRINTING during this function
 */
void can_poll_silent(void)
{
    mxc_can_msg_info_t rx_info;
    uint8_t rx_data[CAN_DLC];

    mxc_can_req_t rx_req;
    rx_req.msg_info = &rx_info;
    rx_req.data = rx_data;
    rx_req.data_sz = CAN_DLC;

    memset(&rx_info, 0, sizeof(rx_info));
    memset(rx_data, 0, sizeof(rx_data));

    int ret = MXC_CAN_MessageRead(CAN_IDX, &rx_req);

    if (ret >= 0) {
        uint32_t id = rx_info.msg_id;
        
        /* Decode frames and fill temp_data[0-12] */
        if (id == 0x200) {
            for (int i = 0; i < 4; i++) {
                temp_data[i] = ((uint16_t)rx_data[2*i] << 8) | rx_data[2*i + 1];
                temp_data_valid[i] = 1;
            }
        }
        else if (id == 0x201) {
            for (int i = 0; i < 4; i++) {
                temp_data[4 + i] = ((uint16_t)rx_data[2*i] << 8) | rx_data[2*i + 1];
                temp_data_valid[4 + i] = 1;
            }
        }
        else if (id == 0x202) {
            for (int i = 0; i < 4; i++) {
                temp_data[8 + i] = ((uint16_t)rx_data[2*i] << 8) | rx_data[2*i + 1];
                temp_data_valid[8 + i] = 1;
            }
        }
        else if (id == 0x203) {
            temp_data[12] = ((uint16_t)rx_data[0] << 8) | rx_data[1];
            temp_data_valid[12] = 1;
        }
    }
}

/**
 * Check if all 13 temperature values are filled
 */
int can_all_temps_ready(void)
{
    for (int i = 0; i < 13; i++) {
        if (!temp_data_valid[i])
            return 0;
    }
    return 1;
}

/**
 * Get temperature value
 */
uint16_t can_get_temp(int channel)
{
    if (channel < 0 || channel >= 13)
        return 0;
    return temp_data[channel];
}

/**
 * Clear the valid flags for next collection
 */
void can_clear_temps(void)
{
    memset(temp_data_valid, 0, sizeof(temp_data_valid));
}

/**
 * Convert raw to Celsius
 */
float can_temp_to_celsius(uint16_t raw)
{
    return (float)raw * 0.1f;
}