#include "renderer/native_backend.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>

using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void need(bool condition,const char* message) {++checks;if(!condition) throw std::runtime_error(message);}
template<class F> void rejects(F&& operation,const char* cause) {
    try {operation();} catch(const Error& error) {
        if(std::string(error.what()).find(cause)==std::string::npos) {
            std::fprintf(stderr,"Expected rejection containing '%s', got '%s'\n",cause,error.what());
            need(false,"Cube rejection cause differs");
        }
        ++checks;return;
    }
    need(false,"Cube operation unexpectedly accepted");
}
using Faces=std::array<std::vector<uint8_t>,6>;
std::vector<uint8_t> pattern(uint32_t size,uint32_t face,uint32_t version) {
    std::vector<uint8_t> result(size_t(size)*size*4);
    for(uint32_t y=0;y<size;++y) for(uint32_t x=0;x<size;++x) {
        const uint32_t r=(x+31*y+167*face+251*version)&1023;
        const uint32_t g=(23*x+3*y+53*face+79*version)&1023;
        const uint32_t b=(7*x+59*y+83*face+41*version)&1023;
        const uint32_t a=(x+2*y+face+version)&3;
        const uint32_t packed=r|(g<<10)|(b<<20)|(a<<30);
        std::memcpy(result.data()+(size_t(y)*size+x)*4,&packed,4);
    }
    return result;
}
void pixels(NativeBackend& backend,const std::shared_ptr<CubeTexture>& cube,const Faces& expected) {
    for(uint32_t face=0;face<6;++face)
        need(backend.readbackCubeFace(cube,face)==expected[face],"Native cube face bytes differ");
}
Faces seed(NativeBackend& backend,const std::shared_ptr<CubeTexture>& cube,uint32_t version) {
    Faces result;
    for(uint32_t face=0;face<6;++face) {
        result[face]=pattern(cube->faceSize(),face,version);
        auto upload=result[face];
        backend.writeCubeRows(cube,face,0,cube->faceSize(),upload);
        std::fill(upload.begin(),upload.end(),uint8_t(0xE7));
    } // Client buffers are gone before readback waits for GPU completion.
    pixels(backend,cube,result);return result;
}
template<class T,class F> void changed(T& field,const T& value,F&& operation) {
    const T saved=field;field=value;
    try {operation();} catch(...) {field=saved;throw;}
    field=saved;
}
}
namespace Simpsons::Graphics {
struct NativeCubeTextureProbe {
    static void backing(const std::shared_ptr<CubeTexture>& cube) {
        D3D11_TEXTURE2D_DESC desc{};cube->texture->GetDesc(&desc);
        need(desc.Width==cube->faceSize() && desc.Height==cube->faceSize() &&
             desc.ArraySize==6 && desc.MipLevels==1,"Cube backing dimensions differ");
        need(desc.Format==DXGI_FORMAT_R10G10B10A2_UNORM && desc.SampleDesc.Count==1 &&
             !desc.SampleDesc.Quality && desc.Usage==D3D11_USAGE_DEFAULT && !desc.CPUAccessFlags &&
             desc.BindFlags==(D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET) &&
             desc.MiscFlags==D3D11_RESOURCE_MISC_TEXTURECUBE,"Cube backing profile differs");
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};cube->view->GetDesc(&view);
        need(view.Format==desc.Format && view.ViewDimension==D3D11_SRV_DIMENSION_TEXTURECUBE &&
             !view.TextureCube.MostDetailedMip && view.TextureCube.MipLevels==1,"Cube sampled view differs");
        ComPtr<ID3D11Resource> actual,expected;cube->view->GetResource(&actual);
        need(SUCCEEDED(cube->texture.As(&expected)) && actual.Get()==expected.Get(),"Cube sampled view aliases another resource");
    }
    static void corruptions(NativeBackend& backend,NativeBackend& foreign,
                            const std::shared_ptr<CubeTexture>& cube,const Faces& expected) {
        const auto n=cube->faceSize();
        auto other=backend.createCubeTexture(n),alien=foreign.createCubeTexture(n);
        const auto otherPixels=seed(backend,other,9),alienPixels=seed(foreign,alien,10);
        need(cube->texture.Get()!=other->texture.Get() && cube->view.Get()!=other->view.Get(),"Distinct cube allocations alias");
        auto rejectAll=[&](const char* cause) {
            rejects([&]{backend.validateCubeTexture(cube);},cause);
            rejects([&]{backend.writeCubeRows(cube,0,0,n,otherPixels[0]);},cause);
            rejects([&]{backend.readbackCubeFace(cube,0);},cause);
        };
        for(const uint32_t extent:{0u,8u,17u,n==16?256u:16u,0xFFFFFFFFu}) {
            changed(cube->extent,extent,[&]{rejectAll("metadata does not match");});pixels(backend,cube,expected);
        }
        changed(cube->texture,ComPtr<ID3D11Texture2D>{},[&]{rejectAll("Missing native cube");});
        changed(cube->view,ComPtr<ID3D11ShaderResourceView>{},[&]{rejectAll("Missing native cube");});
        changed(cube->view,other->view,[&]{rejectAll("sampled view does not match");});
        changed(cube->view,alien->view,[&]{rejectAll("sampled view does not match");});
        changed(cube->texture,other->texture,[&]{rejectAll("sampled view does not match");});
        changed(cube->texture,alien->texture,[&]{rejectAll("another graphics device");});
        // A valid same-resource array SRV must not pass as a cube SRV.
        ComPtr<ID3D11Device> device;cube->texture->GetDevice(&device);
        D3D11_SHADER_RESOURCE_VIEW_DESC array{};array.Format=DXGI_FORMAT_R10G10B10A2_UNORM;
        array.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        array.Texture2DArray.MipLevels=1;array.Texture2DArray.ArraySize=6;
        ComPtr<ID3D11ShaderResourceView> arrayView;
        need(SUCCEEDED(device->CreateShaderResourceView(cube->texture.Get(),&array,&arrayView)),"Array SRV fixture creation failed");
        changed(cube->view,arrayView,[&]{rejectAll("sampled view does not match");});
        pixels(backend,cube,expected);pixels(backend,other,otherPixels);pixels(foreign,alien,alienPixels);
    }
};
}
namespace {
void profile(NativeBackend& backend,NativeBackend& foreign,uint32_t n) {
    auto cube=backend.createCubeTexture(n);backend.validateCubeTexture(cube);
    NativeCubeTextureProbe::backing(cube);
    // No initial pixel read or assumption: initialize all faces with test data.
    auto expected=seed(backend,cube,0);
    constexpr std::array<uint32_t,6> originalOrder{0,1,4,5,2,3};
    const uint32_t initializedRows=n==16?8:n;
    for(const uint32_t face:originalOrder) {
        std::vector<uint8_t> zero(size_t(n)*4*initializedRows,0);
        backend.writeCubeRows(cube,face,0,initializedRows,zero);
        std::fill_n(expected[face].begin(),zero.size(),uint8_t(0));
        std::fill(zero.begin(),zero.end(),uint8_t(0xA3));
        pixels(backend,cube,expected); // Includes untouched rows and other faces.
    }
    for(const uint32_t face:originalOrder) {
        const auto source=pattern(n,face,3);
        for(const auto range:std::array<std::array<uint32_t,2>,3>{{{n-1,1},{3,5},{0,1}}}) {
            const size_t offset=size_t(range[0])*n*4,count=size_t(range[1])*n*4;
            {
                std::vector<uint8_t> upload(source.begin()+offset,source.begin()+offset+count);
                backend.writeCubeRows(cube,face,range[0],range[1],upload);
                std::copy(upload.begin(),upload.end(),expected[face].begin()+offset);
                std::fill(upload.begin(),upload.end(),uint8_t(0xCD));
            }
            pixels(backend,cube,expected);
        }
    }
    const auto replacement=pattern(n,0,6);
    for(const uint32_t face:{6u,0xFFFFFFFFu}) {
        rejects([&]{backend.writeCubeRows(cube,face,0,n,replacement);},"face is outside");
        rejects([&]{backend.readbackCubeFace(cube,face);},"face is outside");
    }
    for(const auto range:std::array<std::array<uint32_t,2>,7>{{{0,0},{n,1},{n-1,2},{0,n+1},
                                                          {0,0xFFFFFFFFu},{0xFFFFFFFFu,1},{n,0}}})
        rejects([&]{backend.writeCubeRows(cube,0,range[0],range[1],replacement);},"row range is outside");
    for(const size_t count:{size_t(0),replacement.size()-1,replacement.size()+1}) {
        const std::vector<uint8_t> invalid(count,0x73);
        rejects([&]{backend.writeCubeRows(cube,0,0,n,invalid);},"byte count mismatch");
    }
    rejects([&]{foreign.validateCubeTexture(cube);},"another graphics device");
    rejects([&]{foreign.writeCubeRows(cube,0,0,n,replacement);},"another graphics device");
    rejects([&]{foreign.readbackCubeFace(cube,0);},"another graphics device");
    pixels(backend,cube,expected);
    NativeCubeTextureProbe::corruptions(backend,foreign,cube,expected);
    const std::array<std::function<void()>,4> calls={
        [&]{backend.createCubeTexture(n);},[&]{backend.validateCubeTexture(cube);},
        [&]{backend.writeCubeRows(cube,0,0,n,replacement);},[&]{backend.readbackCubeFace(cube,0);}};
    for(const auto& call:calls) {
        std::exception_ptr failure;
        std::thread other([&]{try {rejects(call,"different thread");} catch(...) {failure=std::current_exception();}});
        other.join();if(failure) std::rethrow_exception(failure);
    }
    pixels(backend,cube,expected);
    std::weak_ptr<CubeTexture> weak=cube;auto alias=cube;cube.reset();
    need(!weak.expired(),"Cube shared ownership lost");pixels(backend,alias,expected);
    alias.reset();need(weak.expired(),"Cube wrapper retained an unexpected owner");
}
void lifetime(bool hardware) {
    std::shared_ptr<CubeTexture> survivor;
    {
        NativeBackend backend(!hardware);survivor=backend.createCubeTexture(16);seed(backend,survivor,2);
    }
    NativeCubeTextureProbe::backing(survivor);
    NativeBackend replacement(!hardware);
    rejects([&]{replacement.validateCubeTexture(survivor);},"another graphics device");
    rejects([&]{replacement.writeCubeRows(survivor,0,0,16,pattern(16,0,4));},"another graphics device");
    rejects([&]{replacement.readbackCubeFace(survivor,0);},"another graphics device");
    std::weak_ptr<CubeTexture> weak=survivor;survivor.reset();need(weak.expired(),"Cube leaked beyond backend lifetime");
}
}
int main(int argc,char** argv) {
    try {
        const bool hardware=argc==2 && std::string(argv[1])=="--hardware";
        need(argc==1 || hardware,"Use no arguments for WARP or --hardware for hardware");
        NativeBackend backend(!hardware),foreign(!hardware);
        for(const uint32_t n:{0u,1u,8u,15u,17u,128u,255u,257u,16384u,0xFFFFFFFFu})
            rejects([&]{backend.createCubeTexture(n);},"original 16/256 profiles");
        for(const auto& cube:{std::shared_ptr<CubeTexture>{},std::make_shared<CubeTexture>()}) {
            rejects([&]{backend.validateCubeTexture(cube);},"Missing native cube");
            rejects([&]{backend.writeCubeRows(cube,0,0,16,{});},"Missing native cube");
            rejects([&]{backend.readbackCubeFace(cube,0);},"Missing native cube");
        }
        for(const uint32_t n:{16u,256u}) profile(backend,foreign,n);
        lifetime(hardware);
        need(!backend.screenDrawCount() && !backend.presentationCount(),"Cube tests submitted a draw or presentation");
        std::printf("PASS native RGB10A2 cube: %zu checks on %s; six owned faces, exact full/partial row uploads, untouched regions, rejection preservation; no sampling/original pixel claim\n",
                    checks,hardware?"hardware":"WARP");
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL native cube after %zu checks: %s\n",checks,error.what());return 1;
    }
}
