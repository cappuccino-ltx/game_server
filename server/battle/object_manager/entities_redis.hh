#pragma once 

#include <boost/asio/io_context.hpp>
#include <redis.hh>
#include "object.hh"

namespace battle{

class RedisOpt{
public:
    RedisOpt(const std::shared_ptr<sw::redis::Redis>& redis);
    void stop();
    // bool get_entity(uint64_t player_id, Entity& entity);
    void set_entity(uint64_t player_id, const Entity& entity);

private:
    void heandler();
    void set_entity_internal(uint64_t player_id, const Entity& entity);

private:
    redis::Redis redis_;
    boost::asio::io_context io_context_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>  work_;
    std::thread thread_;
}; // namespace EntitiesRedisOpt

} // namespace battle