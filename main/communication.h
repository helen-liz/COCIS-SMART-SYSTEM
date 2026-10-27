#ifndef SMART_SYSTEM_COMMUNICATION_H
#define SMART_SYSTEM_COMMUNICATION_H

#include <stdint.h>

#include "esp_err.h"

esp_err_t communication_init(uint8_t room_id);
void communication_broadcast_occupancy(uint8_t room_id, int32_t occupancy);
int32_t communication_get_global_occupancy(uint8_t room_id, int32_t local_occupancy);

#endif