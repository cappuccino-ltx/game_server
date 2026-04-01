#pragma once 

#include <memory>
#include "envelope.pb.h"
#include <protocol.hh>



namespace gateway{

class Route{
public:
    Route(){}

    void client_to_route(std::shared_ptr<mmo::transport::Envelope> envelope){
        int direction = get_direction(envelope->header().cmd());
        int module = get_module(envelope->header().cmd());
        if (direction != DIRECTION_CLIENT_TO_GATEWAY){
            return ;
        }
        switch (module){
            case MODULE_AUTH:
            case MODULE_SESSION:
            case MODULE_LOGIC:
            // route to battle server
            break;
            case MODULE_MOVE:
            case MODULE_STATE:
            case MODULE_SKILL:
                // route to logic server
                
                break;
            default:
                
                break;
        }
    }
    
};

} // namespace gateway
