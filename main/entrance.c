#include "room_profiles.h"

const room_profile_t ENTRANCE_PROFILE = {
	.room_id = 0,
	.name = "Main Entrance",
	.exit_node = SMART_SYSTEM_EXIT_NODE != 0,
};