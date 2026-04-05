#pragma once

#include <chrono>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <sw/redis++/errors.h>
#include <sw/redis++/redis.h>
#include <unordered_set>
#include <vector>

#include "log.hh"



class UserRedis{
private:
    std::shared_ptr<sw::redis::Redis> _client;
public:
    using ptr = std::shared_ptr<UserRedis>;
    UserRedis(const std::shared_ptr<sw::redis::Redis> &client) : _client(client) {}
    // ==============================Token的缓存======================================
    // 设置token
    bool SetToken(const std::string &token, const std::string &player_id, int ttlSenconds = 300) {
        try {
            _client->set(token, player_id, std::chrono::seconds(ttlSenconds));
            return true;
        } catch (const sw::redis::Error &err) {
            errorlog << "set token error" << err.what();
            return false;
        }
    }
    // 获取token对应的玩家id
    std::optional<std::string> GetPlayerId(const std::string &token) {
        try {
            auto val = _client->get(token);
            if (val)
                return val;
        } catch (const sw::redis::Error &err) {
            errorlog << "GetUserId error!" << err.what();
        }
        return std::nullopt;
    }
    // 删除token
    bool DelToken(const std::string &token) {
        try {
            // 成功删除时返回的是删除token的数量，没有token时返回的是0
            _client->del(token);
            return true;
        } catch (const sw::redis::Error &err) {
            errorlog << "DelToken failes!" << err.what();
            return false;
        }
    }
    // 刷新token的时间
    bool RefreshToken(const std::string &token, const int ttlSenconds) {
        try {
            return _client->expire(token, std::chrono::seconds(ttlSenconds));
        } catch (const sw::redis::Error &err) {
            errorlog << "failed to refresh token time!" << err.what();
            return false;
        }
    }
    // 检查token是否存在
    bool exists(const std::string &token) {
        try {
            return _client->exists(token) > 0;
        } catch (const sw::redis::Error &err) {
            return false;
        }
    }
};
