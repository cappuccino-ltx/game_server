#pragma once 
// std 
#include <functional>
#include <memory>
// common
#include <etcd.hh>
#include <concurrent_map.hh>
// gateway
#include <service/gateway_service.hh>
#include <authentication/authentication.hh>
#include <thread_local_store/thread_local_store.hh>
#include <route/route.hh>
// pb
#include <envelope.pb.h>


namespace gateway{
using std::placeholders::_1;
using std::placeholders::_2;

struct DiscoveryAndRegisterConfig{
    std::string host;
    std::string base_dir;
    std::string register_value;
};

class Controller{
public:
    Controller& init_udp_service(const gateway_service_config& config);
    Controller& init_authentication(const AuthenticationConfig& config);
    Controller& init_route(const RouteCenterConfig& config);
    Controller& init_discovery(const DiscoveryAndRegisterConfig& config);
    Controller& init_register(const DiscoveryAndRegisterConfig& config);
    void start(int udp_port);

private:
    void udp_service_message(kcp::channel_view channel, std::shared_ptr<std::vector<uint8_t>> message);
    // authentication callback 
    void on_player_authenticated(uint64_t player_id, std::shared_ptr<common::PlayerInfo> info);
    // route callback
    void send_to_client(uint64_t player_id, std::shared_ptr<std::vector<uint8_t>> message);
    // internal callback
    void on_internal_message(uint64_t player_id, uint32_t action);

private:
    std::shared_ptr<GatewayService> udp_service;
    std::shared_ptr<common::Discovery> discovery_;
    std::shared_ptr<common::Register> register_;
    std::shared_ptr<RouteCenter> route_;
    std::shared_ptr<Authentication> authentication_;
    ConcurrentMap<uint64_t, kcp::channel_view> player_channels_;
    ConcurrentMap<uint64_t, kcp::channel_view> authenticating_;
    ThreadLocalStore<uint64_t, std::shared_ptr<mmo::transport::Envelope>> packages_thread_local_;
};


} // namespace gateway