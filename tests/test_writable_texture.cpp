#include "renderer/native_backend.h"
#include "renderer/im2d_draw.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <functional>
#include <string>
#include <thread>
#include <utility>

namespace {
using namespace Simpsons::Graphics;
size_t checks{};
void need(bool value,const char* message) {++checks;if(!value) throw Error(message);}
void rejectionCause(const char* actual,const char* reason) {
    const bool matches=std::string(actual).find(reason)!=std::string::npos;
    if(!matches)
        std::fprintf(stderr,"REJECTION check=%zu expected=\"%s\" actual=\"%s\"\n",checks+1,reason,actual);
    need(matches,"Writable texture rejection had an unrelated cause");
}
template<class F> void rejects(F&& action,const char* reason) {
    try {action();} catch(const Error& error) {
        rejectionCause(error.what(),reason);return;
    }
    std::fprintf(stderr,"REJECTION check=%zu expected=\"%s\" actual=<operation accepted>\n",checks+1,reason);
    need(false,"Invalid writable texture operation was accepted");
}
std::vector<uint8_t> pattern(uint32_t width,uint32_t height,uint32_t version,TextureFormat format=TextureFormat::RGBA8) {
    if(format==TextureFormat::R8) {
        std::vector<uint8_t> result(size_t(width)*height);
        for(uint32_t y=0;y<height;++y) for(uint32_t x=0;x<width;++x)
            // Different axes, row/column high bits and a cross term distinguish
            // flips, pitch errors and repeated 256-pixel tiles in movie planes.
            result[size_t(y)*width+x]=uint8_t(x*29+y*61+(x>>8)*17+(y>>8)*43+x*y*7+version*53+3);
        return result;
    }
    std::vector<uint8_t> result(size_t(width)*height*4);
    for(uint32_t y=0;y<height;++y) for(uint32_t x=0;x<width;++x) {
        const size_t at=(size_t(y)*width+x)*4;
        result[at]=uint8_t(x*29+y*17+version*53+3);
        result[at+1]=uint8_t(x*7+y*61+version*31+19);
        result[at+2]=uint8_t(x*43+y*11+version*79+71);
        result[at+3]=uint8_t(x*13+y*47+version*23+101);
    }
    return result;
}
void pixels(NativeBackend& backend,const std::shared_ptr<Texture>& texture,const std::vector<uint8_t>& expected) {
    need(backend.readback(texture)==expected,"Exact native texture byte readback differs");
}
}

