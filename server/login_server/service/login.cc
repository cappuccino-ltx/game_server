
#include "login.hh"
#include "forward.pb.h"
#include "ids.pb.h"
#include "protocol.hh"
#include "router_id.hh"
#include "internal.pb.h"
#include "util.hh"
#include "authenticate.hh"
#include "mysql.hh"

namespace login{

GatewaySelector::GatewaySelector(const std::string& host, const std::string& base_dir, const std::function<void(uint32_t,const std::string&)>& battle_online_notify) {
    discovery_ = std::make_shared<common::Discovery>();

    discovery_->setHost(host)
        .setBaseDir(base_dir)
        .setUpdateCallback([this,battle_online_notify](const std::string &key, const std::string &value){
            infolog("host {} {}" ,key, value);
            size_t pos = key.rfind("/");
            uint32_t id = std::stoi(key.substr(pos + 1));
            
            if (key.find(ROUTER_ID_GATEWAY) != std::string::npos){
                // 网关
                infolog("host {} {} is gateway" ,key, value);
                std::unique_lock<std::mutex> lock;
                gateways_.push_back({id, value});
            }else if (key.find(ROUTER_ID_BATTLE) != std::string::npos) {
                // battle 
                {
                    infolog("host {} {} is battle" ,key, value);
                    std::unique_lock<std::mutex> lock;
                    battles_.insert({id, value});
                }
                if (battle_online_notify) {
                    battle_online_notify(id, value);
                }
            }
        }).setRemoveCallback([this](const std::string &key, const std::string &value){
            infolog("host {} {}" ,key, value);
            size_t pos = key.rfind("/");
            uint32_t id = std::stoi(key.substr(pos + 1));
            std::unique_lock<std::mutex> lock;
            if (key.find(ROUTER_ID_GATEWAY) != std::string::npos){
                // 网关
                std::unique_lock<std::mutex> lock;
                for(auto it = gateways_.begin(); it != gateways_.end(); ++it){
                    if (it->first == id) {
                        gateways_.erase(it);
                        return;
                    }
                }
            }else if (key.find(ROUTER_ID_BATTLE) != std::string::npos) {
                // battle 
                std::unique_lock<std::mutex> lock;
                battles_.erase(id);
            }
            
        }).start();
}

std::pair<uint32_t, std::string> GatewaySelector::pick_one() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (gateways_.empty()) {
        return {};
    }
    // 轮询
    int idx = rand() % gateways_.size();
    return gateways_[idx];
}

std::string GatewaySelector::get_battle_server(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (battles_.count(id) == 0) {
        return "";
    }
    return battles_[id];
}
    

