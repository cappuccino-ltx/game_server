#pragma once
#include <string>


// User表
#pragma db object
class User {
public:
    User(const std::string &act, const std::string &pwd)
        : account(act), password(pwd){
    }
    User() {
    }
#pragma db id auto
  unsigned long id;


#pragma db index type("VARCHAR(255)")
    std::string account;

#pragma db not_null type("VARCHAR(255)")
    std::string password;
};