#include "skin_dualtextured_uv_shader.hlsl"

[maxvertexcount(1)] void GSSkinDualUVProbe(point SkinDualUVOutput input[1],inout PointStream<SkinDualUVOutput> stream) { stream.Append(input[0]); }
[maxvertexcount(1)] void GSSkinDualUVAlphaProbe(point SkinDualUVAlphaOutput input[1],inout PointStream<SkinDualUVAlphaOutput> stream) { stream.Append(input[0]); }
SkinDualUVOutput VSSkinDualUVPixelProbe(uint id:SV_VertexID) {
    SkinDualUVOutput o; o.position=float4(id==1?3:-1,id==2?-3:1,0.5,1);
    o.t0=skinDualUVProbe[0].xy; o.t1=skinDualUVProbe[1].xy; o.t2=skinDualUVProbe[2].xy;
    o.t3=skinDualUVProbe[3].xyz; o.t4=skinDualUVProbe[4].xyz; o.t5=skinDualUVProbe[5]; return o;
}
SkinDualUVAlphaOutput VSSkinDualUVAlphaPixelProbe(uint id:SV_VertexID) {
    SkinDualUVAlphaOutput o; o.position=float4(id==1?3:-1,id==2?-3:1,0.5,1);
    o.t0=skinDualUVProbe[0].xy; o.t1=skinDualUVProbe[1].xy; o.t2=skinDualUVProbe[2].xy;
    o.t3=skinDualUVProbe[3].xyz; o.t4=skinDualUVProbe[4].xyz; o.t5=skinDualUVProbe[5]; return o;
}
