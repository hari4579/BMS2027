#ifndef __BMS_CONFIG_H__
#define __BMS_CONFIG_H__
/**
 * BMS Configuration for APARD32690 + ADBMS6830
 *
 * The block below is carried over as-is from the original no-OS example's
 * Makefile-derived config (examples/Makefile in the no-OS-BMS-Examples
 * package) - NUM_DEV, ADBMS6830_BASIC, MEASUREMENTS_FREQ_MS, etc. all
 * originate there. NUM_DEV is set to 1 here (not the example's default of
 * 2) to match this board's single ADBMS6830B.
 *
 * IMPORTANT: this project now uses a minimal MSDK-only driver (bms_main.c),
 * not the full no-OS adbms_common/adbms6830 stack. Only NUM_DEV is actually
 * read by the current driver. The measurement-type and INC_CELL_* flags
 * below are kept for documentation/parity with the original config, but are
 * NOT wired into bms_main.c - it always does a basic RDCVA-RDCVF cell
 * voltage read regardless of these settings. They're flagged "(inert)"
 * below so it's clear which knobs actually do something right now.
 */

// ========== General Configurations ==========
#define NUM_DEV                     1       // Number of ADBMS devices in chain - USED (adbms_wakeup loop)

// Measurement selection (only one can be y) - (inert, minimal driver only does basic cell voltage)
#define ADBMS6830_BASIC             1       // Cell monitor basic measurements
#define ADBMS2950_BASIC             0       // Pack monitor measurements
#define BMS_MIXED                   0       // Mixed chain
#define CELL_DIAGNOSTICS            0       // Diagnostic measurements

// Timing Configuration - (inert, not yet wired into bms_app_main's loop)
#define MEASUREMENTS_FREQ_MS        1000    // Measurement loop frequency (ms)
#define NUM_MEAS_LOOP                0       // 0 = continuous, otherwise N loops

// Time profiling - (inert)
#define TIME_PROFILING               0       // 0 = disabled, 1 = enabled

// ========== Example Specific Configurations ==========
// For ADBMS6830_BASIC only - (inert, minimal driver only reads RDCVA-RDCVF)
#define INC_CELL_NORMAL_MEAS         1       // Normal cell voltage
#define INC_CELL_FILTERED_MEAS       1       // Filtered cell voltage
#define INC_CELL_AVERAGE_MEAS        1       // Average cell voltage
#define INC_CELL_SPIN_MEAS           1       // Spin cell voltage
#define INC_CELL_GPIO_MEAS           1       // GPIO voltage
#define INC_CELL_STAT_MEAS           1       // Status measurements

// ========== Platform Specific ==========
#define PLATFORM                     "maxim"
#define BMS_TARGET                   "MAX32690"

/**
 * ========== MSDK Driver Debug/Feature Toggles ==========
 * These ARE wired into bms_main.c and control the minimal driver's runtime
 * behavior directly.
 */

// Set to 1 to compute + display the data-PEC10 pass/fail per cell group.
// Set to 0 to skip PEC checking entirely (useful while debugging raw
// communication, so a PEC mismatch doesn't hide whether bytes are moving
// at all).
#define BMS_ENABLE_PEC_CHECK          0

// Set to 1 to print every raw byte written and read over SPI for each
// command frame (wakeup pulses are not SPI frames, so they aren't printed
// here). Very verbose - meant for bring-up/debug only.
#define BMS_ENABLE_SPI_DEBUG          1

#endif /* __BMS_CONFIG_H__ */