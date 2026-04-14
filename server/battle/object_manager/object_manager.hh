#pragma once 

#include "entities_redis.hh"
#include "lockfree/lockfree.hh"
#include "object.hh"
#include <boost/asio/system_timer.hpp>
#include <unordered_map>
#include <cstdint>
#include <memory>
#include <thread>
#include <boost/asio.hpp>
#include "forward.pb.h"
#include "move.pb.h"
#include "node_module/node.hh"
#include "state.pb.h"



namespace battle {
class ObjectManager {
public:
    ObjectManager(int tick, const std::shared_ptr<sw::redis::Redis>& redis);
    ~ObjectManager();
    void push_msg(const std::shared_ptr<mmo::transport::GatewayToServer>& msg);
    void set_send_message_to_player_callback(std::function<void(uint64_t, std::shared_ptr<std::vector<uint8_t>>)> callback);
    void set_send_message_to_server_callback(std::function<void(uint64_t, std::shared_ptr<std::vector<uint8_t>>)> callback);
    void stop();
    inline void set_node(std::shared_ptr<node_interface> node){
        node_ = node;
    }
private:
    void send_message(uint64_t player_id, uint32_t direction, uint32_t module, uint32_t action, std::shared_ptr<std::vector<uint8_t>> msg = nullptr);
    // timer
    void do_timer();
    // thread
    void thread_handle();
    // event
    void handle_event();
    void handle_msg();
    void handle_update();
    void aoi_update(const std::vector<Entity*>& moved_players);
    // handle login 
    void handle_perload_entity(uint64_t player_id, const std::string& body);
    void handle_login(uint64_t player_id);
    // handle move
    void handle_move(uint64_t player_id, const std::string& body);

private:
    void set_entity_fields(mmo::state::EntitySnapshot* entity, const Entity& entity_info, mmo::state::EntityType type, mmo::state::EntityState state);

private:
    uint32_t tick_ = 30;
    uint32_t frame_ = 0;
    std::shared_ptr<node_interface> node_{nullptr};
    std::atomic_bool running_ { true };
    boost::asio::io_context io_context_;
    boost::asio::system_timer timer_;
    std::unordered_map<uint64_t, Entity> entities_;
    std::unordered_map<uint64_t, Entity> wait_bind_entities_;
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<mmo::move::C2S_MoveInput>>> move_inputs_;
    std::function<void(uint64_t, std::shared_ptr<std::vector<uint8_t>>)> send_message_to_player_callback_;
    std::function<void(uint64_t, std::shared_ptr<std::vector<uint8_t>>)> send_message_to_server_callback_;
    RedisOpt redis_opt_;
    lockfree::lfree_queue_spsc<std::shared_ptr<mmo::transport::GatewayToServer>> msg_queue_;
    std::thread obj_thread_;
};
}
