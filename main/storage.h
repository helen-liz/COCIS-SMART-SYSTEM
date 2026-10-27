#ifndef SMART_SYSTEM_STORAGE_H
#define SMART_SYSTEM_STORAGE_H

#include "room_profiles.h"

void storage_init(void);
void storage_load_config(system_config_t *config);
void storage_save_config(const system_config_t *config);
void storage_load_occupancy(int32_t *occupancy);
void storage_save_occupancy(int32_t occupancy);
void storage_write_event(const room_profile_t *profile, const char *event,
						 const uint8_t *uid, const char *detail);
void storage_print_logs(void);

#endif