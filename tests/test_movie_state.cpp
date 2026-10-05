#include "renderer/movie_state.h"
#include <cstdio>
#include <stdexcept>
#include <vector>

using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void need(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
std::vector<uint32_t> snapshot(const EngineState& s) {
    std::vector<uint32_t> bytes;
    for(const auto& f:scalarStateEvidence())bytes.push_back(s.scalar(f.id));
    for(uint32_t i=0;i<16;++i)for(const auto& f:samplerStateEvidence())bytes.push_back(s.sampler(i,f.id));
    for(uint32_t i=0;i<4;++i)bytes.push_back(s.effectiveBlend(i));
    return bytes;
}
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const EngineStateError&){caught=true;}need(caught,"Unsupported movie state accepted");}
EngineState profile() {
    using S=ScalarState;auto s=EngineState::fromOriginalStartup();
    s.setScalar(S::GuardBandX,0x3F800000);s.setScalar(S::GuardBandY,0x3F800000);
    s.setScalar(S::MultisampleMask,0xFFFFFFFF);s.setScalar(S::TessellationMode,0);
    s.setScalar(S::HalfPixelOffset,0);s.setScalar(S::Cull,6);s.setScalar(S::AlphaTest,1);
    s.setScalar(S::BlendEnable,1);s.setScalar(S::SeparateAlpha,1);
    s.setScalar(S::BlendConstant,0x7F112233);
    for(uint32_t i=0;i<16;++i) {
        s.setSampler(i,SamplerState::AddressU,i&1?2:0);
        s.setSampler(i,SamplerState::AddressV,i&1?0:2);
        s.setSampler(i,SamplerState::AddressW,2);
        s.setSampler(i,SamplerState::Minification,0);s.setSampler(i,SamplerState::Magnification,0);
    }
    return s;
}
}
int main() {
    try {
        rejects([]{prepareMovieState(EngineState{});});
        using S=ScalarState;using T=SamplerState;
        for(uint32_t write=0;write<2;++write)for(uint32_t compare=0;compare<8;++compare) {
            auto before=profile();before.setScalar(S::DepthWrite,write);before.setScalar(S::DepthCompare,compare);
            const auto saved=snapshot(before);const auto movie=prepareMovieState(before);const auto& after=movie.after;
            need(snapshot(before)==saved,"Preparing movie state changed caller state");
            for(const auto& f:scalarStateEvidence()) {
                const auto expected=f.id==0x144?1u:(f.id==0x28||f.id==0x38||f.id==0x60?0u:before.scalar(f.id));
                need(after.scalar(f.id)==expected,"Movie altered an unrelated original scalar request");
            }
            for(uint32_t i=0;i<16;++i)for(const auto& f:samplerStateEvidence()) {
                const auto expected=i<3&&(f.id==0x10||f.id==0x14)?1u:before.sampler(i,f.id);
                need(after.sampler(i,f.id)==expected,"Movie altered an unrelated sampler request/stage");
            }
            for(uint32_t i=0;i<4;++i)need(after.effectiveBlend(i)==(i?before.effectiveBlend(i):0x10001u),"Movie altered another target's packed blend");
            need(movie.retainedDepthWrite==(write!=0)&&movie.retainedDepthCompare==compare,"Inactive depth requests were discarded");
            for(uint32_t i=0;i<3;++i) {
                const auto& sample=movie.samplers[i];
                need(sample.minification==1&&sample.magnification==1&&sample.addressU==before.sampler(i,T::AddressU)&&
                    sample.addressV==before.sampler(i,T::AddressV)&&sample.addressW==2&&sample.mipFilter==2&&
                    sample.minimumMip==0&&sample.maximumMip==13&&sample.maximumAnisotropy==1&&sample.lodBias==0,
                    "Movie sampler snapshot lost per-stage retained state");
            }
        }
        for(const auto bad:std::array<std::pair<S,uint32_t>,10>{{{S::StencilEnable,1},{S::ColorMask0,7},
            {S::GuardBandX,0x40000000},{S::GuardBandY,0x40000000},{S::ExpandedBlend0,1},
            {S::ExpandedBlend1,1},{S::ExpandedBlend2,1},{S::AlphaToMask,1},
            {S::MultisampleMask,0xFFFF},{S::TessellationMode,1}}}) {
            auto s=profile();s.setScalar(bad.first,bad.second);const auto saved=snapshot(s);
            rejects([&]{prepareMovieState(s);});need(snapshot(s)==saved,"Rejected movie state changed caller state");
        }
        for(uint32_t i=0;i<3;++i)for(auto field:{T::AddressU,T::AddressV,T::AddressW}) {
            auto s=profile();s.setSampler(i,field,3);const auto saved=snapshot(s);
            rejects([&]{prepareMovieState(s);});need(snapshot(s)==saved,"Rejected movie sampler changed caller state");
        }
        std::printf("PASS original movie state: %zu checks; exact four scalar and six filter writes, target0 packed blend, retained other state, rejection without mutation; no draw claim\n",checks);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL movie state after%zu checks: %s\n",checks,e.what());return 1;}
}
