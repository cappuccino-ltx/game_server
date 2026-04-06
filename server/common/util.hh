#pragma once

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <string>
#include <random>
#include <sstream>
#include <chrono>

using namespace std;



class Generate_UUID{
public:
    static std::string generate_uuid(){
        boost::uuids::random_generator generator;;
        boost::uuids::uuid uuid = generator();
        return boost::uuids::to_string(uuid);
    }
};
class Generate_Token{
public:
    // 生成随机token
    static std::string generate_token(size_t len = 32) {
        static thread_local std::mt19937_64 rng(std::random_device{}());
        static thread_local std::uniform_int_distribution<uint64_t> dist;

        std::stringstream ss;

        // while (ss.str().size() < len) {
        //     ss << std::hex << dist(rng);
        // }

        return ss.str().substr(0, len);
    }
private:
    // 生成带时间的token（推荐）
    std::string generate_token_with_time() {
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        return generate_token(32) + std::to_string(now);
    }
};

class Generate_ID{
public:
    static uint64_t generate_id() {
        static thread_local std::mt19937_64 rng(std::random_device{}());
        static std::uniform_int_distribution<uint64_t> dist;
        return dist(rng);
    }
};