#ifndef SMART_SYSTEM_STORAGE_H
#define SMART_SYSTEM_STORAGE_H

#include <stddef.h>

#include "room_profiles.h"

void storage_init(void);
void storage_load_config(system_config_t *config);
void storage_save_config(const system_config_t *config);
void storage_load_occupancy(int32_t *occupancy);
void storage_save_occupancy(int32_t occupancy);
void storage_load_inside(inside_record_t *records, uint8_t *count);
void storage_save_inside(const inside_record_t *records, uint8_t count);
void storage_write_event(const room_profile_t *profile, const char *event,
						 const uint8_t *uid, const char *detail);
void storage_print_logs(void);
size_t storage_read_logs(char *buffer, size_t max_size);
void storage_clear_logs(void);
size_t storage_log_size(void);

#endif
