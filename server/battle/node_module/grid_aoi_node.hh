#pragma once

#include "node.hh"
#include "object_manager/object.hh"
#include <unordered_map>
#include <unordered_set>

namespace battle {

struct CellKey {
    int32_t gx{0};
    int32_t gz{0};

    bool operator==(const CellKey& other) const {
        return gx == other.gx && gz == other.gz;
    }
};

struct CellKeyHash {
    size_t operator()(const CellKey& key) const;
};

struct AoiPlayerState {
    uint64_t player_id{0};
    Vector3 position{};
    CellKey cell{};
    std::unordered_set<uint64_t> visible_entities;
};

class GridAoiNode final : public node_interface {
public:
    explicit GridAoiNode(float cell_size = 30.0f, float view_radius = 30.0f);

    void add_entity(const Entity* entity) override;
    void remove_entity(const Entity* entity) override;
    void aoi_update(const std::vector<Entity*>& moved_players, std::unordered_map<uint64_t, AoiResult>& result) override;

private:
    using PlayerSet = std::unordered_set<uint64_t>;
    using CellPlayers = std::unordered_map<CellKey, PlayerSet, CellKeyHash>;

    CellKey world_to_cell(const Vector3& position) const;
    bool is_in_view(const Vector3& lhs, const Vector3& rhs) const;
    void update_player_transform(const Entity& entity);
    void move_between_cells(uint64_t player_id, const CellKey& old_cell, const CellKey& new_cell);
    void erase_from_cell(uint64_t player_id, const CellKey& cell);
    void collect_candidates(const CellKey& center_cell, PlayerSet& candidates) const;
    static void append_unique(std::vector<uint64_t>& ids, uint64_t value);

private:
    float cell_size_{30.0f};
    float view_radius_{30.0f};
    float view_radius_sq_{900.0f};
    CellPlayers cells_;
    std::unordered_map<uint64_t, AoiPlayerState> players_;
    std::unordered_set<uint64_t> pending_full_sync_;
};

} // namespace battle
