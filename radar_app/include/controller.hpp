// include/controller.hpp

#pragma once

#include "radar.hpp"
#include "serial.hpp"
#include "udp_client.hpp"
#include <memory>
#include <string>

class Controller {
public:
    Controller(std::shared_ptr<RadarModel> model);
    ~Controller();

    // void connect_control_unit(const std::string& port_path);
    // void disconnect_control_unit();

    void set_radar_unit_ip(const std::string& ip);
    
    void tick();

private:
    void handle_serial_data(const char* data, size_t size);
    void handle_udp_data(const char* data, size_t size);
    
    void send_command_to_radar(const std::string& command);

    std::shared_ptr<RadarModel> m_model;
    std::unique_ptr<SerialPort> m_serial_port;
    std::unique_ptr<UdpClient> m_udp_client;

    std::string m_radar_ip;
    
    // for simulated data when not connected
    float m_sim_sweep_angle_deg = 0.0;
    double m_time_since_last_sim_detection = 0.0;
    std::chrono::steady_clock::time_point m_last_tick_time;
};
