// src/renderer.mm

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "renderer.hpp"
#include "radar.hpp"
#include <simd/simd.h>
#include <vector>
#include <cmath>

#define ANGULAR_DIM 30
#define RADIAL_DIM 4
#define PADDING_PCT 0.05f
#define MAX_ERROR_PX 0.5f
#define N_RINGS 4
#define N_SPOKES 4
#define MAX_R 0.95f
#define FADE_SECONDS 3.0f

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Renderer::Renderer(id<MTLDevice> device, std::shared_ptr<RadarModel> model, CAMetalLayer* layer)
    : m_device(device), m_layer(layer), m_model(std::move(model))
{
    // the layer's drawable size determines the resolution
    // assuming the view spans [-1, 1] in width, the number of pixels per unit is half the width
    m_view_px_per_unit = m_layer.drawableSize.width / 2.0f;
    
    m_queue = [m_device newCommandQueue];
    build_resources();
}

Renderer::~Renderer()
{
    // ARC will handle releasing all obj-c objects if the header is included in an ARC-enabled file
}

void Renderer::build_resources()
{
    build_shaders();
    build_curved_grid();
    build_radar_lines();
}

void Renderer::draw()
{
    @autoreleasepool {
        id<CAMetalDrawable> drawable = [m_layer nextDrawable];
        if (!drawable) { return; }

        // update renderer buffers to reflect radar data model buffer
        
        // update cell hit times
        auto hit_times = m_model->get_cell_hit_times();
        if (hit_times.size() * sizeof(float) == m_cell_hit_time_buffer.length) {
            memcpy(m_cell_hit_time_buffer.contents, hit_times.data(), m_cell_hit_time_buffer.length);
        }

        // update sweep line
        float angle_rad = m_model->get_current_sweep_angle() * M_PI / 180.0f;
        simd::float3 line_verts[2] = {
            {0.0f, 0.0f, 0.0f},
            {MAX_R * cosf(angle_rad), MAX_R * sinf(angle_rad), 0.0f}
        };
        memcpy(m_sweep_buffer.contents, line_verts, sizeof(line_verts));

        MTLRenderPassDescriptor* rpd = [MTLRenderPassDescriptor renderPassDescriptor];
        rpd.colorAttachments[0].texture = drawable.texture;
        rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
        rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
        rpd.colorAttachments[0].clearColor = MTLClearColorMake(0.05, 0.05, 0.1, 1.0);

        id<MTLCommandBuffer> cmdBuf = [m_queue commandBuffer];
        id<MTLRenderCommandEncoder> enc = [cmdBuf renderCommandEncoderWithDescriptor:rpd];

        // radar grid rings / spokes
        [enc setRenderPipelineState:m_grid_pso];
        [enc setVertexBuffer:m_grid_buffer offset:0 atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeLineStrip vertexStart:0 vertexCount:m_grid_vertex_count];
        
        // radar cells
        [enc setRenderPipelineState:m_cell_pso];
        [enc setVertexBuffer:m_cell_buffer offset:0 atIndex:0];
        [enc setVertexBuffer:m_cell_id_buffer offset:0 atIndex:1];
        
        // bind cell hit times
        [enc setFragmentBuffer:m_cell_hit_time_buffer offset:0 atIndex:2];
        
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:m_cell_vertex_count];
        
        // sweep line
        [enc setRenderPipelineState:m_sweep_pso];
        [enc setVertexBuffer:m_sweep_buffer offset:0 atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeLineStrip vertexStart:0 vertexCount:2];

        [enc endEncoding];
        [cmdBuf presentDrawable:drawable];
        [cmdBuf commit];
    }
}

