// include/udp_client.hpp

#pragma once

#include <string>
#include <functional>
#include <thread>

class UdpClient {
public:
    using DataHandler = std::function<void(const char* data, size_t size)>;

    UdpClient(DataHandler handler);
    ~UdpClient();

    void start_listening(int port);
    void stop_listening();
    void send(const std::string& address, int port, const std::string& message);

private:
    int m_socket_fd = -1;
    DataHandler m_handler;
    std::thread m_listener_thread;
    volatile bool m_is_listening = false;
};

