

#include "entities_redis.hh"
#include <boost/asio/executor_work_guard.hpp>
#include <functional>
#include <string>
#include <world.hh>

namespace battle{

RedisOpt::RedisOpt(const std::shared_ptr<sw::redis::Redis>& redis)
    :redis_(redis)
    ,work_(boost::asio::make_work_guard(io_context_))
    ,thread_(std::bind(&RedisOpt::heandler, this))
{}

void RedisOpt::stop(){
    work_.reset();
    io_context_.stop();
}

// bool RedisOpt::get_entity(uint64_t player_id, Entity& entity){
//     std::unordered_map<std::string, std::string> fields;
//     if (redis_.hgetall(WORLD_ENTITY_KEY + std::to_string(player_id), fields)){
//         entity.position.x = std::stof(fields[WORLD_ENTITY_FIELD_X]);
//         entity.position.y = std::stof(fields[WORLD_ENTITY_FIELD_Y]);
//         entity.position.z = std::stof(fields[WORLD_ENTITY_FIELD_Z]);
//         entity.rotation.yaw = std::stof(fields[WORLD_ENTITY_FIELD_YAW]);
//         entity.rotation.pitch = std::stof(fields[WORLD_ENTITY_FIELD_PITCH]);
//         entity.rotation.roll = std::stof(fields[WORLD_ENTITY_FIELD_ROLL]);
//         return true;
//     }
//     return false;
// }

void RedisOpt::set_entity(uint64_t player_id, const Entity& entity) {
    io_context_.post([this, player_id, entity](){
        set_entity_internal(player_id, entity);
    });
}
void RedisOpt::set_entity_internal(uint64_t player_id, const Entity& entity){
    std::unordered_map<std::string, std::string> fields;
    fields[WORLD_ENTITY_FIELD_X] = std::to_string(entity.position.x);
    fields[WORLD_ENTITY_FIELD_Y] = std::to_string(entity.position.y);
    fields[WORLD_ENTITY_FIELD_Z] = std::to_string(entity.position.z);
    fields[WORLD_ENTITY_FIELD_YAW] = std::to_string(entity.rotation.yaw);
    fields[WORLD_ENTITY_FIELD_PITCH] = std::to_string(entity.rotation.pitch);
    fields[WORLD_ENTITY_FIELD_ROLL] = std::to_string(entity.rotation.roll);
    redis_.hsetall(WORLD_ENTITY_KEY + std::to_string(player_id), fields);
}

void RedisOpt::heandler(){
    io_context_.run();
}

} // namespace battle