// src/serial.cpp

#include "serial.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstdio>
#include <chrono>

static void configure(int fd)
{
    termios tty{};
    tcgetattr(fd, &tty);
    cfsetspeed(&tty, 115200);
    tty.c_cflag &= ~PARENB; // no parity
    tty.c_cflag &= ~CSTOPB; // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8; // 8 bits / byte
    tty.c_cflag &= ~CRTSCTS; // no hardware flow control
    tty.c_cflag |= CREAD | CLOCAL; // turn on READ & ignore ctrl lines

    cfmakeraw(&tty);
    tcflush(fd, TCIFLUSH);
    tcsetattr(fd, TCSANOW, &tty);
}

SerialPort::SerialPort(const std::string& path, DataHandler cb)
{
    m_fd = ::open(path.c_str(), O_RDWR | O_NOCTTY);
    if (m_fd < 0) {
        perror("open serial");
        return;
    }
    
    configure(m_fd);
    m_running = true;
    m_rx = std::thread([this, cb] {
        char buf[256];
        std::string line_buffer;
        while (m_running) {
            ssize_t n = ::read(m_fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                line_buffer += buf;
                
                // process complete lines (terminated by newline)
                size_t pos;
                while ((pos = line_buffer.find('\n')) != std::string::npos) {
                    std::string line = line_buffer.substr(0, pos);
                    // trim carriage return if present
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (!line.empty()) {
                        cb(line.c_str(), line.length());
                    }
                    line_buffer.erase(0, pos + 1);
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    });
}

SerialPort::~SerialPort() {
    m_running = false;
    if (m_rx.joinable()) {
        m_rx.join();
    }
    if (m_fd >= 0) {
        ::close(m_fd);
    }
}
