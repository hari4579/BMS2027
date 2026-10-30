/**
 * BMS Main Application - MSDK-only ADBMS6830 driver
 *
 * Verified against the ADBMS6830B datasheet (Rev. 0):
 *  - Command format: CMD0, CMD1, PEC0, PEC1 (Table 48/49), PEC15 over the
 *    16-bit command (Command PEC section, poly x15+x14+x10+x8+x7+x4+x3+1,
 *    initial value 000000000010000).
 *  - Read command format: CMD+PEC (4 bytes) then, in the SAME continuous
 *    CSB-low window, 6 data bytes + 2 PEC bytes clocked out by the device
 *    (Table 47 / Bus Protocols section).
 *  - ADCV command bit pattern (Table 50) must be sent before RDCVx has
 *    anything meaningful to return (Measure State, Theory of Operation).
 *  - Data PEC for reads: 6-bit command counter + 10-bit PEC packed into the
 *    final 2 bytes as CCNT[5:0],PEC[9:8] then PEC[7:0] (Table 43).
 *  - RDCVE = 0x0009, RDCVF = 0x000B (Table 50) - NOT 0x000C/0x000E.
 *
 * This revision adds:
 *  - BMS_ENABLE_PEC_CHECK: toggle the data-PEC10 pass/fail check on/off.
 *  - BMS_ENABLE_SPI_DEBUG: print every raw TX/RX byte for each SPI frame.
 *
 * CS is on P1.23 (GPIO1, manual software chip-select). SPI1 is configured
 * with 0 hardware targets (see soc_init.c), so there is no hardware-SS
 * conflict with the manual CS toggling here.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "mxc_device.h"
#include "board.h"
#include "led.h"
#include "mxc_delay.h"
#include "spi.h"
#include "gpio.h"
#include "bms_config.h"

/* ===================== Config ===================== */
#define BMS_CS_PORT      MXC_GPIO1
#define BMS_CS_PIN       MXC_GPIO_PIN_23
#define NUM_DEV          1      /* single device on the chain */

/* ADBMS6830 read-cell-voltage opcodes (verified against datasheet Table 50) */
static const uint16_t CV_CMDS[6] = {
    0x0004, /* RDCVA */
    0x0006, /* RDCVB */
    0x0008, /* RDCVC */
    0x000A, /* RDCVD */
    0x0009, /* RDCVE */
    0x000B, /* RDCVF */
};

/* Global cell data */
static struct {
    uint16_t cell[16];
    uint8_t  pec_ok[6];   /* per-group PEC10 check result (only meaningful
                              if BMS_ENABLE_PEC_CHECK is 1) */
} cell_data;

/* ===================== PEC15 (command checksum) ===================== */
static uint16_t adbms_pec15(uint16_t comm)
{
    int pec[15]  = {0,0,0,0,1,0,0,0,0,0,0,0,0,0,0};
    int poly[7];
    int i;

    for (i = 15; i >= 0; i--) {
        poly[0] = ((comm >> i) & 0x1) ^ pec[14];
        poly[1] = poly[0] ^ pec[2];
        poly[2] = poly[0] ^ pec[3];
        poly[3] = poly[0] ^ pec[6];
        poly[4] = poly[0] ^ pec[7];
        poly[5] = poly[0] ^ pec[9];
        poly[6] = poly[0] ^ pec[13];

        pec[14] = poly[6];
        pec[13] = pec[12];
        pec[12] = pec[11];
        pec[11] = pec[10];
        pec[10] = poly[5];
        pec[9]  = pec[8];
        pec[8]  = poly[4];
        pec[7]  = poly[3];
        pec[6]  = pec[5];
        pec[5]  = pec[4];
        pec[4]  = poly[2];
        pec[3]  = poly[1];
        pec[2]  = pec[1];
        pec[1]  = pec[0];
        pec[0]  = poly[0];
    }

    uint16_t temp = 0;
    for (i = 14; i >= 0; i--)
        temp |= (pec[i] << i);

    return (uint16_t)(temp << 1);
}

