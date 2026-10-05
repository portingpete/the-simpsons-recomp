// Included inside multitonePixels after the isolated PS cases. Reuse its
// qualified channel oracle/resources, but replace the probe with the real VS.
{
    struct MeshInput {Vertex base;std::array<float,2> uv1;};
    const std::array<MeshInput,3> mesh={{
        {{{-1,1,.5f},{0,2,0},{0,0,.7f,1},{.13f,.19f}},{.173f,.217f}},
        {{{3,1,.5f},{1,3,0},{0,0,1.3f,1},{1.37f,.31f}},{.731f,.349f}},
        {{{-1,-3,.5f},{0,3,1},{0,0,.8f,1},{.29f,1.43f}},{.397f,1.113f}}
    }};
    ComPtr<ID3D11VertexShader> linkedVS;ComPtr<ID3D11InputLayout> meshLayout;
    ComPtr<ID3D11Buffer> meshBuffer,vertexBank;
    hr(device->CreateVertexShader(kVSRigidMultitone,sizeof(kVSRigidMultitone),nullptr,&linkedVS),"linked multitone VS");
    const D3D11_INPUT_ELEMENT_DESC elements[]={
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",3,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",4,DXGI_FORMAT_R32G32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0}};
    hr(device->CreateInputLayout(elements,5,kVSRigidDualTextured,sizeof(kVSRigidDualTextured),&meshLayout),"linked shared mesh layout");
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(mesh);desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    const D3D11_SUBRESOURCE_DATA data{mesh.data(),0,0};
    hr(device->CreateBuffer(&desc,&data,&meshBuffer),"linked mesh buffer");
    desc.ByteWidth=47*sizeof(Vec);desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    hr(device->CreateBuffer(&desc,nullptr,&vertexBank),"linked VS bank");
    context->IASetInputLayout(meshLayout.Get());
    const UINT stride=sizeof(MeshInput),offset=0;auto* vb=meshBuffer.Get();
    context->IASetVertexBuffers(0,1,&vb,&stride,&offset);
    context->VSSetShader(linkedVS.Get(),nullptr,0);
    auto* bank=vertexBank.Get();context->VSSetConstantBuffers(0,1,&bank);
    // Deliberately unbind probe parameters: neither stage may depend on them.
    ID3D11Buffer* absent=nullptr;context->VSSetConstantBuffers(2,1,&absent);
    const auto linkedStart=psDraws;
    for(unsigned variant=0;variant<8;++variant) {
        std::array<Vec,47> v{};for(size_t row=0;row<4;++row)v[row][row]=1;
        v[12]={.25f,0,0,.25f};v[13]={0,.25f,0,.5f};v[14]={0,0,.25f,.75f};
        v[15]={.0625f,0,0,1};v[46]={31,47,59,variant==1?0.f:1.f};
        for(size_t first:{22u,26u}) {
            v[first]={.0001220703125f,0,0,.0009765625f};
            v[first+1]={0,.0001220703125f,0,-.0009765625f};
            v[first+2]={first==26?.125f:-.125f,0,0,.5f};
            v[first+3]={0,0,0,1};
        }
        if(variant==5){v[25]={.125f,0,0,1};v[29]={-.125f,0,0,1};}
        auto c=constants;c[49][3]=variant==2?0.f:1.f;
        c[47][2]=variant==3?0.f:1.f;c[31][0]=variant==4?0.f:1.f;
        if(variant==6)c[49][2]=511;
        auto maps=grids;
        for(size_t b=0;b<2;++b)maps[b].fill(Vec{b==0?.25f:.75f,b==0?.25f:.75f,b==0?.25f:.75f,-19});
        if(variant==7)std::swap(maps[0],maps[1]);
        context->UpdateSubresource(vertexBank.Get(),0,nullptr,v.data(),0,0);
        context->UpdateSubresource(pc.Get(),0,nullptr,c.data(),0,0);
        context->UpdateSubresource(baseTexture.Get(),0,nullptr,base.data(),2*sizeof(Vec),0);
        context->UpdateSubresource(noiseTexture.Get(),0,nullptr,noise.data(),2*sizeof(Vec),0);
        const D3D11_BOX box{511,511,0,514,514,1};
        for(size_t b=0;b<2;++b)context->UpdateSubresource(textures[b].Get(),0,&box,maps[b].data(),3*sizeof(Vec),0);
        const bool shadows=c[47][2]>.5f&&c[31][0]>0;
        ID3D11ShaderResourceView* resources[]={shadows?views[0].Get():nullptr,shadows?views[1].Get():nullptr,
            baseView.Get(),c[49][3]>0?noiseView.Get():nullptr};
        ID3D11SamplerState* states[]={shadows?sampler.Get():nullptr,shadows?sampler.Get():nullptr,
            sampler.Get(),c[49][3]>0?sampler.Get():nullptr};
        context->PSSetShaderResources(0,4,resources);context->PSSetSamplers(0,4,states);
        const Vec sentinel={-17,-19,-23,-29};context->ClearRenderTargetView(rtv.Get(),sentinel.data());
        context->Draw(3,0);++psDraws;++vsDraws;verticesObserved+=3;
        context->CopyResource(staging.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"linked pixel readback");
        std::array<Vec,4> actual{};
        for(size_t y=0;y<2;++y)std::memcpy(actual.data()+2*y,static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch,2*sizeof(Vec));
        context->Unmap(staging.Get(),0);
        // Conventional matrix composition at each vertex, followed by affine
        // interpolation (clip W=1). Pixel centers lie strictly inside the
        // fullscreen triangle: weights are (1-u-v,u,v), u=(x+.5)/4.
        std::array<std::array<std::array<double,4>,6>,3> exports{};
        for(size_t i=0;i<3;++i) {
            const auto& m=mesh[i];auto& e=exports[i];std::array<double,4> world{};
            for(size_t row=0;row<4;++row) {
                world[row]=v[12+row][3];
                for(size_t lane=0;lane<3;++lane)world[row]+=double(v[12+row][lane])*m.base.position[lane];
            }
            for(size_t row=0;row<4;++row)for(size_t lane=0;lane<4;++lane) {
                e[1][row]+=double(v[26+row][lane])*world[lane];
                e[2][row]+=double(v[22+row][lane])*world[lane];
            }
            for(size_t row=0;row<3;++row)for(size_t lane=0;lane<3;++lane)
                e[3][row]+=double(v[12+row][lane])*m.base.normal[lane];
            for(size_t lane=0;lane<4;++lane){e[0][lane]=lane<2?m.base.uv[lane]:m.uv1[lane-2];e[5][lane]=m.base.color[lane];}
            e[4][0]=m.base.uv[0];e[4][1]=m.base.uv[1];
            if(v[46][3]>.5f) {
                size_t axis=0;for(size_t lane=1;lane<3;++lane)if(std::abs(e[3][lane])>std::abs(e[3][axis]))axis=lane;
                e[4][0]=world[axis==0?1:0];e[4][1]=world[axis==2?1:2];
            }
        }
        for(size_t y=0;y<2;++y)for(size_t x=0;x<2;++x) {
            const double u=(double(x)+.5)/4,w=(double(y)+.5)/4;
            const std::array<double,3> weights={1-u-w,u,w};Inputs p{};
            for(size_t field=0;field<6;++field)for(size_t lane=0;lane<4;++lane) {
                double value=0;for(size_t i=0;i<3;++i)value+=weights[i]*exports[i][field][lane];
                p[field][lane]=float(value);
            }
            const auto want=expected(p,c,maps,base,noise);
            char name[100];sprintf_s(name,"multitone linked variant%u pixel%zu",variant,y*2+x);
            for(size_t lane=0;lane<4;++lane)nearValue(actual[y*2+x][lane],want[lane],name,y*2+x,lane);
        }
    }
    std::printf("PASS multitone linkage: %zu real VS/rasterizer/PS draws, four independently interpolated pixels per draw\n",psDraws-linkedStart);
}
