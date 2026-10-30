#ifndef CAN_RECEIVER_H
#define CAN_RECEIVER_H

#include <stdint.h>

int can_init(void);
void can_poll_silent(void);
int can_all_temps_ready(void);
uint16_t can_get_temp(int channel);
void can_clear_temps(void);
float can_temp_to_celsius(uint16_t raw);

#endif