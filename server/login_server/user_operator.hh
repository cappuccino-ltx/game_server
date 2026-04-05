#pragma once

#include <odb/mysql/database.hxx>
#include <odb/mysql/connection.hxx>
#include <memory>

#include "user.hh"
#include "user-odb.hxx"


class UserData{
    private:
        std::shared_ptr<odb::mysql::database> db_;
    public:
        using ptr = std::shared_ptr<UserData>;
        UserData(const std::shared_ptr<odb::mysql::database>& client)
            :db_(client)
        {}
        // ========================
        // 查询用户
        // ========================
        bool exists_by_account(const std::string& act,const std::string& password
                                , std::string& reason) {
            try {
                odb::transaction t(db_->begin());
                auto result = db_->query<User>(
                    odb::query<User>::account == act);
                if (result.empty()) {
                    reason = "account not exist";
                    return false;
                }
                auto user = *result.begin();
                if (user.password != password) {
                    reason = "password error";
                    return false;
                }
                return true;
            } catch (...) {
                reason = "db error";
                return false;
            }
        }
};