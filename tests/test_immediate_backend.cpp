#include "renderer/immediate_draw.h"
#include "header/test_graphics_device.h"
#include "renderer/immediate_copy_region.h"
#include "renderer/device_availability.h"
#include <d3d11sdklayers.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#include <bit>
namespace Simpsons::Graphics {
struct NativeIm2DProbe {
    static D3D11_VIEWPORT physicalViewport(NativeBackend& b,const std::shared_ptr<RenderTarget>& target){return b.renderViewport(target,{0,0,float(target->width),float(target->height),0,1});}
    static void legacyImmediate(NativeBackend& b,const std::shared_ptr<RenderTarget>& target,const std::shared_ptr<DepthTarget>& depth,const ImmediateDraw& draw){b.drawImmediateImpl(target,depth,draw,false);}
    static ID3D11DeviceContext* context(NativeBackend& b){return b.context.Get();}
    static ID3D11Device* device(NativeBackend& b){return b.device.Get();}
    static ComPtr<ID3D11Buffer> cachedDepth(NativeBackend& b,uint32_t reverse,uint32_t bias,uint32_t slope){
        for(const auto& e:b.sceneDepthConstants)if(e.used&&e.reverse==reverse&&e.constantBits==bias&&e.slopeBits==slope)return e.buffer;
        return {};
    }
    static void resetSceneCache(NativeBackend& b){b.resetSceneStateCacheForDevice();}
    static void debug(NativeBackend& b,bool hardware){
        if(testUsesVulkan())return; // D3D11 debug layer: a D3D11-only diagnostic.
        ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL level;
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        if(SUCCEEDED(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_DEBUG,levels,2,D3D11_SDK_VERSION,&d,&level,&c))){
            b.device=d;b.context=c;b.featureLevel=level;b.availability=std::make_shared<DeviceAvailability>(d.Get());
        }
    }
};
}
using namespace Simpsons::Graphics;
void need(bool condition,const char* message){if(!condition)throw Error(message);}
uint32_t word(const std::vector<uint8_t>& bytes,unsigned pixel){uint32_t v;std::memcpy(&v,bytes.data()+pixel*4,4);return v;}

