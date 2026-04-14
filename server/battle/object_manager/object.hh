#pragma once 
#include <cstdint>
#include <unordered_set>
namespace battle{

struct Vector3 {
  // X 轴坐标。
  float x;
  // Y 轴坐标。
  float y;
  // Z 轴坐标。
  float z;
};

// 欧拉角旋转信息。
struct Rotation {
  // 偏航角。
  float yaw;
  // 俯仰角。
  float pitch;
  // 翻滚角。
  float roll;
};

struct Entity {
  // 玩家ID。
  uint64_t player_id = 0;
  // 移动序列号。
  uint32_t move_seq = 0;
  // 位置。
  Vector3 position;
  // 旋转。
  Rotation rotation;
  // 可见 id
  std::unordered_set<uint64_t> visible_entities;
};

} // namesapce battle