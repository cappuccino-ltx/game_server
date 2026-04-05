#pragma once

#include <iterator>
#include <sw/redis++/redis++.h>
#include "log.hh"

namespace redis{

class redis_build{
public:
    static inline std::shared_ptr<sw::redis::Redis> build(
        const std::string& host,
        int port,
        int db_id,
        bool keey_alive = true
    ) {
        sw::redis::ConnectionOptions opts;
        opts.host = host;
        opts.port = port;
        opts.db = db_id;
        opts.keep_alive = keey_alive;
        return std::make_shared<sw::redis::Redis>(opts);
    }
};

class Redis{
public:
    Redis(const std::shared_ptr<sw::redis::Redis>& redis)
        : redis_(redis)
    {}
    ~Redis(){
        redis_.reset();
    }

    std::string get(const std::string& key){
        try {
            return redis_->get(key).value_or("");
        } catch (const std::exception& e) {
            errorlog("get redis key failed, key: {}, error: {}", key, e.what());
            return "";
        }
    }
    void set(const std::string& key, const std::string& value, int expire = 0){
        try {
            redis_->set(key, value, expire);
        } catch (const std::exception& e) {
            errorlog("set redis key failed, key: {}, value: {}, expire: {}", key, value, expire, e.what());
        }
    }
    bool del(const std::string& key){
        try {
            redis_->del(key);
            return true;
        } catch (const std::exception& e) {
            errorlog("del redis key failed, key: {}, error: {}", key, e.what());
            return false;
        }
    }
    bool exists(const std::string& key){
        try {
            return redis_->exists(key);
        } catch (const std::exception& e) {
            errorlog("exists redis key failed, key: {}, error: {}", key, e.what());
            return false;
        }
    }

    bool hsetall(const std::string& key, const std::map<std::string, std::string>& fields){
        try {
            redis_->hset(key, fields.begin(), fields.end());
            return true;
        } catch (const std::exception& e) {
            errorlog("hset redis key failed, key: {}, error: {}", key, e.what());
            return false;
        }
    }
    bool hgetall(const std::string& key, std::unordered_map<std::string, std::string>& fields){
        try {
            fields.clear();
            redis_->hgetall(key, std::inserter(fields, fields.begin()));
            return true;
        } catch (const std::exception& e) {
            errorlog("hget redis key failed, key: {}, error: {}", key, e.what());
            return false;
        }
    }

private:
    std::shared_ptr<sw::redis::Redis> redis_;
};
        
}