void Renderer::build_curved_grid()
{
    const float max_error_px = MAX_ERROR_PX;
    const float padding_pct = PADDING_PCT;
    const uint32_t radial_res = RADIAL_DIM;
    const uint32_t angular_res = ANGULAR_DIM;
    
    m_cell_count = radial_res * angular_res;

    std::vector<simd::float3> verts;
    std::vector<uint32_t> cell_ids;
    verts.reserve(radial_res * angular_res * 32);
    cell_ids.reserve(radial_res * angular_res * 32);

    float dθ = 2.f * M_PI / angular_res; // arc angle per cell
    float dr = 1.f / radial_res; // radial length per cell

    // compute number of line segments needed to approximate
    //   cell arc within visual error threshold max_error_px
    auto segments_for_ring = [&](float radius) -> uint32_t {
        if (radius <= 0.0f) return 3;
        float eps = max_error_px / m_view_px_per_unit;
        float angle_step = std::sqrt(8.f * eps / radius);
        return std::max(3u, (uint32_t)std::ceil(dθ / angle_step));
    };

    for (uint32_t a = 0; a < angular_res; ++a) {
        float θ_0 = a * dθ + (padding_pct * dθ); // start angle
        float θ_1 = (a + 1) * dθ - (padding_pct * dθ); // end angle

        for (uint32_t r = 0; r < radial_res; ++r) {
            float inner = r * dr;
            float outer = inner + dr;
            uint32_t n_segments = segments_for_ring(outer);

            for (uint32_t i = 0; i < n_segments; ++i) {
                float t0 = θ_0 + (θ_1 - θ_0) * i / n_segments;
                float t1 = θ_0 + (θ_1 - θ_0) * (i + 1) / n_segments;

                simd::float3 v0 = {inner * cosf(t0), inner * sinf(t0), 0.f};
                simd::float3 v1 = {outer * cosf(t0), outer * sinf(t0), 0.f};
                simd::float3 v2 = {outer * cosf(t1), outer * sinf(t1), 0.f};
                simd::float3 v3 = {inner * cosf(t1), inner * sinf(t1), 0.f};

                verts.push_back(v0); verts.push_back(v1); verts.push_back(v2);
                verts.push_back(v0); verts.push_back(v2); verts.push_back(v3);

                uint32_t cell_id = a * radial_res + r;
                cell_ids.insert(cell_ids.end(), 6, cell_id);
            }
        }
    }

    m_cell_vertex_count = static_cast<uint32_t>(verts.size());

    const size_t cell_verts_bytes = verts.size() * sizeof(simd::float3);
    m_cell_buffer = [m_device newBufferWithBytes:verts.data() length:cell_verts_bytes options:MTLResourceStorageModeManaged];

    const size_t cell_ids_bytes = cell_ids.size() * sizeof(uint32_t); // Corrected size
    m_cell_id_buffer = [m_device newBufferWithBytes:cell_ids.data() length:cell_ids_bytes options:MTLResourceStorageModeManaged];

    const size_t color_buf_bytes = m_cell_count * sizeof(simd::float4);
    m_color_buffer = [m_device newBufferWithLength:color_buf_bytes options:MTLResourceStorageModeShared];
}


void Renderer::build_radar_lines()
{
    const uint32_t rings = N_RINGS;
    const uint32_t spokes = N_SPOKES;
    const float max_r = MAX_R;

    std::vector<simd::float3> verts;

    // rings
    for (uint32_t r = 1; r <= rings; ++r) {
        float radius = max_r * r / rings;
        for (int s = 0; s <= 72; ++s) {
            float t = 2.f * M_PI * s / 72.f;
            verts.push_back(simd::float3{radius * cosf(t), radius * sinf(t), 0});
        }
    }
    
    // spokes
    for (uint32_t s = 0; s < spokes; ++s) {
        float t = M_PI * s / spokes;
        verts.push_back(simd::float3{0, 0, 0});
        verts.push_back(simd::float3{max_r * cosf(t), max_r * sinf(t), 0});
        verts.push_back(simd::float3{0, 0, 0});
        verts.push_back(simd::float3{-max_r * cosf(t), -max_r * sinf(t), 0});
    }

    m_grid_vertex_count = static_cast<uint32_t>(verts.size());
    const size_t bytes = verts.size() * sizeof(simd::float3);
    m_grid_buffer = [m_device newBufferWithBytes:verts.data() length:bytes options:MTLResourceStorageModeManaged];
}

