#pragma once 
#include "envelope.pb.h"
#include "forward.pb.h"
#include "ids.pb.h"
#include "authenticate.hh"
#include "memory_reuse.hh"
#include <cmath>
#include <cstdint>

namespace common{
namespace protocol{

// (message_id >> 24) & 0xff
// (message_id >> 16) & 0xff


// version 1.0.0
#define TRANSPORT_VERSION 1


struct util{
    static inline uint32_t get_direction(uint32_t message_id){
        return (message_id >> 24) & 0xff;
    }
    static inline void set_direction(uint32_t& message_id, uint32_t direction){
        message_id &= ~(0xff << 24);
        message_id |= (direction << 24);
    }

    static inline uint32_t get_module(uint32_t message_id){
        return (message_id >> 16) & 0xff;
    }
    static inline void set_module(uint32_t& message_id, uint32_t module){
        message_id &= ~(0xff << 16);
        message_id |= (module << 16);
    }

    static inline uint32_t get_action(uint32_t message_id){
        return message_id & 0xff;
    }
    static inline void set_action(uint32_t& message_id, uint32_t action){
        message_id &= ~(0xff);
        message_id |= (action << 00);
    }
    
    static inline float unpack_yaw_to_radians(uint32_t packed) {
        uint16_t yaw16 = static_cast<uint16_t>(packed >> 16);
        return static_cast<float>(yaw16) * (2.0f * static_cast<float>(M_PI) / 65536.0f);
    }
    static inline float unpack_pitch_to_radians(uint32_t packed) {
        uint16_t pitch16 = static_cast<uint16_t>(packed);
        return static_cast<float>(pitch16) * (2.0f * static_cast<float>(M_PI) / 65536.0f);
    }
    static inline uint32_t pack_yaw_pitch(float yaw, float pitch){
        uint16_t yaw16 = static_cast<uint16_t>(yaw / (2.0f * static_cast<float>(M_PI) * 65536.0f));
        uint16_t pitch16 = static_cast<uint16_t>(pitch / (2.0f * static_cast<float>(M_PI) * 65536.0f));
        return (yaw16 << 16) | pitch16;
    }

    #define DEFAULT_SCALE 100
    static inline float itos(uint32_t value, float scale){
        return value / 1.0 * scale;
    }
    static inline uint32_t oito(float value, float scale){
        return static_cast<uint32_t>(value * scale);
    }


    // send to internal server
    static inline std::shared_ptr<mmo::transport::GatewayToServer> make_gateway_to_server(const std::shared_ptr<mmo::transport::Envelope>& envelope, const std::shared_ptr<common::PlayerInfo>& player_info){
        auto gateway_to_server = memory_reuse::get_object<mmo::transport::GatewayToServer>();
        auto header = gateway_to_server->mutable_header();
        header->set_player_id(envelope->header().player_id());
        header->set_session_id(player_info->session_id);
        header->set_gateway_id(player_info->gateway_id);
        header->set_zone_id(player_info->zone_id);
        header->set_battle_id(player_info->battle_id);
        header->set_trace_id(std::chrono::system_clock::now().time_since_epoch().count() / 1000000);
        gateway_to_server->set_body(envelope->body());
        return gateway_to_server;
    }
    // send to client
    static inline std::shared_ptr<mmo::transport::Envelope> make_envelope(const std::shared_ptr<mmo::transport::ServerToGateway>& gateway_to_server){
        auto envelope = memory_reuse::get_object<mmo::transport::Envelope>();
        auto header = envelope->mutable_header();
        header->set_version(TRANSPORT_VERSION);
        header->set_player_id(gateway_to_server->player_id());
        uint32_t cmd = gateway_to_server->cmd();
        set_direction(cmd, mmo::ids::Direction::G2C);
        header->set_cmd(cmd);
        header->set_timestamp_ms(std::chrono::system_clock::now().time_since_epoch().count() / 1000000);
        envelope->set_body(gateway_to_server->body());
        return envelope;
    }
};


} // namespace protocol
} // namespace common
