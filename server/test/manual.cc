

#include <cstdint>
#include <log.hh>
#include <random>
#include "httplib.h"
#include <akcp/client.hh>
#include <string>
#include "ids.pb.h"
#include "login.pb.h"
#include "memory_reuse.hh"
#include "envelope.pb.h"
#include "protocol.hh"
#include "move.pb.h"
#include "state.pb.h"
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <iomanip>
#include <thread>

class KeyDetector {
public:
    KeyDetector() {
        // 保存原始终端设置
        tcgetattr(STDIN_FILENO, &old_t);
        new_t = old_t;
        // 关闭行缓冲 (ICANON) 和回显 (ECHO)
        new_t.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &new_t);
        
        // 设置为非阻塞模式
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }

    ~KeyDetector() {
        // 恢复终端设置
        tcsetattr(STDIN_FILENO, TCSANOW, &old_t);
    }

    int checkKey() {
        unsigned char ch;
        if (read(STDIN_FILENO, &ch, 1) > 0) {
            return ch;
        }
        return -1;
    }

private:
    struct termios old_t, new_t;
};
// 检测按键
KeyDetector detector;
/*
    w : 119
    s : 115
    a : 97
    d : 100
*/ 

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

uint64_t player_id = 0;
uint64_t player_token = 0;
uint64_t seq = 0;
std::atomic_bool linking {false};

void send_message(kcp::channel_view channel){
    // infolog("timer task handler");
    auto envelope = memory_reuse::get_object<mmo::transport::Envelope>();
    envelope->Clear();
    auto head = envelope->mutable_header();
    head->set_version(1);
    head->set_player_id(player_id);
    head->set_token(std::to_string(player_token));
    head->set_seq(++seq);
    uint32_t cmd = 0;
    common::protocol::util::set_direction(cmd,mmo::ids::Direction::C2G);
    common::protocol::util::set_module(cmd,mmo::ids::Module::MOVE);
    common::protocol::util::set_action(cmd, mmo::ids::MOVE_INPUT_REQ);
    head->set_cmd(cmd);
    auto move = memory_reuse::get_object<mmo::move::C2S_MoveInput>();
    move->Clear();
    move->set_move_seq(seq);
    move->set_entity_id(player_id);
    // 获取按键信息
    int key = detector.checkKey();
    if (key == 'w') {
        move->set_key_mask(1 << 0);
        // std::cout << "\r当前状态: 向前进" << std::flush;
    }else if (key == 's') {
        // std::cout << "\r当前状态: 向后退" << std::flush;
        move->set_key_mask(1 << 1);
    }else if (key == 'a') {
        // std::cout << "\r当前状态: 向左走" << std::flush;
        move->set_key_mask(1 << 2);
    }else if (key == 'd') {
        // std::cout << "\r当前状态: 向右走" << std::flush;
        move->set_key_mask(1 << 3);
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
        linking = false;
        auto envelope = memory_reuse::get_object<mmo::transport::Envelope>();
        envelope->Clear();
        auto head = envelope->mutable_header();
        head->set_version(1);
        head->set_player_id(player_id);
        head->set_token(std::to_string(player_token));
        head->set_seq(seq);
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
float per_x = 0, per_y = 0, per_z = 0;
void on_message(kcp::channel_view channel, kcp::packet packet){
    auto envelope = memory_reuse::get_object<mmo::transport::Envelope>();
    envelope->Clear();
    if (!envelope->ParseFromArray(packet->data(), packet->size())){
        errorlog("failed to parse envelope");
    }
    auto frame = memory_reuse::get_object<mmo::state::S2C_StateFrame>();
    frame->Clear();
    if (!frame->ParseFromString(envelope->body())){
        errorlog("failed to parse state frame");
    }
    int player_num = frame->entities_size() - 1;
    float x = 0, y = 0, z = 0;
    std::vector<uint64_t> enter_ids;
    std::vector<uint64_t> exit_ids;
    for (int i = 0; i <= player_num; i++){
        auto& entity = frame->entities(i);
        if (entity.entity_id() == player_id){
            // x = 1.0 * entity.pos().x() / entity.pos().scale();
            x = common::protocol::util::itof(entity.pos().x(), entity.pos().scale());
            // y = 1.0 * entity.pos().y() / entity.pos().scale();
            y = common::protocol::util::itof(entity.pos().y(), entity.pos().scale());
            // z = 1.0 * entity.pos().z() / entity.pos().scale();
            z = common::protocol::util::itof(entity.pos().z(), entity.pos().scale());
        }
        if (entity.state() == mmo::state::EntityState::ES_IN_VIEW){
            enter_ids.push_back(entity.entity_id());
        }else if (entity.state() == mmo::state::EntityState::ES_OUT_OF_VIEW){
            exit_ids.push_back(entity.entity_id());
        }
    }
    if (x != 0 || y != 0 || z != 0){
        per_x = x;
        per_y = y;
        per_z = z;
    }
    // std::cout << "\n";
    std::cout << "\r id:" << player_id << " x:" << per_x << " y:" << per_y << " z:" << per_z;
    std::cout << " look_num:" << player_num;
    std::cout << " enter_ids:" << enter_ids.size();
    std::cout << " exit_ids:" << exit_ids.size();
    std::cout  << std::flush;
    // std::cout << "\n";
}


void create_client(int n) {
    kcp::client client;
    client.set_connect_callback(on_connect);
    client.set_message_callback(on_message);
    // 创建 http 客户端发起登录请求
    httplib::Client http_client("127.0.0.1:10000");
    auto login_req = memory_reuse::get_object<mmo::login::C2L_LoginReq>();
    login_req->Clear();
    login_req->set_account("user" + std::to_string(n));
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
        errorlog("error to login for : ");
    }
    auto result_pro = memory_reuse::get_object<mmo::login::L2C_LoginRsp>();
    result_pro->Clear();
    if (!result_pro->ParseFromArray(result->body.data(), result->body.size())){
        errorlog("failed to parse  ");
    }
    if (!result_pro->ok()){
        errorlog("failed to login for : {}", result_pro->reason());
    }
    // 创建 akcp udp 客户端进行通信
    std::string host = result_pro->gateway_host();
    size_t pos = host.find(":");
    std::string ip = host.substr(0, pos);
    int port = std::stoi(host.substr(pos + 1));
    player_id = result_pro->player_id();
    player_token = result_pro->session_id();
    infolog << "ip : " << ip << " port : " << port;
    client.connect(ip, port,true);
    linking = true;
    while(linking.load());
}

int main(int argc, char* argv[]){

    if (argc != 2) {
        exit(1);
    }
    std::cout << std::fixed << std::setprecision(2);
    int n = std::stoi(argv[1]);
    create_client(n);
    std::this_thread::sleep_for(std::chrono::seconds(10000000));
    return 0;
}