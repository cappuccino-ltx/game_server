

#pragma once

#include <unordered_map>
#include <vector>
#include <cstdint>
#include <memory>

namespace battle{
class Entity;

struct AoiResult {
    // 首次进入场景时可置 true
    bool full_sync{false};
    // 新进入视野的实体
    std::vector<uint64_t> enter_entities;
    // 仍在视野中，但这一帧状态有变化的实体
    std::vector<uint64_t> update_entities;
    // 离开视野的实体 id
    std::vector<uint64_t> leave_entity_ids;
};

class node_interface{
public:
    virtual ~node_interface() = default;
    // add entity
    virtual void add_entity(const Entity* entity) = 0;
    // remove entity
    virtual void remove_entity(const Entity* entity) = 0;
    // aoi update
    virtual void aoi_update(const std::vector<Entity*>& moved_players, std::unordered_map<uint64_t, AoiResult>& result) = 0;
};

std::shared_ptr<node_interface> create_grid_aoi_node(float cell_size = 30.0f, float view_radius = 30.0f);

} // namespace battle
