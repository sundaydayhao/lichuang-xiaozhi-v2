#ifndef L511_UDP_H
#define L511_UDP_H

#include "udp.h"
#include "at_uart.h"

#include <string>
#include <list>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#define L511_UDP_CONNECTED BIT0
#define L511_UDP_DISCONNECTED BIT1
#define L511_UDP_ERROR BIT2
#define L511_UDP_RECEIVE BIT3
#define L511_UDP_SEND_COMPLETE BIT4

#define UDP_CONNECT_TIMEOUT_MS 10000

class L511Udp : public Udp {
public:
    L511Udp(std::shared_ptr<AtUart> at_uart, int udp_id);
    ~L511Udp();

    bool Connect(const std::string& host, int port) override;
    void Disconnect() override;
    int Send(const std::string& data) override;

private:
    std::shared_ptr<AtUart> at_uart_;
    int udp_id_;
    bool instance_active_ = false;
    EventGroupHandle_t event_group_handle_;
    std::list<UrcCallback>::iterator urc_callback_it_;
};

#endif // L511_UDP_H