namespace Simpsons::Graphics {
// Read actual D3D descriptions before the first write, without reading initial
// pixels. Deliberate metadata faults verify rejection before a GPU update.
struct NativeWritableTextureProbe {
    static void noPipelines(const NativeBackend& backend) {
        need(!backend.screenPipeline && !backend.im2dPipeline,"R8 storage entered an unrelated draw pipeline");
        need(!backend.screenDrawCount() && !backend.im2dDrawCount(),"R8 storage submitted an unrelated draw");
    }
    static void backing(const std::shared_ptr<Texture>& texture,bool writable,TextureFormat format=TextureFormat::RGBA8) {
        need(texture && texture->texture && texture->view,"Create did not allocate an actual texture and view");
        need(texture->writable==writable && texture->format==format && texture->levelCount()==1,
            "Texture format, level count or mutability differs");
        const auto nativeFormat=format==TextureFormat::R8?DXGI_FORMAT_R8_UNORM:DXGI_FORMAT_R8G8B8A8_UNORM;
        const uint32_t bytesPerPixel=format==TextureFormat::R8?1u:4u;
        D3D11_TEXTURE2D_DESC actual{};texture->texture->GetDesc(&actual);
        need(actual.Width==texture->width && actual.Height==texture->height &&
            actual.Format==nativeFormat && actual.MipLevels==1 && actual.ArraySize==1 &&
            actual.SampleDesc.Count==1 && !actual.SampleDesc.Quality &&
            actual.Usage==(writable?D3D11_USAGE_DEFAULT:D3D11_USAGE_IMMUTABLE) &&
            actual.BindFlags==D3D11_BIND_SHADER_RESOURCE && !actual.CPUAccessFlags && !actual.MiscFlags,
            "Actual native texture resource description differs");
        need(texture->rowBytes==texture->width*bytesPerPixel && texture->rows==texture->height,"Tight native layout differs");
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};texture->view->GetDesc(&view);
        ComPtr<ID3D11Resource> resource;texture->view->GetResource(&resource);
        need(resource.Get()==texture->texture.Get() && view.Format==actual.Format &&
            view.ViewDimension==D3D11_SRV_DIMENSION_TEXTURE2D && !view.Texture2D.MostDetailedMip && view.Texture2D.MipLevels==1,
            "Actual view description/resource association differs");
        ComPtr<ID3D11Device> device;texture->texture->GetDevice(&device);
        need(device && SUCCEEDED(device->GetDeviceRemovedReason()),"Texture did not retain a real live device");
    }
    static bool distinct(const std::shared_ptr<Texture>& a,const std::shared_ptr<Texture>& b) {
        return a->texture.Get()!=b->texture.Get() && a->view.Get()!=b->view.Get();
    }
    template<class T,class F> static void changed(T& field,T value,F&& action) {
        const T old=field;field=std::move(value);
        try {action();} catch(...) {field=old;throw;}
        field=old;
    }
    static void metadataRejections(NativeBackend& backend,NativeBackend& foreign,
        const std::shared_ptr<Texture>& texture,const std::vector<uint8_t>& expected) {
        const auto format=texture->format;
        const auto replacement=pattern(texture->width,texture->height,9,format);
        auto reject=[&](const char* reason) {
            rejects([&]{backend.validateTextureStorage(texture);},reason);
            rejects([&]{backend.validateTexture(texture);},reason);
            rejects([&]{backend.writeTexture(texture,replacement);},reason);
            rejects([&]{backend.readback(texture);},reason);
        };
        auto rejectBacking=[&] {reject("Native texture metadata does not match its backing");};
        auto rejectView=[&] {reject("Native texture view belongs to another resource");};
        changed(texture->width,texture->width+1,rejectBacking);pixels(backend,texture,expected);
        changed(texture->height,texture->height+1,rejectBacking);pixels(backend,texture,expected);
        changed(texture->format,format==TextureFormat::R8?TextureFormat::RGBA8:TextureFormat::R8,rejectBacking);
        pixels(backend,texture,expected);
        changed(texture->format,TextureFormat::BC1,[&] {
            // The 19x7 profile fails layout's block-edge check before the
            // writable-format/backing checks. Keep that first cause explicit.
            reject("Unverified compressed texture edge layout");
        });pixels(backend,texture,expected);
        changed(texture->rowBytes,texture->rowBytes+4,rejectBacking);pixels(backend,texture,expected);
        changed(texture->rows,texture->rows+1,rejectBacking);pixels(backend,texture,expected);
        for(const uint32_t levels:{0u,2u}) {
            changed(texture->mipLevels,levels,rejectBacking);pixels(backend,texture,expected);
        }
        // DEFAULT storage cannot masquerade as IMMUTABLE by changing the flag.
        changed(texture->writable,false,rejectBacking);pixels(backend,texture,expected);
        changed(texture->view,ComPtr<ID3D11ShaderResourceView>{},[&] {
            reject("Missing native texture resource/view");
        });pixels(backend,texture,expected);
        const auto other=backend.createWritableTexture(texture->width,texture->height,format);
        backend.writeTexture(other,replacement);
        changed(texture->view,other->view,rejectView);pixels(backend,texture,expected);pixels(backend,other,replacement);
        const auto foreignTexture=foreign.createWritableTexture(texture->width,texture->height,format);
        foreign.writeTexture(foreignTexture,replacement);
        changed(texture->view,foreignTexture->view,rejectView);pixels(backend,texture,expected);pixels(foreign,foreignTexture,replacement);
        changed(texture->texture,foreignTexture->texture,[&] {
            reject("Native resource belongs to another graphics device");
        });pixels(backend,texture,expected);pixels(foreign,foreignTexture,replacement);
        // A forged writable flag on actual immutable storage must fail validation
        // before the update. It is not enough to trust the host flag alone.
        const auto immutable=backend.createTexture(texture->width,texture->height,format,expected);
        changed(immutable->writable,true,[&] {
            rejects([&]{backend.validateTextureStorage(immutable);},"does not match its backing");
            rejects([&]{backend.validateTexture(immutable);},"does not match its backing");
            rejects([&]{backend.writeTexture(immutable,replacement);},"does not match its backing");
            rejects([&]{backend.readback(immutable);},"does not match its backing");
        });
        backing(immutable,false,format);pixels(backend,immutable,expected);
    }
};
}

