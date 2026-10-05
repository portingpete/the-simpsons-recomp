// Included inside the standalone GPU Fixture. No runtime admission implied.
void multitoneVertices() {
    struct Input {Vertex base;std::array<float,2> uv1;};
    std::vector<Input> vertices;
    for(float x:{-2.f,-1.f,0.f,1.f,2.f})for(float y:{-2.f,-1.f,0.f,1.f,2.f})
        for(float z:{-2.f,-1.f,0.f,1.f,2.f}) {
            const float i=float(vertices.size());
            vertices.push_back({{{.125f*i-3,.0625f*i+2,-.25f*i-1},{x,y,z},
                {.25f,-.5f,1.25f,2},{.375f,-.625f}},{-.75f,.875f}});
        }
    ComPtr<ID3D11VertexShader> shader;ComPtr<ID3D11GeometryShader> observer;ComPtr<ID3D11InputLayout> inputLayout;
    hr(device->CreateVertexShader(kVSRigidMultitone,sizeof(kVSRigidMultitone),nullptr,&shader),"multitone VS");
    const D3D11_INPUT_ELEMENT_DESC elements[]={
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",3,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",4,DXGI_FORMAT_R32G32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0}};
    hr(device->CreateInputLayout(elements,5,kVSRigidDualTextured,sizeof(kVSRigidDualTextured),&inputLayout),"multitone shared mesh layout");
    const D3D11_SO_DECLARATION_ENTRY outputs[]={{0,"SV_Position",0,0,4,0},{0,"TEXCOORD",0,0,4,0},
        {0,"TEXCOORD",1,0,4,0},{0,"TEXCOORD",2,0,4,0},{0,"TEXCOORD",3,0,3,0},
        {0,"TEXCOORD",4,0,2,0},{0,"TEXCOORD",5,0,4,0}};
    const UINT outputStride=25*sizeof(float);
    hr(device->CreateGeometryShaderWithStreamOutput(kGSRigidMultitoneProbe,sizeof(kGSRigidMultitoneProbe),outputs,7,
        &outputStride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&observer),"multitone VS observer");
    D3D11_BUFFER_DESC d{};d.ByteWidth=UINT(vertices.size()*sizeof(Input));d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    const D3D11_SUBRESOURCE_DATA data{vertices.data(),0,0};ComPtr<ID3D11Buffer> vb,out,read,bank;
    hr(device->CreateBuffer(&d,&data,&vb),"multitone vertices");
    d.ByteWidth=UINT(vertices.size()*outputStride);d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_STREAM_OUTPUT;
    hr(device->CreateBuffer(&d,nullptr,&out),"multitone outputs");d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    hr(device->CreateBuffer(&d,nullptr,&read),"multitone staging");
    d.ByteWidth=47*sizeof(Vec);d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    hr(device->CreateBuffer(&d,nullptr,&bank),"multitone 47-register bank");
    context->OMSetRenderTargets(0,nullptr,nullptr);context->IASetInputLayout(inputLayout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    auto* buffer=vb.Get();const UINT stride=sizeof(Input),zero=0;context->IASetVertexBuffers(0,1,&buffer,&stride,&zero);
    context->VSSetShader(shader.Get(),nullptr,0);context->GSSetShader(observer.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
    auto* cb=bank.Get();context->VSSetConstantBuffers(0,1,&cb);
    for(bool transform:{false,true})for(float mode:{-1.f,0.f,std::nextafter(.5f,0.f),.5f,std::nextafter(.5f,1.f),1.f}) {
        std::array<Vec,47> c{};
        for(size_t first:{0u,12u,22u,26u})for(size_t row=0;row<4;++row)c[first+row][row]=1;
        if(transform) {
            c[12]={0,2,0,3};c[13]={-1,0,0,-2};c[14]={0,0,.5f,5};
            c[15]={.25f,-.5f,.125f,2}; // Nonconstant homogeneous W, not an affine shortcut.
            for(size_t first:{22u,26u})for(size_t row=0;row<4;++row)for(size_t lane=0;lane<4;++lane)
                c[first+row][lane]=(row==lane?1.25f:0)+float(int((first+3*row+lane)%9)-4)*.0625f;
        }
        c[46]={31,47,59,mode};context->UpdateSubresource(bank.Get(),0,nullptr,c.data(),0,0);
        auto* output=out.Get();context->SOSetTargets(1,&output,&zero);context->Draw(UINT(vertices.size()),0);
        context->SOSetTargets(0,nullptr,nullptr);context->CopyResource(read.Get(),out.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped),"multitone readback");
        std::vector<std::array<float,25>> actual(vertices.size());std::memcpy(actual.data(),mapped.pData,vertices.size()*outputStride);context->Unmap(read.Get(),0);
        // Geometric reference: project world position onto the plane normal to
        // the largest absolute world-normal axis. Strict comparisons give ties
        // priority X, then Y, then Z. Disabled mode passes UV0 unchanged.
        for(size_t i=0;i<vertices.size();++i) {
            const auto& v=vertices[i];std::array<double,3> world{},normal{};
            for(size_t row=0;row<3;++row){world[row]=c[12+row][3];for(size_t lane=0;lane<3;++lane){
                world[row]+=double(c[12+row][lane])*v.base.position[lane];normal[row]+=double(c[12+row][lane])*v.base.normal[lane];}}
            std::array<double,2> uv={v.base.uv[0],v.base.uv[1]};
            if(mode>.5f){size_t axis=0;for(size_t j=1;j<3;++j)if(std::abs(normal[j])>std::abs(normal[axis]))axis=j;
                uv=axis==0?std::array<double,2>{world[1],world[2]}:axis==1?std::array<double,2>{world[0],world[2]}:std::array<double,2>{world[0],world[1]};}
            for(size_t j=0;j<4;++j){nearValue(actual[i][j],j<3?v.base.position[j]:1,"multitone clip",i,j);
                nearValue(actual[i][4+j],j<2?v.base.uv[j]:v.uv1[j-2],"multitone UV pair",i,j);
                nearValue(actual[i][21+j],v.base.color[j],"multitone color",i,j);}
            // Independent matrix composition, not a copy of slot24's register
            // construction. CNDE assembles homogeneous world position before
            // either shadow matrix; c46/noise-axis branches must not alter it.
            double worldW=c[15][3];
            for(size_t lane=0;lane<3;++lane)worldW+=double(c[15][lane])*v.base.position[lane];
            for(size_t bank=0;bank<2;++bank)for(size_t row=0;row<4;++row) {
                const auto& matrixRow=c[(bank==0?26:22)+row];
                double shadow=matrixRow[3]*worldW;
                for(size_t lane=0;lane<3;++lane)shadow+=double(matrixRow[lane])*world[lane];
                nearValue(actual[i][8+bank*4+row],shadow,bank==0?"multitone character shadow":"multitone world shadow",i,row);
            }
            for(size_t j=0;j<3;++j)nearValue(actual[i][16+j],normal[j],"multitone normal",i,j);
            for(size_t j=0;j<2;++j)nearValue(actual[i][19+j],uv[j],"multitone noise UV",i,j);
        }
        ++vsDraws;verticesObserved+=vertices.size();
    }
    std::puts("PASS multitone VS: 1500 vertices, threshold neighbors, signed axes/ties, zero normals, transformed projection, UV passthrough and both world-to-shadow exports (nonaffine W)");
}
