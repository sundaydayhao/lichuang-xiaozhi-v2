#ifndef NETWORK_INTERFACE_H
#define NETWORK_INTERFACE_H

#include <memory>
#include "http.h"
#include "tcp.h"

class NetworkInterface {
public:
    virtual ~NetworkInterface() = default;

    virtual std::unique_ptr<Http> CreateHttp(int connect_id = -1) = 0;
    virtual std::unique_ptr<Tcp> CreateTcp(int connect_id = -1) = 0;
    virtual std::unique_ptr<Tcp> CreateSsl(int connect_id = -1) = 0;
};

#endif // NETWORK_INTERFACE_H