void Renderer::build_shaders()
{
    // allocate the new GPU buffers
    m_cell_hit_time_buffer = [m_device newBufferWithLength:(ANGULAR_DIM * RADIAL_DIM * sizeof(float)) options:MTLResourceStorageModeShared];
    m_sweep_buffer = [m_device newBufferWithLength:(2 * sizeof(simd::float3)) options:MTLResourceStorageModeShared];
    
    // shader string with the fade duration hardcoded to 5.0 seconds.
    const char* shaderSrc = R"(
    #include <metal_stdlib>
    using namespace metal;

    // A simple pass-through shader for grid and sweep lines
    vertex float4 vs_passthrough(device const float3* pos [[buffer(0)]], uint vid [[vertex_id]])
    {
        return float4(pos[vid], 1.0);
    }
    
    fragment float4 fs_grid() { return float4(0.2, 1.0, 0.3, 0.7); }

    fragment float4 fs_sweep() { return float4(0.5, 1.0, 1.0, 0.9); }

    // --- Cell Shaders ---
    struct VSCellOut {
        float4 pos [[position]];
        uint   id  [[flat]];
    };

    vertex VSCellOut vs_cell(device const float3* pos       [[buffer(0)]],
                               device const uint* cell_id   [[buffer(1)]],
                               uint                vid       [[vertex_id]])
    {
        return { float4(pos[vid], 1.0), cell_id[vid] };
    }

    constant float FADE_SECONDS = 5.0;

    fragment float4 fs_cell(VSCellOut in [[stage_in]],
                              device const float* hit_times [[buffer(2)]])
    {
        float time_since_hit = hit_times[in.id];
        float intensity = saturate(1.0 - (time_since_hit / FADE_SECONDS));

        if (intensity <= 0.0) {
            discard_fragment();
        }

        return float4(1.0, 0.2, 0.2, 0.9) * intensity;
    }
    )";
    
    NSError* error = nil;
    NSString* source = [NSString stringWithUTF8String:shaderSrc];
    
    m_shader_library = [m_device newLibraryWithSource:source options:nil error:&error];
    if (!m_shader_library) { NSLog(@"Library creation failed: %@", [error localizedDescription]); assert(false); }

    id<MTLFunction> passthroughVertFunc = [m_shader_library newFunctionWithName:@"vs_passthrough"];
    id<MTLFunction> cellVertFunc = [m_shader_library newFunctionWithName:@"vs_cell"];
    id<MTLFunction> gridFragFunc = [m_shader_library newFunctionWithName:@"fs_grid"];
    id<MTLFunction> sweepFragFunc = [m_shader_library newFunctionWithName:@"fs_sweep"];
    id<MTLFunction> cellFragFunc = [m_shader_library newFunctionWithName:@"fs_cell"];
    
    // grid PSO
    MTLRenderPipelineDescriptor* gridDesc = [[MTLRenderPipelineDescriptor alloc] init];
    gridDesc.vertexFunction = passthroughVertFunc;
    gridDesc.fragmentFunction = gridFragFunc;
    gridDesc.colorAttachments[0].pixelFormat = m_layer.pixelFormat;
    m_grid_pso = [m_device newRenderPipelineStateWithDescriptor:gridDesc error:&error];
    if (!m_grid_pso) { NSLog(@"Grid PSO failed: %@", [error localizedDescription]); assert(false); }

    // sweep line PSO
    MTLRenderPipelineDescriptor* sweepDesc = [[MTLRenderPipelineDescriptor alloc] init];
    sweepDesc.vertexFunction = passthroughVertFunc;
    sweepDesc.fragmentFunction = sweepFragFunc;
    sweepDesc.colorAttachments[0].pixelFormat = m_layer.pixelFormat;
    m_sweep_pso = [m_device newRenderPipelineStateWithDescriptor:sweepDesc error:&error];
    if (!m_sweep_pso) { NSLog(@"Sweep PSO failed: %@", [error localizedDescription]); assert(false); }

    // cell PSO (w/ blending)
    MTLRenderPipelineDescriptor* cellDesc = [[MTLRenderPipelineDescriptor alloc] init];
    cellDesc.vertexFunction = cellVertFunc;
    cellDesc.fragmentFunction = cellFragFunc;
    cellDesc.colorAttachments[0].pixelFormat = m_layer.pixelFormat;
    MTLRenderPipelineColorAttachmentDescriptor* ca = cellDesc.colorAttachments[0];
    ca.blendingEnabled = YES;
    ca.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    ca.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    ca.rgbBlendOperation = MTLBlendOperationAdd;
    ca.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    ca.alphaBlendOperation = MTLBlendOperationAdd;
    m_cell_pso = [m_device newRenderPipelineStateWithDescriptor:cellDesc error:&error];
    if (!m_cell_pso) { NSLog(@"Cell PSO failed: %@", [error localizedDescription]); assert(false); }
}
