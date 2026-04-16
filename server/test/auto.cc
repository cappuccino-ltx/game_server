

#include <chrono>
#include <cstdint>
#include <log.hh>
#include <mutex>
#include <random>
#include "httplib.h"
#include <akcp/client.hh>
#include <string>
#include <thread>
#include <unordered_map>
#include "ids.pb.h"
#include "login.pb.h"
#include "memory_reuse.hh"
#include "envelope.pb.h"
#include "protocol.hh"
#include "move.pb.h"

/*

enum MoveKeyMask : uint32_t {
    MOVE_FORWARD = 1 << 0,
    MOVE_BACK    = 1 << 1,
    MOVE_LEFT    = 1 << 2,
    MOVE_RIGHT   = 1 << 3,
};

*/



static int number(size_t min, size_t max) {
    // 1. ⽣成⼀个机器随机数，作为伪随机数种⼦
    static std::random_device rd;
    // 2. 根据种⼦，构造伪随机数引擎
    static std::mt19937 generator(rd());
    // 3. ⽣成伪随机数
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(generator);
}

uint64_t global_player_id = 0;
uint64_t global_player_token = 0;
std::unordered_map<kcp::channel_view, std::pair<uint64_t, uint64_t>> tokens;
std::unordered_map<kcp::channel_view, size_t> seqs_;
std::unordered_map<kcp::channel_view, size_t> keep_active;
std::unordered_map<kcp::channel_view, size_t> message_count;
std::atomic_bool linking {false};

void send_message(kcp::channel_view channel){
    // infolog("timer task handler");
    auto envelope = memory_reuse::get_object<mmo::transport::Envelope>();
    envelope->Clear();
    auto head = envelope->mutable_header();
    head->set_version(1);
    head->set_player_id(tokens[channel].first);
    head->set_token(std::to_string(tokens[channel].second));
    head->set_seq(++seqs_[channel]);
    uint32_t cmd = 0;
    common::protocol::util::set_direction(cmd,mmo::ids::Direction::C2G);
    common::protocol::util::set_module(cmd,mmo::ids::Module::MOVE);
    common::protocol::util::set_action(cmd, mmo::ids::MOVE_INPUT_REQ);
    head->set_cmd(cmd);
    auto move = memory_reuse::get_object<mmo::move::C2S_MoveInput>();
    move->Clear();
    move->set_move_seq(seqs_[channel]);
    move->set_entity_id(tokens[channel].first);
    if ((keep_active[channel] & 0x00ffffff) == 0) {
        keep_active[channel] = number(0, 1500);
        int yaw = keep_active[channel] % 4;
        keep_active[channel] &= yaw << 30;
        move->set_key_mask(1 << yaw);
    }else {
        int yaw = keep_active[channel]-- >> 30;
        move->set_key_mask(1 << yaw);
    }
    move->SerializeToString(envelope->mutable_body());

    size_t size = envelope->ByteSizeLong();
    auto buffer = memory_reuse::get_buffer<uint8_t>(size);
    buffer->resize(size);
    envelope->SerializeToArray(buffer->data(), buffer->size());
    channel->send(buffer);
    channel->timer_task([channel](){
        send_message(channel);
    }, 30);
}

void on_connect(kcp::channel_view channel, bool linked){
    if (linked){
        infolog << "链接成功";
        tokens[channel] = {global_player_id,global_player_token};
        seqs_[channel] = 1;
        keep_active[channel] = 0;
        message_count[channel] = 0;
        linking = false;
        auto envelope = memory_reuse::get_object<mmo::transport::Envelope>();
        envelope->Clear();
        auto head = envelope->mutable_header();
        head->set_version(1);
        head->set_player_id(tokens[channel].first);
        head->set_token(std::to_string(tokens[channel].second));
        head->set_seq(seqs_[channel]);
        uint32_t cmd = 0;
        common::protocol::util::set_direction(cmd,mmo::ids::Direction::C2G);
        common::protocol::util::set_module(cmd,mmo::ids::Module::AUTH);
        common::protocol::util::set_action(cmd, mmo::ids::AUTH_BIND_REQ);
        head->set_cmd(cmd);
        size_t size = envelope->ByteSizeLong();
        auto buffer = memory_reuse::get_buffer<uint8_t>(size);
        buffer->resize(size);
        envelope->SerializeToArray(buffer->data(), buffer->size());
        channel->send(buffer);
        channel->timer_task([channel](){
            send_message(channel);
        }, 30);
    } else {
        warninglog << "断开连接" ;
    }
}
void on_message(kcp::channel_view channel, kcp::packet packet){
    message_count[channel]++;
    if (message_count[channel] % 20 == 0) {
        infolog << "message count : " << message_count[channel];
    }
}


void create_client(int n) {
    kcp::client client;
    client.set_connect_callback(on_connect);
    client.set_message_callback(on_message);
    for (int i = 1000; i < 1000 + n; i++) {
        // 创建 http 客户端发起登录请求
        httplib::Client http_client("127.0.0.1:10000");
        auto login_req = memory_reuse::get_object<mmo::login::C2L_LoginReq>();
        login_req->Clear();
        login_req->set_account("user" + std::to_string(i));
        login_req->set_password("123123");
        size_t size = login_req->ByteSizeLong();
        auto buffer = memory_reuse::get_buffer<uint8_t>(size);
        buffer->resize(size);
        login_req->SerializeToArray(buffer->data(), buffer->size());
        ////////   debug
        auto login_req1 = memory_reuse::get_object<mmo::login::C2L_LoginReq>();
        login_req1->Clear();
        if(login_req1->ParseFromArray(buffer->data(), buffer->size())){
            infolog << "account : " << login_req1->account() << " password : " << login_req1->password();
        }
        ////////   debug

        auto result = http_client.Post("/user/login",(const char*)buffer->data(),buffer->size(),"application/protobuf");
        if (!result) {
            errorlog("error to login for : {}", i);
            continue;
        }
        auto result_pro = memory_reuse::get_object<mmo::login::L2C_LoginRsp>();
        result_pro->Clear();
        if (!result_pro->ParseFromArray(result->body.data(), result->body.size())){
            errorlog("failed to parse for : {}", i);
            continue;
        }
        if (!result_pro->ok()){
            errorlog("failed to login for : {}, {}", i, result_pro->reason());
            continue;
        }
        // 创建 akcp udp 客户端进行通信
        std::string host = result_pro->gateway_host();
        size_t pos = host.find(":");
        std::string ip = host.substr(0, pos);
        int port = std::stoi(host.substr(pos + 1));
        global_player_id = result_pro->player_id();
        global_player_token = result_pro->session_id();
        infolog << "ip : " << ip << " port : " << port;
        client.connect(ip, port,true);
        linking = true;
        while(linking.load());
    }
    std::this_thread::sleep_for(std::chrono::seconds(10000));
}

int main(int argc, char* argv[]){

    if (argc != 2) {
        exit(1);
    }
    int n = std::stoi(argv[1]);
    create_client(n);

    return 0;
}