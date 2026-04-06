#pragma once

#include <atomic>
#include <cstdint>
// #include <etcd/Client.hpp>
// #include <etcd/SyncClient.hpp>
#include <memory>
#include <odb/mysql/database.hxx>
#include <string>
#include <vector>

#include "redis.hh"
#include "mysql.hh"
#include "login.pb.h"
#include "user_operator.hh"
#include "util.hh"
#include "etcd.hh"
#include "user_redis.h"
#include "redis_store_name.hh"


struct AuthenticationConfig_{
    std::string host = "localhost";
    int port = 6379;
    int db_id = 0;
    bool keey_alive = true;
    int thread_count = 1;
};

struct GatewayInfo{
    int id;
    std::string host;
    int port;
    int load;
};

std::vector<GatewayInfo> gws = {
    {1, "localhost", 9001, 0},
    {2, "localhost", 9002, 0},
    {3, "localhost", 9003, 0},
};


class GatewaySelector {
public:
    using ptr = std::shared_ptr<GatewaySelector>;
    GatewaySelector(const std::string& host, const std::string& base_dir) {
        gateways_.push_back(9001);
        gateways_.push_back(9002);
        discovery_ = std::make_shared<common::Discovery>();

        discovery_->setHost(host)
            .setBaseDir(base_dir)
            .setUpdateCallback([](const std::string &key, const std::string &value){
            infolog("host {} {}" ,key, value);
            }).setRemoveCallback([](const std::string &key, const std::string &value){
            infolog("host {} {}" ,key, value);
            }).start();
    }

    uint32_t pick_one() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (gateways_.empty()) {
            return 0;
        }
        // 轮询
        int idx = rand() % gateways_.size();
        return gateways_[idx];
    }

private:
    std::vector<uint32_t> gateways_;
    std::mutex mutex_;
    common::Discovery::ptr discovery_;
};




class LoginService{
public:
    using ptr = std::shared_ptr<LoginService>;
    LoginService(const std::shared_ptr<odb::mysql::database>& odb
        , const std::shared_ptr<sw::redis::Redis>& redis,const std::string& etcd_host,const std::string& base_dir):
        odb_(std::make_shared<UserData>(odb)),
        redis_(redis),
        select_(std::make_shared<GatewaySelector>(etcd_host,base_dir))
    {}
    // 登录逻辑

    void login(const mmo::login::C2L_LoginReq& req,mmo::login::L2C_LoginRsp& rsp){
        auto handler = [&](const bool ok,const std::string& reason ){
            rsp.set_ok(ok);
            rsp.set_reason(reason);
            return;
        };
        // 从数据库中校验账号密码
        std::string reason;
        if (!CheckAccount(req.account(), req.password(),reason)) {
            return handler(false,reason);
        }
        // 玩家id
        uint64_t player_id = generate_id();
        // 登录id
        uint64_t session_id = generate_id();
        // 网关选择 -> etcd
        auto gateway = select_->pick_one();
        // 写入redis PlayerInfo->redis
        std::string key = REDIS_STORE_NAME_SESSION_TOKEN + std::to_string(player_id);

        std::map<std::string, std::string> fields;
        fields["version"] = "1";
        fields["session_id"] = std::to_string(session_id);
        fields["gateway_id"] = std::to_string(gateway);
        fields["zone_id"] = "1";
        fields["battle_id"] = "0";

        redis_.hsetall(key, fields);

        // 返回登录结果
        rsp.set_ok(true);
        rsp.set_reason("登录成功");
        rsp.set_session_id(session_id);
        rsp.set_player_id(player_id);
    }
private:
    // token生成
    std::string generate_token(){
        return Generate_UUID::generate_uuid();
    }
    // PlayerID生成
    uint64_t generate_id(){
        return Generate_ID::generate_id();
    }
    // 网关选择
    GatewayInfo select_gateway(){
        size_t idx = index_++ % gws.size();
        gws[idx].load++;
        return gws[idx];
    }
    // 从数据库中校验账号
    bool CheckAccount(const std::string& act, const std::string& password,std::string& reason){
        return odb_->exists_by_account(act, password, reason);
    }

private:
    UserData::ptr odb_;
    redis::Redis redis_;
    uint32_t index_ {0};
    common::Discovery::ptr client_;
    GatewaySelector::ptr select_;
    std::string dir_;
};