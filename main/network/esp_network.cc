#include "esp_network.h"

#include "esp_tcp.h"
#include "esp_ssl.h"
#include "http_client.h"


EspNetwork::EspNetwork() {

}

EspNetwork::~EspNetwork() {

}

std::unique_ptr<Http> EspNetwork::CreateHttp(int connect_id) {
    return std::make_unique<HttpClient>(this, connect_id);
}

std::unique_ptr<Tcp> EspNetwork::CreateTcp(int connect_id) {
    return std::make_unique<EspTcp>();
}

std::unique_ptr<Tcp> EspNetwork::CreateSsl(int connect_id) {
    return std::make_unique<EspSsl>();
}
