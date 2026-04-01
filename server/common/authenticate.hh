#pragma once 

#include <cstdint>

namespace common{

struct PlayerInfo{
    uint64_t version;
    uint64_t session_id;
    uint32_t gateway_id;
    uint32_t zone_id;
    uint32_t battle_id;
};

#define PLAYER_INFO_VERSION "version"
#define PLAYER_INFO_SESSION_ID "session_id"
#define PLAYER_INFO_GATEWAY_ID "gateway_id"
#define PLAYER_INFO_ZONE_ID "zone_id"
#define PLAYER_INFO_BATTLE_ID "battle_id"

} // namespace common