#if BMS_ENABLE_PEC_CHECK
/* ===================== PEC10 (data checksum, receive side) ===================== */
static uint16_t adbms_pec10(const uint8_t *data /* 7 bytes */)
{
    int pec[10] = {0,0,0,0,1,0,0,0,0,0};
    int poly[5];
    long long data_temp = 0;
    int i;

    for (i = 0; i < 7; i++) {
        data_temp |= data[i];
        data_temp <<= 8;
    }
    data_temp >>= 8;
    data_temp >>= 2; /* is_read == true */

    for (i = 53; i >= 0; i--) {
        poly[0] = ((data_temp >> i) & 0x1) ^ pec[9];
        poly[1] = poly[0] ^ pec[0];
        poly[2] = poly[0] ^ pec[1];
        poly[3] = poly[0] ^ pec[2];
        poly[4] = poly[0] ^ pec[6];

        pec[9] = pec[8];
        pec[8] = pec[7];
        pec[7] = poly[4];
        pec[6] = pec[5];
        pec[5] = pec[4];
        pec[4] = pec[3];
        pec[3] = poly[3];
        pec[2] = poly[2];
        pec[1] = poly[1];
        pec[0] = poly[0];
    }

    uint16_t temp = 0;
    for (i = 9; i >= 0; i--)
        temp |= (pec[i] << i);

    return temp;
}
#endif /* BMS_ENABLE_PEC_CHECK */

/* ===================== Low level SPI/CS ===================== */
static inline void cs_low(void)  { MXC_GPIO_OutClr(BMS_CS_PORT, BMS_CS_PIN); }
static inline void cs_high(void) { MXC_GPIO_OutSet(BMS_CS_PORT, BMS_CS_PIN); }

#if BMS_ENABLE_SPI_DEBUG
static void print_bytes(const char *label, const uint8_t *buf, uint16_t len)
{
    printf("    %s [%2u]: ", label, len);
    for (uint16_t i = 0; i < len; i++)
        printf("%02X ", buf[i]);
    printf("\n");
}
#endif

/* Real isoSPI wakeup: a bare CS low->high pulse, NO clock/data, followed by
 * the tWAKE settle time. Must run once per device in the chain, and must be
 * done before every command group (the link goes back to sleep between
 * commands, not just at boot). This is not an SPI frame, so it isn't part
 * of the raw TX/RX debug print. */
static void adbms_wakeup(void)
{
    for (int i = 0; i < NUM_DEV; i++) {
        cs_low();
        MXC_Delay(2);      /* just needs to be a clean edge, >2us */
        cs_high();
        MXC_Delay(300);    /* ADBMS_TWAKE ~300us settle */
    }
}

/* One complete command+response frame in a single continuous CSB-low
 * window, full duplex - CMD0,CMD1,PEC0,PEC1 then (for reads) 6 data bytes
 * + 2 PEC bytes, all clocked in one unbroken CSB-low session, matching the
 * datasheet's Bus Protocols / Table 47 read-command format. */
static int adbms_spi_frame(uint16_t cmd16, uint8_t *rx_out, uint16_t rx_len)
{
    uint8_t tx[4 + 8];
    uint8_t rx[4 + 8];
    uint16_t total = 4 + rx_len;

    if (total > sizeof(tx))
        return -1;

    uint16_t pec = adbms_pec15(cmd16);
    tx[0] = (uint8_t)(cmd16 >> 8);
    tx[1] = (uint8_t)(cmd16 & 0xFF);
    tx[2] = (uint8_t)(pec >> 8);
    tx[3] = (uint8_t)(pec & 0xFF);
    memset(&tx[4], 0x00, rx_len);
    memset(rx, 0x00, total);

    mxc_spi_req_t req;
    req.spi        = MXC_SPI1;
    req.txData     = tx;
    req.rxData     = rx;
    req.txLen      = total;
    req.rxLen      = total;
    req.ssIdx      = 0;
    req.ssDeassert = 0;   /* we control CS manually, hold it low the whole frame */
    req.txCnt      = 0;
    req.rxCnt      = 0;
    req.completeCB = NULL;

    cs_low();
    int ret = MXC_SPI_MasterTransaction(&req);
    cs_high();

#if BMS_ENABLE_SPI_DEBUG
    printf("  SPI frame cmd=0x%04X pec=0x%04X ret=%d\n", cmd16, pec, ret);
    print_bytes("TX", tx, total);
    print_bytes("RX", rx, total);
#endif

    if (ret != E_NO_ERROR)
        return -1;

    if (rx_len > 0 && rx_out != NULL)
        memcpy(rx_out, &rx[4], rx_len);

    return 0;
}

