#pragma once 


// (message_id >> 24) & 0xff
#include <cstdint>
#define DIRECTION_CLIENT_TO_GATEWAY 0x01
#define DIRECTION_GATEWAY_TO_CLIENT 0x02
#define DIRECTION_GATEWAY_TO_BATTLE 0x11
#define DIRECTION_BATTLE_TO_GATEWAY 0x12
#define DIRECTION_GATEWAY_TO_LOGIC 0x21
#define DIRECTION_LOGIC_TO_GATEWAY 0x22
// (message_id >> 16) & 0xff
#define MODULE_AUTH         0x01
#define MODULE_SESSION      0x02
#define MODULE_MOVE         0x03
#define MODULE_STATE        0x04
#define MODULE_SKILL        0x05
#define MODULE_LOGIC        0x06
#define MODULE_GATEWAY      0x0F
#define MODULE_INTERNAL     0x1F

inline uint32_t get_direction(uint32_t message_id){
    return (message_id >> 24) & 0xff;
}

inline uint32_t get_module(uint32_t message_id){
    return (message_id >> 16) & 0xff;
}