#ifndef L511_SSL_H
#define L511_SSL_H

#include "l511_tcp.h"

class L511Ssl : public L511Tcp {
public:
    L511Ssl(std::shared_ptr<AtUart> at_uart, int tcp_id);
    ~L511Ssl() = default;

protected:
    // 重写SSL配置方法
    bool ConfigureSsl(int port) override;
};

#endif // L511_SSL_H 