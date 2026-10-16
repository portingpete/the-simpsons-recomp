#ifndef RIGID_PALETTE_COORDINATE
#define RIGID_PALETTE_COORDINATE
// Every rigid and skin palette PS (the opaque passes and the skin dual UV
// alpha pass) packs a palette coordinate (uv0 or uv1) as
// floor(64*frac(u)) and 32*floor(32*frac(2v)) plus the 2v>=1 bit, which change
// only at multiples of 1/64. Xenos interpolates each attribute as
// P0+i*(P1-P0)+j*(P2-P0), so a value all three vertices share arrives exactly;
// host perspective interpolation can land a few ulps away, and a shared v=0.5
// interpolated an ulp low wraps frac(2v) to ~1 and changes the code on
// alternate rows (the Eighty Bites floor strip). Snapping onto that grid within
// 2^-16 of the value, at most 1/16384 (a 256th of a palette cell, so large
// coordinates still floor), restores a shared grid value exactly and moves a
// varying one by at most that. Generators inline this file; hand-written
// wrappers include it.
float2 rigidPaletteCoordinate(float2 value) {
    precise float2 grid=round(value*64)/64;
    precise float2 tolerance=min(max(abs(grid),1.0/64)/65536,1.0/16384);
    return float2(abs(value.x-grid.x)<=tolerance.x?grid.x:value.x,
                  abs(value.y-grid.y)<=tolerance.y?grid.y:value.y);
}
#endif
