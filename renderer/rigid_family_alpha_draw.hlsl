// Color and viewport-depth adapter; original alpha shader arithmetic is shared.
#include "rigid_family_alpha_shader.hlsl"
#include "shadow_mesh.hlsl"
struct RigidFamilyAlphaDrawOutput { float4 color:SV_Target0; float depth:SV_Depth; };
RigidFamilyAlphaDrawOutput PSRigidGlossAlphaDraw(RigidFamilyAlphaOutput input) {
    RigidFamilyAlphaDrawOutput result; result.color=PSRigidGlossAlpha(input);
    result.depth=PSShadowMeshDepth(input.position); return result;
}
RigidFamilyAlphaDrawOutput PSRigidMultitoneAlphaDraw(RigidFamilyAlphaOutput input) {
    RigidFamilyAlphaDrawOutput result; result.color=PSRigidMultitoneAlpha(input);
    result.depth=PSShadowMeshDepth(input.position); return result;
}
RigidFamilyAlphaDrawOutput PSRigidNormalmapAlphaDraw(RigidFamilyAlphaOutput input) {
    RigidFamilyAlphaDrawOutput result; result.color=PSRigidNormalmapAlpha(input);
    result.depth=PSShadowMeshDepth(input.position); return result;
}
