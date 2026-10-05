// The original opaque UV pass samples world depth t0 and character depth t1.
#define RIGID_SINGLE_UV_DEPTH_RRRR
#include "rigid_uv_shader.hlsl"
#include "shadow_mesh.hlsl"
struct RigidSingleUVDrawOutput { float4 color:SV_Target0; float depth:SV_Depth; };
RigidSingleUVDrawOutput PSRigidUVDraw(RigidSingleUVOutput input) {
    RigidSingleUVDrawOutput result; result.color=PSRigidUV(input);
    result.depth=PSShadowMeshDepth(input.position); return result;
}
