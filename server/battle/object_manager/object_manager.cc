// current dir
#include "object.hh"
#include "object_manager.hh"
#include "entities_redis.hh"
#include "move.hh"
// std lib
#include <chrono>
// third party 
#include "lockfree/lockfree.hh"
// common
#include <memory_reuse.hh>
#include <protocol.hh>
#include <log.hh>
// proto
#include "forward.pb.h"
#include "ids.pb.h"
#include "internal.pb.h"
#include "move.pb.h"
#include "state.pb.h"

namespace battle{

ObjectManager::ObjectManager(int tick, const std::shared_ptr<sw::redis::Redis>& redis)
    : tick_(tick)
    , timer_(io_context_)
    , msg_queue_(lockfree::K2)
    , redis_opt_(redis)
    , obj_thread_(std::bind(&ObjectManager::thread_handle, this))
{
    do_timer();
}
ObjectManager::~ObjectManager()
{
    obj_thread_.join();
}

void ObjectManager::push_msg(const std::shared_ptr<mmo::transport::GatewayToServer>& msg){
    while(!msg_queue_.try_put(msg));
}
void ObjectManager::set_send_message_to_player_callback(std::function<void(uint64_t, std::shared_ptr<std::vector<uint8_t>>)> callback){
    send_message_to_player_callback_ = callback;
}
void ObjectManager::set_send_message_to_server_callback(std::function<void(uint64_t, std::shared_ptr<std::vector<uint8_t>>)> callback){
    send_message_to_server_callback_ = callback;
}

void ObjectManager::stop(){
    running_ = false;
}

void ObjectManager::send_message(uint64_t player_id, uint32_t direction, uint32_t module, uint32_t action, std::shared_ptr<std::vector<uint8_t>> body){
    if (!send_message_to_player_callback_) {
        return;
    }
    auto msg = memory_reuse::get_object<mmo::transport::ServerToGateway>();
    msg->Clear();
    msg->set_player_id(player_id);
    uint32_t cmd = 0;
    common::protocol::util::set_direction(cmd, direction);
    common::protocol::util::set_module(cmd, module);
    common::protocol::util::set_action(cmd, action);
    msg->set_cmd(cmd);
    if (body && !body->empty()) {
        msg->set_body(body->data(), body->size());
    } else {
        msg->set_body("");
    }

    // serializer 
    size_t size = msg->ByteSizeLong();
    auto buffer = memory_reuse::get_buffer<uint8_t>(size);
    buffer->resize(size);
    msg->SerializeToArray(buffer->data(), size);
    // push_msg(msg);
    send_message_to_player_callback_(player_id, buffer);
}

void ObjectManager::do_timer(){
    if (!running_) {
        return;
    }
    timer_.expires_after(std::chrono::milliseconds(1000 / tick_));
    timer_.async_wait(std::bind(&ObjectManager::handle_event, this));
}

void ObjectManager::thread_handle(){
    io_context_.run();
}
void ObjectManager::handle_event(){
    do_timer();
    handle_msg();
    ++frame_;
    handle_update();
}
void ObjectManager::handle_msg(){
    std::shared_ptr<mmo::transport::GatewayToServer> msg;
    size_t size = msg_queue_.size_approx();
    for (size_t i = 0; i < size; i++) {
        if (!msg_queue_.try_get(msg)) {
            continue;
        }
        // handle msg
        uint32_t cmd = msg->header().cmd();
        uint32_t module = common::protocol::util::get_module(cmd);
        uint32_t action = common::protocol::util::get_action(cmd);
        switch (module){
            // internal module 
            case mmo::ids::Module::INTERNAL:{
                if (action == mmo::ids::IA_PRELOAD_ENTITY_REQ){
                    // handle login
                    handle_perload_entity(msg->header().player_id(), msg->body());
                }
                break;
            }
            // auth module
            case mmo::ids::Module::AUTH:{
                if (action == mmo::ids::AUTH_BIND_REQ){
                    // handle login
                    handle_login(msg->header().player_id());
                }
                break;
            }
            // move module
            case mmo::ids::Module::MOVE:{
                if (action == mmo::ids::MOVE_INPUT_REQ){
                    // handle move
                    handle_move(msg->header().player_id(), msg->body());
                }
                break;
            }
            default:{
                errorlog("unknown module: %d", module);
                break;
            }
        }   
    }
}

void ObjectManager::handle_update(){
    std::vector<Entity*> moved_players;
    moved_players.reserve(entities_.size());
    for (auto& [player_id, move_inputs] : move_inputs_) {
        if (move_inputs.empty()) {
            continue;
        }
        if (!entities_.count(player_id)) {
            move_inputs.clear();
            continue;
        }
        Entity& entity = entities_[player_id];
        // handle move input
        bool move = apply_latest_move_input(entity, move_inputs, 1.0f / tick_, MOVE_SPEED);
        if (move) {
            moved_players.push_back(&entity);
        }
        move_inputs.clear();
    }
    // aoi update
    aoi_update(moved_players);
}

void ObjectManager::aoi_update(const std::vector<Entity*>& moved_players){
    // node interface call
    std::unordered_map<uint64_t, AoiResult> result;
    node_->aoi_update(moved_players, result);
    // handle result
    for (auto& [player_id, aoi_result] : result) {
        // handle enter entities
        auto frame = memory_reuse::get_object<mmo::state::S2C_StateFrame>();
        frame->Clear();
        frame->set_frame(frame_);
        for (auto& enter_entity : aoi_result.enter_entities) {
            // handle enter entity
            set_entity_fields(
                frame->add_entities(),
                entities_[enter_entity], 
                mmo::state::EntityType::PLAYER, 
                mmo::state::EntityState::ES_IN_VIEW
            );
        }
        for (auto& leave_entity : aoi_result.leave_entity_ids) {
            // handle leave entity
            set_entity_fields(
                frame->add_entities(),
                entities_[leave_entity], 
                mmo::state::EntityType::PLAYER, 
                mmo::state::EntityState::ES_OUT_OF_VIEW
            );
        }
        for (auto& move_entity : aoi_result.update_entities) {
            // handle move entity
            set_entity_fields(
                frame->add_entities(),
                entities_[move_entity], 
                mmo::state::EntityType::PLAYER, 
                mmo::state::EntityState::ES_UPDATE
            );
        }
        if (frame->entities_size() == 0) {
            continue;
        }
        size_t size = frame->ByteSizeLong();
        auto body = memory_reuse::get_buffer<uint8_t>(size);
        body->resize(size);
        frame->SerializeToArray(body->data(), size);
        send_message(
            player_id,
            mmo::ids::BATTLE2GW,
            mmo::ids::Module::STATE,
            mmo::ids::StateAction::STATE_FRAME_NTF,
            body
        );
    }
}

void ObjectManager::set_entity_fields(mmo::state::EntitySnapshot* entity, const Entity& entity_info, mmo::state::EntityType type, mmo::state::EntityState state){
    entity->set_entity_id(entity_info.player_id);
    entity->set_type(type);
    entity->set_state(state);
    auto pos = entity->mutable_pos();
    pos->set_x(common::protocol::util::itos(entity_info.position.x, DEFAULT_SCALE));
    pos->set_y(common::protocol::util::itos(entity_info.position.y, DEFAULT_SCALE));
    pos->set_z(common::protocol::util::itos(entity_info.position.z, DEFAULT_SCALE));
    pos->set_scale(DEFAULT_SCALE);
    auto look = entity->mutable_look();
    look->set_packed(common::protocol::util::pack_yaw_pitch(entity_info.rotation.yaw, entity_info.rotation.pitch));
}



void ObjectManager::handle_perload_entity(uint64_t player_id, const std::string& body){
    auto info = memory_reuse::get_object<mmo::internal::PreloadEntityInfo>();
    info->Clear();
    if (!info->ParseFromString(body)){
        errorlog("parse preload entity info failed");
        return;
    }
    // handle preload entity info
    Entity& entity = wait_bind_entities_[player_id];
    entity.player_id = player_id;
    entity.position.x = info->x();
    entity.position.y = info->y();
    entity.position.z = info->z();
    entity.rotation.yaw = info->yaw();
    entity.rotation.pitch = info->pitch();
    entity.rotation.roll = info->roll();
}

void ObjectManager::handle_login(uint64_t player_id){
    if (entities_.count(player_id)) {
        // 已经登录过了, 或者是短线重连
        return;
    }
    if (!wait_bind_entities_.count(player_id)) {
        // 未 预加载实体信息
        errorlog("player information not found : {}", player_id);
        send_message(
            player_id, 
            mmo::ids::BATTLE2GW,
            mmo::ids::Module::ERROR, 
            mmo::ids::EA_AUTH_BIND_REQ_FAILED);
        return;
    }
    // 登录成功
    entities_[player_id] = wait_bind_entities_[player_id];
    wait_bind_entities_.erase(player_id);
    // add entity to node
    node_->add_entity(&entities_[player_id]);
}

void ObjectManager::handle_move(uint64_t player_id, const std::string& body){
    auto input = memory_reuse::get_object<mmo::move::C2S_MoveInput>();
    input->Clear();
    if (!input->ParseFromString(body)){
        errorlog("parse move input failed {}", player_id);
        return;
    }
    // handle move input
    if (!entities_.count(player_id)) {
        return;
    }
    move_inputs_[player_id].push_back(input);
}


} // namesapce battle
