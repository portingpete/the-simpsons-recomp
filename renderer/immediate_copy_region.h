#pragma once
#include "immediate_draw.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <span>

namespace Simpsons::Graphics {
// VSImmediate uses three ordered multiply/adds and an implicit position.w=1.
// With positive clip W, perspective division maps a triangle into the convex
// hull of its divided vertices. Clipping cannot enlarge that hull. Bound the
// float transform before dividing, then round outwards in physical texels.
// Nonempty regions keep original rasterization, depth and ordered packed
// blending with the original viewport/scissor. Proven empty draws keep bookkeeping.
inline D3D11_BOX immediateCopyRegion(const ImmediateDraw& draw,std::span<const uint16_t> order,
                                    UINT width,UINT height,bool temporal,const D3D11_VIEWPORT& viewport) {
    const D3D11_BOX full{0,0,0,width,height,1},empty{0,0,0,0,0,1};
    // Radial/projected/temporal vertex programs have separate coverage rules.
    if(!width || !height || order.empty() || temporal || draw.radial || draw.projected)return full;
    if(!std::isfinite(viewport.TopLeftX) || !std::isfinite(viewport.TopLeftY) ||
       !std::isfinite(viewport.Width) || !std::isfinite(viewport.Height) || viewport.Width<=0 || viewport.Height<=0)return full;
    const auto regular=[](float value) {
        const uint32_t bits=std::bit_cast<uint32_t>(value)&0x7fffffffu;
        // Input denormals may be flushed before multiplication by the GPU.
        return bits<0x7f800000u && (!bits || bits>=0x00800000u);
    };
    for(size_t row=0;row<4;++row)for(size_t lane:{0u,1u,3u})
        if(!regular(draw.constants[row][lane]))return full;
    double left=std::numeric_limits<double>::infinity(),top=left,right=-left,bottom=-left;
    bool allBehind=true,allPositive=true;
    for(const auto index:order) {
        if(index>=draw.vertices.size())return full;
        const auto& position=draw.vertices[index].position;
        for(float value:position)if(!regular(value))return full;
        std::array<double,3> low{},high{};
        for(size_t axis=0;axis<3;++axis) {
            const size_t lane=axis==2?3:axis;
            double value=draw.constants[3][lane],magnitude=std::abs(value);
            for(size_t row=0;row<3;++row) {
                const double product=double(position[row])*double(draw.constants[row][lane]);
                value+=product;magnitude+=std::abs(product);
            }
            if(!std::isfinite(magnitude) || magnitude>double(std::numeric_limits<float>::max())/16)return full;
            // More than the roundoff of all three products/adds, including
            // fused/unfused evaluation and flushed intermediate denormals.
            // The absolute-product sum also covers cancellation; the host's
            // double summation error is smaller than this float envelope.
            const double error=64*double(std::numeric_limits<float>::epsilon())*magnitude+
                               32*double(std::numeric_limits<float>::min());
            low[axis]=value-error;high[axis]=value+error;
        }
        if(high[2]>=0)allBehind=false;
        if(!(low[2]>0)){allPositive=false;continue;}
        const auto divided=[&](size_t axis) {
            const std::array values{low[axis]/low[2],low[axis]/high[2],high[axis]/low[2],high[axis]/high[2]};
            return std::array{*std::min_element(values.begin(),values.end()),*std::max_element(values.begin(),values.end())};
        };
        const auto x=divided(0),y=divided(1);
        left=std::min(left,viewport.TopLeftX+(x[0]*.5+.5)*viewport.Width);
        right=std::max(right,viewport.TopLeftX+(x[1]*.5+.5)*viewport.Width);
        top=std::min(top,viewport.TopLeftY+(.5-y[1]*.5)*viewport.Height);
        bottom=std::max(bottom,viewport.TopLeftY+(.5-y[0]*.5)*viewport.Height);
    }
    // A strictly negative W at every consumed vertex stays negative over
    // every triangle. No point can satisfy the homogeneous clip volume.
    // No native draw or copy can affect a texel; callers keep submission bookkeeping.
    if(allBehind)return empty;
    if(!allPositive)return full; // Mixed/uncertain/near-zero W.
    // Two extra texels cover viewport rounding and rasterizer subpixel snap.
    const auto lower=[](double value,UINT extent){return UINT(std::clamp(std::floor(value)-2,0.0,double(extent)));};
    const auto upper=[](double value,UINT extent){return UINT(std::clamp(std::ceil(value)+2,0.0,double(extent)));};
    const D3D11_BOX region{lower(left,width),lower(top,height),0,upper(right,width),upper(bottom,height),1};
    // An empty guarded hull cannot rasterize into the physical target.
    return region.left<region.right && region.top<region.bottom?region:empty;
}
}
