#include "runtime/guest_chain.h"
#include <cstdio>
#include <stdexcept>

void need(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
int main() {
    try {
        // Exhaust every short prefix/cycle combination, including self links,
        // a cycle through the head and cycles preceded by an acyclic prefix.
        for(uint32_t prefix=0;prefix<32;++prefix)for(uint32_t cycle=1;cycle<=32;++cycle) {
            Simpsons::GuestChainWalk<256> walk;
            uint32_t node=1,steps=0;
            while(walk.visit(node)) {
                ++steps;need(steps<256,"Cycle was not detected before the independent bound");
                node=node==prefix+cycle?prefix+1:node+1;
            }
            need(steps>=prefix+cycle,"Unique prefix was mistaken for a cycle");
        }
        Simpsons::GuestChainWalk<256> plugin;
        for(uint32_t i=1;i<=256;++i)need(plugin.visit(i*64),"Valid full-size plugin chain rejected");
        need(!plugin.visit(257*64),"Oversized plugin chain accepted");
        Simpsons::GuestChainWalk<65536> rasters;
        for(uint32_t i=1;i<=65536;++i)need(rasters.visit(0x100000+i*8),"Valid full-size raster chain rejected");
        need(!rasters.visit(0x100000+65537*8),"Oversized raster chain accepted");
        Simpsons::GuestChainWalk<1> one;
        need(!one.visit(0)&&one.visit(0xffffffff)&&!one.visit(1),"Null/address/boundary handling differs");
        std::puts("PASS bounded guest chains: 1024 cycle shapes, exact 256/65536 limits, null and high addresses");
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"Guest chain failure: %s\n",error.what());return 1;}
}
