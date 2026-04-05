#pragma once 

#include <odb/mysql/database.hxx>
#include <odb/mysql/connection.hxx>
#include <memory>
#include <string>
#include "log.hh"

namespace mysql{

    class mysql_build{
    public:
        static std::shared_ptr<odb::mysql::database> build(
            const std::string& user,
            const std::string& passwd,
            const std::string& db_name,
            const std::string& host,
            size_t port,
            const std::string& charset,
            size_t conn_pool_num
        ) {
            std::unique_ptr<odb::mysql::connection_pool_factory> pool(new odb::mysql::connection_pool_factory(conn_pool_num));
            auto ret =  std::make_shared<odb::mysql::database>(user,passwd,db_name,host,port,"",charset,0,std::move(pool));
            return ret;
        }
    };

    class Mysql {
    public:
        explicit Mysql(const std::shared_ptr<odb::mysql::database>& db)
            : db_(db)
        {}

        ~Mysql() {
            db_.reset();
        }

        // ========================
        // 通用执行（无返回）
        // ========================
        bool execute(const std::string& sql) {
            try {
                odb::transaction t(db_->begin());
                db_->execute(sql);
                t.commit();
                return true;
            } catch (const std::exception& e) {
                errorlog("mysql execute failed, sql: {}, error: {}", sql, e.what());
                return false;
            }
        }

        // ========================
        // 插入（示例）
        // ========================
        template<typename T>
        bool insert(const T& obj) {
            try {
                odb::transaction t(db_->begin());
                db_->persist(obj);
                t.commit();
                return true;
            } catch (const std::exception& e) {
                errorlog("mysql insert failed, error: {}", e.what());
                return false;
            }
        }

        // ========================
        // 查询单个
        // ========================
        template<typename T, typename ID>
        std::shared_ptr<T> get(const ID& id) {
            try {
                odb::transaction t(db_->begin());
                std::shared_ptr<T> obj(db_->load<T>(id));
                t.commit();
                return obj;
            } catch (const std::exception& e) {
                errorlog("mysql get failed, id: {}, error: {}", id, e.what());
                return nullptr;
            }
        }

        // ========================
        // 更新
        // ========================
        template<typename T>
        bool update(const T& obj) {
            try {
                odb::transaction t(db_->begin());
                db_->update(obj);
                t.commit();
                return true;
            } catch (const std::exception& e) {
                errorlog("mysql update failed, error: {}", e.what());
                return false;
            }
        }

        // ========================
        // 删除
        // ========================
        template<typename T, typename ID>
        bool remove(const ID& id) {
            try {
                odb::transaction t(db_->begin());
                db_->erase<T>(id);
                t.commit();
                return true;
            } catch (const std::exception& e) {
                errorlog("mysql delete failed, id: {}, error: {}", id, e.what());
                return false;
            }
        }

    private:
        std::shared_ptr<odb::mysql::database> db_;
    };


}