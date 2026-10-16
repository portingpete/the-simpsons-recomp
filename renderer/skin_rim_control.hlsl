// The opaque base and dual-textured skin PSs (8200A02C slot5, 82020B9C slot36)
// take floor(color.x) as the artists' "no rim shadow" flag (eye whites). Its
// UNORM8 values are in [0,1]: floor is one only on a wholly flagged triangle.
// Xenos interpolates that shared 1 exactly; host perspective interpolation can
// land an ulp below it, where floor gives 0 and rim shadow speckles. The draw
// wrappers pass the flag as a separate noperspective varying: affine
// interpolation keeps constant 1 exact and the floor decision unchanged, while
// the original color, normals and positions keep perspective sampling.
// Include after shadow_mesh.hlsl, whose b1 carries the option bit.
#ifndef SKIN_RIM_CONTROL
#define SKIN_RIM_CONTROL
float skinRimControl(float control) {
    // Video menu "Character eyes: Clean" (b1 reserved bit 0): the flag counts
    // from 0.5 instead of only at exactly 1, so partially flagged eye-white
    // triangles do not speckle with rim shadow.
    return (reserved&1u)!=0&&control>=0.5?1.0:control;
}
#endif