LoginService::LoginService(
    const std::shared_ptr<odb::mysql::database>& odb, 
    const std::shared_ptr<sw::redis::Redis>& redis,
    const std::string& etcd_host,
    const std::string& base_dir,
    int http_thread_num
):
    odb_(std::make_shared<UserData>(odb)),
    redis_(redis),
    select_(std::make_shared<GatewaySelector>(etcd_host,base_dir,std::bind(&LoginService::battle_online, this, placeholders::_1,placeholders::_2))),
    internal_tcp_(-1,http_thread_num)
{
    internal_tcp_.set_connection_callback(std::bind(&LoginService::on_connect, this, common::tcp::_1, common::tcp::_2,common::tcp::_3));
    internal_tcp_.async_start();
}
// 登录逻辑
void LoginService::login(std::shared_ptr<mmo::login::C2L_LoginReq> req, std::shared_ptr<mmo::login::L2C_LoginRsp> rsp){
    auto handler = [&](const bool ok,const std::string& reason ){
        rsp->set_ok(ok);
        rsp->set_reason(reason);
        return;
    };
    // 从数据库中校验账号密码
    std::string reason;
    if (!CheckAccount(req->account(), req->password(),reason)) {
        debuglog << "登录失败 : " << reason << " :" << req->account();
        return handler(false,reason);
    }
    // 查询玩家信息
    auto info = odb_->get_player_info(req->account());
    // 玩家id
    uint64_t player_id = info.player_id;
    // 登录id
    uint64_t session_id = generate_id();
    // 网关选择 -> etcd
    auto gateway = select_->pick_one();
    uint32_t gateway_id = gateway.first;
    uint32_t battle_id = get_battle_id(player_id);
    // 写入redis PlayerInfo->redis
    std::string key = REDIS_STORE_NAME_SESSION_TOKEN + std::to_string(player_id);

    std::unordered_map<std::string, std::string> fields;
    fields["version"] = "1";
    fields["session_id"] = std::to_string(session_id);
    fields["gateway_id"] = std::to_string(gateway_id);
    fields["zone_id"] = "1";
    fields["battle_id"] = std::to_string(battle_id);
    // 写入 网关验证用的 token 信息
    redis_.hsetall(key, fields);
    debuglog("login success, player_id: {}, session_id: {}", player_id, session_id);

    // 设置返回登录结果
    rsp->set_ok(true);
    rsp->set_reason("登录成功");
    rsp->set_session_id(session_id);
    rsp->set_player_id(player_id);
    rsp->set_gateway_host(gateway.second);
    // 发送玩家实体上次退出世界时候的坐标 : 
    auto notify_info = memory_reuse::get_object<mmo::internal::PreloadEntityInfo>();
    notify_info->Clear();
    notify_info->set_x(info.x);
    notify_info->set_y(info.y);
    notify_info->set_z(info.z);
    notify_info->set_yaw(info.yaw);
    notify_info->set_pitch(info.pitch);
    notify_info->set_roll(info.roll);

    auto notify = memory_reuse::get_object<mmo::transport::GatewayToServer>();
    notify->Clear();
    auto head = notify->mutable_header();
    head->set_player_id(player_id);
    head->set_zone_id(1);
    head->set_battle_id(battle_id);
    head->set_gateway_id(gateway_id);
    uint32_t cmd = 0;
    common::protocol::util::set_direction(cmd, mmo::ids::GW2BATTLE); //GW2BATTLE
    common::protocol::util::set_module(cmd, mmo::ids::INTERNAL);
    common::protocol::util::set_action(cmd, mmo::ids::IA_PRELOAD_ENTITY_REQ);
    head->set_cmd(cmd);
    notify_info->SerializeToString(notify->mutable_body());

    size_t size = notify->ByteSizeLong();
    auto buffer = memory_reuse::get_buffer<uint8_t>(size);
    buffer->resize(size);
    notify->SerializeToArray(buffer->data(), buffer->size());
    auto channel = get_battle_server(battle_id);
    if(channel){
        channel->write(buffer);
        return ;
    }
    rsp->set_ok(false);
    rsp->set_reason("未找到战斗服");
    debuglog << "未找到战斗服 " << battle_id;
}
// token生成
std::string LoginService::generate_token(){
    return Generate_UUID::generate_uuid();
}
// PlayerID生成
uint64_t LoginService::generate_id(){
    return Generate_ID::generate_id();
}
// 从数据库中校验账号
bool LoginService::CheckAccount(const std::string& act, const std::string& password,std::string& reason){
    return odb_->exists_by_account(act, password, reason);
}
// 用 player_id 计算或者查询 battle_id
uint32_t LoginService::get_battle_id(uint64_t player_id){
    // 目前先固定
    return 1;
}
// battle 上线
void LoginService::battle_online(uint32_t id, const std::string& host){
    size_t pos = host.find(":");
    std::string ip = host.substr(0,pos);
    int port = std::stoi(host.substr(pos + 1));
    internal_tcp_.connect(ip, port, std::to_string(id));
    infolog("connect {}, {}", id, host);
}
void LoginService::on_connect(common::tcp::Channel channel,bool linked,const std::string& flag){
    uint32_t id = std::stoi(flag);
    infolog("connected {}", flag);
    if (linked) {
        std::unique_lock<std::mutex> lock(mtx);
        battle_servers_.insert({id, channel});
    }else {
        std::unique_lock<std::mutex> lock(mtx);
        battle_servers_.erase(id);
    }
}
common::tcp::Channel LoginService::get_battle_server(uint32_t id){
    std::unique_lock<std::mutex> lock(mtx);
    if (battle_servers_.count(id)){
        return battle_servers_[id];
    }
    return nullptr;
}

HttpServer& HttpServer::init_http(const std::string ip, uint16_t port, int thread_num){
    http_ = std::make_shared<web::HttpServer>(ip, port, thread_num);
    http_->Post("^/user/login$", std::bind(&HttpServer::login, this,web::_1,web::_2,web::_3));
    return *this;
}
HttpServer& HttpServer::init_odb(const odb_config& config){
    odb_ = mysql::mysql_build::build(
        config.user,
        config.password,
        config.db_name,
        config.host,
        config.port,
        config.charset,
        config.conn_pool_size
    );
    return *this;
}
HttpServer& HttpServer::init_redis(const AuthenticationConfig_& config){
    redis_ = redis::redis_build::build(
        config.host, 
        config.port, 
        config.db_id, 
        config.keey_alive);
    return *this;
}
HttpServer& HttpServer::init_login_service(const std::string& etcd_host, const std::string& path, int http_thread_num){
    if (!odb_ || !redis_) {
        errorlog("odb or redis client uninitalizer");
        exit(-1);
    }
    login_ = std::make_shared<LoginService>(
        odb_,
        redis_,
        etcd_host,
        path,
        http_thread_num
    );
    return *this;
}

void HttpServer::start(){
    http_->start();
}

void HttpServer::login(std::shared_ptr<web::Request> req, 
        std::shared_ptr<web::Response> rsp, 
        std::shared_ptr<web::HttpSession> session)
{
    auto& body = req->body();
    auto req_pro = memory_reuse::get_object<mmo::login::C2L_LoginReq>();
    auto rsp_pro = memory_reuse::get_object<mmo::login::L2C_LoginRsp>();
    req_pro->Clear();
    rsp_pro->Clear();
    // 反序列化
    if(!req_pro->ParseFromArray(body.data(), body.size())){
        // 反序列化失败
        session->send(rsp);
        return ;
    }
    // 处理登录逻辑
    login_->login(req_pro, rsp_pro);
    // 序列化
    size_t size = rsp_pro->ByteSizeLong();
    std::string data;
    rsp_pro->SerializeToString(&data);
    rsp->body() = data;
    // rsp_pro->SerializeToArray(rsp->body().data(), rsp->body().size());
    // 手动触发send
    session->send(rsp);
}
}
