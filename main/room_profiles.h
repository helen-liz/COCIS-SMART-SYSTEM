#ifndef SMART_SYSTEM_ROOM_PROFILES_H
#define SMART_SYSTEM_ROOM_PROFILES_H

#include "system_types.h"

#if defined(CONFIG_SMART_SYSTEM_ROLE_ENTRANCE)
#define SMART_SYSTEM_ROOM_PROFILE 0
#elif defined(CONFIG_SMART_SYSTEM_ROLE_ROOM2)
#define SMART_SYSTEM_ROOM_PROFILE 2
#else
#define SMART_SYSTEM_ROOM_PROFILE 1
#endif

#if defined(CONFIG_SMART_SYSTEM_EXIT_NODE)
#define SMART_SYSTEM_EXIT_NODE 1
#else
#define SMART_SYSTEM_EXIT_NODE 0
#endif

extern const room_profile_t ROOM1_PROFILE;
extern const room_profile_t ROOM2_PROFILE;
extern const room_profile_t ENTRANCE_PROFILE;

#endif
