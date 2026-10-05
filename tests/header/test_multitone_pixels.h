// Included inside the standalone GPU Fixture. Tests the unadapted original PS;
// runtime texture ownership, depth-format conversion and VS linkage are separate.
void multitonePixels() {
    ComPtr<ID3D11VertexShader> probe;ComPtr<ID3D11PixelShader> shader;
    hr(device->CreateVertexShader(kVSRigidMultitonePixelProbe,sizeof(kVSRigidMultitonePixelProbe),nullptr,&probe),"multitone pixel fixture");
    hr(device->CreatePixelShader(kPSRigidMultitone,sizeof(kPSRigidMultitone),nullptr,&shader),"multitone original PS");
    ComPtr<ID3D11Buffer> parameters;
    D3D11_BUFFER_DESC bd{};bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.ByteWidth=6*sizeof(Vec);
    hr(device->CreateBuffer(&bd,nullptr,&parameters),"multitone interpolators");
    ComPtr<ID3D11Texture2D> noiseTexture;ComPtr<ID3D11ShaderResourceView> noiseView;
    D3D11_TEXTURE2D_DESC td{};baseTexture->GetDesc(&td);
    hr(device->CreateTexture2D(&td,nullptr,&noiseTexture),"independent multitone noise texture");
    hr(device->CreateShaderResourceView(noiseTexture.Get(),nullptr,&noiseView),"multitone noise view");
    auto* color=rtv.Get();context->OMSetRenderTargets(1,&color,nullptr);
    context->OMSetDepthStencilState(depth.Get(),0);context->OMSetBlendState(blend.Get(),nullptr,0xFFFFFFFF);
    const D3D11_VIEWPORT vp{0,0,2,2,0,1};context->RSSetViewports(1,&vp);context->RSSetState(raster.Get());
    context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(probe.Get(),nullptr,0);context->GSSetShader(nullptr,nullptr,0);context->PSSetShader(shader.Get(),nullptr,0);
    auto* cb=parameters.Get();context->VSSetConstantBuffers(2,1,&cb);cb=pc.Get();context->PSSetConstantBuffers(0,1,&cb);
    using Inputs=std::array<Vec,6>;
    const float center=512.5f/1024;
    const Vec projection={(center-.5f)*2,(.5f-center)*2,.5f,1};
    const Inputs input={Vec{.25f,.75f,.1875f,.8125f},projection,projection,Vec{0,2,0,0},Vec{.75f,.25f,0,0},Vec{-5,8,.9f,11}};
    PConstants constants{};constants[31][0]=1;constants[36][1]=1;constants[40][0]=37;
    constants[45]={.5f,.25f,.75f,19};constants[46]={2,4,1,-23};constants[47]={1,1,1,1};
    constants[49][3]=1;
    // Different RGB and alpha at every location catch UV, slot and channel swaps.
    const std::array<Vec,4> base={Vec{.125f,.25f,.375f,-9},Vec{.5f,.625f,.75f,5},
        Vec{.875f,.375f,.5f,1},Vec{1.25f,-.25f,1.5f,0}};
    const std::array<Vec,4> noise={Vec{.125f,.25f,1.25f,91},Vec{.5f,.75f,.375f,-5},
        Vec{.875f,-.25f,.75f,13},Vec{1.5f,.5f,-.5f,0}};
    std::array<Grid,2> grids;for(auto& grid:grids)grid.fill(Vec{.25f,.25f,.25f,-19});
    const auto startDraws=psDraws;
    // Channel-space oracle, not a register interpreter. Noise supplies a signed,
    // saturated distance from a per-channel center, scaled around a gain of one.
    // Round the intermediate gain/products to float32 before integer packing.
    const auto expected=[&](const Inputs& p,const PConstants& c,const std::array<Grid,2>& maps,
                            const std::array<Vec,4>& basePixels,const std::array<Vec,4>& noisePixels) {
        const auto texel=[](const std::array<Vec,4>& pixels,float u,float v) {
            const auto x=size_t(std::clamp(std::floor(double(u)*2),0.0,1.0));
            const auto y=size_t(std::clamp(std::floor(double(v)*2),0.0,1.0));
            return pixels[y*2+x];
        };
        Vec sampled=texel(basePixels,p[0][0],p[0][1]);
        if(c[49][3]>0) {
            const auto n=texel(noisePixels,float(p[4][0]*c[47][0]),float(p[4][1]*c[47][1]));
            for(size_t lane=0;lane<3;++lane) {
                const float distance=n[lane]-c[45][lane];
                const float magnitude=float(sat(float(std::abs(distance)*c[46][lane])));
                const float gain=float(1+((distance>0)?magnitude:(distance<0)?-magnitude:0));
                const float scaled=gain*c[49][3];
                sampled[lane]=float(sat(float(sampled[lane]*scaled)));
            }
        }
        // Multitone retains the dual UV/color packing and spatial filter, but
        // owns different gate registers. Test those comparisons at their edges.
        auto controls=c;controls[46][0]=c[47][3]>=.5f?1.f:0.f;
        controls[47][0]=c[47][2]>.5f?1.f:0.f;
        const Probe packed={p[0],p[1],p[2],p[3],p[5]};
        return pixelExpected(packed,controls,maps,&sampled,true);
    };
    const auto draw=[&](const Inputs& p,const PConstants& c,const std::array<Grid,2>& maps,const char* name,
                        const std::array<Vec,4>& basePixels,const std::array<Vec,4>& noisePixels) {
        const bool shadows=c[47][2]>.5f&&c[31][0]>0,noiseEnabled=c[49][3]>0;
        ID3D11ShaderResourceView* resources[]={shadows?views[0].Get():nullptr,shadows?views[1].Get():nullptr,
            baseView.Get(),noiseEnabled?noiseView.Get():nullptr};context->PSSetShaderResources(0,4,resources);
        ID3D11SamplerState* states[]={shadows?sampler.Get():nullptr,shadows?sampler.Get():nullptr,
            sampler.Get(),noiseEnabled?sampler.Get():nullptr};context->PSSetSamplers(0,4,states);
        context->UpdateSubresource(pc.Get(),0,nullptr,c.data(),0,0);
        context->UpdateSubresource(parameters.Get(),0,nullptr,p.data(),0,0);
        context->UpdateSubresource(baseTexture.Get(),0,nullptr,basePixels.data(),2*sizeof(Vec),0);
        context->UpdateSubresource(noiseTexture.Get(),0,nullptr,noisePixels.data(),2*sizeof(Vec),0);
        const D3D11_BOX box{511,511,0,514,514,1};
        for(size_t bank=0;bank<2;++bank)context->UpdateSubresource(textures[bank].Get(),0,&box,maps[bank].data(),3*sizeof(Vec),0);
        const Vec sentinel={-17,-19,-23,-29};context->ClearRenderTargetView(rtv.Get(),sentinel.data());
        context->Draw(3,0);++psDraws;context->CopyResource(staging.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"multitone PS readback");
        std::array<Vec,4> actual{};
        for(size_t y=0;y<2;++y)std::memcpy(actual.data()+2*y,static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch,2*sizeof(Vec));
        context->Unmap(staging.Get(),0);
        const auto want=expected(p,c,maps,basePixels,noisePixels);
        for(size_t i=0;i<4;++i)for(size_t lane=0;lane<4;++lane)nearValue(actual[i][lane],want[lane],name,psDraws-startDraws,lane);
    };
    for(float amount:{-1.f,-0.f,0.f,.25f,1.f,2.f})
        for(float u:{-.25f,.25f,.75f,1.25f})for(float v:{-.25f,.25f,.75f,1.25f})
        for(float scale:{-.5f,.5f,2.f}) {
            auto p=input;p[0][0]=u;p[0][1]=v;p[4]={v,u,0,0};auto c=constants;
            c[49][3]=amount;c[47][0]=scale;c[47][1]=2;
            draw(p,c,grids,"multitone independent base/noise sampling",base,noise);
        }
    for(size_t lane=0;lane<4;++lane)for(float value:{-.5f,0.f,.125f,.5f,.96f,1.5f}) {
        auto b=base;for(auto& pixel:b)pixel[lane]=value;
        auto n=noise;for(auto& pixel:n)pixel[lane]=value;
        draw(input,constants,grids,"multitone base channel isolation",b,noise);
        draw(input,constants,grids,"multitone noise channel isolation",base,n);
    }
    for(float gain:{-2.f,0.f,.5f,4.f})for(float centerValue:{-.25f,.25f,.5f,1.5f}) {
        auto c=constants;c[45]={centerValue,centerValue,centerValue,71};c[46]={gain,gain,gain,-31};
        draw(input,c,grids,"multitone noise center and slope",base,noise);
    }
    for(float edge:{std::nextafter(.5f,0.f),.5f,std::nextafter(.5f,1.f)})
        for(float blue:{std::nextafter(.9f,0.f),.9f,std::nextafter(.9f,1.f)})
        for(float custom:{0.f,-7.f,511.f}) {
            auto p=input;p[0][3]=edge;p[5][2]=blue;auto c=constants;
            c[47][2]=c[47][3]=edge;c[49][2]=custom;c[36][1]=.125f;
            draw(p,c,grids,"multitone packing and gate equality",base,noise);
        }
    for(const Vec normal:{Vec{0,0,0,0},Vec{-0.f,0,-0.f,0},Vec{0,2,0,0},Vec{1,-2,3,0}})
        for(float receiver:{-1.f,0.f,1.f})for(float enabled:{-1.f,.5f,1.f}) {
            auto p=input;p[3]=normal;auto c=constants;c[31][0]=receiver;c[47][2]=enabled;
            draw(p,c,grids,"multitone normals and unbound shadow gates",base,noise);
        }
    for(float shadowGate:{std::nextafter(.5f,0.f),.5f,std::nextafter(.5f,1.f)})
        for(float rimGate:{std::nextafter(.5f,0.f),.5f,std::nextafter(.5f,1.f)})
        for(float light:{std::nextafter(.125f,0.f),.125f,std::nextafter(.125f,1.f)}) {
            auto c=constants;c[47][2]=shadowGate;c[47][3]=rimGate;c[36][1]=light;
            auto maps=grids;for(auto& grid:maps)grid.fill(Vec{.75f,.75f,.75f,-17});
            draw(input,c,maps,"multitone observable shadow/rim threshold neighbors",base,noise);
        }
    for(float u:{-2.125f,0.f,.999f,3.625f})for(float v:{-1.375f,.25f,.75f,2.25f}) {
        auto p=input;p[0][2]=u;p[0][3]=v;
        draw(p,constants,grids,"multitone secondary UV wrap packing",base,noise);
    }
    // At half-texel fractions every selected tap must independently flip final
    // alpha. Nonselected RGB lanes deliberately disagree with the selected lane.
    const int weights[]={1,2,1,2,4,2,1,2,1};
    for(size_t bank=0;bank<2;++bank)for(size_t tap=0;tap<9;++tap) {
        unsigned subset=0;bool found=false;
        for(unsigned mask=0;mask<512&&!found;++mask)if(!(mask&(1u<<tap))) {
            int sum=0;for(size_t i=0;i<9;++i)if(mask&(1u<<i))sum+=weights[i];
            if(sum==8-weights[tap]){subset=mask;found=true;}
        }
        need(found,"Multitone tap isolation subset missing");
        for(unsigned on=0;on<2;++on) {
            auto maps=grids;
            for(size_t i=0;i<9;++i) {
                maps[bank][i]={.9375f,.8125f,.6875f,-11};
                maps[bank][i][i%3]=((subset&(1u<<i))||(i==tap&&on))?.25f:.75f;
            }
            nearValue(float(expected(input,constants,maps,base,noise)[3]),on?.7:1,"multitone independent tap outcome",bank*9+tap,3);
            char name[100];sprintf_s(name,"multitone shadow bank%zu tap%zu channel%zu on%u",bank,tap,tap%3,on);
            draw(input,constants,maps,name,base,noise);
        }
    }
    for(float value:{std::nextafter(.5f,0.f),.5f,std::nextafter(.5f,1.f)}) {
        auto maps=grids;for(auto& grid:maps)grid.fill(Vec{value,value,value,-99});
        draw(input,constants,maps,"multitone depth equality",base,noise);
    }
    for(float fraction:{.125f,.375f,.75f})for(float amount:{0.f,.25f,1.f}) {
        auto p=input;p[1]={((512+fraction)/1024-.5f)*4,(.5f-(512+.75f)/1024)*4,1,2};
        auto maps=grids;for(size_t bank=0;bank<2;++bank)for(size_t i=0;i<9;++i)
            maps[bank][i]={float((i+bank)%3)*.375f,float((i+2*bank+1)%3)*.375f,float((2*i+bank)%3)*.375f,23};
        auto c=constants;c[30][1]=amount;
        draw(p,c,maps,"multitone distinct projections and fractional filter",base,noise);
    }
    std::printf("PASS multitone PS: %zu draws; independent base/noise coordinates, noise arithmetic, packing, gates, equality and 18 isolated RGB shadow taps\n",psDraws-startDraws);
#include "test_multitone_linkage.h"
}

