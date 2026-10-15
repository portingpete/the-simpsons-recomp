// Native presentation extras: the on-screen frame-rate counter and the
// edge-adaptive upscaler used when the window is larger than the rendered image.
cbuffer PresentationExtras : register(b0) {
    uint4 text[2];       // Up to eight glyph codes (0-9 digits, 10 '.', 11 F, 12 P, 13 S, 14 space).
    float4 textRect;     // x, y origin in target pixels, pixels per font pixel, glyph count.
    float4 sourceExtent; // 1/width, 1/height, width, height of the upscaled image.
    float4 destination;  // x, y of the content rectangle, 1/width, 1/height.
};
Texture2D<float4> source : register(t0);
struct Varying {float4 position : SV_POSITION;float2 uv : TEXCOORD0;};
Varying VSPresentationExtras(uint id : SV_VertexID) {
    Varying result;
    result.uv=float2((id<<1)&2,id&2);
    result.position=float4(result.uv*float2(2,-2)+float2(-1,1),0,1);
    return result;
}
// 5x7 glyphs, row-major from the top; bit 4 of each row is the leftmost column.
static const uint2 glyphs[15]={
    uint2(0xA39ACE2Eu,0x3u),uint2(0x88421184u,0x3u),uint2(0xD041062Eu,0x7u),uint2(0xA211105Fu,0x3u),
    uint2(0x85F928C2u,0x0u),uint2(0xA210FA1Fu,0x3u),uint2(0xA31F4106u,0x3u),uint2(0x1082083Fu,0x2u),
    uint2(0xA317462Eu,0x3u),uint2(0x0417C62Eu,0x3u),uint2(0x18000000u,0x3u),uint2(0x210F421Fu,0x4u),
    uint2(0x210F463Eu,0x4u),uint2(0x8217420Fu,0x7u),uint2(0x00000000u,0x0u)};
float4 PSFpsOverlay(Varying input) : SV_TARGET {
    // One font pixel of padding around six-pixel glyph cells.
    const float2 font=(input.position.xy-textRect.xy)/textRect.z-1;
    const int cell=int(floor(font.x/6)),column=int(floor(font.x))-cell*6,row=int(floor(font.y));
    float4 color=float4(0,0,0,0.6);
    if(cell>=0&&cell<int(textRect.w)&&column>=0&&column<5&&row>=0&&row<7) {
        const uint code=min(text[cell>>2][cell&3],14u);
        const uint bit=uint(row*5+(4-column));
        const uint word=bit<32?glyphs[code].x:glyphs[code].y;
        if((word>>(bit&31))&1u)color=float4(1,1,0.6,1);
    }
    return color;
}
float catmullRom(float x) {
    x=abs(x);
    return x<1?(1.5*x-2.5)*x*x+1:x<2?((-0.5*x+2.5)*x-4)*x+2:0;
}
float4 PSUpscaleEdge(Varying input) : SV_TARGET {
    const float2 uv=(input.position.xy-destination.xy)*destination.zw;
    const float2 position=uv*sourceExtent.zw-0.5;
    const float2 base=floor(position),f=position-base;
    const int2 origin=int2(base),last=int2(sourceExtent.zw)-1;
    float3 taps[4][4];float luma[4][4];
    [unroll] for(int y=0;y<4;++y) [unroll] for(int x=0;x<4;++x) {
        taps[y][x]=source.Load(int3(clamp(origin+int2(x-1,y-1),int2(0,0),last),0)).rgb;
        luma[y][x]=dot(taps[y][x],float3(0.299,0.587,0.114));
    }
    // Bilinear-weighted central-difference gradient of the inner 2x2.
    const float4 inner=float4((1-f.x)*(1-f.y),f.x*(1-f.y),(1-f.x)*f.y,f.x*f.y);
    float2 gradient=0;
    gradient+=inner.x*float2(luma[1][2]-luma[1][0],luma[2][1]-luma[0][1]);
    gradient+=inner.y*float2(luma[1][3]-luma[1][1],luma[2][2]-luma[0][2]);
    gradient+=inner.z*float2(luma[2][2]-luma[2][0],luma[3][1]-luma[1][1]);
    gradient+=inner.w*float2(luma[2][3]-luma[2][1],luma[3][2]-luma[1][2]);
    const float3 low=min(min(taps[1][1],taps[1][2]),min(taps[2][1],taps[2][2]));
    const float3 high=max(max(taps[1][1],taps[1][2]),max(taps[2][1],taps[2][2]));
    const float contrast=dot(high-low,float3(0.299,0.587,0.114));
    const float magnitude=length(gradient);
    const float2 across=magnitude>1e-5?gradient/magnitude:float2(1,0);
    const float2 along=float2(-across.y,across.x);
    // Flat areas keep a round kernel; edges stretch it along their direction
    // so the filter stays sharp across the edge and smooth along it.
    const float edge=saturate(magnitude/(2*contrast+1e-3))*saturate(contrast*8);
    const float stretch=1+1.5*edge;
    float3 sum=0;float total=0;
    [unroll] for(int ty=0;ty<4;++ty) [unroll] for(int tx=0;tx<4;++tx) {
        const float2 offset=float2(tx-1,ty-1)-f;
        const float a=dot(offset,across),b=dot(offset,along)/stretch;
        const float weight=catmullRom(sqrt(a*a+b*b));
        sum+=taps[ty][tx]*weight;total+=weight;
    }
    return float4(clamp(sum/max(total,1e-4),low,high),1);
}