namespace {
void invalidCreates(NativeBackend& backend) {
    for(auto format:{TextureFormat::BC1,TextureFormat::BC2,TextureFormat::BC3,TextureFormat::BGRX8,static_cast<TextureFormat>(0xFFFFFFFFu)})
        rejects([&]{backend.createWritableTexture(4,4,format);},"support only RGBA8/R8");
    for(auto format:{TextureFormat::RGBA8,TextureFormat::R8})
        for(const auto dimensions:std::array<std::array<uint32_t,2>,6>{{{0,1},{1,0},{16385,1},{1,16385},{0xFFFFFFFFu,1},{1,0xFFFFFFFFu}}})
            rejects([&]{backend.createWritableTexture(dimensions[0],dimensions[1],format);},"dimensions exceed D3D11 bounds");
    rejects([&]{backend.writeTexture(nullptr,{});},"Missing native texture");
    rejects([&]{backend.writeTexture(std::make_shared<Texture>(),{});},"Missing native texture");
}
void writableProfile(NativeBackend& backend,NativeBackend& foreign,uint32_t width,uint32_t height,
    TextureFormat format=TextureFormat::RGBA8) {
    auto texture=backend.createWritableTexture(width,height,format);
    need(texture->width==width && texture->height==height,"Writable allocation dimensions differ");
    backend.validateTextureStorage(texture);NativeWritableTextureProbe::backing(texture,true,format);
    if(format!=TextureFormat::R8) backend.validateTexture(texture);
    // Before the first write only resource metadata is inspected. No pixel value
    // or implicit clear is assumed for CreateTexture2D(..., nullptr, ...).
    std::weak_ptr<Texture> weak=texture;
    auto alias=texture;texture.reset();need(!weak.expired(),"Shared texture ownership was lost");
    std::vector<uint8_t> expected;
    for(uint32_t version=0;version<3;++version) {
        {
            auto upload=pattern(width,height,version,format);expected=upload;
            backend.writeTexture(alias,upload);
            std::fill(upload.begin(),upload.end(),uint8_t(0xED));
        } // Release client storage BEFORE staging/map waits for the GPU.
        pixels(backend,alias,expected);NativeWritableTextureProbe::backing(alias,true,format);
    }
    const auto replacement=pattern(width,height,7,format);
    const auto other=backend.createWritableTexture(width,height,format);
    backend.writeTexture(other,replacement);
    need(NativeWritableTextureProbe::distinct(alias,other),"Writable textures alias native resources/views");
    pixels(backend,alias,expected);pixels(backend,other,replacement);
    std::vector<size_t> invalidSizes{size_t(0),expected.size()-1,expected.size()+1};
    if(format==TextureFormat::R8) {
        // Reject both RGBA-sized input and rows padded by one byte.
        invalidSizes.push_back(expected.size()*4);invalidSizes.push_back(expected.size()+height);
    }
    for(const size_t n:invalidSizes) {
        const std::vector<uint8_t> invalid(n,0xBC);
        rejects([&]{backend.writeTexture(alias,invalid);},"byte count mismatch");
        if(format==TextureFormat::R8)
            rejects([&]{backend.createTexture(width,height,format,invalid);},"byte count mismatch");
        pixels(backend,alias,expected);pixels(backend,other,replacement);
    }
    rejects([&]{foreign.writeTexture(alias,replacement);},"another graphics device");
    rejects([&]{foreign.validateTexture(alias);},"another graphics device");
    rejects([&]{foreign.validateTextureStorage(alias);},"another graphics device");
    rejects([&]{foreign.readback(alias);},"another graphics device");
    pixels(backend,alias,expected);pixels(backend,other,replacement);
    auto immutable=backend.createTexture(width,height,format,expected);
    backend.validateTextureStorage(immutable);NativeWritableTextureProbe::backing(immutable,false,format);
    if(format!=TextureFormat::R8) backend.validateTexture(immutable);
    rejects([&]{backend.writeTexture(immutable,replacement);},"requires writable storage");
    pixels(backend,immutable,expected);pixels(backend,alias,expected);
    if(format==TextureFormat::R8) {
        for(const uint32_t level:{1u,0xFFFFFFFFu})
            rejects([&]{backend.readbackMip(alias,level);},"mip level is out of range");
        const std::span<const uint8_t> base=expected;
        const std::array single{base};
        const auto smaller=pattern(width/2,height/2,1,format);
        const std::array<std::span<const uint8_t>,2> chain{base,smaller};
        rejects([&]{backend.createTextureMipChain(width,height,format,single);},"support only RGBA8/BGRX8");
        rejects([&]{backend.createTextureMipChain(width,height,format,chain);},"support only RGBA8/BGRX8");
        pixels(backend,alias,expected);
    }
    if(width==19 && height==7) NativeWritableTextureProbe::metadataRejections(backend,foreign,alias,expected);
    alias.reset();need(weak.expired(),"Texture wrapper retained an unexpected owner");
}
void r8MoviePlanes(NativeBackend& backend) {
    // Exact native storage transfers only; no movie decoding or sampling claim.
    for(const auto dimensions:std::array<std::array<uint32_t,2>,4>{{{1280,720},{640,360},{640,480},{320,240}}}) {
        const auto width=dimensions[0],height=dimensions[1];
        const auto texture=backend.createWritableTexture(width,height,TextureFormat::R8);
        need(texture->width==width && texture->height==height,"R8 movie-plane dimensions differ");
        backend.validateTextureStorage(texture);NativeWritableTextureProbe::backing(texture,true,TextureFormat::R8);
        for(uint32_t version=0;version<2;++version) {
            std::vector<uint8_t> expected;
            {
                auto upload=pattern(width,height,version,TextureFormat::R8);expected=upload;
                backend.writeTexture(texture,upload);
                std::fill(upload.begin(),upload.end(),uint8_t(0xED));
            } // Destroy caller bytes before readback waits for GPU completion.
            pixels(backend,texture,expected);
        }
    }
    // Endpoint bytes and a minimal legal extent are independent of the pattern.
    const auto scalar=backend.createWritableTexture(1,1,TextureFormat::R8);
    NativeWritableTextureProbe::backing(scalar,true,TextureFormat::R8);
    for(const uint8_t value:std::array<uint8_t,5>{0,1,127,128,255}) {
        const std::vector<uint8_t> bytes{value};backend.writeTexture(scalar,bytes);pixels(backend,scalar,bytes);
    }
}
void r8PipelineRejections(NativeBackend& backend) {
    NativeWritableTextureProbe::noPipelines(backend);
    const auto target=backend.createTarget(4,4,TargetFormat::RGB10A2);
    const auto depth=backend.createDepthTarget(4,4);
    backend.bindTargets({target,nullptr,nullptr,nullptr},depth);
    backend.setViewport({0,0,4,4,0,1});
    backend.clearTarget(target,{0.25f,0.5f,0.75f,1});
    const auto targetBefore=backend.readbackTarget(target);
    ScreenDraw screen{};
    screen.vertices={ScreenVertex{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}};
    screen.color={1,1,1,1};screen.blendSelector=3;screen.colorWriteMask=15;
    screen.sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    screen.sampler.AddressU=screen.sampler.AddressV=screen.sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
    screen.sampler.MaxAnisotropy=1;screen.sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
    Im2DDraw im2d{};im2d.primitiveType=4;im2d.rasterWidth=4;im2d.rasterHeight=4;
    im2d.blendWord=0x00010001;im2d.colorWriteMask=15;im2d.pixelCenterHalf=true;im2d.sampler=screen.sampler;
    im2d.vertices={{{0.5f,0.5f,0,1},{1,1,1,1},{0,0}},{{4.5f,0.5f,0,1},{1,1,1,1},{1,0}},
                   {{0.5f,4.5f,0,1},{1,1,1,1},{0,1}},{{4.5f,4.5f,0,1},{1,1,1,1},{1,1}}};
    const auto expected=pattern(19,7,3,TextureFormat::R8);
    for(const bool writable:{false,true}) {
        const auto texture=writable?backend.createWritableTexture(19,7,TextureFormat::R8):
            backend.createTexture(19,7,TextureFormat::R8,expected);
        if(writable) backend.writeTexture(texture,expected);
        backend.validateTextureStorage(texture);screen.texture=texture;im2d.texture=texture;
        constexpr auto reason="Native R8 storage is unsupported by existing pipelines";
        rejects([&]{backend.validateTexture(texture);},reason);
        rejects([&]{backend.drawScreen(target,screen);},reason);
        rejects([&]{backend.drawOriginalScreen(target,depth,screen,true,6);},reason);
        rejects([&]{backend.drawIm2D(target,depth,im2d);},reason);
        NativeWritableTextureProbe::noPipelines(backend);
        backend.requireSelectedTargets({target,nullptr,nullptr,nullptr},depth);
        pixels(backend,texture,expected);
    }
    need(backend.readbackTarget(target)==targetBefore,"Rejected R8 draws changed target bytes");
    backend.clearBindings();
}
void paddedGuestRows(NativeBackend& backend) {
    // Runtime unlock must gather the original 256-byte-pitch lock rows into
    // 128-byte native rows. This models that boundary, not original lock code.
    constexpr uint32_t width=32,height=32,guestPitch=256,tightPitch=width*4;
    const auto expected=pattern(width,height,3);
    std::vector<uint8_t> staging(guestPitch*height,0xD7),tight(tightPitch*height);
    for(uint32_t row=0;row<height;++row)
        std::copy_n(expected.begin()+size_t(row)*tightPitch,tightPitch,staging.begin()+size_t(row)*guestPitch);
    const auto texture=backend.createWritableTexture(width,height,TextureFormat::RGBA8);
    backend.writeTexture(texture,expected);
    rejects([&]{backend.writeTexture(texture,staging);},"byte count mismatch");pixels(backend,texture,expected);
    for(uint32_t row=0;row<height;++row)
        std::copy_n(staging.begin()+size_t(row)*guestPitch,tightPitch,tight.begin()+size_t(row)*tightPitch);
    need(staging.size()==8192 && tight.size()==4096,"Original/native pitch distinction changed");
    backend.writeTexture(texture,tight);
    std::fill(staging.begin(),staging.end(),0);std::fill(tight.begin(),tight.end(),0);
    pixels(backend,texture,expected);
}
void immutableCompressed(NativeBackend& backend) {
    for(auto format:{TextureFormat::BC1,TextureFormat::BC2,TextureFormat::BC3}) {
        std::vector<uint8_t> blocks(3*2*(format==TextureFormat::BC1?8:16));
        for(size_t i=0;i<blocks.size();++i) blocks[i]=uint8_t(i*41+3);
        const auto texture=backend.createTexture(12,8,format,blocks);
        backend.validateTexture(texture);pixels(backend,texture,blocks);
        const std::vector<uint8_t> replacement(blocks.size(),0x72);
        rejects([&]{backend.writeTexture(texture,replacement);},"requires writable storage");
        pixels(backend,texture,blocks);
    }
}
void wrongThread(NativeBackend& backend) {
    const auto expected=pattern(19,7,4),replacement=pattern(19,7,5);
    const auto texture=backend.createWritableTexture(19,7,TextureFormat::RGBA8);backend.writeTexture(texture,expected);
    const std::array<std::function<void()>,5> calls={
        [&]{backend.createWritableTexture(19,7,TextureFormat::RGBA8);},
        [&]{backend.writeTexture(texture,replacement);},
        [&]{backend.validateTexture(texture);},[&]{backend.validateTextureStorage(texture);},[&]{backend.readback(texture);}};
    for(const auto& call:calls) {
        bool rejected=false;std::string actualError;std::exception_ptr failure;
        std::thread thread([&] {
            try {call();} catch(const Error& error) {
                actualError=error.what();rejected=true;
            } catch(...) {failure=std::current_exception();}
        });
        thread.join();if(failure) std::rethrow_exception(failure);
        if(!rejected) {
            std::fprintf(stderr,"REJECTION check=%zu expected=\"different thread\" actual=<operation accepted>\n",checks+1);
            need(false,"Writable texture API accepted a different owner thread");
        }
        rejectionCause(actualError.c_str(),"different thread");pixels(backend,texture,expected);
    }
}
void backendLifetime(bool hardware) {
    std::shared_ptr<Texture> survivor;std::weak_ptr<Texture> weak;
    {
        NativeBackend owner(!hardware);
        survivor=owner.createWritableTexture(7,3,TextureFormat::RGBA8);
        owner.writeTexture(survivor,pattern(7,3,1));weak=survivor;
    }
    // COM resource ownership survives its backend wrapper. No dangling backend
    // pointer is consulted, and another backend cannot adopt the old resource.
    NativeWritableTextureProbe::backing(survivor,true);
    NativeBackend replacement(!hardware);
    rejects([&]{replacement.writeTexture(survivor,pattern(7,3,2));},"another graphics device");
    rejects([&]{replacement.readback(survivor);},"another graphics device");
    survivor.reset();need(weak.expired(),"Texture wrapper leaked beyond the last shared owner");
}
}

