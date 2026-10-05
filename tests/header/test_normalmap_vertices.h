// Included inside the standalone GPU Fixture. No runtime admission implied.
void normalmapVertices() {
    struct NInput {Vertex base;std::array<float,2> uv1;std::array<float,3> tangent;};
    static_assert(sizeof(NInput)==68);
    // Legacy zero/denormal annihilation matches rigidLegacyMultiply: only the
    // product rule is shared, every wiring/order assertion below is an
    // independent geometric check of the transcribed slot sequence.
    const auto lmul=[](double a,double b) {
        if(std::fpclassify(a)==FP_ZERO||std::fpclassify(a)==FP_SUBNORMAL||
           std::fpclassify(b)==FP_ZERO||std::fpclassify(b)==FP_SUBNORMAL)return 0.0;
        return a*b;
    };
    const auto dot4zxyw=[](const Vec& m,const std::array<double,4>& v) {
        return double(m[2])*v[2]+double(m[0])*v[0]+double(m[1])*v[1]+double(m[2+1])*v[3];
    };
    const auto dot3=[](double ax,double ay,double az,double bx,double by,double bz) {
        return ax*bx+ay*by+az*bz;
    };
    const auto dot4wzxy=[](const Vec& m,const std::array<double,4>& v) {
        return double(v[0])*m[3]+double(v[1])*m[2]+double(v[2])*m[0]+double(v[3])*m[1];
    };
    const std::array<std::array<float,3>,7> normals{{{1,0,0},{0,1,0},{0,0,1},
        {.5773503f,.5773503f,.5773503f},{.5f,-.25f,.75f},{0,0,0},{-1,2,-.5f}}};
    const std::array<std::array<float,3>,6> tangents{{{0,1,0},{1,0,0},{0,0,1},
        {1,1,0},{-.5f,.5f,-.5f},{0,0,0}}};
    std::vector<NInput> vertices;
    for(float x:{-2.f,-1.f,0.f,1.f,2.f})for(float y:{-2.f,-1.f,0.f,1.f,2.f})
        for(float z:{-2.f,-1.f,0.f,1.f,2.f}) {
            const float i=float(vertices.size());
            vertices.push_back({{{x,y,z},
                normals[size_t(i)%normals.size()],{.25f,-.5f,1.25f,2},{.375f,-.625f}},
                {-.75f,.875f},tangents[size_t(i)%tangents.size()]});
        }
    ComPtr<ID3D11VertexShader> shader;ComPtr<ID3D11GeometryShader> observer;ComPtr<ID3D11InputLayout> inputLayout;
    hr(device->CreateVertexShader(kVSRigidNormalmap,sizeof(kVSRigidNormalmap),nullptr,&shader),"normalmap VS");
    const D3D11_INPUT_ELEMENT_DESC elements[]={
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",2,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",3,DXGI_FORMAT_R32G32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",4,DXGI_FORMAT_R32G32_FLOAT,0,48,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",5,DXGI_FORMAT_R32G32B32_FLOAT,0,56,D3D11_INPUT_PER_VERTEX_DATA,0}};
    hr(device->CreateInputLayout(elements,6,kVSRigidNormalmap,sizeof(kVSRigidNormalmap),&inputLayout),"normalmap tangent layout");
    const D3D11_SO_DECLARATION_ENTRY outputs[]={{0,"SV_Position",0,0,4,0},{0,"TEXCOORD",0,0,4,0},
        {0,"TEXCOORD",1,0,4,0},{0,"TEXCOORD",2,0,4,0},{0,"TEXCOORD",3,0,4,0},
        {0,"TEXCOORD",4,0,3,0},{0,"TEXCOORD",5,0,3,0},{0,"TEXCOORD",6,0,3,0},
        {0,"TEXCOORD",7,0,4,0}};
    const UINT outputStride=33*sizeof(float);
    hr(device->CreateGeometryShaderWithStreamOutput(kGSRigidNormalmapProbe,sizeof(kGSRigidNormalmapProbe),outputs,9,
        &outputStride,1,D3D11_SO_NO_RASTERIZED_STREAM,nullptr,&observer),"normalmap VS observer");
    D3D11_BUFFER_DESC d{};d.ByteWidth=UINT(vertices.size()*sizeof(NInput));d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    const D3D11_SUBRESOURCE_DATA data{vertices.data(),0,0};ComPtr<ID3D11Buffer> vb,out,read,bank;
    hr(device->CreateBuffer(&d,&data,&vb),"normalmap vertices");
    d.ByteWidth=UINT(vertices.size()*outputStride);d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_STREAM_OUTPUT;
    hr(device->CreateBuffer(&d,nullptr,&out),"normalmap outputs");d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    hr(device->CreateBuffer(&d,nullptr,&read),"normalmap staging");
    d.ByteWidth=30*sizeof(Vec);d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    hr(device->CreateBuffer(&d,nullptr,&bank),"normalmap 30-register bank");
    context->OMSetRenderTargets(0,nullptr,nullptr);context->IASetInputLayout(inputLayout.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    auto* buffer=vb.Get();const UINT stride=sizeof(NInput),zero=0;context->IASetVertexBuffers(0,1,&buffer,&stride,&zero);
    context->VSSetShader(shader.Get(),nullptr,0);context->GSSetShader(observer.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
    auto* cb=bank.Get();context->VSSetConstantBuffers(0,1,&cb);
    for(bool transform:{false,true}) {
        std::array<Vec,30> c{};
        for(size_t first:{0u,12u,22u,26u})for(size_t row=0;row<4;++row)c[first+row][row]=1;
        if(transform) {
            c[12]={0,2,0,3};c[13]={-1,0,0,-2};c[14]={0,0,.5f,5};
            c[15]={.25f,-.5f,.125f,2}; // Nonconstant homogeneous W, not an affine shortcut.
            for(size_t first:{22u,26u})for(size_t row=0;row<4;++row)for(size_t lane=0;lane<4;++lane)
                c[first+row][lane]=(row==lane?1.25f:0)+float(int((first+3*row+lane)%9)-4)*.0625f;
        }
        context->UpdateSubresource(bank.Get(),0,nullptr,c.data(),0,0);
        auto* output=out.Get();context->SOSetTargets(1,&output,&zero);context->Draw(UINT(vertices.size()),0);
        context->SOSetTargets(0,nullptr,nullptr);context->CopyResource(read.Get(),out.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};hr(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped),"normalmap readback");
        std::vector<std::array<float,33>> actual(vertices.size());std::memcpy(actual.data(),mapped.pData,vertices.size()*outputStride);context->Unmap(read.Get(),0);
        for(size_t i=0;i<vertices.size();++i) {
            const auto& v=vertices[i];
            const std::array<double,4> p={v.base.position[0],v.base.position[1],v.base.position[2],1};
            const std::array<double,3> n={v.base.normal[0],v.base.normal[1],v.base.normal[2]};
            const std::array<double,3> t={v.tangent[0],v.tangent[1],v.tangent[2]};
            std::array<double,33> want{};
            // Slots 11-14 clip, 25 uv passthrough, 26 color passthrough.
            for(size_t row=0;row<4;++row)want[row]=dot4zxyw(c[row],p);
            want[4]=v.base.uv[0];want[5]=v.base.uv[1];want[6]=v.uv1[0];want[7]=v.uv1[1];
            for(size_t lane=0;lane<4;++lane)want[29+lane]=v.base.color[lane];
            // Slots 18-21 world rows in permuted vc15/14/12/13 order, consumed
            // by shadows/out1 exactly in this lane order.
            const std::array<double,4> world={dot4zxyw(c[15],p),dot4zxyw(c[14],p),
                dot4zxyw(c[12],p),dot4zxyw(c[13],p)};
            // Slot 28 swizzle r2.zwyx precedes the slot-40 world overwrite.
            want[8]=world[2];want[9]=world[3];want[10]=world[1];want[11]=world[0];
            for(size_t row=0;row<4;++row) {
                want[12+row]=dot4wzxy(c[26+row],world);
                want[16+row]=dot4wzxy(c[22+row],world);
            }
            // Slots 15-17 tangent rows and 22-24 world normal rows.
            const std::array<double,4> r0in={0,dot3(t[2],t[0],t[1],c[14][2],c[14][0],c[14][1]),
                dot3(t[2],t[0],t[1],c[12][2],c[12][0],c[12][1]),
                dot3(t[2],t[0],t[1],c[13][2],c[13][0],c[13][1])};
            const std::array<double,4> r1n={dot3(n[2],n[0],n[1],c[13][2],c[13][0],c[13][1]),
                dot3(n[2],n[0],n[1],c[12][2],c[12][0],c[12][1]),
                dot3(n[2],n[0],n[1],c[14][2],c[14][0],c[14][1]),0};
            // Slot 27 swizzle precedes the slot-42 normal overwrite.
            want[20]=r1n[1];want[21]=r1n[0];want[22]=r1n[2];
            // Slots 37-39 normalize the tangent into out5.
            const double l0=r0in[1]*r0in[1]+r0in[2]*r0in[2]+r0in[3]*r0in[3];
            const double inv=1.0/std::sqrt(l0);
            want[23]=lmul(r0in[2],inv);want[24]=lmul(r0in[3],inv);want[25]=lmul(r0in[1],inv);
            // Slots 40-44 cross into the binormal rows.
            const double qx=lmul(r0in[1],inv),qy=lmul(r0in[2],inv),qz=lmul(r0in[3],inv);
            const double r0x=lmul(qz,r1n[2]),r0y=lmul(qy,r1n[2]);
            const std::array<double,4> r1b={lmul(qx,r1n[0]),lmul(qy,r1n[0]),
                lmul(qx,r1n[1]),lmul(qz,r1n[1])};
            const double b0z=r1b[0]-r0x,b0w=r0y-r1b[2],b0y=r1b[3]-r1b[1];
            // Slots 45-47 normalize the binormal into out6.
            const double l1=b0y*b0y+b0z*b0z+b0w*b0w;
            const double inv2=1.0/std::sqrt(l1);
            want[26]=lmul(b0z,inv2);want[27]=lmul(b0w,inv2);want[28]=lmul(b0y,inv2);
            for(size_t lane=0;lane<33;++lane)nearValue(actual[i][lane],want[lane],"normalmap VS",i,lane);
        }
        ++vsDraws;verticesObserved+=vertices.size();
    }
    std::puts("PASS normalmap VS: 125 vertices x2 banks, tangent/normal/world/shadow exports, orthonormalized tangent space (nonaffine W)");
}
