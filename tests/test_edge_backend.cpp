#include "renderer/native_material_compiler.h"
#include <fstream>
#include <iterator>
#include <cstdio>
#include <limits>
#include <cmath>

using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void need(bool b,const char* message){++checks;if(!b)throw Error(message);}
template<class F>void rejects(F f){try{f();}catch(const Error&){++checks;return;}throw Error("Invalid edge operation accepted");}
void run(const char* image,bool software) {
    std::ifstream file(image,std::ios::binary);const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
    need(bytes.size()==15466496,"Wrong original image");NativeBackend backend(software);NativeMaterialCompiler compiler(backend);MaterialRegistry registry;
    std::array<MaterialId,2> ids{};
    for(const auto& record:originalMaterialIdentities())if(record.originalAddress==0x8202E6F0 || record.originalAddress==0x8202E840)
        ids[record.stage==MaterialStage::Vertex?0:1]=registry.create(record.originalAddress,
            std::span<const uint8_t>(bytes).subspan(record.originalAddress-0x82000000,record.recordBytes));
    const auto& vertex=registry.prepareForBind(ids[0],compiler);const auto& pixel=registry.prepareForBind(ids[1],compiler);
    auto target=backend.createTarget(32,32,TargetFormat::RGB10A2),source=backend.createTarget(32,32,TargetFormat::RGB10A2);
    auto depth=backend.createDepthTarget(32,32);backend.clearTarget(target,{0,0,0,1});backend.clearDepthTarget(depth,1,7);
    ScreenDraw region{};region.vertices={ScreenVertex{-1,1,0,0},ScreenVertex{0,1,1,0},ScreenVertex{-1,-1,0,1},ScreenVertex{0,-1,1,1}};
    region.color={1,0,0,1};region.blendSelector=3;region.colorWriteMask=15;backend.drawScreen(target,region);
    backend.bindTargets({target,nullptr,nullptr,nullptr},depth);backend.copyFront(target,source);
    const auto copied=backend.readbackTarget(source),depthBefore=backend.readbackDepthTarget(depth);
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){const auto p=(y*32+x)*4;
        need(copied[p]==(x<16?255:0) && copied[p+1]==(x<16?3:0) && copied[p+2]==0 && copied[p+3]==0xC0,"Independent source pattern differs");}
    backend.setViewport({0,0,32,32,0,1});backend.bindEdgeShaders(vertex,pixel);
    EdgeConstants constants{};constants.kernel[0]={0,1,0,1};constants.kernel[1]={1,0,0,1};
    constants.kernel[2]={0,-1,0,1};constants.kernel[3]={-1,0,0,1};constants.dimensions={32,32,1,0};
    D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxAnisotropy=1;sampler.MaxLOD=13;
    const std::array<ScreenVertex,3> corners={ScreenVertex{-1,1,0,0},ScreenVertex{1,1,1,0},ScreenVertex{-1,-1,0,1}};
    rejects([&]{backend.drawEdge(target,depth,{},corners,vertex,pixel);});
    auto commit=backend.commitEdge(source,constants,sampler);backend.requireEdgeCommit(commit);
    rejects([&]{backend.drawEdge(target,depth,commit,corners,vertex,pixel);});
    backend.bindEdgeDeclaration();
    auto badConstants=constants;badConstants.dimensions[2]=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{backend.commitEdge(source,badConstants,sampler);});backend.requireEdgeCommit(commit);
    auto badSampler=sampler;badSampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    rejects([&]{backend.commitEdge(source,constants,badSampler);});backend.requireEdgeCommit(commit);
    rejects([&]{backend.commitEdge(target,constants,sampler);});backend.requireEdgeCommit(commit);
    {NativeBackend other(true);rejects([&]{other.requireEdgeCommit(commit);});}
    auto badCorners=corners;badCorners[1].u=0.5f;
    rejects([&]{backend.drawEdge(target,depth,commit,badCorners,vertex,pixel);});
    backend.setViewport({0,0,16,32,0,1});rejects([&]{backend.drawEdge(target,depth,commit,corners,vertex,pixel);});
    backend.setViewport({0,0,32,32,0,1});
    need(!backend.edgeDrawCount() && backend.readbackTarget(target)==copied,"Rejected edge operation drew or changed output");
    for(float width:{0.0f,0.5f,1.0f,1.5f,2.0f,2.5f}) {
        constants.dimensions[2]=width;auto previous=commit;commit=backend.commitEdge(source,constants,sampler);
        rejects([&]{backend.drawEdge(target,depth,previous,corners,vertex,pixel);});
        backend.drawEdge(target,depth,commit,corners,vertex,pixel);
        const auto result=backend.readbackTarget(target);
        // Half-integer point offsets select the positive-side texel.
        const int positive=int(std::floor(width+0.5f)),negative=int(std::ceil(width-0.5f));
        for(int y=0;y<32;++y)for(int x=0;x<32;++x){const bool red=x<16;
            const bool edge=((x+positive)%32<16)!=red || ((x-negative+32)%32<16)!=red;
            const auto p=size_t(y*32+x)*4;
            need(result[p]==(edge?255:0) && result[p+1]==(edge?255:0) && result[p+2]==(edge?255:0) && result[p+3]==(edge?255:192),
                 "Native edge packed output differs from independent stripe classification");}
        need(backend.readbackTarget(source)==copied,"Edge changed its sampled source");
        need(backend.readbackDepthTarget(depth)==depthBefore,"Edge changed disabled depth/stencil");
    }
    backend.clearEngineTexture(0);rejects([&]{backend.drawEdge(target,depth,commit,corners,vertex,pixel);});
    need(backend.edgeDrawCount()==6 && !backend.presentationCount(),"Wrong draw count or spurious presentation");
    std::array<MaterialId,2> aaIds{};
    for(const auto& record:originalMaterialIdentities())if(record.originalAddress==0x820301A0 || record.originalAddress==0x820302EC)
        aaIds[record.stage==MaterialStage::Vertex?0:1]=registry.create(record.originalAddress,
            std::span<const uint8_t>(bytes).subspan(record.originalAddress-0x82000000,record.recordBytes));
    const auto& aaVertex=registry.prepareForBind(aaIds[0],compiler);const auto& aaPixel=registry.prepareForBind(aaIds[1],compiler);
    rejects([&]{backend.bindEdgeShaders(vertex,aaPixel);});
    auto word=[](const std::vector<uint8_t>& image,int x,int y){const size_t p=size_t(((y+32)%32)*32+(x+32)%32)*4;
        return uint32_t(image[p])|uint32_t(image[p+1])<<8|uint32_t(image[p+2])<<16|uint32_t(image[p+3])<<24;};
    for(unsigned pattern=0;pattern<4;++pattern) {
        backend.clearEngineTexture(0);
        backend.clearTarget(target,pattern==0?std::array<float,4>{0,0,0,1}:std::array<float,4>{17.0f/1023,333.0f/1023,999.0f/1023,0});
        region.color=pattern==0?std::array<float,4>{1,0,0,1}:std::array<float,4>{798.0f/1023,13.0f/1023,431.0f/1023,float(pattern)/3};
        region.vertices[1].x=region.vertices[3].x=pattern<2?0:0.5f;
        backend.drawScreen(target,region);backend.bindTargets({target,nullptr,nullptr,nullptr},depth);
        backend.copyFront(target,source);const auto input=backend.readbackTarget(source);
        backend.bindEdgeShaders(aaVertex,aaPixel);backend.bindEdgeDeclaration();backend.setViewport({0,0,32,32,0,1});
        constants.dimensions[2]=pattern?913.0f:0.15f; // KernelWidth is an inactive shader input.
        auto aaCommit=backend.commitEdge(source,constants,sampler,true);
        rejects([&]{backend.drawEdge(target,depth,aaCommit,corners,vertex,pixel);});
        backend.drawEdge(target,depth,aaCommit,corners,aaVertex,aaPixel);
        const auto output=backend.readbackTarget(target);
        for(int y=0;y<32;++y)for(int x=0;x<32;++x){
            const std::array<uint32_t,5> taps={word(input,x,y),word(input,x,y+1),word(input,x+1,y),word(input,x,y-1),word(input,x-1,y)};
            uint32_t expected=0xC0000000;
            for(unsigned channel=0;channel<3;++channel){uint32_t sum=0;for(auto tap:taps)sum+=(tap>>(10*channel))&1023;
                expected|=((sum+2)/5)<<(10*channel);}
            need(word(output,x,y)==expected,"AA native RGB10A2 output differs from independent integer five-tap mean");
        }
        need(backend.readbackTarget(source)==input && backend.readbackDepthTarget(depth)==depthBefore,"AA changed its source or disabled depth");
    }
    need(backend.aaDrawCount()==4 && backend.edgeDrawCount()==6 && !backend.presentationCount(),"Wrong AA/edge submission accounting");
    std::array<MaterialId,2> edgeAAIds{};
    for(const auto& record:originalMaterialIdentities())if(record.originalAddress==0x820347B0 || record.originalAddress==0x82034900)
        edgeAAIds[record.stage==MaterialStage::Vertex?0:1]=registry.create(record.originalAddress,
            std::span<const uint8_t>(bytes).subspan(record.originalAddress-0x82000000,record.recordBytes));
    const auto& edgeAAVertex=registry.prepareForBind(edgeAAIds[0],compiler);const auto& edgeAAPixel=registry.prepareForBind(edgeAAIds[1],compiler);
    rejects([&]{backend.bindEdgeShaders(aaVertex,edgeAAPixel);});rejects([&]{backend.bindEdgeShaders(edgeAAVertex,pixel);});
    EdgeAAInputs inputs{backend.createTarget(32,32,TargetFormat::RGB10A2),backend.createTarget(32,32,TargetFormat::RGB10A2),
        backend.createTarget(32,32,TargetFormat::RGB10A2),backend.createDepthTarget(32,32),{}};
    std::vector<uint8_t> palette(64*64*4);for(size_t at=0;at<palette.size();at+=4){palette[at]=51;palette[at+1]=102;palette[at+2]=204;palette[at+3]=17;}
    inputs.palette=backend.createTexture(64,64,TextureFormat::RGBA8,palette);
    EdgeAAConstants composite{};
    composite.c20_27={std::array<float,4>{64,0,0,1},{160,0,0,1},{0.4f,0,0,1},{32,0,0,1},
        {32,0,0,1},{1,0,0,1},{0,0,0,1},{1,0,0,1}};
    composite.c48_50={std::array<float,4>{10,0,0,1},{1.5f,0,0,1},{1,0,0,1}};
    const std::array<D3D11_SAMPLER_DESC,5> samplers={sampler,sampler,sampler,sampler,sampler};
    backend.clearBindings();backend.bindTargets({target,nullptr,nullptr,nullptr},depth);backend.bindEdgeDeclaration();backend.setViewport({0,0,32,32,0,1});
    backend.clearTarget(inputs.base,{0,0,0,0});backend.clearTarget(inputs.color,{0,0,0,1});backend.clearTarget(inputs.line,{0,0,0,1});
    backend.clearDepthTarget(inputs.depth,1.0f/256,11);backend.bindEdgeShaders(edgeAAVertex,edgeAAPixel);
    auto compositeCommit=backend.commitEdgeAA(inputs,composite,samplers);
    auto invalid=composite;invalid.c48_50[0][0]=9;rejects([&]{backend.commitEdgeAA(inputs,invalid,samplers);});
    invalid=composite;invalid.c20_27[0][0]=std::numeric_limits<float>::infinity();rejects([&]{backend.commitEdgeAA(inputs,invalid,samplers);});
    auto invalidSamplers=samplers;invalidSamplers[4].AddressU=D3D11_TEXTURE_ADDRESS_CLAMP;rejects([&]{backend.commitEdgeAA(inputs,composite,invalidSamplers);});
    auto invalidInputs=inputs;invalidInputs.depth=depth;rejects([&]{backend.commitEdgeAA(invalidInputs,composite,samplers);});
    invalidInputs=inputs;invalidInputs.base=target;rejects([&]{backend.commitEdgeAA(invalidInputs,composite,samplers);});
    invalidInputs=inputs;invalidInputs.line=inputs.color;rejects([&]{backend.commitEdgeAA(invalidInputs,composite,samplers);});
    invalidInputs=inputs;invalidInputs.palette.reset();rejects([&]{backend.commitEdgeAA(invalidInputs,composite,samplers);});
    {NativeBackend other(true);rejects([&]{other.commitEdgeAA(inputs,composite,samplers);});rejects([&]{other.requireEdgeCommit(compositeCommit);});}
    backend.requireEdgeCommit(compositeCommit);
    for(UINT stage=0;stage<5;++stage){backend.clearEngineTexture(stage);rejects([&]{backend.requireEdgeCommit(compositeCommit);});
        auto old=compositeCommit;compositeCommit=backend.commitEdgeAA(inputs,composite,samplers);rejects([&]{backend.requireEdgeCommit(old);});}
    for(unsigned mode=0;mode<4;++mode){
        backend.clearTarget(inputs.color,mode?std::array<float,4>{1,1,1,1}:std::array<float,4>{0,0,0,1});
        backend.clearTarget(inputs.line,mode==3?std::array<float,4>{1,0,0,1}:std::array<float,4>{0,0,0,1});
        backend.clearDepthTarget(inputs.depth,mode<2?1.0f/256:1.0f/64,11);
        const auto colorBefore=backend.readbackTarget(inputs.color),baseBefore=backend.readbackTarget(inputs.base),lineBefore=backend.readbackTarget(inputs.line);
        const auto depthInputBefore=backend.readbackDepthTarget(inputs.depth);
        compositeCommit=backend.commitEdgeAA(inputs,composite,samplers);
        backend.drawEdge(target,depth,compositeCommit,corners,edgeAAVertex,edgeAAPixel);
        const auto output=backend.readbackTarget(target);
        const std::array<double,4> rgba=mode==0?std::array<double,4>{0.2,0.4,0.8,1}:
            (mode==1?std::array<double,4>{0.175,0.35,0.7,0.875}:(mode==2?std::array<double,4>{0.2,0.4,0.8,0}:std::array<double,4>{0,1,1,0}));
        uint32_t expected=uint32_t(std::lround(rgba[3]*3))<<30;
        for(unsigned lane=0;lane<3;++lane)expected|=uint32_t(std::lround(rgba[lane]*1023))<<(lane*10);
        for(int y=0;y<32;++y)for(int x=0;x<32;++x)need(word(output,x,y)==expected,"Integrated edgeAA output differs from independent branch formula");
        need(backend.readbackTarget(inputs.color)==colorBefore && backend.readbackTarget(inputs.base)==baseBefore &&
             backend.readbackTarget(inputs.line)==lineBefore && backend.readbackDepthTarget(inputs.depth)==depthInputBefore &&
             backend.readbackDepthTarget(depth)==depthBefore,"EdgeAA modified a sampled input or disabled attachment depth");
    }
    need(backend.edgeAADrawCount()==4 && backend.edgeDrawCount()==6 && backend.aaDrawCount()==4 && !backend.presentationCount(),"Wrong edgeAA draw accounting");
    backend.bindEdgeShaders(aaVertex,aaPixel);rejects([&]{backend.drawEdge(target,depth,compositeCommit,corners,aaVertex,aaPixel);});
    for(auto id:edgeAAIds)registry.release(id);
    for(auto id:aaIds)registry.release(id);
    backend.clearBindings();commit.reset();source.reset();target.reset();depth.reset();for(auto id:ids)registry.release(id);
    need(!registry.liveCount(),"Edge shader owners leaked");
}
void runScaledSampling(const char* image,bool software) {
    std::ifstream file(image,std::ios::binary);const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
    need(bytes.size()==15466496,"Wrong original image for scaled edge test");
    const std::array<ScreenVertex,3> corners={ScreenVertex{-1,1,0,0},ScreenVertex{1,1,1,0},ScreenVertex{-1,-1,0,1}};
    D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
    sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler.MaxAnisotropy=1;sampler.MaxLOD=13;
    for(const auto percent:{50u,67u,75u}) {
        NativeBackend backend(software);backend.configureRendering(1280,720,1,Antialiasing::Original,percent);
        NativeMaterialCompiler compiler(backend);MaterialRegistry registry;
        std::array<MaterialId,6> ids{};
        constexpr std::array<uint32_t,6> addresses{0x8202E6F0,0x8202E840,0x820301A0,0x820302EC,0x820347B0,0x82034900};
        for(const auto& record:originalMaterialIdentities())for(size_t i=0;i<addresses.size();++i)
            if(record.originalAddress==addresses[i])ids[i]=registry.create(record.originalAddress,
                std::span<const uint8_t>(bytes).subspan(record.originalAddress-0x82000000,record.recordBytes));
        std::array<const CompiledMaterial*,6> shaders{};
        for(size_t i=0;i<ids.size();++i)shaders[i]=&registry.prepareForBind(ids[i],compiler);
        struct Copies {
            std::shared_ptr<RenderTarget> target,source,base,line;
            std::shared_ptr<DepthTarget> depth,inputDepth;
        };
        const auto make=[&](uint32_t w,uint32_t h,TargetScale scale) {
            return Copies{backend.createTarget(w,h,TargetFormat::RGB10A2,scale),backend.createTarget(w,h,TargetFormat::RGB10A2,scale),
                backend.createTarget(w,h,TargetFormat::RGB10A2,scale),backend.createTarget(w,h,TargetFormat::RGB10A2,scale),
                backend.createDepthTarget(w,h,scale),backend.createDepthTarget(w,h,scale)};
        };
        auto scaled=make(32,32,TargetScale::Scene);
        auto reference=make(scaled.target->pixelWidth(),scaled.target->pixelHeight(),TargetScale::Fixed);
        need(scaled.target->pixelWidth()<scaled.target->width&&scaled.target->pixelHeight()<scaled.target->height,
             "Scaled edge fixture did not exercise smaller scene storage");
        const auto stripe=[&](const std::shared_ptr<RenderTarget>& target,std::array<float,4> left,std::array<float,4> right) {
            backend.clearBindings();backend.clearTarget(target,right);
            ScreenDraw draw{};draw.vertices={ScreenVertex{-1,1,0,0},ScreenVertex{0,1,1,0},ScreenVertex{-1,-1,0,1},ScreenVertex{0,-1,1,1}};
            draw.color=left;draw.blendSelector=3;draw.colorWriteMask=15;backend.drawScreen(target,draw);
            // A second boundary exercises both width and height sampling.
            draw.vertices={ScreenVertex{-1,1,0,0},ScreenVertex{1,1,1,0},ScreenVertex{-1,0,0,1},ScreenVertex{1,0,1,1}};
            for(size_t lane=0;lane<4;++lane)draw.color[lane]=(left[lane]+right[lane])*0.5f;
            backend.drawScreen(target,draw);
        };
        const auto bind=[&](const Copies& copies,size_t shader) {
            backend.clearBindings();backend.bindTargets({copies.target,nullptr,nullptr,nullptr},copies.depth);
            backend.setViewport({0,0,float(copies.target->width),float(copies.target->height),0,1});
            backend.bindEdgeShaders(*shaders[shader],*shaders[shader+1]);backend.bindEdgeDeclaration();
        };
        for(auto* copies:{&reference,&scaled}) {
            stripe(copies->source,{1,0,0,1},{0,0,0,1});backend.clearDepthTarget(copies->depth,0.75f,7);
        }
        need(backend.readbackTarget(scaled.source)==backend.readbackTarget(reference.source),"Scaled and reference edge inputs differ");
        for(const bool aa:{false,true})for(const float width:{0.5f,1.5f}) {
            for(auto* copies:{&reference,&scaled}) {
                bind(*copies,aa?2:0);
                EdgeConstants values{};values.kernel[0]={0,1,0,1};values.kernel[1]={1,0,0,1};
                values.kernel[2]={0,-1,0,1};values.kernel[3]={-1,0,0,1};
                values.dimensions={float(copies->source->width),float(copies->source->height),width,0};
                // Qualification must still require original logical dimensions.
                if(copies==&scaled) {auto bad=values;bad.dimensions[0]=float(copies->source->pixelWidth());
                    rejects([&]{backend.commitEdge(copies->source,bad,sampler,aa);});}
                const auto commit=backend.commitEdge(copies->source,values,sampler,aa);
                backend.drawEdge(copies->target,copies->depth,commit,corners,*shaders[aa?2:0],*shaders[aa?3:1]);
            }
            need(backend.readbackTarget(scaled.target)==backend.readbackTarget(reference.target),
                 aa?"Downscaled AA no longer samples the authored physical neighbor span":"Downscaled half-texel ink kernel lost outlines");
        }
        std::vector<uint8_t> palette(64*64*4);for(size_t at=0;at<palette.size();at+=4){palette[at]=51;palette[at+1]=102;palette[at+2]=204;palette[at+3]=255;}
        const auto paletteTexture=backend.createTexture(64,64,TextureFormat::RGBA8,palette);
        const std::array<D3D11_SAMPLER_DESC,5> samplers={sampler,sampler,sampler,sampler,sampler};
        for(auto* copies:{&reference,&scaled}) {
            backend.clearBindings();backend.clearTarget(copies->source,{1,1,1,1});backend.clearTarget(copies->base,{0,0,0,0});
            stripe(copies->line,{0.7f,0.8f,0.2f,1},{0.1f,0.25f,0.9f,1});backend.clearDepthTarget(copies->inputDepth,1.0f/64,11);
        }
        need(backend.readbackTarget(scaled.line)==backend.readbackTarget(reference.line),"Scaled and reference compositor line inputs differ");
        for(const float blur:{0.5f,1.5f}) {
            for(auto* copies:{&reference,&scaled}) {
                bind(*copies,4);
                EdgeAAInputs inputs{copies->source,copies->base,copies->line,copies->inputDepth,paletteTexture};
                EdgeAAConstants values{};
                values.c20_27={std::array<float,4>{64,0,0,1},{160,0,0,1},{0.4f,0,0,1},
                    {float(copies->source->width),0,0,1},{float(copies->source->height),0,0,1},{1,0,0,1},{0,0,0,1},{1,0,0,1}};
                values.c48_50={std::array<float,4>{10,0,0,1},{blur,0,0,1},{1,0,0,1}};
                if(copies==&scaled) {auto bad=values;bad.c20_27[3][0]=float(copies->source->pixelWidth());
                    rejects([&]{backend.commitEdgeAA(inputs,bad,samplers);});}
                const auto commit=backend.commitEdgeAA(inputs,values,samplers);
                backend.drawEdge(copies->target,copies->depth,commit,corners,*shaders[4],*shaders[5]);
            }
            need(backend.readbackTarget(scaled.target)==backend.readbackTarget(reference.target),
                 "Downscaled cel compositor lost the authored physical line sampling span");
        }
        backend.clearBindings();for(auto id:ids)registry.release(id);
        need(!registry.liveCount(),"Scaled edge shader owners leaked");
        std::printf("Verified %u%% edge/AA/cel sampling against independent fixed-size physical targets\n",percent);
    }
}
}
int main(int argc,char** argv){try{need(argc==2 || (argc==3 && std::string(argv[2])=="--hardware"),"Image and optional --hardware required");
    run(argv[1],argc==2);runScaledSampling(argv[1],argc==2);std::printf("PASS %zu native edge resource/commit/geometry/pixel/depth checks\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL native edge after%zu checks: %s\n",checks,e.what());return 1;}}
