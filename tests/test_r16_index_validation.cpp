#include "renderer/r16_index_validation.h"
#include "renderer/r16_strip_chunks.h"
#include <array>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>
using Simpsons::Graphics::validR16DrawRange;
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try {
    constexpr std::array<uint16_t,9> shifted{65534,2,3,4,65535,4,3,5,65534};
    need(validR16DrawRange(shifted,4,1,7,-2),"Signed offset/restart or unselected words were rejected");
    need(!validR16DrawRange(shifted,4,1,7,-3),"Selected effective underflow was accepted");
    need(!validR16DrawRange(shifted,4,1,7,-1),"Selected effective overrun was accepted");
    constexpr std::array<uint16_t,9> positive{65535,0,1,2,65535,2,1,3,65535};
    need(validR16DrawRange(positive,6,1,7,2),"Positive offset into its owned records was rejected");
    need(!validR16DrawRange(positive,6,1,7,3),"Positive offset beyond its owner was accepted");
    constexpr std::array<uint16_t,6> selected{65534,0,1,2,3,65534};
    need(validR16DrawRange(selected,4,1,4,0),"Unselected opaque words were validated as drawn indices");
    need(!validR16DrawRange(selected,4,0,4,0),"Selected opaque word outside its owner was accepted");
    for(int32_t extreme:{std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max()})
        need(!validR16DrawRange(positive,6,1,7,extreme),"Extreme signed offset bypassed widened ownership bounds");
    constexpr std::array<uint16_t,3> cuts{65535,65535,65535};
    need(validR16DrawRange(cuts,4,0,3,std::numeric_limits<int32_t>::min())&&
         validR16DrawRange(cuts,4,0,3,std::numeric_limits<int32_t>::max()),"Restart markers were offset into vertex accesses");
    need(!validR16DrawRange(cuts,0,0,3,0),"Empty vertex owner was accepted");
    for(uint32_t count=0;count<3;++count)
        need(validR16DrawRange(positive,6,1,count,2),"Valid original no-triangle strip range was rejected");
    need(validR16DrawRange(positive,6,uint32_t(positive.size()),0,std::numeric_limits<int32_t>::min()),
         "Empty selected range fetched a vertex or rejected its retained owner boundary");
    need(!validR16DrawRange({},6,0,0,0)&&!validR16DrawRange(positive,0,0,0,0),
         "Zero selected count admitted a missing index or vertex owner");
    need(!validR16DrawRange(positive,6,uint32_t(positive.size()+1),0,0)&&
         !validR16DrawRange(positive,6,uint32_t(positive.size()),1,0)&&
         !validR16DrawRange(positive,6,1,1,-1)&&!validR16DrawRange(positive,6,1,2,6),
         "Short selected range bypassed actual index or effective vertex ownership");
    need(!validR16DrawRange(positive,6,8,3,2)&&
         !validR16DrawRange(positive,6,UINT32_MAX,3,2)&&
         !validR16DrawRange(positive,6,UINT32_MAX,UINT32_MAX,2),"Selected index extent overflow was accepted");
    constexpr std::array<uint16_t,3> last{65534,65534,65535};
    need(validR16DrawRange(last,65535,0,3,0)&&!validR16DrawRange(last,65535,0,3,1),"R16 upper bound/restart differs");
    // For fixed selected minimum2/maximum5 and four owned vertices, the full
    // safe interval is exactly base=-2. Exhaust the neighboring signed range.
    for(int32_t base=-65535;base<=65535;++base)
        need(validR16DrawRange(shifted,4,1,7,base)==(base==-2),"Exhaustive effective base interval differs");
    for(const uint32_t count:{0u,1u,2u,65534u,65535u,65536u,131067u,131068u,0x08000000u}) {
        std::vector<std::array<uint32_t,2>> packets;
        Simpsons::Graphics::forEachOriginalR16StripChunk(1,count,[&](uint32_t n,uint32_t start){packets.push_back({n,start});});
        need(!packets.empty()&&packets.front()[1]==1,"Original packet lost its selected start or count0 packet");
        uint64_t covered{};
        for(size_t i=0;i<packets.size();++i) {
            need(packets[i][0]<=65535&&uint64_t(packets[i][1])+packets[i][0]<=uint64_t(count)+1,
                 "SDK split packet escaped the complete selected owner interval");
            covered+=packets[i][0];
            if(i+1<packets.size())need(packets[i][0]==65534&&packets[i+1][1]==packets[i][1]+65532,
                                     "SDK split lost its even chunk or two-word overlap");
        }
        need(covered-2*(packets.size()-1)==count,"SDK split lost or added selected words");
        if(count==65536)need(packets==std::vector<std::array<uint32_t,2>>{{65534,1},{4,65533}},
                            "Pinned original65536 packets differ");
    }
    // The bounds-carrying container (whole-buffer min/max, scanned only when it cannot vouch for a
    // draw) must agree with the plain per-draw scan for every start, count, vertex count and base.
    {
        uint32_t seed=0x1234567;const auto next=[&]{seed=seed*1664525u+1013904223u;return seed>>8;};
        for(int round=0;round<4000;++round) {
            std::vector<uint16_t> words(1+next()%24);
            for(auto& word:words)word=(next()%5==0)?uint16_t(0xFFFF):uint16_t(next()%12);
            Simpsons::Graphics::R16Indices owned;owned.assign(words.begin(),words.end());
            for(int query=0;query<30;++query) {
                const uint32_t vertices=next()%16,start=next()%(words.size()+2),count=next()%(words.size()+2);
                const int32_t base=int32_t(next()%40)-20;
                need(validR16DrawRange(owned,vertices,start,count,base)==
                     validR16DrawRange(std::span<const uint16_t>(words),vertices,start,count,base),
                     "Bounds-carrying index form differs from the plain scan");
            }
        }
    }
    std::puts("PASS R16 selected effective indices: signed offsets, restartFFFF, unselected words, extents, extreme offsets and complete safe base interval");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL R16 selected index validation: %s\n",e.what());return 1;}}
