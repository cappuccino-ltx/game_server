#pragma once
#include <string>


// User表
#pragma db object
class User {
public:
    User(const std::string &act, const std::string &pwd, uint64_t pid, float x_, float y_, float z_, float yaw_, float pitch_, float roll_)
        : account(act),
        password(pwd),
        player_id(pid),
        x(x_),
        y(y_),
        z(z_),
        yaw(yaw_),
        pitch(pitch_),
        roll(roll_)
    {}
    User() {
    }
#pragma db id auto
  unsigned long id;
#pragma db index type("VARCHAR(255)")
    std::string account;
#pragma db not_null type("VARCHAR(255)")
    std::string password;

#pragma db not_null
    uint64_t player_id;
#pragma db not_null
    float x;
#pragma db not_null
    float y;
#pragma db not_null
    float z;
#pragma db not_null
    float yaw;
#pragma db not_null
    float pitch;
#pragma db not_null
    float roll;
};

#pragma db view object(User)
struct UserPositionView {
    std::string account;
    uint64_t player_id;
    float x;
    float y;
    float z;
    float yaw;
    float pitch;
    float roll;
};