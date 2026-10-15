// Native temporal antialiasing. The world projection is jittered by a
// subpixel Halton offset each frame; this pass reprojects the previous
// resolved image with the camera motion recovered from scene depth, clips
// it to the current 3x3 neighbourhood and blends it with the new sample.
// Scene alpha carries packed material flags and is copied, never blended.
cbuffer TemporalAAConstants : register(b0) {
    // Current jittered NDC (x, y, depth z, 1) -> previous unjittered clip.
    float4 reprojection[4];
    float4 extent;  // 1/width, 1/height, width, height (physical pixels).
    float4 control; // xy: jitter shift to the history pixel centre (uv), z: history valid, w: new-sample weight.
    float4 motion;  // x: new-sample weight at fast motion, y: speed (px) for that weight, z: clip box sigma scale.
};
Texture2D<float4> currentColor : register(t0);
Texture2D<float> sceneDepth : register(t1);
Texture2D<float4> historyColor : register(t2);
SamplerState linearClamp : register(s0);
struct Varying {float4 position : SV_POSITION;float2 uv : TEXCOORD0;};
Varying VSTemporalAA(uint id : SV_VertexID) {
    Varying result;
    result.uv=float2((id<<1)&2,id&2);
    result.position=float4(result.uv*float2(2,-2)+float2(-1,1),0,1);
    return result;
}
float3 toYCoCg(float3 c) {
    return float3(dot(c,float3(0.25,0.5,0.25)),dot(c,float3(0.5,0,-0.5)),dot(c,float3(-0.25,0.5,-0.25)));
}
float3 fromYCoCg(float3 c) {return float3(c.x+c.y-c.z,c.x+c.z,c.x-c.y-c.z);}
// Five-tap Catmull-Rom history reconstruction; keeps the accumulated image
// sharp where plain bilinear would soften it every frame.
float3 sampleHistory(float2 uv) {
    const float2 position=uv*extent.zw;
    const float2 centre=floor(position-0.5)+0.5;
    const float2 f=position-centre;
    const float2 w0=f*(-0.5+f*(1.0-0.5*f));
    const float2 w1=1.0+f*f*(-2.5+1.5*f);
    const float2 w2=f*(0.5+f*(2.0-1.5*f));
    const float2 w3=f*f*(-0.5+0.5*f);
    const float2 w12=w1+w2;
    const float2 t12=(centre+w2/w12)*extent.xy;
    const float2 t0=(centre-1.0)*extent.xy,t3=(centre+2.0)*extent.xy;
    float3 sum=historyColor.SampleLevel(linearClamp,float2(t12.x,t0.y),0).rgb*(w12.x*w0.y);
    sum+=historyColor.SampleLevel(linearClamp,float2(t0.x,t12.y),0).rgb*(w0.x*w12.y);
    sum+=historyColor.SampleLevel(linearClamp,t12,0).rgb*(w12.x*w12.y);
    sum+=historyColor.SampleLevel(linearClamp,float2(t3.x,t12.y),0).rgb*(w3.x*w12.y);
    sum+=historyColor.SampleLevel(linearClamp,float2(t12.x,t3.y),0).rgb*(w12.x*w3.y);
    const float total=w12.x*w0.y+w0.x*w12.y+w12.x*w12.y+w3.x*w12.y+w12.x*w3.y;
    return max(sum/total,0.0);
}
struct Resolved {float4 color : SV_Target0;float4 history : SV_Target1;};
Resolved PSTemporalAAResolve(Varying input) {
    const int2 pixel=int2(input.position.xy);
    const int2 last=int2(extent.zw)-1;
    const float4 center=currentColor.Load(int3(pixel,0));
    float3 sum=0,squares=0,low=1e9,high=-1e9;
    float nearest=0;
    // Scene depth is reversed (1 near, 0 far). The nearest neighbour's depth
    // carries foreground motion across its antialiased silhouette.
    [unroll] for(int y=-1;y<=1;++y) [unroll] for(int x=-1;x<=1;++x) {
        const int2 p=clamp(pixel+int2(x,y),int2(0,0),last);
        const float3 c=toYCoCg(currentColor.Load(int3(p,0)).rgb);
        sum+=c;squares+=c*c;low=min(low,c);high=max(high,c);
        nearest=max(nearest,sceneDepth.Load(int3(p,0)));
    }
    Resolved result;
    result.color=center;result.history=float4(center.rgb,1);
    if(control.z==0)return result;
    const float2 uv=(float2(pixel)+0.5)*extent.xy;
    const float4 ndc=float4(uv.x*2-1,1-uv.y*2,1-nearest,1);
    const float4 previous=float4(dot(reprojection[0],ndc),dot(reprojection[1],ndc),dot(reprojection[2],ndc),dot(reprojection[3],ndc));
    const float3 mean=sum/9,sigma=sqrt(max(squares/9-mean*mean,0));
    const float3 boxLow=max(low,mean-motion.z*sigma),boxHigh=min(high,mean+motion.z*sigma);
    const float3 boxCentre=0.5*(boxHigh+boxLow),boxExtent=0.5*(boxHigh-boxLow)+1e-5;
    // Camera reprojection is exact for the world. The character the camera
    // follows stays put on screen instead, so also consider the unmoved
    // history pixel and keep whichever candidate the current neighbourhood
    // supports (smaller normalised distance outside the clip box).
    const float2 staticUv=uv+control.xy;
    float2 previousUv=staticUv;
    float3 history=toYCoCg(sampleHistory(staticUv));
    float3 units=abs((history-boxCentre)/boxExtent);
    float outside=max(units.x,max(units.y,units.z));
    if(previous.w>0) {
        const float2 reprojected=float2(previous.x/previous.w*0.5+0.5,0.5-previous.y/previous.w*0.5)+control.xy;
        if(all(reprojected>=0)&&all(reprojected<=1)) {
            const float3 candidate=toYCoCg(sampleHistory(reprojected));
            const float3 candidateUnits=abs((candidate-boxCentre)/boxExtent);
            const float candidateOutside=max(candidateUnits.x,max(candidateUnits.y,candidateUnits.z));
            // Prefer the reprojection unless the static pixel fits clearly better.
            if(candidateOutside<=outside*1.25+0.05){history=candidate;outside=candidateOutside;previousUv=reprojected;}
        }
    }
    if(outside>1)history=boxCentre+(history-boxCentre)/outside;
    const float speed=length((uv-previousUv)*extent.zw);
    const float weight=lerp(control.w,motion.x,saturate(speed/motion.y));
    // Luma-weighted blend keeps bright subpixel highlights from flickering.
    const float3 current=toYCoCg(center.rgb);
    const float currentWeight=weight/(1+current.x),historyWeight=(1-weight)/(1+history.x);
    const float3 rgb=saturate(fromYCoCg((current*currentWeight+history*historyWeight)/(currentWeight+historyWeight)));
    result.color=float4(rgb,center.a);result.history=float4(rgb,1);
    return result;
}