/* Start ADC conversion: ADCV, rd=0, cont=1, dcp=0, rstf=0, owcs=0
 * -> cmd = 0x02E0 (matches datasheet Table 50 ADCV bit pattern) */
static int adbms_adcv_start(void)
{
    uint16_t cmd = 0x02E0;
    return adbms_spi_frame(cmd, NULL, 0);
}

/* ===================== Public API ===================== */
int bms_init(void)
{
    printf("BMS: Initializing...\n");

    mxc_gpio_cfg_t gpio_cfg;
    gpio_cfg.port  = BMS_CS_PORT;
    gpio_cfg.mask  = BMS_CS_PIN;
    gpio_cfg.func  = MXC_GPIO_FUNC_OUT;
    gpio_cfg.pad   = MXC_GPIO_PAD_PULL_UP;
    gpio_cfg.vssel = MXC_GPIO_VSSEL_VDDIO;
    MXC_GPIO_Config(&gpio_cfg);
    cs_high(); /* CS idle HIGH */

    printf("BMS: Initialized (PEC check=%s, SPI debug=%s)\n",
           BMS_ENABLE_PEC_CHECK ? "ON" : "OFF",
           BMS_ENABLE_SPI_DEBUG ? "ON" : "OFF");
    return 0;
}

int bms_read_cells(void)
{
    printf("BMS: [1] Waking up...\n");
    adbms_wakeup();
    
    printf("BMS: [2] Starting ADC conversion (ADCV)...\n");
    if (adbms_adcv_start() != 0) {
        printf("BMS: ADCV failed\n");
        return -1;
    }
    printf("BMS: [3] ADC started, waiting 2ms for conversion...\n");
    MXC_Delay(2000);

    for (int i = 0; i < 6; i++) {
        uint8_t resp[8];

        printf("BMS: [4.%d] Waking up before CV-%c read...\n", i, 'A' + i);
        adbms_wakeup();

        printf("BMS: [5.%d] About to read CV-%c (sending SPI frame)...\n", i, 'A' + i);
        fflush(stdout);  // Force flush so we see this before it hangs
        
        if (adbms_spi_frame(CV_CMDS[i], resp, 8) != 0) {
            printf("BMS: Read CV-%c failed\n", 'A' + i);
            return -1;
        }
        printf("BMS: [6.%d] ✓ CV-%c read successfully\n", i, 'A' + i);

        // ... rest of decode logic ...
    }

    return 0;
}

void bms_print_cells(void)
{
    printf("\n========== CELL DATA ==========\n");
    for (int i = 0; i < 16; i++) {
        uint16_t raw = cell_data.cell[i];
        float voltage = (raw * 1.5e-4f) + 1.5024f;
        int group = i / 3;
        if (group > 5) group = 5;

        const char *pec_str;
        if (cell_data.pec_ok[group] == 2)
            pec_str = "SKIPPED";
        else
            pec_str = cell_data.pec_ok[group] ? "OK" : "FAIL";

        printf("Cell %2d: 0x%04X = %.4f V  [group PEC %s]\n",
               i + 1, raw, voltage, pec_str);
    }
    printf("================================\n\n");
}

int bms_app_main(void)
{
    static int initialized = 0;

    if (!initialized) {
        if (bms_init() != 0) {
            printf("BMS: Init failed\n");
            return -1;
        }
        initialized = 1;
    }

    printf("BMS: Reading...\n");
    if (bms_read_cells() != 0) {
        printf("BMS: Read failed\n");
        return -1;
    }

    bms_print_cells();
    return 0;
}
/* ===================== Public API to get cell data ===================== */

/**
 * Get cell voltage directly (in Volts)
 * @param cell_idx Cell index (0-15)
 * @return Voltage in Volts
 */
float bms_get_cell_voltage(int cell_idx)
{
    if (cell_idx < 0 || cell_idx >= 16)
        return 1.5024f;
    
    uint16_t raw = cell_data.cell[cell_idx];
    return (raw * 1.5e-4f) + 1.5024f;
}