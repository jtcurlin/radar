/*
 *
 * Copyright 2022 Apple Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cassert>

#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define MTK_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include <Metal/Metal.hpp>
#include <Foundation/Foundation.hpp>
// #include <MetalKit/MetalKit.hpp>

#include <simd/simd.h>
#include <span>
#include <vector>

#define RADIAL_DIMENSION 4
#define ANGULAR_DIMENSION 30
#define ANGULAR_CELL_PADDING 0.05f

#pragma region Declarations {

struct SectorState {
    uint8_t occupancy;
    uint8_t intensity;
};

class radar_model {
public:
    radar_model(uint32_t radial, uint32_t angular);
    
    void set_sector(uint32_t a, uint32_t r, SectorState s);
    std::span<const uint32_t> dirty_indices() const;
    std::span<const SectorState> all() const;
    void clear_dirty();
    
private:
    uint32_t m_radial_resolution;
    uint32_t m_angular_resolution;
    std::vector<SectorState> sectors;
    std::vector<uint32_t> dirty;
};


struct GpuCellColor {
    uint32_t rgba;
};

struct CellAttrib {
    uint32_t rgba;
};

class Renderer
{
    public:
    Renderer( MTL::Device* pDevice );
        ~Renderer();
        void buildShaders();
    
        void buildCurvedGrid(uint32_t radial_res,
                             uint32_t angular_res,
                             float padding_pct);

    
        void buildRadarLines(uint32_t rings = 4,
                             uint32_t spokes = 4,
                             float maxR = 0.95f);
        void setViewPxPerUnit(float pxPerUnit);
    
        void uploadColors(const radar_model& model);
            
        void draw( MTK::View* pView );

    private:
        MTL::Device* _pDevice;
        MTL::CommandQueue* _pCommandQueue;
        MTL::Library* _pShaderLibrary;
    
        MTL::Buffer* _pGridBuf = nullptr;
        MTL::Buffer* _pCellBuf = nullptr;
        MTL::Buffer* _pCellIdBuf = nullptr;
        MTL::Buffer* _pColorBuf = nullptr;

        MTL::RenderPipelineState* _pGridPSO = nullptr;
        MTL::RenderPipelineState* _pCellPSO = nullptr;

        uint32_t _cell_count = ANGULAR_DIMENSION * RADIAL_DIMENSION;
        uint32_t _gridVertexCount = 0;
        uint32_t _cellVertexCount = 0;

        uint32_t _view_px_per_unit;
};

class MyMTKViewDelegate : public MTK::ViewDelegate
{
    public:
        MyMTKViewDelegate( MTL::Device* pDevice );
        virtual ~MyMTKViewDelegate() override;
        virtual void drawInMTKView( MTK::View* pView ) override;
    
        Renderer* get_renderer() const { return _pRenderer; }

    private:
        Renderer* _pRenderer;
};

class MyAppDelegate : public NS::ApplicationDelegate
{
    public:
        ~MyAppDelegate();

        NS::Menu* createMenuBar();

        virtual void applicationWillFinishLaunching( NS::Notification* pNotification ) override;
        virtual void applicationDidFinishLaunching( NS::Notification* pNotification ) override;
        virtual bool applicationShouldTerminateAfterLastWindowClosed( NS::Application* pSender ) override;

    private:
        NS::Window* _pWindow;
        MTK::View* _pMtkView;
        MTL::Device* _pDevice;
        MyMTKViewDelegate* _pViewDelegate = nullptr;
};

#pragma endregion Declarations }


int main( int argc, char* argv[] )
{
    NS::AutoreleasePool* pAutoreleasePool = NS::AutoreleasePool::alloc()->init();

    MyAppDelegate del;

    NS::Application* pSharedApplication = NS::Application::sharedApplication();
    pSharedApplication->setDelegate( &del );
    pSharedApplication->run();

    pAutoreleasePool->release();

    return 0;
}


#pragma mark - AppDelegate
#pragma region AppDelegate {

MyAppDelegate::~MyAppDelegate()
{
    _pMtkView->release();
    _pWindow->release();
    _pDevice->release();
    delete _pViewDelegate;
}

NS::Menu* MyAppDelegate::createMenuBar()
{
    using NS::StringEncoding::UTF8StringEncoding;

    NS::Menu* pMainMenu = NS::Menu::alloc()->init();
    NS::MenuItem* pAppMenuItem = NS::MenuItem::alloc()->init();
    NS::Menu* pAppMenu = NS::Menu::alloc()->init( NS::String::string( "Appname", UTF8StringEncoding ) );

    NS::String* appName = NS::RunningApplication::currentApplication()->localizedName();
    NS::String* quitItemName = NS::String::string( "Quit ", UTF8StringEncoding )->stringByAppendingString( appName );
    SEL quitCb = NS::MenuItem::registerActionCallback( "appQuit", [](void*,SEL,const NS::Object* pSender){
        auto pApp = NS::Application::sharedApplication();
        pApp->terminate( pSender );
    } );

    NS::MenuItem* pAppQuitItem = pAppMenu->addItem( quitItemName, quitCb, NS::String::string( "q", UTF8StringEncoding ) );
    pAppQuitItem->setKeyEquivalentModifierMask( NS::EventModifierFlagCommand );
    pAppMenuItem->setSubmenu( pAppMenu );

    NS::MenuItem* pWindowMenuItem = NS::MenuItem::alloc()->init();
    NS::Menu* pWindowMenu = NS::Menu::alloc()->init( NS::String::string( "Window", UTF8StringEncoding ) );

    SEL closeWindowCb = NS::MenuItem::registerActionCallback( "windowClose", [](void*, SEL, const NS::Object*){
        auto pApp = NS::Application::sharedApplication();
            pApp->windows()->object< NS::Window >(0)->close();
    } );
    NS::MenuItem* pCloseWindowItem = pWindowMenu->addItem( NS::String::string( "Close Window", UTF8StringEncoding ), closeWindowCb, NS::String::string( "w", UTF8StringEncoding ) );
    pCloseWindowItem->setKeyEquivalentModifierMask( NS::EventModifierFlagCommand );

    pWindowMenuItem->setSubmenu( pWindowMenu );

    pMainMenu->addItem( pAppMenuItem );
    pMainMenu->addItem( pWindowMenuItem );

    pAppMenuItem->release();
    pWindowMenuItem->release();
    pAppMenu->release();
    pWindowMenu->release();

    return pMainMenu->autorelease();
}

void MyAppDelegate::applicationWillFinishLaunching( NS::Notification* pNotification )
{
    NS::Menu* pMenu = createMenuBar();
    NS::Application* pApp = reinterpret_cast< NS::Application* >( pNotification->object() );
    pApp->setMainMenu( pMenu );
    pApp->setActivationPolicy( NS::ActivationPolicy::ActivationPolicyRegular );
}

void MyAppDelegate::applicationDidFinishLaunching( NS::Notification* pNotification )
{
    CGRect frame = (CGRect){ {100.0, 100.0}, {1024.0, 1024.0} };

    _pWindow = NS::Window::alloc()->init(
        frame,
        NS::WindowStyleMaskClosable|NS::WindowStyleMaskTitled,
        NS::BackingStoreBuffered,
        false );

    _pDevice = MTL::CreateSystemDefaultDevice();

    _pMtkView = MTK::View::alloc()->init( frame, _pDevice );
    _pMtkView->setColorPixelFormat( MTL::PixelFormat::PixelFormatBGRA8Unorm_sRGB );
    _pMtkView->setClearColor( MTL::ClearColor::Make( 0.35, 0.35, 0.35, 1.0 ) );

    _pViewDelegate = new MyMTKViewDelegate( _pDevice );
    _pMtkView->setDelegate( _pViewDelegate );
    
    float pxPerUnit = _pMtkView->drawableSize().height / 2.f;
    auto* r = _pViewDelegate->get_renderer();
    r->setViewPxPerUnit(pxPerUnit);
    r->buildCurvedGrid(RADIAL_DIMENSION, ANGULAR_DIMENSION, ANGULAR_CELL_PADDING);

    _pWindow->setContentView( _pMtkView );
    _pWindow->setTitle( NS::String::string( "03 - Animation", NS::StringEncoding::UTF8StringEncoding ) );

    _pWindow->makeKeyAndOrderFront( nullptr );

    NS::Application* pApp = reinterpret_cast< NS::Application* >( pNotification->object() );
    pApp->activateIgnoringOtherApps( true );
}

bool MyAppDelegate::applicationShouldTerminateAfterLastWindowClosed( NS::Application* pSender )
{
    return true;
}

#pragma endregion AppDelegate }


#pragma mark - ViewDelegate
#pragma region ViewDelegate {

MyMTKViewDelegate::MyMTKViewDelegate( MTL::Device* pDevice )
: MTK::ViewDelegate()
, _pRenderer( new Renderer( pDevice ) )
{
}

MyMTKViewDelegate::~MyMTKViewDelegate()
{
    delete _pRenderer;
}

void MyMTKViewDelegate::drawInMTKView( MTK::View* pView )
{
    _pRenderer->draw( pView );
}

#pragma endregion ViewDelegate }


#pragma mark - Renderer
#pragma region Renderer {


// const int Renderer::kMaxFramesInFlight = 3;

Renderer::Renderer( MTL::Device* pDevice )
: _pDevice( pDevice->retain() )
{
    _pCommandQueue = _pDevice->newCommandQueue();
    
    buildShaders();
    buildCurvedGrid(RADIAL_DIMENSION, ANGULAR_DIMENSION, ANGULAR_CELL_PADDING);
    buildRadarLines();
    // buildSectorMesh();
}

Renderer::~Renderer()
{
    _pShaderLibrary->release();
    
    _pCellBuf->release();
    _pGridBuf->release();
    
    _pGridPSO->release();
    _pCellPSO->release();
    
    _pCommandQueue->release();
    _pDevice->release();
}

void Renderer::setViewPxPerUnit(float pxPerUnit)
{
    _view_px_per_unit = pxPerUnit;
}

void Renderer::buildCurvedGrid(uint32_t radial_res, uint32_t angular_res, float padding_pct)
{
    const float max_error_px = 0.5f;
    std::vector<simd::float3> verts;
    std::vector<uint32_t> cell_ids;
    verts.reserve(radial_res * angular_res * 32);
    
    float dθ = 2.f * M_PI / angular_res;
    float dr = 1.f / radial_res;
    
    auto segments_for_ring = [&](float radius) -> uint32_t {
        float eps = max_error_px / _view_px_per_unit;
        float Δθ = std::sqrt(8.f * eps / radius);
        return std::max(3u, (uint32_t)std::ceil(dθ / Δθ));
    };
    
    for (uint32_t a = 0; a < angular_res; ++a)
    {
        float θ_0 = a * dθ + (padding_pct * dθ);
        float θ_1 = (a + 1) * dθ - (padding_pct * dθ);
        
        for (uint32_t r = 0; r < radial_res; ++r)
        {
            float inner = r * dr;
            float outer = inner + dr;
            
            uint32_t n_segments = segments_for_ring(outer);
            
            for (uint32_t i = 0; i < n_segments; ++i) {
                float t0 = θ_0 + (θ_1-θ_0) *  i    / n_segments;
                float t1 = θ_0 + (θ_1-θ_0) * (i+1) / n_segments;

                simd::float3 v0 = { inner * cosf(t0), inner * sinf(t0), 0.f };
                simd::float3 v1 = { outer * cosf(t0), outer * sinf(t0), 0.f };
                simd::float3 v2 = { outer * cosf(t1), outer * sinf(t1), 0.f };
                simd::float3 v3 = { inner * cosf(t1), inner * sinf(t1), 0.f };

                // Quad → two tris
                verts.emplace_back(v0); verts.emplace_back(v1); verts.emplace_back(v2);
                verts.emplace_back(v0); verts.emplace_back(v2); verts.emplace_back(v3);
                
                // push 6 cell ids (0-based row-major)
                cell_ids.insert(cell_ids.end(), 6, a * radial_res + r);
            }
        }
    }
    
    _cellVertexCount = static_cast<uint32_t>(verts.size());
    
    const size_t cell_verts_bytes = verts.size() * sizeof(simd::float3);
    _pCellBuf = _pDevice->newBuffer(cell_verts_bytes, MTL::ResourceStorageModeManaged);
    std::memcpy(_pCellBuf->contents(), verts.data(), cell_verts_bytes);
    _pCellBuf->didModifyRange(NS::Range::Make(0,cell_verts_bytes));

    
    const size_t cell_ids_bytes = cell_ids.size() * sizeof(simd::float3);
    _pCellIdBuf = _pDevice->newBuffer(cell_ids_bytes, MTL::ResourceStorageModeManaged);
    std::memcpy(_pCellIdBuf->contents(), cell_ids.data(), cell_ids_bytes);
    _pCellIdBuf->didModifyRange(NS::Range::Make(0,cell_ids_bytes));
    
    _pColorBuf = _pDevice->newBuffer(_cell_count * sizeof(GpuCellColor), MTL::ResourceStorageModeShared);
    
}

void Renderer::buildRadarLines(uint32_t rings, uint32_t spokes, float maxR)
{
    constexpr int Rings = 4;
    constexpr int Spokes = 4;
    constexpr float MaxR = 0.95f;
    
    std::vector<simd::float3> verts;
    
    for (int r = 1; r <= Rings; ++r) {
        float radius = MaxR * r / Rings; // compute radius of current ring, e.g. ring 1 has a radius 1/4 * max_radius
        for (int s = 0; s <= 60; ++s) {
            float t = 2.f * M_PI * s / 60.f;
            verts.push_back(simd::float3{radius * cosf(t), radius * sinf(t), 0});
        }
    }
    
    for (int s = 0; s < Spokes; ++s) {
        float t = 2.f * M_PI * s / Spokes;
        verts.push_back(simd::float3{0,0,0});
        verts.push_back(simd::float3{MaxR * cosf(t), MaxR * sinf(t), 0});
    }
    
    _gridVertexCount = static_cast<uint32_t>(verts.size());
    size_t bytes = verts.size() * sizeof(simd::float3);
    _pGridBuf = _pDevice->newBuffer(bytes, MTL::ResourceStorageModeManaged);
    memcpy(_pGridBuf->contents(), verts.data(), bytes);
    _pGridBuf->didModifyRange(NS::Range::Make(0, bytes));
}

void Renderer::uploadColors(const radar_model& model)
{
    auto& dst = reinterpret_cast<GpuCellColor*>(_pColorBuf->contents());
    
    for (uint32_t idx : model.dirty_indices()) {
        const auto& s = model.all()[idx];
        dst[idx].rgba = packRGBA()
    }
}

void Renderer::buildShaders()
{
    using NS::StringEncoding::UTF8StringEncoding;

    const char* shaderSrc = R"(
    #include <metal_stdlib>
    using namespace metal;

    struct VSOut { 
        float4 pos [[position]]; 
        uint id [[flat]];
    };

    vertex VSOut vs(device const float3* pos [[buffer(0)]],
                    device const uint * cellId [[buffer(1)]],
                    uint vid [[vertex_id]])
    {
        return { float4(pos[vid], 1), cellId[vid] };
    }

    fragment float4 fs_grid()  { return float4(0.0, 1.0, 0.0, 1.0); } // green
    fragment float4 fs_cell() { return float4(1.0, 1.0, 1.0, 0.5); } // white
    )";
    
    NS::Error* pError = nullptr;
    MTL::Library* pLibrary = _pDevice->newLibrary( NS::String::string(shaderSrc, UTF8StringEncoding), nullptr, &pError );
    if ( !pLibrary )
    {
        __builtin_printf( "%s", pError->localizedDescription()->utf8String() );
        assert( false );
    }

    MTL::Function* pVertexFn = pLibrary->newFunction( NS::String::string("vs", UTF8StringEncoding) );
    MTL::Function* pGridFragFn = pLibrary->newFunction( NS::String::string("fs_grid", UTF8StringEncoding) );
    MTL::Function* pCellFragFn = pLibrary->newFunction( NS::String::string("fs_cell", UTF8StringEncoding) );

    MTL::RenderPipelineDescriptor* pCellDesc = MTL::RenderPipelineDescriptor::alloc()->init();
    MTL::RenderPipelineDescriptor* pGridDesc = MTL::RenderPipelineDescriptor::alloc()->init();
    
    pGridDesc->colorAttachments()->object(0)->setPixelFormat( MTL::PixelFormat::PixelFormatBGRA8Unorm_sRGB );
    
    /* enable blending / transparency for cell pipeline */
    auto* cell_ca = pCellDesc->colorAttachments()->object(0);
    cell_ca->setPixelFormat(MTL::PixelFormat::PixelFormatBGRA8Unorm_sRGB);
    
    cell_ca->setBlendingEnabled(true);
    cell_ca->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
    cell_ca->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    cell_ca->setRgbBlendOperation(MTL::BlendOperationAdd);
    
    cell_ca->setSourceAlphaBlendFactor(MTL::BlendFactorSourceAlpha);
    cell_ca->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    cell_ca->setAlphaBlendOperation(MTL::BlendOperationAdd);
    
    // set shader functions
    pGridDesc->setVertexFunction( pVertexFn );
    pGridDesc->setFragmentFunction( pGridFragFn );
    
    pCellDesc->setVertexFunction( pVertexFn );
    pCellDesc->setFragmentFunction( pCellFragFn );
    
    // initialize pipeline state objects
    _pGridPSO = _pDevice->newRenderPipelineState( pGridDesc, &pError );
    
    if (pError) { __builtin_printf("%s", pError->localizedDescription()->utf8String()); }
    
    _pCellPSO = _pDevice->newRenderPipelineState( pCellDesc, &pError );
    
    if (pError) { __builtin_printf("%s", pError->localizedDescription()->utf8String()); }
    
    if ( !_pGridPSO || !_pCellPSO )
    {
        __builtin_printf( "%s", pError->localizedDescription()->utf8String() );
        assert( false );
    }

    pVertexFn->release();
    pGridFragFn->release();
    pCellFragFn->release();
    
    pGridDesc->release();
    pCellDesc->release();
    _pShaderLibrary = pLibrary;
}


struct FrameData
{
    float angle;
};


void Renderer::draw( MTK::View* pView )
{
    NS::AutoreleasePool* pPool = NS::AutoreleasePool::alloc()->init();
    
    MTL::CommandBuffer* cmd = _pCommandQueue->commandBuffer();
    MTL::RenderPassDescriptor* rpd = pView->currentRenderPassDescriptor();
    MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(rpd);

    enc->setRenderPipelineState( _pGridPSO );
    enc->setVertexBuffer( _pGridBuf, 0, 0 );
    enc->drawPrimitives(MTL::PrimitiveType::PrimitiveTypeLineStrip, NS::UInteger(0), NS::UInteger(_gridVertexCount));

    enc->setRenderPipelineState(_pCellPSO);
    enc->setVertexBuffer(_pCellBuf, 0, 0);
    enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(_cellVertexCount));

    enc->endEncoding();
    cmd->presentDrawable(pView->currentDrawable());
    cmd->commit();

    pPool->release();
}

#pragma endregion Renderer }

