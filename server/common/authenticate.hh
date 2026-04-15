#pragma once 

#include <cstdint>

namespace common{
/*
// player_id : 100
// username -> player_id
// username:210020202 -> 100
session:token:player_id -> {
    version: 1,
    session_id: 1,
    gateway_id: 1,
    zone_id: 1,
    battle_id: 1
}

*/ 
struct PlayerInfo{
    uint64_t version;       // 版本号
    uint64_t session_id;    // 会话ID
    uint32_t gateway_id;    // 网关服ID
    uint32_t zone_id;       // 区域ID
    uint32_t battle_id;     // 战斗服ID
};

#define REDIS_STORE_NAME_SESSION_TOKEN "demo:session:token:" // player_id
#define PLAYER_INFO_VERSION "version"
#define PLAYER_INFO_SESSION_ID "session_id"
#define PLAYER_INFO_GATEWAY_ID "gateway_id"
#define PLAYER_INFO_ZONE_ID "zone_id"
#define PLAYER_INFO_BATTLE_ID "battle_id"

} // namespace common