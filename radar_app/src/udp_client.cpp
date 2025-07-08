// src/udp_client.cpp

#include "udp_client.hpp"
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>

UdpClient::UdpClient(DataHandler handler) : m_handler(handler) {}

UdpClient::~UdpClient() {
    stop_listening();
    if (m_socket_fd >= 0) {
        ::close(m_socket_fd);
    }
}

void UdpClient::start_listening(int port) {
    if (m_is_listening) {
        return;
    }

    m_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socket_fd < 0) {
        perror("socket creation failed");
        return;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(m_socket_fd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        ::close(m_socket_fd);
        m_socket_fd = -1;
        return;
    }

    m_is_listening = true;
    /*
    m_listener_thread = std::thread([this] {
        char buffer[1024];
        while (m_is_listening) {
            sockaddr_in client_addr{};
            socklen_t len = sizeof(client_addr);
            ssize_t n = recvfrom(m_socket_fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &len);
            if (n > 0) {
                m_handler(buffer, n);
            }
        }
    });
    */
    m_listener_thread = std::thread([this] {
        char buffer[1024];
        while (m_is_listening) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(m_socket_fd, &read_fds);

            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 500000;  // 500 ms timeout

            int result = select(m_socket_fd + 1, &read_fds, NULL, NULL, &timeout);

            if (result > 0 && FD_ISSET(m_socket_fd, &read_fds)) {
                sockaddr_in client_addr{};
                socklen_t len = sizeof(client_addr);
                ssize_t n = recvfrom(m_socket_fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &len);
                if (n > 0) {
                    m_handler(buffer, n);
                }
            }
        }
    });
}

void UdpClient::stop_listening() {
    m_is_listening = false;
    if (m_listener_thread.joinable()) {
        m_listener_thread.join();
    }
}

void UdpClient::send(const std::string& address, int port, const std::string& message) {
    if (m_socket_fd < 0) {
        return;
    }

    sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    inet_pton(AF_INET, address.c_str(), &dest_addr.sin_addr);

    sendto(m_socket_fd, message.c_str(), message.length(), 0, (const struct sockaddr *)&dest_addr, sizeof(dest_addr));
}

