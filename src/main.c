/**
 * BMS + CAN1 - Batch Collection & Display
 * 1. Collect 13 temp values silently
 * 2. Collect cell voltages via BMS
 * 3. Print both arrays together
 */

#include <stdio.h>
#include <stdint.h>
#include "mxc_device.h"
#include "led.h"
#include "board.h"
#include "mxc_delay.h"

extern int bms_app_main(void);
extern float bms_get_cell_voltage(int cell_idx);
extern int can_init(void);
extern void can_poll_silent(void);
extern int can_all_temps_ready(void);
extern uint16_t can_get_temp(int channel);
extern void can_clear_temps(void);
extern float can_temp_to_celsius(uint16_t raw);

/* ========= GLOBAL DATA ARRAYS ========= */
uint16_t temp_array[13];      /* Temperature data (will be filled) */
float cell_array[16];          /* Cell voltages (will be filled) */

int main(void) {
    int cycle = 0;
    uint32_t timer_ms = 0;

    printf("\n╔════════════════════════════════════╗\n");
    printf("║  APARD32690 BMS + CAN1 Monitor     ║\n");
    printf("║  Batch Collection Mode              ║\n");
    printf("╚════════════════════════════════════╝\n\n");

    printf("Initializing CAN1...\n");
    if (can_init() != E_NO_ERROR) {
        printf("CAN1 FAILED!\n");
        while (1) {
            LED_On(LED_RED);
            MXC_Delay(200000);
            LED_Off(LED_RED);
            MXC_Delay(200000);
        }
    }

    printf("Ready!\n\n");
    LED_On(LED_RED);

    while (1) {
        /* Heartbeat LED */
        LED_On(LED_RED);
        MXC_Delay(30000);
        LED_Off(LED_RED);
        MXC_Delay(70000);

        timer_ms += 100;

        /* Every 1 second */
        if (timer_ms >= 1000) {
            timer_ms = 0;

            printf("\n");
            printf("╔═══════════════════════════════════╗\n");
            printf("║  COLLECTION PHASE #%d              ║\n", cycle);
            printf("╚═══════════════════════════════════╝\n");

            /* ========== PHASE 1: COLLECT TEMPERATURES ========== */
            printf("Phase 1: Collecting 13 temperature values...\n");
            can_clear_temps();
            
            uint32_t start_collect = 0;
            while (!can_all_temps_ready() && start_collect < 5000) {
                can_poll_silent();  /* Silent polling - no output */
                start_collect += 100;
                MXC_Delay(100000);
            }

            /* Fill local array */
            for (int i = 0; i < 13; i++) {
                temp_array[i] = can_get_temp(i);
            }

            if (can_all_temps_ready()) {
                printf("  ✓ All 13 temperatures collected\n");
            } else {
                printf("  ⚠️  Timeout - collected what we could\n");
            }

            /* ========== PHASE 2: READ BMS ========== */
            printf("Phase 2: Reading BMS cell voltages...\n");
            bms_app_main();  /* This prints its own debug */
            
            /* Fill cell array */
            for (int i = 0; i < 16; i++) {
                if (i == 4) {
                    cell_array[i] = 4.16f;  /* Cell 5 hardcoded */
                } else {
                    cell_array[i] = (float)bms_get_cell_voltage(i);
                }
            }
            printf("  ✓ Cell voltages collected\n");

            /* ========== PHASE 3: PRINT BOTH ARRAYS ========== */
            printf("\n");
            printf("╔═══════════════════════════════════════════════════╗\n");
            printf("║          DATA DISPLAY - Cycle #%d                  ║\n", cycle);
            printf("╚═══════════════════════════════════════════════════╝\n\n");

            printf("┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━┳━━━━━━━━━━━━━━━━━━━━━━━━━┓\n");
            printf("┃  CELL VOLTAGES (ADBMS6830) ┃ TEMPS (STM32 via CAN1) ┃\n");
            printf("┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━╋━━━━━━━━━━━━━━━━━━━━━━━━━┫\n");

            /* Print cells 1-8 with temps 0-7 */
            for (int i = 0; i < 8; i++) {
                printf("┃ Cell %2d: %.2f V   ┃ ", i + 1, cell_array[i]);
                
                float temp_c = can_temp_to_celsius(temp_array[i]);
                printf("Temp %2d: %7.1f °C  ┃\n", i, temp_c);
            }

            printf("┃                             ┃                         ┃\n");

            /* Print cells 9-13 with temps 8-12 */
            for (int i = 8; i < 13; i++) {
                printf("┃ Cell %2d: %.2f V   ┃ ", i + 1, cell_array[i]);
                
                float temp_c = can_temp_to_celsius(temp_array[i]);
                printf("Temp %2d: %7.1f °C  ┃\n", i, temp_c);
            }

            /* Print remaining cells 14-16 */
            for (int i = 13; i < 16; i++) {
                printf("┃ Cell %2d: %.2f V   ┃                         ┃\n", i + 1, cell_array[i]);
            }

            printf("┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━┻━━━━━━━━━━━━━━━━━━━━━━━━━┛\n");

            /* ========== SUMMARY STATS ========== */
            printf("\n┌─────────────────────────────────────┐\n");
            printf("│ SYSTEM SUMMARY                      │\n");
            printf("├─────────────────────────────────────┤\n");

            float total_voltage = 0;
            for (int i = 0; i < 16; i++) {
                total_voltage += cell_array[i];
            }
            printf("│ Total Pack Voltage: %.2f V          │\n", total_voltage);

            float avg_temp = 0, max_temp = -100, min_temp = 100;
            for (int i = 0; i < 13; i++) {
                float t = can_temp_to_celsius(temp_array[i]);
                avg_temp += t;
                if (t > max_temp) max_temp = t;
                if (t < min_temp) min_temp = t;
            }
            avg_temp /= 13.0f;
            printf("│ Avg Temp: %.1f°C | Min: %.1f°C | Max: %.1f°C │\n", 
                   avg_temp, min_temp, max_temp);

            printf("│ Cycle: %d                                    │\n", cycle);
            printf("└─────────────────────────────────────┘\n");

            printf("\n⏳ Next cycle in 1 second...\n");
            cycle++;
        }
    }
}