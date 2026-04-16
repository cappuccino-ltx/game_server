#include "grid_aoi_node.hh"

#include <algorithm>
#include <cmath>
#include <utility>

namespace battle {

namespace {

constexpr float kMinCellSize = 1.0f;

} // namespace

size_t CellKeyHash::operator()(const CellKey& key) const {
    size_t seed = static_cast<size_t>(static_cast<uint32_t>(key.gx));
    seed ^= static_cast<size_t>(static_cast<uint32_t>(key.gz)) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
}

GridAoiNode::GridAoiNode(float cell_size, float view_radius)
    : cell_size_(std::max(cell_size, kMinCellSize))
    , view_radius_(std::max(view_radius, cell_size_))
    , view_radius_sq_(view_radius_ * view_radius_)
{}

void GridAoiNode::add_entity(const Entity* entity) {
    if (entity == nullptr) {
        return;
    }

    auto it = players_.find(entity->player_id);
    if (it != players_.end()) {
        erase_from_cell(entity->player_id, it->second.cell);
    }

    AoiPlayerState state;
    state.player_id = entity->player_id;
    state.position = entity->position;
    state.cell = world_to_cell(entity->position);
    players_[entity->player_id] = std::move(state);
    cells_[players_[entity->player_id].cell].insert(entity->player_id);
    pending_full_sync_.insert(entity->player_id);
}

void GridAoiNode::remove_entity(const Entity* entity) {
    if (entity == nullptr) {
        return;
    }

    auto it = players_.find(entity->player_id);
    if (it == players_.end()) {
        return;
    }

    for (uint64_t other_id : it->second.visible_entities) {
        auto other_it = players_.find(other_id);
        if (other_it == players_.end()) {
            continue;
        }
        other_it->second.visible_entities.erase(entity->player_id);
    }

    erase_from_cell(entity->player_id, it->second.cell);
    pending_full_sync_.erase(entity->player_id);
    players_.erase(it);
}

void GridAoiNode::aoi_update(const std::vector<Entity*>& moved_players, std::unordered_map<uint64_t, AoiResult>& result) {
    // 1. 先把本帧发生移动的玩家整理成去重后的集合，避免同一玩家在一帧内被重复处理。
    std::unordered_map<uint64_t, const Entity*> dirty_entities;
    dirty_entities.reserve(moved_players.size());
    for (const Entity* entity : moved_players) {
        if (entity == nullptr) {
            continue;
        }
        dirty_entities[entity->player_id] = entity;
    }

    // 2. 把脏玩家的最新位置同步到 AOI 内部索引里；如果玩家还没进 AOI 系统，这里顺手补注册。
    for (const auto& [player_id, entity] : dirty_entities) {
        if (!players_.count(player_id)) {
            add_entity(entity);
        }
        update_player_transform(*entity);
    }

    // 3. 确定这一帧真正需要重算 AOI 的玩家：
    //    包括本帧移动过的玩家，以及刚加入场景、等待首次全量同步的玩家。
    std::unordered_set<uint64_t> dirty_player_ids = pending_full_sync_;
    for (const auto& [player_id, entity] : dirty_entities) {
        (void)entity;
        dirty_player_ids.insert(player_id);
    }

    if (dirty_player_ids.empty()) {
        return;
    }

    // 4. 对每个脏玩家重算“新的可见集合”：
    //    先从九宫格收集候选玩家，再用真实距离做一次精确过滤，避免仅靠格子造成误判。
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> recalculated_visible_sets;
    recalculated_visible_sets.reserve(dirty_player_ids.size());

    for (uint64_t player_id : dirty_player_ids) {
        auto player_it = players_.find(player_id);
        if (player_it == players_.end()) {
            continue;
        }
        // 先从九宫格中找到所有候选的玩家
        PlayerSet candidates;
        collect_candidates(player_it->second.cell, candidates);
        // candidates.erase(player_id);

        PlayerSet new_visible;
        for (uint64_t candidate_id : candidates) {
            auto candidate_it = players_.find(candidate_id);
            if (candidate_it == players_.end()) {
                continue;
            }
            // 通过实际距离判断是否可见，避免仅靠格子造成误判
            if (is_in_view(player_it->second.position, candidate_it->second.position)) {
                new_visible.insert(candidate_id);
            }
        }
        recalculated_visible_sets[player_id] = std::move(new_visible);
    }

    // 5. 把“旧可见集合”和“新可见集合”做差集，生成这帧的 enter / leave / update 结果。
    //    同时补齐其他观察者视角下对该玩家的变化，这样双方都能收到正确的 AOI 通知。
    for (uint64_t player_id : dirty_player_ids) {
        auto player_it = players_.find(player_id);
        auto new_visible_it = recalculated_visible_sets.find(player_id);
        if (player_it == players_.end() || new_visible_it == recalculated_visible_sets.end()) {
            continue;
        }

        const bool full_sync = pending_full_sync_.count(player_id) > 0;
        const auto old_visible = player_it->second.visible_entities;
        const auto& new_visible = new_visible_it->second;
        auto& self_result = result[player_id];
        // 判断是否是首次全量同步
        if (full_sync) {
            self_result.full_sync = true;
        }
        // 对当前的实体视野进行处理
        // 处理进入视野的玩家
        for (uint64_t other_id : new_visible) {
            if (full_sync || !old_visible.count(other_id)) {
                append_unique(self_result.enter_entities, other_id);
            }
        }
        // 如果不是首次进行 aoi 更新, 那就需要处理离开视野的玩家, 并更新其他玩家的可见集合
        if (!full_sync) {
            for (uint64_t other_id : old_visible) {
                if (!new_visible.count(other_id)) {
                    append_unique(self_result.leave_entity_ids, other_id);
                }
            }
        }
        // 处理因为位置移动导致的视野变化的其他玩家
        // 处理进入视野的玩家
        for (uint64_t other_id : new_visible) {
            auto other_it = players_.find(other_id);
            if (other_it == players_.end()) {
                continue;
            }

            if (!old_visible.count(other_id)) {
                append_unique(result[other_id].enter_entities, player_id);
                if (!dirty_player_ids.count(other_id)) {
                    other_it->second.visible_entities.insert(player_id);
                }
            } else {
                append_unique(result[other_id].update_entities, player_id);
            }
        }
        // 处理因为位置移动导致的视野变化的其他玩家
        // 处理离开视野的玩家
        for (uint64_t other_id : old_visible) {
            if (new_visible.count(other_id)) {
                continue;
            }

            auto other_it = players_.find(other_id);
            if (other_it == players_.end()) {
                continue;
            }

            append_unique(result[other_id].leave_entity_ids, player_id);
            if (!dirty_player_ids.count(other_id)) {
                other_it->second.visible_entities.erase(player_id);
            }
        }
    }

    // 6. 在所有结果都生成之后，统一回写最新可见集合，并清掉首次全量同步标记，
    //    保证本帧计算过程始终基于旧状态，不会被中途写回打乱。
    for (uint64_t player_id : dirty_player_ids) {
        auto player_it = players_.find(player_id);
        auto new_visible_it = recalculated_visible_sets.find(player_id);
        if (player_it == players_.end() || new_visible_it == recalculated_visible_sets.end()) {
            continue;
        }
        player_it->second.visible_entities = new_visible_it->second;
        pending_full_sync_.erase(player_id);
    }
}

CellKey GridAoiNode::world_to_cell(const Vector3& position) const {
    return {
        static_cast<int32_t>(std::floor(position.x / cell_size_)),
        static_cast<int32_t>(std::floor(position.z / cell_size_))
    };
}

bool GridAoiNode::is_in_view(const Vector3& lhs, const Vector3& rhs) const {
    const float dx = lhs.x - rhs.x;
    const float dz = lhs.z - rhs.z;
    return dx * dx + dz * dz <= view_radius_sq_;
}

void GridAoiNode::update_player_transform(const Entity& entity) {
    auto it = players_.find(entity.player_id);
    if (it == players_.end()) {
        return;
    }

    const CellKey new_cell = world_to_cell(entity.position);
    if (!(it->second.cell == new_cell)) {
        move_between_cells(entity.player_id, it->second.cell, new_cell);
        it->second.cell = new_cell;
    }
    it->second.position = entity.position;
}

void GridAoiNode::move_between_cells(uint64_t player_id, const CellKey& old_cell, const CellKey& new_cell) {
    erase_from_cell(player_id, old_cell);
    cells_[new_cell].insert(player_id);
}

void GridAoiNode::erase_from_cell(uint64_t player_id, const CellKey& cell) {
    auto cell_it = cells_.find(cell);
    if (cell_it == cells_.end()) {
        return;
    }

    cell_it->second.erase(player_id);
    if (cell_it->second.empty()) {
        cells_.erase(cell_it);
    }
}

void GridAoiNode::collect_candidates(const CellKey& center_cell, PlayerSet& candidates) const {
    for (int32_t dz = -1; dz <= 1; ++dz) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const CellKey cell{center_cell.gx + dx, center_cell.gz + dz};
            auto it = cells_.find(cell);
            if (it == cells_.end()) {
                continue;
            }
            candidates.insert(it->second.begin(), it->second.end());
        }
    }
}

void GridAoiNode::append_unique(std::vector<uint64_t>& ids, uint64_t value) {
    if (std::find(ids.begin(), ids.end(), value) == ids.end()) {
        ids.push_back(value);
    }
}

std::shared_ptr<node_interface> create_grid_aoi_node(float cell_size, float view_radius) {
    return std::make_shared<GridAoiNode>(cell_size, view_radius);
}

} // namespace battle
