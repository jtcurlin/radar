// include/serial.hpp

#pragma once

#include <thread>
#include <functional>
#include <string>

class SerialPort {
public:
    using DataHandler = std::function<void(const char* data, size_t size)>;
    
    SerialPort(const std::string& path, DataHandler cb);
    ~SerialPort();
    
private:
    int m_fd = -1;
    std::thread m_rx;
    volatile bool m_running = false;
};
