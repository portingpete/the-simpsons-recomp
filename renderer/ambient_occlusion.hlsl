// Native screen-space ambient occlusion from scene depth alone. Obscurance is
// estimated at half resolution with a normal-oriented disk of samples, blurred
// with depth-aware weights and multiplied into the finished world image.
cbuffer AmbientOcclusionConstants : register(b0) {
    // Row-vector projection terms: x scale, y scale, depth scale (P22),
    // depth offset (P32); view z = P32 / (ndc z - P22).
    float4 projection;
    float4 offsets;   // xy: off-centre terms (P20, P21); z: frame noise seed.
    float4 params;    // x: radius (world), y: intensity, z: bias (world per unit depth), w: sample count.
    float4 extent;    // full width, height, cell width, cell height.
    float4 blur;      // xy: blur direction in cell-res pixels, z: depth tolerance.
};
Texture2D<float> sceneDepth : register(t0);
Texture2D<float> occlusion : register(t1);
SamplerState linearClamp : register(s0);
struct Varying {float4 position : SV_POSITION;float2 uv : TEXCOORD0;};
Varying VSAmbientOcclusion(uint id : SV_VertexID) {
    Varying result;
    result.uv=float2((id<<1)&2,id&2);
    result.position=float4(result.uv*float2(2,-2)+float2(-1,1),0,1);
    return result;
}
// Scene depth is reversed (1 near); view z grows away from the camera.
float viewZ(float depth) {return projection.w/((1-depth)-projection.z);}
float3 viewPosition(float2 uv,float depth) {
    const float z=viewZ(depth);
    const float2 ndc=float2(uv.x*2-1,1-uv.y*2);
    return float3((ndc.x-offsets.x)*z/projection.x,(ndc.y-offsets.y)*z/projection.y,z);
}
float loadDepth(int2 pixel) {
    return sceneDepth.Load(int3(clamp(pixel,int2(0,0),int2(extent.xy)-1),0));
}
float PSAmbientOcclusion(Varying input) : SV_TARGET {
    const int2 cell=int2(input.position.xy);
    const int2 full=cell*2;
    const float depth=loadDepth(full);
    if(depth<=0.0001)return 1; // Sky / cleared far plane.
    const float2 uv=(float2(full)+0.5)/extent.xy;
    const float3 p=viewPosition(uv,depth);
    // Normal from the flatter of the two neighbours on each axis.
    const float2 texel=1/extent.xy;
    const float3 right=viewPosition(uv+float2(texel.x*2,0),loadDepth(full+int2(2,0)));
    const float3 left=viewPosition(uv-float2(texel.x*2,0),loadDepth(full-int2(2,0)));
    const float3 down=viewPosition(uv+float2(0,texel.y*2),loadDepth(full+int2(0,2)));
    const float3 up=viewPosition(uv-float2(0,texel.y*2),loadDepth(full-int2(0,2)));
    const float3 dx=abs(right.z-p.z)<abs(p.z-left.z)?right-p:p-left;
    const float3 dy=abs(down.z-p.z)<abs(p.z-up.z)?down-p:p-up;
    float3 n=normalize(cross(dy,dx));
    if(n.z>0)n=-n; // Face the camera (view z points away).
    // Screen radius of the world-space sample disk, in full-res pixels.
    const float radius=params.x*projection.x*extent.x*0.5/p.z;
    if(radius<1)return 1;
    const uint count=uint(params.w);
    // Interleaved gradient noise rotates the spiral per pixel.
    const float noise=frac(52.9829189*frac(dot(float2(cell),float2(0.06711056,0.00583715))+offsets.z));
    float sum=0;
    [loop] for(uint i=0;i<count;++i) {
        const float alpha=(float(i)+0.5)/float(count);
        const float angle=alpha*(7*6.2831853)+noise*6.2831853;
        const float2 offset=float2(cos(angle),sin(angle))*alpha*radius;
        const int2 tap=full+int2(offset);
        const float tapDepth=loadDepth(tap);
        if(tapDepth<=0.0001)continue;
        const float3 q=viewPosition((float2(tap)+0.5)/extent.xy,tapDepth);
        const float3 v=q-p;
        const float vv=dot(v,v);
        const float falloff=saturate(1-vv/(params.x*params.x));
        sum+=falloff*max(0,dot(v,n)-params.z*p.z)/(vv+0.01);
    }
    return saturate(1-params.y*2*sum/float(count));
}
// Depth-aware separable blur at half resolution.
float PSAmbientOcclusionBlur(Varying input) : SV_TARGET {
    const int2 cell=int2(input.position.xy);
    const float centreZ=viewZ(loadDepth(cell*2));
    float total=0,weights=0;
    [unroll] for(int i=-4;i<=4;++i) {
        const int2 tap=clamp(cell+int2(blur.xy)*i,int2(0,0),int2(extent.zw)-1);
        const float z=viewZ(loadDepth(tap*2));
        const float weight=exp(-float(i*i)/8)*saturate(1-abs(z-centreZ)/(blur.z*centreZ));
        total+=occlusion.Load(int3(tap,0))*weight;weights+=weight;
    }
    return weights>0?total/weights:1;
}
// Multiply the blurred obscurance into the scene (blend: dest * src).
float4 PSAmbientOcclusionApply(Varying input) : SV_TARGET {
    const float ao=occlusion.SampleLevel(linearClamp,input.position.xy/extent.xy,0);
    return float4(ao,ao,ao,1);
}
