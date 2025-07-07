// src/controller.cpp

#include "controller.hpp"
#include <iostream>
#include <string>
#include <cstring>

constexpr int RADAR_LISTENING_PORT = 8888;
constexpr int RADAR_COMMAND_PORT = 8889;

Controller::Controller(std::shared_ptr<RadarModel> model)
    : m_model(model)
{
    // bind member functions for the callbacks
    auto udp_handler = std::bind(&Controller::handle_udp_data, this, std::placeholders::_1, std::placeholders::_2);
    m_udp_client = std::make_unique<UdpClient>(udp_handler);
    
    // start listening for incoming data from radar unit
    m_udp_client->start_listening(RADAR_LISTENING_PORT);
}

Controller::~Controller() {
    disconnect_control_unit();
}

/*
void Controller::connect_control_unit(const std::string& port_path) {
    if (m_serial_port) {
        disconnect_control_unit();
    }
    
    // bind the member function for the serial data callback
    auto serial_handler = std::bind(&Controller::handle_serial_data, this, std::placeholders::_1, std::placeholders::_2);
    m_serial_port = std::make_unique<SerialPort>(port_path, serial_handler);
}

void Controller::disconnect_control_unit() {
    m_serial_port.reset();
}
*/

void Controller::set_radar_unit_ip(const std::string& ip) {
    m_radar_ip = ip;
    std::cout << "Radar IP set to: " << m_radar_ip << std::endl;
}

void Controller::handle_serial_data(const char* data, size_t size) {
    std::string message(data, size);
    std::cout << "Received from Control Unit: " << message << std::endl;
    
    if (message.rfind("IR:", 0) == 0) {
        std::string command = message.substr(3);
        send_command_to_radar(command);
    }
}

void Controller::handle_udp_data(const char* data, size_t size) {
    std::string packet(data, size);
    
    float deg = 0, dist = 0;
    if (sscanf(packet.c_str(), "%f,%f", &deg, &dist) == 2) {
        m_model->add_detection(deg, dist);
    }
    
    // parse sweep angle here?
}

void Controller::send_command_to_radar(const std::string& command) {
    if (m_radar_ip.empty() || !m_udp_client) {
        return;
    }
    std::cout << "Sending to Radar Unit: " << command << std::endl;
    m_udp_client->send(m_radar_ip, RADAR_COMMAND_PORT, command);
}

void Controller::tick() {
    // to be implemented
}

