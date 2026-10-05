#include "movie_state.h"

namespace Simpsons::Graphics {
namespace {
void need(bool value,const char* reason){if(!value)throw EngineStateError(reason);}
}
MovieState prepareMovieState(const EngineState& before) {
    MovieState result;result.after=before;auto& state=result.after;
    using S=ScalarState;using T=SamplerState;
    // Exact direct setters in8282E404..E528. The remaining effective requests
    // survive, including scalar blend shadows and the caller's pending queue.
    state.setScalar(S::HalfPixelOffset,1);state.setScalar(S::Cull,0);
    state.setScalar(S::DepthEnable,0);state.setScalar(S::AlphaTest,0);
    state.setPackedBlend(0,0x00010001);
    for(uint32_t i=0;i<3;++i){state.setSampler(i,T::Minification,1);state.setSampler(i,T::Magnification,1);}
    need(!state.scalar(S::StencilEnable)&&!state.scalar(S::Fill)&&state.scalar(S::ColorMask0)==15,
         "Native movie requires filled polygons, disabled stencil and full RGBA writes");
    need(!state.scalar(S::DepthBias)&&!state.scalar(S::SlopeBias)&&!state.scalar(S::AlphaToMask),
         "Native movie bias or alpha-to-mask state is unsupported");
    need(state.scalar(S::MultisampleMask)==0xFFFFFFFF&&!state.scalar(S::TessellationMode),
         "Native movie sample-mask or tessellation profile is unqualified");
    need(state.scalar(S::GuardBandX)==0x3F800000&&state.scalar(S::GuardBandY)==0x3F800000&&
         !state.scalar(S::ClipPlaneEnable)&&!state.scalar(S::ScissorEnable)&&state.scalar(S::ViewportEnable)==1,
         "Native movie requires the original full-viewport guardband-one policy");
    for(auto id:{S::ExpandedBlend0,S::ExpandedBlend1,S::ExpandedBlend2,S::ExpandedBlend3})
        need(!state.scalar(id),"Native movie expanded-target mode remains unqualified");
    for(uint32_t id=uint32_t(S::Wrap0);id<=uint32_t(S::Wrap15);id+=4)
        need(!state.scalar(id),"Native movie interpolator wrapping is unsupported");
    result.retainedDepthWrite=state.scalar(S::DepthWrite)!=0;
    result.retainedDepthCompare=state.scalar(S::DepthCompare);
    for(uint32_t i=0;i<3;++i) {
        for(auto id:{T::AddressU,T::AddressV,T::AddressW})
            need(state.sampler(i,id)==0||state.sampler(i,id)==2,"Native movie supports only wrap/clamp addressing");
        need(state.sampler(i,T::MinimumMip)==0&&state.sampler(i,T::MaximumMip)==13&&
             !state.sampler(i,T::LodBiasBits)&&state.sampler(i,T::MaximumAnisotropy)==1,
             "Native movie mip/LOD/anisotropy state is unsupported");
        // These planes have exactly one mip and are sampled as 2D. The
        // retained mip and separate-Z requests cannot select another image.
        // Border color is inactive with wrap/clamp, but remains in state.after.
        result.samplers[i]={state.sampler(i,T::Minification),state.sampler(i,T::Magnification),state.sampler(i,T::MipFilter),
            state.sampler(i,T::AddressU),state.sampler(i,T::AddressV),state.sampler(i,T::AddressW),
            state.sampler(i,T::MaximumAnisotropy),state.sampler(i,T::MinimumMip),state.sampler(i,T::MaximumMip),0.0f};
    }
    return result;
}
}