int main(int argc,char** argv) {
    try {
        const bool hardware=argc==2 && std::string(argv[1])=="--hardware";
        need(argc==1 || hardware,"Use no arguments for WARP or --hardware for the hardware device");
        NativeBackend backend(!hardware),foreign(!hardware);
        invalidCreates(backend);
        for(const auto dimensions:std::array<std::array<uint32_t,2>,4>{{{1,1},{19,7},{32,32},{257,3}}})
            writableProfile(backend,foreign,dimensions[0],dimensions[1]);
        // Run ownership/corruption cases once at an odd R8 pitch, not at every
        // movie extent. All original RGBA and BC profiles remain in this suite.
        writableProfile(backend,foreign,19,7,TextureFormat::R8);r8MoviePlanes(backend);r8PipelineRejections(backend);
        paddedGuestRows(backend);immutableCompressed(backend);wrongThread(backend);backendLifetime(hardware);
        need(!backend.screenDrawCount() && !backend.im2dDrawCount() && !backend.presentationCount(),"Writable texture tests submitted a draw or presentation");
        std::printf("PASS writable RGBA8/R8 textures: %zu checks on %s; eager real allocation, full updates, exact bytes, rejection preservation, ownership; no initial pixel/draw claim\n",
            checks,hardware?"hardware":"WARP");
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL writable texture after %zu checks: %s\n",checks,error.what());return 1;}
}
