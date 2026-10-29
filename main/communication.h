#ifndef SMART_SYSTEM_COMMUNICATION_H
#define SMART_SYSTEM_COMMUNICATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
	uint8_t mac[6];
	uint8_t room_id;
	int32_t occupancy;
	uint32_t age_ms;
	bool active;
} remote_node_info_t;

esp_err_t communication_init(uint8_t room_id);
void communication_broadcast_occupancy(uint8_t room_id, int32_t occupancy);
int32_t communication_get_global_occupancy(uint8_t room_id, int32_t local_occupancy);
size_t communication_get_remote_nodes(remote_node_info_t *nodes, size_t max_nodes);

#endif
