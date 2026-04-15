#pragma once

#include "move.pb.h"
#include "object.hh"
#include "protocol.hh"
#include <cmath>
#include <cstdint>

namespace battle {

#define MOVE_SPEED 5.0f

enum MoveKeyMask : uint32_t {
    MOVE_FORWARD = 1 << 0,
    MOVE_BACK    = 1 << 1,
    MOVE_LEFT    = 1 << 2,
    MOVE_RIGHT   = 1 << 3,
};

struct Vec2 {
    float x{0.f};
    float y{0.f};
};

inline float vec2_length(const Vec2& v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

inline Vec2 normalize_vec2(const Vec2& v) {
    float len = vec2_length(v);
    if (len <= 0.00001f) {
        return {};
    }
    return {v.x / len, v.y / len};
}

inline Vec2 key_mask_to_local_dir(uint32_t key_mask) {
    int x = 0;
    int y = 0;
    if (key_mask & MOVE_LEFT) --x;
    if (key_mask & MOVE_RIGHT) ++x;
    if (key_mask & MOVE_FORWARD) ++y;
    if (key_mask & MOVE_BACK) --y;
    return normalize_vec2({static_cast<float>(x), static_cast<float>(y)});
}


inline Vec2 rotate_local_to_world(const Vec2& local_dir, float yaw) {
    float c = std::cos(yaw);
    float s = std::sin(yaw);
    return {
        local_dir.x * c + local_dir.y * s,
        -local_dir.x * s + local_dir.y * c
    };
}

template<typename MoveInputPtrContainer>
const typename MoveInputPtrContainer::value_type* pick_latest_valid_input(
    const Entity& entity,
    const MoveInputPtrContainer& move_inputs
) {
    const typename MoveInputPtrContainer::value_type* latest = nullptr;
    uint32_t best_seq = entity.move_seq;
    for (const auto& input : move_inputs) {
        if (!input) {
            continue;
        }
        uint32_t seq = input->move_seq();
        if (seq <= entity.move_seq) {
            continue;
        }
        if (latest == nullptr || seq > best_seq) {
            latest = &input;
            best_seq = seq;
        }
    }
    return latest;
}

template<typename MoveInputPtrContainer>
bool apply_latest_move_input(Entity& entity,
                             const MoveInputPtrContainer& move_inputs,
                             float dt_seconds,
                             float move_speed) {
    auto latest_ptr = pick_latest_valid_input(entity, move_inputs);
    if (latest_ptr == nullptr) {
        return false;
    }
    const auto& input = **latest_ptr;
    entity.move_seq = input.move_seq();
    entity.rotation.yaw = common::protocol::util::unpack_yaw_to_radians(input.rotation().packed());
    entity.rotation.pitch = common::protocol::util::unpack_pitch_to_radians(input.rotation().packed());
    Vec2 local_dir = key_mask_to_local_dir(input.key_mask());
    Vec2 dir = rotate_local_to_world(local_dir, entity.rotation.yaw);
    if (vec2_length(dir) <= 0.00001f) {
        return false;
    }
    float distance = move_speed * dt_seconds;
    entity.position.x += dir.x * distance;
    entity.position.z += dir.y * distance;
    return true;
}

} // namespace battle