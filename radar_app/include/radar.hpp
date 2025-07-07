// include/radar.hpp

#pragma once

#include <cstdint>
#include <mutex>
#include <chrono>
#include <vector>

class RadarModel
{
public:
    RadarModel(uint32_t angular = 30, uint32_t radial = 4);
    
    // producer methods
    void add_detection(float deg, float dist);
    void set_current_sweep_angle(float deg);

    // consumer methods
    std::vector<float> get_cell_hit_times() const;
    float get_current_sweep_angle() const;
    void clear_hits();
    
private:
    std::vector<std::chrono::steady_clock::time_point> m_cell_last_hit;
    uint32_t m_radial_res, m_angular_res;
    float m_sweep_deg = 0.0f;
    
    mutable std::mutex m_mutex;
};
