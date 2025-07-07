// include/renderer.hpp

#pragma once

#include <cstdint>
#include <memory>

#ifdef __OBJC__
#import <Metal/Metal.h>
@class CAMetalLayer;
#endif

class RadarModel;

class Renderer
{
public:
    Renderer(id<MTLDevice> device, std::shared_ptr<RadarModel> model, CAMetalLayer* layer);
    ~Renderer();

    void draw();

private:
    void build_resources();
    void build_shaders();
    void build_curved_grid();
    void build_radar_lines();

    // metal objects (strong references)
    id<MTLDevice> m_device = nil;
    id<MTLCommandQueue> m_queue = nil;
    id<MTLLibrary> m_shader_library = nil;
    id<MTLRenderPipelineState> m_grid_pso = nil;
    id<MTLRenderPipelineState> m_cell_pso = nil;
    id<MTLRenderPipelineState> m_sweep_pso = nil;
    id<MTLBuffer> m_grid_buffer = nil;
    id<MTLBuffer> m_cell_buffer = nil;
    id<MTLBuffer> m_cell_id_buffer = nil;
    id<MTLBuffer> m_cell_hit_time_buffer = nil;
    id<MTLBuffer> m_sweep_buffer = nil;         
    id<MTLBuffer> m_color_buffer = nil;

    // view & model
    CAMetalLayer* m_layer = nil; // weak ref
    std::shared_ptr<RadarModel> m_model;

    // geometry metadata
    uint32_t m_grid_vertex_count = 0;
    uint32_t m_cell_vertex_count = 0;
    uint32_t m_cell_count = 0;
    float m_view_px_per_unit = 1.0f;
};