// This fixture makes the reference copy the full surface without adding a
// production test switch: an identical-vertex triangle containing a denormal
// forces the conservative full fallback but generates no fragments (also after
// GPU FTZ). Both executions use the same actual device.
struct ImmediateRegionState {
    std::vector<uintptr_t> words;
    template<class T> void pointer(T* value){words.push_back(reinterpret_cast<uintptr_t>(value));if(value)value->Release();}
    void bytes(const void* value,size_t count){const auto* p=static_cast<const uint8_t*>(value);for(size_t i=0;i<count;++i)words.push_back(p[i]);}
    explicit ImmediateRegionState(ID3D11DeviceContext* c){
        ComPtr<ID3D11DeviceContext1> ranged;c->QueryInterface(IID_PPV_ARGS(&ranged));
        ID3D11VertexShader* vs{};ID3D11PixelShader* ps{};c->VSGetShader(&vs,nullptr,nullptr);c->PSGetShader(&ps,nullptr,nullptr);pointer(vs);pointer(ps);
        for(auto stage:{detail::ConstantBufferStage::Vertex,detail::ConstantBufferStage::Pixel})for(UINT slot=0;slot<4;++slot){
            const auto binding=detail::captureConstantBuffer(c,ranged.Get(),stage,slot);
            words.push_back(reinterpret_cast<uintptr_t>(binding.buffer.Get()));words.push_back(binding.firstConstant);
            words.push_back(binding.numConstants);words.push_back(uintptr_t(binding.view));
        }
        ID3D11Buffer* vb{},*ib{};UINT stride{},offset{},indexOffset{};DXGI_FORMAT format{};
        c->IAGetVertexBuffers(0,1,&vb,&stride,&offset);c->IAGetIndexBuffer(&ib,&format,&indexOffset);pointer(vb);pointer(ib);
        words.insert(words.end(),{stride,offset,indexOffset,uintptr_t(format)});
        ID3D11InputLayout* layout{};D3D11_PRIMITIVE_TOPOLOGY topology{};c->IAGetInputLayout(&layout);c->IAGetPrimitiveTopology(&topology);pointer(layout);words.push_back(topology);
        for(UINT i=0;i<5;++i){ID3D11ShaderResourceView* view{};c->PSGetShaderResources(i,1,&view);pointer(view);}
        for(UINT i=0;i<4;++i){ID3D11SamplerState* sampler{};c->PSGetSamplers(i,1,&sampler);pointer(sampler);}
        ID3D11DepthStencilState* ds{};UINT stencil{};c->OMGetDepthStencilState(&ds,&stencil);pointer(ds);words.push_back(stencil);
        ID3D11BlendState* blend{};FLOAT factors[4]{};UINT mask{};c->OMGetBlendState(&blend,factors,&mask);pointer(blend);bytes(factors,sizeof(factors));words.push_back(mask);
        ID3D11RasterizerState* raster{};c->RSGetState(&raster);pointer(raster);
        std::array<ID3D11RenderTargetView*,8> targets{};ID3D11DepthStencilView* depth{};c->OMGetRenderTargets(8,targets.data(),&depth);
        for(auto* target:targets)pointer(target);pointer(depth);
        std::array<D3D11_VIEWPORT,16> viewports{};UINT count=16;c->RSGetViewports(&count,viewports.data());words.push_back(count);bytes(viewports.data(),count*sizeof(viewports[0]));
        std::array<D3D11_RECT,16> scissors{};count=16;c->RSGetScissorRects(&count,scissors.data());words.push_back(count);bytes(scissors.data(),count*sizeof(scissors[0]));
    }
    bool operator==(const ImmediateRegionState& other)const{return words==other.words;}
};
void immediateRegionCopyContracts(bool hardware){
    size_t comparisons=0;
    const auto full=[](const D3D11_BOX& b,UINT w,UINT h){return b.left==0&&b.top==0&&b.front==0&&b.right==w&&b.bottom==h&&b.back==1;};
    const auto empty=[](const D3D11_BOX& b){return b.left==b.right||b.top==b.bottom;};
    const auto definedDepth=[](const std::vector<uint8_t>& a,const std::vector<uint8_t>& b){
        need(a.size()==b.size()&&a.size()%8==0,"Immediate region depth extent differs");
        for(size_t i=0;i<a.size();i+=8)need(!std::memcmp(a.data()+i,b.data()+i,5),"Immediate region changed defined depth/stencil bytes");
    };
    for(const uint32_t percent:{100u,125u,67u}){
        NativeBackend b(!hardware);NativeIm2DProbe::debug(b,hardware);b.configureRendering(1280,720,1,Antialiasing::Original,percent);
        constexpr UINT logicalW=48,logicalH=32;
        auto color=b.createTarget(logicalW,logicalH,TargetFormat::RGB10A2,TargetScale::Scene);
        auto depth=b.createDepthTarget(logicalW,logicalH,TargetScale::Scene);
        auto* c=NativeIm2DProbe::context(b);b.bindTargets({color,nullptr,nullptr,nullptr},depth);
        const auto viewport=NativeIm2DProbe::physicalViewport(b,color);
        const UINT width=color->pixelWidth(),height=color->pixelHeight();
        need((percent==100)==(width==logicalW&&height==logicalH),"Immediate region scaling control did not change physical storage");
        b.setViewport({1,2,9,7,0,1});b.setScissor({7,5,31,23});c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        // Seed actual ranged bindings when this device supports them; Restore
        // must preserve the buffer owner and both range words, not just GetCB.
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=1024;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.Usage=D3D11_USAGE_IMMUTABLE;
        const std::array<uint32_t,256> zeros{};const D3D11_SUBRESOURCE_DATA initial{zeros.data(),0,0};ComPtr<ID3D11Buffer> retained;
        need(SUCCEEDED(NativeIm2DProbe::device(b)->CreateBuffer(&desc,&initial,&retained)),"Immediate region retained constant creation failed");
        ComPtr<ID3D11DeviceContext1> ranged;c->QueryInterface(IID_PPV_ARGS(&ranged));D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
        const bool ranges=ranged&&SUCCEEDED(NativeIm2DProbe::device(b)->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,&options,sizeof(options)))&&options.ConstantBufferOffsetting;
        auto* raw=retained.Get();const UINT first=16,count=16;
        if(ranges){ranged->VSSetConstantBuffers1(0,1,&raw,&first,&count);ranged->VSSetConstantBuffers1(3,1,&raw,&first,&count);
            ranged->PSSetConstantBuffers1(1,1,&raw,&first,&count);ranged->PSSetConstantBuffers1(2,1,&raw,&first,&count);}
        else{c->VSSetConstantBuffers(0,1,&raw);c->VSSetConstantBuffers(3,1,&raw);c->PSSetConstantBuffers(1,1,&raw);c->PSSetConstantBuffers(2,1,&raw);}
        ImmediateDraw base{};base.viewport={0,0,logicalW,logicalH,0,0x3F800000};base.viewportEnable=base.halfPixelOffset=1;
        base.depthEnable=base.depthWrite=1;base.depthCompare=7;base.colorMask=15;base.multisampleMask=0xFFFFFFFF;
        base.primitive=4;base.blendEnable=1;base.blendWord=0x10106;
        const std::array<uint8_t,4> white{255,255,255,255};base.texture=b.createTexture(1,1,TextureFormat::RGBA8,white);
        base.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;base.sampler.AddressU=base.sampler.AddressV=base.sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        base.sampler.MaxLOD=13;base.sampler.MaxAnisotropy=1;base.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
        // Distinct retained resources, an interior null, and the next untouched
        // slot expose shifted/overlong batched capture or restoration.
        std::array<std::shared_ptr<Texture>,5> retainedTextures;
        std::array<ComPtr<ID3D11SamplerState>,4> retainedSamplers;
        for(UINT slot=0;slot<retainedTextures.size();++slot){
            retainedTextures[slot]=b.createTexture(1,1,TextureFormat::RGBA8,white);
            b.bindEngineTexture(slot,retainedTextures[slot]);
        }
        ID3D11ShaderResourceView* emptyView=nullptr;c->PSSetShaderResources(1,1,&emptyView);
        for(UINT slot=0;slot<retainedSamplers.size();++slot){
            auto desc=base.sampler;desc.AddressU=D3D11_TEXTURE_ADDRESS_MODE(D3D11_TEXTURE_ADDRESS_WRAP+slot);
            need(SUCCEEDED(NativeIm2DProbe::device(b)->CreateSamplerState(&desc,&retainedSamplers[slot])),"Immediate retained sampler creation failed");
            auto* sample=slot==1?nullptr:retainedSamplers[slot].Get();c->PSSetSamplers(slot,1,&sample);
        }
        for(UINT i=0;i<4;++i)base.constants[i][i]=1;base.constants[4]={.25f,.375f,.5f,.5f};
        const auto quad=[](ImmediateDraw& d,float x0,float y0,float x1,float y1){
            const ImmediateVertex a{{x0,y0,.5f},1,{0,0,0,0}},b{{x0,y1,.5f},1,{0,0,0,0}},
                e{{x1,y0,.5f},1,{0,0,0,0}},f{{x1,y1,.5f},1,{0,0,0,0}};
            d.vertices.insert(d.vertices.end(),{a,b,e,e,b,f});
        };
        quad(base,-.5f,.5f,.5f,-.5f);
        const auto orderOf=[](const ImmediateDraw& d){
            std::vector<uint16_t> order;
            const size_t step=d.primitive==4?3:1;
            for(size_t i=0;i+2<d.vertices.size();i+=step){bool finite=true;for(size_t j=0;j<3;++j)for(float value:d.vertices[i+j].position)finite=finite&&std::isfinite(value);
                if(finite){if(d.primitive==4)for(size_t j=0;j<3;++j)order.push_back(uint16_t(i+j));
                    else{order.push_back(uint16_t(i+(i&1)));order.push_back(uint16_t(i+!(i&1)));order.push_back(uint16_t(i+2));}}}
            return order;
        };
        const auto padded=[&](ImmediateDraw d){
            if(d.primitive==6){const auto order=orderOf(d);const auto vertices=d.vertices;d.vertices.clear();
                for(const auto index:order)d.vertices.push_back(vertices[index]);d.primitive=4;}
            // This remains a full-copy reference even when every real vertex
            // has constant negative W: the helper rejects any denormal input.
            // All three shader inputs are identical, hence exactly zero area.
            const ImmediateVertex degenerate{{std::numeric_limits<float>::denorm_min(),0,.5f},1,{0,0,0,0}};
            d.vertices.insert(d.vertices.end(),{degenerate,degenerate,degenerate});
            return d;
        };
        std::vector<uint32_t> background(size_t(width)*height);
        for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x)background[size_t(y)*width+x]=((x/3+y/3)&1)?
            (192u|(64u<<10)|(320u<<20)|(3u<<30)):(64u|(128u<<10)|(256u<<20)|(3u<<30));
        const auto execute=[&](std::span<const ImmediateDraw> draws,bool reference,bool reduced,bool expectEmpty){
            b.clearDepthTarget(depth,.125f,0x6D);
            // Independent exact packed checkerboard, uploaded through the
            // actual selected RTV owner; no ImmediatePipeline surface is seeded.
            ComPtr<ID3D11RenderTargetView> view;ComPtr<ID3D11DepthStencilView> dsv;c->OMGetRenderTargets(1,&view,&dsv);
            ComPtr<ID3D11Resource> resource;view->GetResource(&resource);c->OMSetRenderTargets(0,nullptr,nullptr);
            c->UpdateSubresource(resource.Get(),0,nullptr,background.data(),width*4,0);c->OMSetRenderTargets(1,view.GetAddressOf(),dsv.Get());
            for(const auto& draw:draws){const auto actual=reference&&!draw.radial?padded(draw):draw;const auto order=orderOf(actual); // Radial strips already copy the full surface.
                const auto box=immediateCopyRegion(actual,order,width,height,false,viewport);
                if(reference)need(full(box,width,height),"Degenerate reference did not select full-resource copies");
                else {if(reduced)need(!full(box,width,height),"Immediate region positive control selected full copies");
                    if(expectEmpty)need(empty(box),"Proven zero-coverage immediate retained nonempty copies");}
                const auto before=ImmediateRegionState(c);const auto oldVertices=actual.vertices;const auto countBefore=b.immediateDrawCount();
                if(reference)NativeIm2DProbe::legacyImmediate(b,color,depth,actual);else b.drawImmediate(color,depth,actual);
                need(b.immediateDrawCount()==countBefore+1,"Immediate region draw was not submitted exactly once");
                need(ImmediateRegionState(c)==before,"Immediate region draw changed retained native state/ranges");
                need(!std::memcmp(oldVertices.data(),actual.vertices.data(),oldVertices.size()*sizeof(ImmediateVertex)),"Immediate region changed caller input");
            }
            return std::pair{b.readbackTarget(color),b.readbackDepthTarget(depth)};
        };
        const auto compare=[&](std::vector<ImmediateDraw> draws,bool reduced=true,bool outside=true,bool expectEmpty=false){
            // Region first exposes stale/uninitialized packed texels outside
            // its box before the full-reference execution fills the surface.
            const auto region=execute(draws,false,reduced,expectEmpty),reference=execute(draws,true,false,false);
            need(region.first==reference.first,"Immediate rectangle copies differ from full-copy packed color");definedDepth(region.second,reference.second);
            if(outside){need(word(region.first,0)==(64u|(128u<<10)|(256u<<20)|(3u<<30)),"Immediate region corrupted independent outside background");
                need(word(region.second,0)==0x3E000000u&&region.second[4]==0x6D,"Immediate region corrupted outside depth/stencil");}
            if(expectEmpty){need(region.first.size()==background.size()*sizeof(uint32_t)&&
                !std::memcmp(region.first.data(),background.data(),region.first.size()),"Zero-coverage immediate changed exact background bytes");
                for(size_t at=0;at<region.second.size();at+=8)need(word(region.second,unsigned(at/4))==0x3E000000u&&region.second[at+4]==0x6D,
                    "Zero-coverage immediate changed depth/stencil");}
            ++comparisons;return region;
        };
        const auto overlap=compare({base,base});const UINT middle=(height/2)*width+width/2;
        const uint32_t initialMiddle=background[middle];
        const uint32_t expectedMiddle=((initialMiddle&1023)+256u)|((((initialMiddle>>10)&1023)+384u)<<10)|((((initialMiddle>>20)&1023)+512u)<<20)|(2u<<30);
        need(word(overlap.first,middle)==expectedMiddle,"Ordered region overlap lost independently expected packed accumulation");
        need(word(overlap.second,2*middle)==0x3F000000u&&overlap.second[middle*8+4]==0x6D,"Ordered region overlap depth/stencil differs");
        auto strip=base;strip.primitive=6;strip.vertices={base.vertices[0],base.vertices[1],base.vertices[2],base.vertices[5]};compare({strip});
        auto folded=strip;folded.vertices.push_back(strip.vertices[0]);folded.vertices.push_back(strip.vertices[1]);compare({folded});
        // Queue changed geometry without an intervening readback. The shared
        // VB is DISCARDed repeatedly while prior draws still own earlier data.
        // Exact strips alternate with independent triangle lists (dedicated IB).
        auto left=strip;left.vertices.clear();left.blendEnable=0;left.blendWord=0x10001;
        left.constants[4]={.25f,.375f,.5f,.5f};
        left.vertices={{{-.9f,.5f,.5f},1,{0,0,0,0}},{{-.9f,-.5f,.5f},1,{0,0,0,0}},
                       {{-.35f,.5f,.5f},1,{0,0,0,0}},{{-.35f,-.5f,.5f},1,{0,0,0,0}}};
        // Dead second UVs must still be canonicalized before the stream copy.
        for(auto& vertex:left.vertices)vertex.uv[2]=vertex.uv[3]=std::numeric_limits<float>::quiet_NaN();
        auto right=base;right.vertices.clear();right.blendEnable=0;right.blendWord=0x10001;
        right.constants[4]={.5f,.25f,.125f,1};quad(right,.35f,.5f,.9f,-.5f);
        auto centerDraw=strip;centerDraw.vertices={{{-.2f,.5f,.5f},1,{0,0,0,0}},{{-.2f,-.5f,.5f},1,{0,0,0,0}},
                                           {{.2f,.5f,.5f},1,{0,0,0,0}},{{.2f,-.5f,.5f},1,{0,0,0,0}}};
        centerDraw.blendEnable=0;centerDraw.blendWord=0x10001;centerDraw.constants[4]={.125f,.5f,.25f,1};
        std::vector<ImmediateDraw> queued;
        for(UINT i=0;i<12;++i){queued.push_back(left);queued.push_back(right);}queued.push_back(centerDraw);
        const auto queuedResult=compare(std::move(queued));
        const UINT y=height/2,leftPixel=y*width+width/5,rightPixel=y*width+(width*4)/5,middlePixel=y*width+width/2;
        need(word(queuedResult.first,leftPixel)==(256u|(384u<<10)|(512u<<20)|(2u<<30)),"DISCARD changed queued left strip pixels");
        need(word(queuedResult.first,rightPixel)==(512u|(256u<<10)|(128u<<20)|(3u<<30)),"DISCARD changed queued right list pixels");
        need(word(queuedResult.first,middlePixel)==(128u|(512u<<10)|(256u<<20)|(3u<<30)),"DISCARD changed queued final strip pixels");
        for(UINT pixel:{leftPixel,rightPixel,middlePixel})need(word(queuedResult.second,2*pixel)==0x3F000000u&&queuedResult.second[size_t(pixel)*8+4]==0x6D,
            "DISCARD changed queued defined depth/stencil");
        auto culledStrip=strip;culledStrip.vertices[0].position[0]=std::numeric_limits<float>::quiet_NaN();compare({culledStrip});
        auto separate=base;separate.vertices.clear();quad(separate,-.85f,.8f,-.35f,.2f);quad(separate,.3f,-.15f,.8f,-.75f);compare({separate});
        auto translated=base;translated.constants[0][0]=.75f;translated.constants[1][1]=.5f;translated.constants[3][0]=.125f;translated.constants[3][1]=-.125f;compare({translated});
        auto perspective=base;perspective.constants[3][3]=2;compare({perspective});
        perspective=base;perspective.constants[0][3]=.25f;compare({perspective}); // Positive W varies across the actual triangle.
        auto edge=base;edge.vertices.clear();quad(edge,-1.25f,1.25f,-.6f,.6f);compare({edge},true,false);
        edge=base;edge.vertices.clear();quad(edge,.7f,-.7f,1.2f,-1.2f);compare({edge});
        auto subpixel=base;subpixel.vertices.clear();quad(subpixel,-.501f,.501f,.499f,-.499f);compare({subpixel});
        auto clipped=base;clipped.vertices[0].position[2]=-.25f;clipped.vertices[1].position[2]=1.25f;compare({clipped});
        // Four sides and four corners are wholly beyond the physical target,
        // even after the conservative two-texel raster guard. Draws still run.
        for(const auto shift:std::array<std::array<float,2>,8>{{{-3,0},{3,0},{0,-3},{0,3},{-3,-3},{-3,3},{3,-3},{3,3}}}){
            auto offscreen=base;for(auto& v:offscreen.vertices){v.position[0]+=shift[0];v.position[1]+=shift[1];}compare({offscreen},true,true,true);}
        auto behind=base;behind.constants[3][3]=-1;compare({behind},true,true,true);
        behind.constants[3][3]=-2;behind.constants[0][3]=.5f;compare({behind},true,true,true);
        auto mixed=base;mixed.constants[3][3]=.5f;mixed.constants[0][3]=2;
        need(full(immediateCopyRegion(mixed,orderOf(mixed),width,height,false,viewport),width,height),"Mixed-W immediate skipped conservative full copies");compare({mixed},false,false);
        auto uncertain=base;uncertain.constants[3][3]=std::numeric_limits<float>::min();
        need(full(immediateCopyRegion(uncertain,orderOf(uncertain),width,height,false,viewport),width,height),"Near-zero uncertain W skipped full copies");compare({uncertain},false);
        auto guard=base;guard.vertices.clear();quad(guard,-1.00001f,.2f,-.92f,-.2f);
        const auto edgeResult=compare({guard});const UINT edgePixel=(height/2)*width;
        need(word(edgeResult.first,edgePixel)!=background[edgePixel],"Near-edge positive coverage control drew no fragment");
        guard.vertices.clear();quad(guard,1.00001f,.2f,1.02f,-.2f);
        need(!empty(immediateCopyRegion(guard,orderOf(guard),width,height,false,viewport)),"Conservative two-texel guard was incorrectly discarded");compare({guard});
        auto scissored=base;scissored.scissorEnable=1;compare({scissored});
        // Direct typed-UNORM output is compared with the retained UINT-copy
        // implementation, including independently seeded preceding pixels.
        for(const uint32_t blend:{0x10001u,0x10106u,0x10186u,0x10706u}){
            auto blended=base;blended.blendWord=blend;compare({blended,blended});}
        for(const uint32_t mask:{0u,1u,2u,4u,8u,5u,10u,15u}){
            auto masked=base;masked.colorMask=mask;compare({masked,masked});}
        auto alphaRejected=base;alphaRejected.alphaTest=1;alphaRejected.alphaReference=255;compare({alphaRejected});
        auto depthRejected=base;depthRejected.depthCompare=0;compare({depthRejected});
        auto noDepth=base;noDepth.depthEnable=noDepth.depthWrite=0;compare({noDepth});
        auto dual=base;dual.texture2=base.texture;compare({dual,dual});
        // Radial fans now use the direct snapshot path. Their copy box stays the
        // full surface (the 2D fan transform is not modeled), so no reduction is
        // required; every case is compared with the retained full-copy oracle.
        {
            constexpr float pi=3.14159265358979323846f;
            const auto fan=[&](float cx,float cy,float rx,float ry,float inner,unsigned segments,std::array<float,4> rgba){
                auto radial=base;radial.radial=true;radial.texture.reset();radial.primitive=6;radial.constants={};
                radial.constants[2]={cx,cy,rx,ry};
                radial.constants[3]={2.0f/float(logicalW),2.0f/float(logicalH),.5f/64,.5f/8};
                radial.vertices.clear();
                for(unsigned i=0;i<=segments;++i)for(float radius:{1.0f,inner})
                    radial.vertices.push_back({{radius,float(i)*2*pi/float(segments),0},0,{rgba[0],rgba[1],rgba[2],rgba[3]}});
                return radial;
            };
            const auto filled=fan(24,17,18,11,0,24,{.75f,.375f,.25f,.5f});
            const auto ring=fan(20,15,14,12,.45f,17,{.125f,.625f,.875f,.75f});
            for(const uint32_t blend:{0x10001u,0x10106u,0x10186u,0x10706u}){
                auto first=filled,second=ring;first.blendWord=second.blendWord=blend;compare({first,second},false);}
            for(const uint32_t cull:{2u,6u}){auto culled=filled;culled.cull=cull;compare({culled,ring},false);}
            auto flat=filled;flat.depthEnable=flat.depthWrite=0;compare({flat,flat},false);
            auto masked=ring;masked.colorMask=5;compare({filled,masked},false);
            auto query=b.createTarget(64,8,TargetFormat::RGB10A2);b.clearTarget(query,{.5f,0,0,0});
            auto visible=filled;visible.query=query;compare({visible,ring},false);
            visible.alphaTest=1;visible.alphaReference=96;compare({visible},false);
        }
        auto nan=base;nan.vertices[0].position[0]=std::numeric_limits<float>::quiet_NaN();compare({nan});
        auto inf=base;inf.vertices[0].position[1]=std::numeric_limits<float>::infinity();compare({inf});
        // Unsafe transform evidence must use the full-copy path, without
        // broadening the original draw admission or rejecting valid W=0.
        for(UINT kind=0;kind<6;++kind){auto fallback=base;
            if(kind==0)fallback.constants[3][3]=0;
            if(kind==1)fallback.constants[3][3]=-1;
            if(kind==2)fallback.constants[0][0]=std::numeric_limits<float>::max();
            if(kind==3)fallback.vertices[0].position[0]=std::numeric_limits<float>::denorm_min();
            if(kind==4)fallback.constants[0][0]=std::numeric_limits<float>::denorm_min();
            if(kind==5)fallback.radial=true;
            const auto box=immediateCopyRegion(fallback,orderOf(base),width,height,false,viewport);
            if(kind==1)need(empty(box),"Strictly negative W did not produce an empty copy region");
            else need(full(box,width,height),"Unsafe immediate region did not fall back to full surface");
        }
        need(full(immediateCopyRegion(base,orderOf(base),width,height,true,viewport),width,height),"Temporal immediate region did not fall back");
        auto rejected=base;rejected.vertices[3].alpha=std::numeric_limits<float>::quiet_NaN();
        const auto before=ImmediateRegionState(c);const auto oldColor=b.readbackTarget(color),oldDepth=b.readbackDepthTarget(depth);const auto countBefore=b.immediateDrawCount();
        bool caught=false;try{b.drawImmediate(color,depth,rejected);}catch(const Error& error){caught=std::strstr(error.what(),"vertex is nonfinite")!=nullptr;}
        need(caught&&b.immediateDrawCount()==countBefore&&ImmediateRegionState(c)==before&&b.readbackTarget(color)==oldColor,"Region copies mutated a rejected draw");definedDepth(b.readbackDepthTarget(depth),oldDepth);
        ComPtr<ID3D11InfoQueue> queue;NativeIm2DProbe::device(b)->QueryInterface(IID_PPV_ARGS(&queue));
        if(queue)for(UINT64 i=0;i<queue->GetNumStoredMessages();++i){SIZE_T n=0;queue->GetMessage(i,nullptr,&n);std::vector<uint8_t> bytes(n);auto* message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());queue->GetMessage(i,message,&n);
            need(message->Severity>D3D11_MESSAGE_SEVERITY_ERROR,"Immediate region D3D11 validation error");}
    }
    std::printf("PASS immediate rectangle/full-copy GPU differentials: %zu cases, exact packed pixels and defined depth/stencil\n",comparisons);
}
int main(int argc,char** argv){try{
    const bool hardware=argc==2&&std::strcmp(argv[1],"--hardware")==0;
    immediateRegionCopyContracts(hardware);
    NativeBackend b(!hardware);NativeIm2DProbe::debug(b,hardware);
    auto color=b.createTarget(16,16,TargetFormat::RGB10A2);auto depth=b.createDepthTarget(16,16);
    b.bindTargets({color,nullptr,nullptr,nullptr},depth);b.setViewport({0,0,16,16,0,1});
    b.clearTarget(color,{0,0,0,0});b.clearDepthTarget(depth,0,0x5A);
    ImmediateDraw d{};d.viewport={0,0,16,16,0,0x3F800000};d.depthEnable=d.depthWrite=1;d.depthCompare=6;
    d.viewportEnable=d.halfPixelOffset=1;d.colorMask=15;d.multisampleMask=0xFFFFFFFF;d.blendWord=0x10106;
    const uint8_t texel[]={255,255,255,255};d.texture=b.createTexture(1,1,TextureFormat::RGBA8,texel);
    d.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;d.sampler.AddressU=d.sampler.AddressV=d.sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    d.sampler.MaxLOD=13;d.sampler.MaxAnisotropy=1;d.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
    for(unsigned row=0;row<4;++row)d.constants[row][row]=1;
    d.constants[4]={.25f,.5f,.75f,1};
    for(auto xy:std::array<std::array<float,2>,4>{{{-.5f,.5f},{-.5f,-.5f},{.5f,.5f},{.5f,-.5f}}})
        d.vertices.push_back({{xy[0],xy[1],.5f},.5f,{0,0,0,0}});
    auto* context=NativeIm2DProbe::context(b);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    b.drawImmediate(color,depth,d);auto pixels=b.readbackTarget(color);auto depths=b.readbackDepthTarget(depth);
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){
        const unsigned i=y*16+x;const auto v=word(pixels,i);const bool inside=x>=4&&x<12&&y>=4&&y<12;
        need(v==(inside?(128u|(256u<<10)|(384u<<20)|(2u<<30)):0),"Immediate coverage, color or shared strip edge differs");
        need(word(depths,2*i)==(inside?0x3F000000u:0u)&&depths[i*8+4]==0x5A,"Immediate depth/stencil differs");
    }
    // The direct draw has restored PS1 when it returns. Its queued GPU work
    // must retain the immutable bank even after the backend cache drops it.
    const auto bankWords=[&](ID3D11Buffer* bank){
        need(bank!=nullptr,"Immediate draw did not publish its immutable depth bank");
        D3D11_BUFFER_DESC desc{};bank->GetDesc(&desc);
        need(desc.ByteWidth==16&&desc.Usage==D3D11_USAGE_IMMUTABLE&&desc.BindFlags==D3D11_BIND_CONSTANT_BUFFER&&
            !desc.CPUAccessFlags&&!desc.MiscFlags&&!desc.StructureByteStride,"Immediate depth bank descriptor differs");
        ComPtr<ID3D11Device> owner;bank->GetDevice(&owner);
        need(owner.Get()==NativeIm2DProbe::device(b),"Immediate depth bank has a foreign device");
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> staging;
        need(SUCCEEDED(owner->CreateBuffer(&desc,nullptr,&staging)),"Immediate depth bank staging creation failed");
        context->CopyResource(staging.Get(),bank);D3D11_MAPPED_SUBRESOURCE mapped{};
        need(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Immediate depth bank GPU readback failed");
        std::array<uint32_t,4> words{};const bool valid=mapped.pData!=nullptr;
        if(valid){std::memcpy(words.data(),mapped.pData,sizeof(words));}context->Unmap(staging.Get(),0);
        need(valid,"Immediate depth bank map returned no bytes");return words;
    };
    auto bank=NativeIm2DProbe::cachedDepth(b,0,0,0);
    need(bankWords(bank.Get())==std::array<uint32_t,4>{0,0,0,0},"Immediate depth bank changed exact zero/reserved words");
    auto* bankIdentity=bank.Get();
    b.clearTarget(color,{0,0,0,0});b.clearDepthTarget(depth,0,0x5A);b.drawImmediate(color,depth,d);
    need(NativeIm2DProbe::cachedDepth(b,0,0,0).Get()==bankIdentity,"Repeated immediate draw replaced its exact depth bank");
    bank.Reset();NativeIm2DProbe::resetSceneCache(b); // No test-owned depth bank masks queued GPU lifetime.
    need(!NativeIm2DProbe::cachedDepth(b,0,0,0),"Immediate scene reset retained its depth bank");
    need(b.readbackTarget(color)==pixels,"Cache reset changed an already queued immediate color draw");
    const auto queuedDepth=b.readbackDepthTarget(depth);
    need(queuedDepth.size()==depths.size()&&depths.size()%8==0,"Queued immediate depth extent changed");
    for(size_t i=0;i<depths.size();i+=8){
        // D32_FLOAT_S8X24_UINT exposes only float depth and stencil; X24 is undefined.
        need(!std::memcmp(queuedDepth.data()+i,depths.data()+i,5),"Cache reset changed queued immediate depth/stencil");
    }
    auto signedDepth=d;signedDepth.depthBiasBits=signedDepth.slopeBiasBits=0x80000000;
    b.clearTarget(color,{0,0,0,0});b.clearDepthTarget(depth,0,0x5A);b.drawImmediate(color,depth,signedDepth);
    auto signedBank=NativeIm2DProbe::cachedDepth(b,0,0x80000000,0x80000000);
    need(bankWords(signedBank.Get())==std::array<uint32_t,4>{0,0x80000000,0x80000000,0},"Immediate depth bank canonicalized signed zero");
    b.drawImmediate(color,depth,signedDepth);
    need(NativeIm2DProbe::cachedDepth(b,0,0x80000000,0x80000000).Get()==signedBank.Get(),"Repeated signed-zero bank was not reused");
    auto reverseDepth=signedDepth;reverseDepth.viewport[4]=0x3F800000;reverseDepth.viewport[5]=0;
    b.drawImmediate(color,depth,reverseDepth);auto reverseBank=NativeIm2DProbe::cachedDepth(b,1,0x80000000,0x80000000);
    need(reverseBank.Get()!=signedBank.Get()&&bankWords(reverseBank.Get())==std::array<uint32_t,4>{1,0x80000000,0x80000000,0},
        "Immediate reverse depth selected a different exact immutable profile");
    // The original decal mesh producer emits independent triangles. Preserve
    // their order and avoid inventing connector triangles between groups.
    auto triangles=d;triangles.primitive=4;
    triangles.vertices={d.vertices[0],d.vertices[1],d.vertices[2],d.vertices[2],d.vertices[1],d.vertices[3]};
    b.clearTarget(color,{0,0,0,0});b.clearDepthTarget(depth,0,0x5A);b.drawImmediate(color,depth,triangles);
    need(b.readbackTarget(color)==pixels&&b.readbackDepthTarget(depth)==depths,"Independent immediate triangles differ from the original quad");
    auto separated=triangles;
    const std::array<std::array<float,3>,6> separatePositions={{{-.9f,.9f,.5f},{-.9f,.1f,.5f},{-.1f,.9f,.5f},
        {.1f,-.1f,.5f},{.9f,-.9f,.5f},{.9f,-.1f,.5f}}};
    for(size_t i=0;i<separated.vertices.size();++i)separated.vertices[i].position=separatePositions[i];
    b.clearTarget(color,{0,0,0,0});b.clearDepthTarget(depth,0,0x5A);
    for(size_t at:{0u,3u}){auto single=separated;single.primitive=6;single.vertices.assign(separated.vertices.begin()+at,separated.vertices.begin()+at+3);b.drawImmediate(color,depth,single);}
    const auto separateColor=b.readbackTarget(color),separateDepth=b.readbackDepthTarget(depth);
    b.clearTarget(color,{0,0,0,0});b.clearDepthTarget(depth,0,0x5A);b.drawImmediate(color,depth,separated);
    need(b.readbackTarget(color)==separateColor&&b.readbackDepthTarget(depth)==separateDepth,"Immediate triangle list introduced connector geometry");
    auto rejectsWithoutDraw=[&](const ImmediateDraw& bad){
        const auto priorDraws=b.immediateDrawCount();const auto priorColor=b.readbackTarget(color),priorDepth=b.readbackDepthTarget(depth);
        bool rejected=false;try{b.drawImmediate(color,depth,bad);}catch(const Error&){rejected=true;}
        need(rejected&&b.immediateDrawCount()==priorDraws&&b.readbackTarget(color)==priorColor&&b.readbackDepthTarget(depth)==priorDepth,
             "Invalid immediate draw changed geometry or attachments");
    };
    auto invalidTriangles=triangles;invalidTriangles.vertices.pop_back();rejectsWithoutDraw(invalidTriangles);
    invalidTriangles=triangles;invalidTriangles.primitive=5;rejectsWithoutDraw(invalidTriangles);
    // Original six-face queue can begin with texture identity zero. The pinned
    // null-fetch descriptor returns RGBA zero; it must not reuse a prior SRV.
    auto nullTexture=d;nullTexture.texture.reset();rejectsWithoutDraw(nullTexture);
    nullTexture.nullTexture=true;nullTexture.alphaReference=1;nullTexture.sampler={};
    const auto nullBeforeColor=b.readbackTarget(color),nullBeforeDepth=b.readbackDepthTarget(depth);
    b.bindEngineTexture(0,d.texture);ComPtr<ID3D11ShaderResourceView> retainedTexture;
    context->PSGetShaderResources(0,1,&retainedTexture);
    b.drawImmediate(color,depth,nullTexture);
    need(b.readbackTarget(color)==nullBeforeColor&&b.readbackDepthTarget(depth)==nullBeforeDepth,
         "Original null immediate sample reused a texture or wrote rejected fragments");
    ComPtr<ID3D11ShaderResourceView> restoredTexture;context->PSGetShaderResources(0,1,&restoredTexture);
    need(restoredTexture==retainedTexture,"Null immediate sample did not restore the retained SRV");
    nullTexture.alphaTest=0;nullTexture.blendEnable=0;nullTexture.depthWrite=0;
    b.clearTarget(color,{1,1,1,1});b.drawImmediate(color,depth,nullTexture);
    need(word(b.readbackTarget(color),136)==0,"Original null immediate sample is not RGBA zero");
    auto inconsistentNull=d;inconsistentNull.nullTexture=true;rejectsWithoutDraw(inconsistentNull);
    // Exact binary planes give an independent depth oracle: SDK CC is slope
    // (GPU subpixel scaling cancels), while SDK D0 is the constant offset.
    // At pixel (8,8), the unbiased depth is .53125 and max derivative .0625.
    auto biased=d;biased.depthBiasBits=std::bit_cast<uint32_t>(.00390625f);biased.slopeBiasBits=std::bit_cast<uint32_t>(.5f);
    biased.vertices[0].position[2]=biased.vertices[1].position[2]=.25f;
    biased.vertices[2].position[2]=biased.vertices[3].position[2]=.75f;
    for(bool reverse:{false,true}){
        biased.viewport[4]=reverse?0x3F800000u:0;biased.viewport[5]=reverse?0:0x3F800000u;
        b.clearTarget(color,{0,0,0,0});b.clearDepthTarget(depth,0,0x5A);b.drawImmediate(color,depth,biased);
        const auto result=b.readbackDepthTarget(depth);const float expected=(reverse?.46875f:.53125f)+.00390625f+.03125f;
        need(word(result,2*136)==std::bit_cast<uint32_t>(expected)&&result[136*8+4]==0x5A,
             "Immediate constant/slope bias or reversed depth differs");
    }
    biased=d;biased.depthBiasBits=0x38FBA882;biased.slopeBiasBits=0x3F000000;
    b.clearDepthTarget(depth,0,0x5A);b.drawImmediate(color,depth,biased);
    // Flat surface: slope contributes zero; round .5 + original constant to
    // the nearest 20e4 value. Swapped fields incorrectly clamp this to1.
    const uint32_t flatBits=std::bit_cast<uint32_t>(.5f+std::bit_cast<float>(0x38FBA882u));
    const uint32_t flatRounded=(flatBits+3u+((flatBits>>3)&1u))&~7u;
    need(word(b.readbackDepthTarget(depth),2*136)==flatRounded,"Original mode-one constant/slope fields are reversed");
    for(bool slope:{false,true}){auto bad=d;(slope?bad.slopeBiasBits:bad.depthBiasBits)=0x7FC00000;rejectsWithoutDraw(bad);}
    auto overflowBias=d;overflowBias.slopeBiasBits=0x7F7FFFFF;rejectsWithoutDraw(overflowBias);
    b.clearTarget(color,{0,0,0,0});b.clearDepthTarget(depth,0,0x5A);b.drawImmediate(color,depth,d);
    D3D11_PRIMITIVE_TOPOLOGY topology;context->IAGetPrimitiveTopology(&topology);need(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"Immediate topology leaked");
    // The first immediate effect after startup may have no retained viewport.
    // Its own qualified viewport must determine coverage while every prior
    // viewport is restored exactly, including an unrelated multiple-view case.
    const std::array<D3D11_VIEWPORT,2> retainedViewports={{{1,2,3,4,.25f,.75f},{5,6,7,8,0,1}}};
    for(UINT retainedCount:{0u,2u}) {
        context->RSSetViewports(retainedCount,retainedCount?retainedViewports.data():nullptr);
        b.clearTarget(color,{0,0,0,0});b.clearDepthTarget(depth,0,0x5A);b.drawImmediate(color,depth,d);
        need(b.readbackTarget(color)==pixels&&b.readbackDepthTarget(depth)==depths,"Immediate coverage depended on retained viewport state");
        UINT restoredCount=0;context->RSGetViewports(&restoredCount,nullptr);
        need(restoredCount==retainedCount,"Immediate changed the retained viewport count");
        if(retainedCount) {
            std::array<D3D11_VIEWPORT,2> restored{};context->RSGetViewports(&restoredCount,restored.data());
            need(!std::memcmp(restored.data(),retainedViewports.data(),sizeof(retainedViewports)),"Immediate changed retained viewport values");
        }
    }
    b.setViewport({0,0,16,16,0,1});
    b.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);
    b.drawImmediate(color,depth,d);need((word(b.readbackTarget(color),136)&1023)==256,"Immediate additive overlap lost preceding output");
    d.alphaReference=255;auto before=b.readbackTarget(color);b.drawImmediate(color,depth,d);need(b.readbackTarget(color)==before,"Immediate alpha rejection wrote color");
    d.alphaReference=0;d.blendEnable=0;d.depthWrite=0;d.constants[3][0]=.5f;
    const uint8_t cyan[]={0,255,255,128};d.texture=b.createTexture(1,1,TextureFormat::RGBA8,cyan);
    before=b.readbackDepthTarget(depth);b.clearTarget(color,{0,0,0,0});b.drawImmediate(color,depth,d);
    need(b.readbackDepthTarget(depth)==before,"Immediate disabled depth write changed depth/stencil");
    pixels=b.readbackTarget(color);need(word(pixels,136)==((512u<<10)|(767u<<20)|(1u<<30)),"Immediate texture/constant alpha modulation differs");
    need(!word(pixels,8*16+7),"Immediate matrix translation was ignored");
    const uint8_t dualTexels[]={0,255,0,255,255,0,0,255,255,0,0,255,255,0,0,255};
    d.texture=b.createTexture(1,1,TextureFormat::RGBA8,texel);
    d.texture2=b.createTexture(2,2,TextureFormat::RGBA8,dualTexels);
    d.constants[3][0]=0;d.constants[4]={1,1,1,1};
    for(auto& vertex:d.vertices){vertex.alpha=1;vertex.uv={.75f,.75f,.25f,.25f};}
    b.clearTarget(color,{0,0,0,0});b.drawImmediate(color,depth,d);
    need(word(b.readbackTarget(color),136)==((1023u<<10)|(3u<<30)),"Dual immediate texture or second UV pair differs");
    d.texture2.reset();
    // The original immediate texture helper selects point mip filtering.
    // At LOD about .75, point selects the green mip while trilinear would
    // mix the red base mip into it. Each level is spatially constant.
    std::array<uint8_t,64> mip0{};std::array<uint8_t,16> mip1{};const std::array<uint8_t,4> mip2{0,0,255,255};
    for(size_t i=0;i<mip0.size();i+=4){mip0[i]=mip0[i+3]=255;}
    for(size_t i=0;i<mip1.size();i+=4){mip1[i+1]=mip1[i+3]=255;}
    const std::array<std::span<const uint8_t>,3> mipLevels={mip0,mip1,mip2};
    d.texture=b.createTextureMipChain(4,4,TextureFormat::RGBA8,mipLevels);d.sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    const float span=2*std::pow(2.0f,.75f);
    d.vertices[0].uv={0,0,0,0};d.vertices[1].uv={0,span,0,0};d.vertices[2].uv={span,0,0,0};d.vertices[3].uv={span,span,0,0};
    b.clearTarget(color,{0,0,0,0});b.drawImmediate(color,depth,d);
    need(word(b.readbackTarget(color),136)==((1023u<<10)|(3u<<30)),"Immediate original point-mip sampling blended adjacent mip levels");
    // Original mode-zero beam/strip producers leave the second UV pair
    // unwritten. Poison it like reused ring storage and retain identical pixels
    // without changing the caller's CPU snapshot. Dual shaders read those lanes
    // and must continue rejecting the same poison before drawing.
    const auto finitePixels=b.readbackTarget(color),finiteDepth=b.readbackDepthTarget(depth);
    const auto finiteVertices=d.vertices;
    for(auto& vertex:d.vertices){vertex.uv[2]=std::numeric_limits<float>::quiet_NaN();vertex.uv[3]=std::numeric_limits<float>::infinity();}
    const auto poisonedVertices=d.vertices;
    b.clearTarget(color,{0,0,0,0});b.drawImmediate(color,depth,d);
    need(b.readbackTarget(color)==finitePixels&&b.readbackDepthTarget(depth)==finiteDepth,
         "Unused immediate UV pair changed pixels or depth");
    need(!std::memcmp(d.vertices.data(),poisonedVertices.data(),d.vertices.size()*sizeof(ImmediateVertex)),
         "Immediate upload changed the caller's unused UV bytes");
    const auto rejectVertex=[&](const ImmediateDraw& bad){
        const auto beforeCount=b.immediateDrawCount();const auto beforeColor=b.readbackTarget(color),beforeDepth=b.readbackDepthTarget(depth);
        bool caught=false;try{b.drawImmediate(color,depth,bad);}catch(const Error& error){caught=std::strstr(error.what(),"vertex is nonfinite")!=nullptr;}
        need(caught&&b.immediateDrawCount()==beforeCount&&b.readbackTarget(color)==beforeColor&&b.readbackDepthTarget(depth)==beforeDepth,
             "Nonfinite consumed immediate vertex lane was accepted or changed attachments");
    };
    auto poisonedDual=d;poisonedDual.texture2=d.texture;rejectVertex(poisonedDual);
    // A consumed alpha or first-UV lane that is not finite still rejects. A vertex with a non-finite POSITION is not rasterized by the
    // original GPU: every triangle using it is dropped (live gamehub: a dual-texture quad whose first vertex was all quiet NaN).
    for(unsigned lane=3;lane<6;++lane){auto bad=d;reinterpret_cast<float*>(&bad.vertices[0])[lane]=std::numeric_limits<float>::quiet_NaN();rejectVertex(bad);}
    {
        const auto nan=std::numeric_limits<float>::quiet_NaN();
        const auto pixelsOf=[&](const ImmediateDraw& draw){b.clearTarget(color,{0,0,0,0});const auto count=b.immediateDrawCount();b.drawImmediate(color,depth,draw);
            need(b.immediateDrawCount()==count+1,"A culled or partly culled immediate draw was not counted");return b.readbackTarget(color);};
        d.vertices=finiteVertices;const auto fullQuad=pixelsOf(d);
        auto survivor=d;survivor.vertices={finiteVertices[1],finiteVertices[2],finiteVertices[3]};const auto oneTriangle=pixelsOf(survivor);
        need(oneTriangle!=fullQuad&&oneTriangle!=std::vector<uint8_t>(fullQuad.size()),"The one-triangle oracle does not differ from the quad or from nothing");
        for(unsigned lane=0;lane<3;++lane) {
            auto bad=d;reinterpret_cast<float*>(&bad.vertices[0])[lane]=nan;
            need(pixelsOf(bad)==oneTriangle,"A non-finite position lane did not drop exactly the triangles using that vertex");
            bad=d;reinterpret_cast<float*>(&bad.vertices[0])[lane]=std::numeric_limits<float>::infinity();
            need(pixelsOf(bad)==oneTriangle,"An infinite position lane did not drop exactly the triangles using that vertex");
        }
        {auto bad=d;bad.vertices[0].position={nan,nan,nan};bad.vertices[0].alpha=nan;bad.vertices[0].uv={0,.1279f,0,.1279f};
            const auto depthBefore=b.readbackDepthTarget(depth);
            need(pixelsOf(bad)==oneTriangle&&b.readbackDepthTarget(depth)==depthBefore,"The live all-NaN first vertex was not dropped cleanly");}
        {auto bad=d;for(auto& vertex:bad.vertices)vertex.position[1]=nan;
            need(pixelsOf(bad)==std::vector<uint8_t>(fullQuad.size()),"A draw whose triangles all use non-finite positions drew pixels");}
        {auto bad=d;bad.vertices[0].position[0]=nan;bad.vertices[3].alpha=nan;rejectVertex(bad);} // A surviving triangle's consumed lane still rejects.
        {auto list=d;list.primitive=4;list.vertices={finiteVertices[0],finiteVertices[1],finiteVertices[2],finiteVertices[1],finiteVertices[2],finiteVertices[3]};
            const auto both=pixelsOf(list);
            auto bad=list;bad.vertices[4].position[2]=nan; // Only the second triangle uses vertex 4.
            auto first=list;first.vertices.resize(3);const auto firstOnly=pixelsOf(first);
            need(pixelsOf(bad)==firstOnly&&firstOnly!=both,"A triangle list did not drop only the triangle using the non-finite vertex");
            auto last=list;last.vertices[5].position[1]=nan; // The third vertex of the second triangle.
            need(pixelsOf(last)==firstOnly,"A triangle list ignored a non-finite third vertex");}
        {auto bad=d;bad.vertices[3].position[0]=nan; // The last strip vertex belongs to the second triangle only.
            auto firstStrip=d;firstStrip.vertices={finiteVertices[0],finiteVertices[1],finiteVertices[2]};
            need(pixelsOf(bad)==pixelsOf(firstStrip),"A strip ignored a non-finite last vertex");}
    }
    d.vertices=finiteVertices;
    auto invalid=d;invalid.vertices.resize(2);const auto count=b.immediateDrawCount();bool rejected=false;
    try{b.drawImmediate(color,depth,invalid);}catch(const Error&){rejected=true;}
    need(rejected&&b.immediateDrawCount()==count,"Invalid immediate extent submitted");
    // Original four-segment radial ring: inner radius zero produces a filled
    // diamond. Its pixel-space center/scales are deliberately asymmetric.
    d.radial=true;d.texture.reset();d.depthEnable=0;d.alphaTest=0;d.constants={};
    d.constants[2]={8,8,6,4};d.constants[3]={.125f,.125f,.5f/64,.5f/8};d.vertices.clear();
    constexpr float pi=3.14159265358979323846f;
    for(unsigned i=0;i<=4;++i)for(float radius:{1.0f,0.0f})
        d.vertices.push_back({{radius,float(i)*pi*.5f,0},0,{1,.5f,.25f,1}});
    b.clearTarget(color,{0,0,0,0});b.drawImmediate(color,depth,d);pixels=b.readbackTarget(color);
    need(word(pixels,8*16+8)==(1023u|(512u<<10)|(256u<<20)|(3u<<30)),"Radial normalized color differs");
    need(word(pixels,8*16+12)&&!word(pixels,8*16+14)&&word(pixels,5*16+8)&&!word(pixels,3*16+8),"Radial center, radius or viewport transform differs");
    {auto bad=d;bad.vertices[0].position[0]=std::numeric_limits<float>::quiet_NaN();rejectVertex(bad);} // Radial draws keep their all-vertex validation.
    // Original radial batches retain culling. Opposite ring order reverses
    // winding without changing coverage, exposing a hardcoded no-cull state.
    const auto radialDepth=b.readbackDepthTarget(depth);auto winding=d;
    std::array<std::vector<uint8_t>,2> culled;
    for(size_t i=0;i<culled.size();++i){winding.cull=i?6u:2u;b.clearTarget(color,{0,0,0,0});
        b.drawImmediate(color,depth,winding);culled[i]=b.readbackTarget(color);
        need(b.readbackDepthTarget(depth)==radialDepth,"Inherited radial culling changed depth/stencil");}
    const std::vector<uint8_t> emptyRadial(pixels.size());
    need((culled[0]==pixels&&culled[1]==emptyRadial)||(culled[1]==pixels&&culled[0]==emptyRadial),
         "Radial opposite cull modes did not preserve/reject the original winding");
    for(size_t i=0;i<winding.vertices.size();i+=2)std::swap(winding.vertices[i],winding.vertices[i+1]);
    for(size_t i=0;i<culled.size();++i){winding.cull=i?6u:2u;b.clearTarget(color,{0,0,0,0});
        b.drawImmediate(color,depth,winding);need(b.readbackTarget(color)==culled[1-i],"Radial reversed winding did not reverse culling");}
    auto invalidRadial=d;invalidRadial.cull=4;rejectsWithoutDraw(invalidRadial);
    auto nonradialCull=triangles;nonradialCull.cull=2;rejectsWithoutDraw(nonradialCull);
    auto query=b.createTarget(64,8,TargetFormat::RGB10A2);b.clearTarget(query,{0,0,0,0});d.query=query;
    b.clearTarget(color,{0,0,0,0});b.drawImmediate(color,depth,d);
    need(!word(b.readbackTarget(color),8*16+8),"Radial zero visibility did not suppress color");
    b.clearTarget(query,{1,0,0,0});b.drawImmediate(color,depth,d);
    need(b.readbackTarget(color)==pixels,"Radial full visibility differs from flat draw");
    b.requireSelectedTargets({color,nullptr,nullptr,nullptr},depth);
    if(NativeIm2DProbe::device(b)->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1){
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=16;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=4;
        ComPtr<ID3D11Buffer> buffer;need(SUCCEEDED(NativeIm2DProbe::device(b)->CreateBuffer(&desc,nullptr,&buffer)),"Immediate high UAV storage creation failed");
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;view.Buffer.NumElements=4;
        ComPtr<ID3D11UnorderedAccessView> uav;need(SUCCEEDED(NativeIm2DProbe::device(b)->CreateUnorderedAccessView(buffer.Get(),&view,&uav)),"Immediate high UAV view creation failed");
        for(UINT slot:{1u,8u,63u}){
            auto* raw=uav.Get();context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,slot,1,&raw,nullptr);
            const auto pixelsBefore=b.readbackTarget(color);const auto depthBytesBefore=b.readbackDepthTarget(depth);const auto countBefore=b.immediateDrawCount();
            bool rejectedUav=false;try{b.drawImmediate(color,depth,d);}catch(const Error&){rejectedUav=true;}
            need(rejectedUav&&b.immediateDrawCount()==countBefore,"Immediate high UAV draw was not rejected before submission");
            ComPtr<ID3D11UnorderedAccessView> retained;context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,slot,1,&retained);
            need(retained==uav&&b.readbackTarget(color)==pixelsBefore&&b.readbackDepthTarget(depth)==depthBytesBefore,"Immediate rejected high UAV draw changed state or attachments");
            ID3D11UnorderedAccessView* empty=nullptr;context->OMSetRenderTargetsAndUnorderedAccessViews(D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,slot,1,&empty,nullptr);
        }
    }
    ComPtr<ID3D11InfoQueue> queue;NativeIm2DProbe::device(b)->QueryInterface(IID_PPV_ARGS(&queue));
    if(queue)for(UINT64 i=0;i<queue->GetNumStoredMessages();++i){SIZE_T n=0;queue->GetMessage(i,nullptr,&n);std::vector<uint8_t> bytes(n);auto* message=reinterpret_cast<D3D11_MESSAGE*>(bytes.data());queue->GetMessage(i,message,&n);
        if(message->Severity<=D3D11_MESSAGE_SEVERITY_ERROR){std::fprintf(stderr,"D3D11: %s\n",message->pDescription);throw Error("Immediate D3D11 validation error");}}
    std::puts("PASS immediate strip, matrix, texture/alpha, blending, depth/stencil, state restoration and rejection");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL immediate backend: %s\n",e.what());return 1;}}
