// src/radar.cpp

#include "radar.hpp"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

RadarModel::RadarModel(uint32_t angular, uint32_t radial)
: m_radial_res(radial), m_angular_res(angular)
{
    // init all hit times to a long time ago
    m_cell_last_hit.resize(m_radial_res * m_angular_res, std::chrono::steady_clock::now() - std::chrono::hours(1));
}

void RadarModel::set_current_sweep_angle(float deg) {
    std::scoped_lock lk(m_mutex);
    m_sweep_deg = deg;
}

void RadarModel::add_detection(float deg, float dist)
{
    std::scoped_lock lk(m_mutex);
    
    // normalize distance to [0, 1] range
    // assuming max distance corresponds to the edge of the radar
    float normalized_dist = std::min(1.0f, std::max(0.0f, dist));
    
    // convert degrees to angular sector index
    uint32_t angular_idx = static_cast<uint32_t>(fmod(deg, 360.0) / 360.0 * m_angular_res);
    
    // convert normalized distance to radial sector index
    uint32_t radial_idx = static_cast<uint32_t>(normalized_dist * m_radial_res);
    if (radial_idx >= m_radial_res) radial_idx = m_radial_res - 1;

    uint32_t cell_index = angular_idx * m_radial_res + radial_idx;
    if (cell_index < m_cell_last_hit.size()) {
        m_cell_last_hit[cell_index] = std::chrono::steady_clock::now();
    }
}

std::vector<float> RadarModel::get_cell_hit_times() const {
    std::scoped_lock lk(m_mutex);
    std::vector<float> times_sec;
    times_sec.reserve(m_cell_last_hit.size());
    
    auto now = std::chrono::steady_clock::now();
    for(const auto& time_point : m_cell_last_hit) {
        float elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - time_point).count() / 1000.0f;
        times_sec.push_back(elapsed);
    }
    return times_sec;
}

float RadarModel::get_current_sweep_angle() const {
    std::scoped_lock lk(m_mutex);
    return m_sweep_deg;
}

void RadarModel::clear_hits()
{
    std::scoped_lock lk(m_mutex);
    
    // reset all hit times to a long time ago
    std::fill(m_cell_last_hit.begin(), m_cell_last_hit.end(), std::chrono::steady_clock::now() - std::chrono::hours(1));
}
