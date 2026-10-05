#include "renderer/immediate_draw.h"
#include "renderer/device_availability.h"
#include <d3d11sdklayers.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#include <bit>
namespace Simpsons::Graphics {
struct NativeIm2DProbe {
    static ID3D11DeviceContext* context(NativeBackend& b){return b.context.Get();}
    static ID3D11Device* device(NativeBackend& b){return b.device.Get();}
    static void debug(NativeBackend& b,bool hardware){
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
int main(int argc,char** argv){try{
    const bool hardware=argc==2&&std::strcmp(argv[1],"--hardware")==0;
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
