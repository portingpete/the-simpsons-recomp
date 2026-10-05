#include "renderer/native_backend.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
using namespace Simpsons::Graphics;
namespace {
size_t checks{},sampled{};
void need(bool v,const char* why){++checks;if(!v)throw Error(why);}
template<class F> void rejects(F&& f,const char* reason){
    try{f();}catch(const Error& e){need(std::string(e.what()).find(reason)!=std::string::npos,"Unexpected mip rejection cause");return;}
    need(false,"Invalid native mip operation accepted");
}
auto views(const std::vector<std::vector<uint8_t>>& data){
    std::vector<std::span<const uint8_t>> result;for(const auto& mip:data)result.emplace_back(mip);return result;
}
auto pattern(uint32_t width,uint32_t height,uint32_t count){
    std::vector<std::vector<uint8_t>> result;
    for(uint32_t level=0;level<count;++level){
        const auto w=std::max(1u,width>>level),h=std::max(1u,height>>level);std::vector<uint8_t> bytes(w*h*4);
        for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)for(uint32_t c=0;c<4;++c)
            bytes[(y*w+x)*4+c]=uint8_t(x*(13+7*c)+y*(37+9*c)+c*47+level*59+3);
        result.push_back(std::move(bytes));
    }
    return result;
}
void exact(NativeBackend& b,const std::shared_ptr<Texture>& t,const std::vector<std::vector<uint8_t>>& expected){
    need(t->levelCount()==expected.size(),"Explicit level count changed");
    for(uint32_t i=0;i<expected.size();++i)need(b.readbackMip(t,i)==expected[i],"Uploaded native mip bytes differ");
    need(b.readback(t)==expected.front(),"Level-zero readback compatibility changed");
}
}
namespace Simpsons::Graphics {
struct NativeMipTextureProbe {
    static void backing(const std::shared_ptr<Texture>& t){
        D3D11_TEXTURE2D_DESC d{};t->texture->GetDesc(&d);
        const auto format=t->format==TextureFormat::BC1?DXGI_FORMAT_BC1_UNORM:
            t->format==TextureFormat::BC2?DXGI_FORMAT_BC2_UNORM:t->format==TextureFormat::BC3?DXGI_FORMAT_BC3_UNORM:
            t->format==TextureFormat::BGRX8?DXGI_FORMAT_B8G8R8X8_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM;
        need(d.Width==t->width && d.Height==t->height && d.MipLevels==t->levelCount() && d.ArraySize==1 &&
             d.Usage==D3D11_USAGE_IMMUTABLE && d.BindFlags==D3D11_BIND_SHADER_RESOURCE && !d.CPUAccessFlags &&
             !d.MiscFlags && d.SampleDesc.Count==1 && !d.SampleDesc.Quality &&
             d.Format==format,"Actual mip backing differs");
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};t->view->GetDesc(&v);ComPtr<ID3D11Resource> resource;t->view->GetResource(&resource);
        need(resource.Get()==t->texture.Get() && v.Format==d.Format && v.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D &&
             !v.Texture2D.MostDetailedMip && v.Texture2D.MipLevels==d.MipLevels,"Mip view does not expose exactly the owned levels");
    }
    template<class T,class F> static void changed(T& field,T replacement,F&& f){auto old=field;field=replacement;try{f();}catch(...){field=old;throw;}field=old;}
    static void invalidMetadata(NativeBackend& b,const std::shared_ptr<Texture>& t){
        const auto baseline=b.readbackMip(t,1);
        for(uint32_t levels:{0u,1u,t->mipLevels+1,0xFFFFFFFFu})changed(t->mipLevels,levels,[&]{rejects([&]{b.readbackMip(t,1);},"metadata does not match");});
        changed(t->writable,true,[&]{rejects([&]{b.writeTexture(t,baseline);},t->format==TextureFormat::RGBA8?"metadata does not match":"support only RGBA8");});
        const auto other=b.createTextureMipChain(t->width,t->height,t->format,views(pattern(t->width,t->height,t->mipLevels)));
        changed(t->view,other->view,[&]{rejects([&]{b.readbackMip(t,1);},"view belongs to another resource");});
        ComPtr<ID3D11Device> device;t->texture->GetDevice(&device);
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};t->view->GetDesc(&v);v.Texture2D.MipLevels=1;
        ComPtr<ID3D11ShaderResourceView> narrow;need(SUCCEEDED(device->CreateShaderResourceView(t->texture.Get(),&v,&narrow)),"Probe view creation failed");
        changed(t->view,narrow,[&]{rejects([&]{b.readbackMip(t,1);},"view does not match");});
        need(b.readbackMip(t,1)==baseline,"Rejected metadata changed mip contents");
    }
    static void bindingPreservation(NativeBackend& b,const std::shared_ptr<Texture>& t){
        ComPtr<ID3D11Device> device;t->texture->GetDevice(&device);ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
        ID3D11ShaderResourceView* view=t->view.Get();context->PSSetShaderResources(7,1,&view);
        const auto copy=b.createTextureMipChain(t->width,t->height,t->format,views(pattern(t->width,t->height,t->mipLevels)));
        b.readbackMip(copy,copy->levelCount()-1);ComPtr<ID3D11ShaderResourceView> after;context->PSGetShaderResources(7,1,&after);
        need(after.Get()==view,"Native mip creation/readback changed an active shader binding");
        view=nullptr;context->PSSetShaderResources(7,1,&view);
    }
};
}
namespace {
void sampleLevels(NativeBackend& b,const std::shared_ptr<Texture>& t,const std::vector<std::vector<uint8_t>>& expected){
    for(uint32_t level=0;level<t->levelCount();++level){
        const auto w=std::max(1u,t->width>>level),h=std::max(1u,t->height>>level);
        auto target=b.createTarget(w,h,TargetFormat::RGBA32Float);ScreenDraw draw{};
        draw.vertices={ScreenVertex{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}};
        draw.color={1,1,1,1};draw.texture=t;draw.blendSelector=3;draw.colorWriteMask=15;
        draw.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        draw.sampler.AddressU=draw.sampler.AddressV=draw.sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        draw.sampler.MaxAnisotropy=1;draw.sampler.ComparisonFunc=D3D11_COMPARISON_ALWAYS;
        draw.sampler.MinLOD=draw.sampler.MaxLOD=float(level);b.drawScreen(target,draw);++sampled;
        const auto bytes=b.readbackTarget(target);std::vector<float> pixels(bytes.size()/4);std::memcpy(pixels.data(),bytes.data(),bytes.size());
        need(pixels.size()==expected[level].size(),"Sampled output size differs");
        for(uint32_t i=0;i<pixels.size();++i){
            const uint32_t channel=i%4,at=i-channel;
            const float value=t->format==TextureFormat::BGRX8 ? (channel==3?1.0f:float(expected[level][at+(channel==0?2:channel==2?0:1)])/255.0f):float(expected[level][i])/255.0f;
            need(std::isfinite(pixels[i]) && std::abs(pixels[i]-value)<0.000002f,"Native mip sampling, channel order or forced alpha differs");
        }
    }
}
void profile(NativeBackend& b,NativeBackend& foreign,uint32_t w,uint32_t h,uint32_t count,TextureFormat format){
    const auto expected=pattern(w,h,count);std::shared_ptr<Texture> texture;
    {auto upload=expected;texture=b.createTextureMipChain(w,h,format,views(upload));for(auto& mip:upload)std::fill(mip.begin(),mip.end(),0xEE);}
    exact(b,texture,expected);NativeMipTextureProbe::backing(texture);sampleLevels(b,texture,expected);
    rejects([&]{b.readbackMip(texture,count);},"out of range");rejects([&]{b.readbackMip(texture,0xFFFFFFFF);},"out of range");
    rejects([&]{b.writeTexture(texture,expected[0]);},"requires writable storage");
    rejects([&]{foreign.readbackMip(texture,0);},"another graphics device");
    if(count>1){NativeMipTextureProbe::invalidMetadata(b,texture);NativeMipTextureProbe::bindingPreservation(b,texture);}
    for(uint32_t i=0;i<count;++i){
        auto invalid=expected;invalid[i].pop_back();rejects([&]{b.createTextureMipChain(w,h,format,views(invalid));},"byte count mismatch");
    }
    const auto before=texture->levelCount();bool rejected=false;std::thread thread([&]{try{b.readbackMip(texture,0);}catch(const Error&){rejected=true;}});thread.join();
    need(rejected && texture->levelCount()==before,"Wrong-thread mip readback accepted or changed ownership");
    exact(b,texture,expected);std::weak_ptr<Texture> weak=texture;auto alias=texture;texture.reset();need(!weak.expired(),"Alias lost mip ownership");alias.reset();need(weak.expired(),"Mip wrapper retained an unexpected owner");
}
void compressed(NativeBackend& b,NativeBackend& foreign,TextureFormat format){
    constexpr std::array<uint16_t,6> colors={0xF800,0x07E0,0x001F,0xFFE0,0x07FF,0xF81F};
    const auto blockBytes=format==TextureFormat::BC1?8u:16u;
    std::vector<std::vector<uint8_t>> blocks,pixels;
    for(uint32_t level=0;level<colors.size();++level){
        const auto w=std::max(1u,32u>>level),h=std::max(1u,16u>>level),color=uint32_t(colors[level]);
        std::vector<uint8_t> block(blockBytes);
        block[blockBytes-8]=uint8_t(color);block[blockBytes-7]=uint8_t(color>>8);
        uint8_t alpha=255;
        if(format==TextureFormat::BC2){alpha=uint8_t((level+1)*17);std::fill_n(block.begin(),8,alpha);}
        if(format==TextureFormat::BC3){alpha=uint8_t(30+level*30);block[0]=alpha;}
        auto& bytes=blocks.emplace_back();
        for(uint32_t i=0;i<((w+3)/4)*((h+3)/4);++i)bytes.insert(bytes.end(),block.begin(),block.end());
        auto& rgba=pixels.emplace_back();
        for(uint32_t i=0;i<w*h;++i)rgba.insert(rgba.end(),{uint8_t(color&0xF800?255:0),uint8_t(color&0x07E0?255:0),uint8_t(color&31?255:0),alpha});
    }
    auto upload=blocks;const auto texture=b.createTextureMipChain(32,16,format,views(upload));
    for(auto& level:upload)std::fill(level.begin(),level.end(),0);
    exact(b,texture,blocks);NativeMipTextureProbe::backing(texture);sampleLevels(b,texture,pixels);
    rejects([&]{foreign.readbackMip(texture,5);},"another graphics device");
    rejects([&]{b.writeTexture(texture,blocks[0]);},"requires writable storage");
    for(size_t level=0;level<blocks.size();++level){auto invalid=blocks;invalid[level].pop_back();
        rejects([&]{b.createTextureMipChain(32,16,format,views(invalid));},"byte count mismatch");}
    const auto partialViews=views(blocks);
    const auto partial=b.createTextureMipChain(32,16,format,std::span(partialViews).first(3));
    need(partial->levelCount()==3 && b.readbackMip(partial,2)==blocks[2],"Partial BC chain changed");
    rejects([&]{b.readbackMip(partial,3);},"out of range");
}
}
int main(int argc,char** argv){
    try{
        const bool hardware=argc==2 && std::string(argv[1])=="--hardware";NativeBackend b(!hardware),foreign(!hardware);
        for(auto format:{TextureFormat::RGBA8,TextureFormat::BGRX8})for(const auto dimensions:std::array<std::array<uint32_t,2>,5>{{{32,32},{16,16},{19,7},{1,16},{1,1}}}){
            const auto w=dimensions[0],h=dimensions[1];profile(b,foreign,w,h,std::bit_width(std::max(w,h)),format);
        }
        profile(b,foreign,32,16,3,TextureFormat::RGBA8);
        rejects([&]{b.createTextureMipChain(32,32,TextureFormat::RGBA8,{});},"level count");
        rejects([&]{b.createTextureMipChain(1,1,TextureFormat::RGBA8,views(pattern(1,1,2)));},"level count");
        for(auto format:{TextureFormat::BC1,TextureFormat::BC2,TextureFormat::BC3}){
            compressed(b,foreign,format);rejects([&]{b.createTextureMipChain(4,4,format,{});},"level count");
            rejects([&]{b.createTextureMipChain(2,4,format,{});},"compressed texture edge layout");
        }
        rejects([&]{b.createTextureMipChain(4,4,static_cast<TextureFormat>(999),{});},"support only RGBA8/BGRX8/BC1/BC2/BC3");
        rejects([&]{b.createTextureMipChain(0,1,TextureFormat::RGBA8,{});},"dimensions exceed");
        rejects([&]{b.readbackMip(nullptr,0);},"Missing native texture");
        std::printf("PASS native explicit mip textures:%zu checks,%zu sampled levels;RGBA8/BGRX8/BC1/BC2/BC3,full/partial chains,NPOT,all raw levels,1x1 BC sampling,source lifetime and rejected states;%s\n",checks,sampled,hardware?"hardware":"WARP");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL native mip textures:%zu checks %s\n",checks,e.what());return 1;}
}
