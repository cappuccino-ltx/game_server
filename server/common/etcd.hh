#pragma once

#include <etcd/Client.hpp>
#include <etcd/KeepAlive.hpp>
#include <etcd/Response.hpp>
#include <etcd/SyncClient.hpp>
#include <etcd/Value.hpp>
#include <etcd/Watcher.hpp>
#include <functional>
#include <memory>
#include "log.hh"

namespace common {

    class Register {
    public:
        using ptr = std::shared_ptr<Register>;
        Register(const std::string &host)
            : client(std::make_shared<etcd::SyncClient>(host)), keepAlive(client->leasekeepalive(1)), lease(keepAlive->Lease()) 
        {}

        bool registory(const std::string &key, const std::string &value){
            infolog("register: {} {}", key, value);
            etcd::Response rsp = client->put(key, value, lease);
            if (!rsp.is_ok()) {
                return false;
            }
            return true;
        }

    private:
        std::shared_ptr<etcd::SyncClient> client;
        std::shared_ptr<etcd::KeepAlive> keepAlive;
        int64_t lease;
    };

    class Discovery {
    public:
        using ptr = std::shared_ptr<Discovery>;

        Discovery &setUpdateCallback(const std::function<void(const std::string &, const std::string &)> &back){
            update_callback = back;
            return *this;
        }
        Discovery &setUpdateCallback(const std::function<void(const std::string &)> &back){
            update_callback_val = back;
            return *this;
        }
        Discovery &setRemoveCallback(const std::function<void(const std::string &, const std::string &)> &back) {
            remove_callback = back;
            return *this;
        }
        Discovery &setRemoveCallback(const std::function<void(const std::string &)> &back){
            remove_callback_val = back;
            return *this;
        }
        Discovery &setHost(const std::string &host){
            client = std::make_shared<etcd::SyncClient>(host);
            return *this;
        }
        Discovery &setBaseDir(const std::string &dir){
            basedir = dir;
            return *this;
        }
        void start(){
            etcd::Response rsp = client->ls(basedir);
            if (!rsp.is_ok()) {
                errorlog("etcd ls 操作失败: {}",rsp.error_message());
                abort();
            }
            for (int i = 0; i < rsp.keys().size(); i++) {
                if (update_callback) {
                    update_callback(rsp.value(i).key(), rsp.value(i).as_string());
                } else if (update_callback_val) {
                    update_callback_val(rsp.value(i).as_string());
                }
            }

            watcher = std::make_shared<etcd::Watcher>(
                *(client.get()), basedir,
                [this](const etcd::Response &rsp) {
                    if (!rsp.is_ok()) {
                        return;
                    } else {
                        for (auto e : rsp.events()) {
                            if (e.event_type() == etcd::Event::EventType::PUT) {
                                if (update_callback) {
                                    update_callback(e.kv().key(), e.kv().as_string());
                                }   else if (update_callback_val) {
                                    update_callback_val(e.kv().as_string());
                                }
                            } else if (e.event_type() == etcd::Event::EventType::DELETE_) {
                                if (remove_callback) {
                                    remove_callback(e.prev_kv().key(), e.prev_kv().as_string());
                                } else if (remove_callback_val) {
                                    remove_callback_val(e.kv().as_string());
                                }
                            }
                        }
                    }
                },
                true);
        }

    private:
        std::function<void(const std::string &, const std::string &)> update_callback;
        std::function<void(const std::string &)> update_callback_val;
        std::function<void(const std::string &, const std::string &)> remove_callback;
        std::function<void(const std::string &)> remove_callback_val;
        std::shared_ptr<etcd::Watcher> watcher;
        std::shared_ptr<etcd::SyncClient> client;
        std::string basedir;
    };
} // namespace ltx
