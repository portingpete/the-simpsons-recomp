#include "common/geometry_extent.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {
size_t checks{};
void need(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
}
int main() {
    try {
        using namespace Simpsons;
        // Every representable stride can own65536 vertices. For each one,
        // verify its actual maximum fully fetched record count under the original
        // resource byte field, without allocating a huge synthetic buffer.
        for(uint32_t stride=4;stride<=1020;stride+=4) {
            need(validOriginalVertexExtent(size_t{65536}*stride,stride),"Valid65536 owner rejected");
            const auto count=originalGeometryMaxVertexBytes/stride;
            need(validOriginalVertexExtent(count*stride,stride),"Largest complete original extent rejected");
            need(!validOriginalVertexExtent((count+1)*stride,stride),"Original upper-byte alias admitted");
            need(!validOriginalVertexExtent(count*stride-1,stride),"Truncated original record admitted");
            need(originalFetchedVertexCount(count*stride,stride,stride)==count,"Owned full-stride fetch count differs");
            need(originalFetchedVertexCount(originalGeometryMaxVertexBytes,stride,stride)==count,
                 "Unused partial tail changed the fully fetched record count");
        }
        need(validOriginalVertexExtent(0x1C0000,28),"Concrete sky owner extent rejected");
        for(const auto bytes:std::array<size_t,6>{0,1,27,29,0x04000000,std::numeric_limits<size_t>::max()})
            need(!validOriginalVertexExtent(bytes,28),"Malformed sky owner extent admitted");
        for(const uint32_t stride:std::array<uint32_t,5>{0,1,27,1024,UINT32_MAX})
            need(!validOriginalVertexExtent(0x1C0000,stride),"Malformed original stride admitted");
        need(validOriginalVertexExtent(65536*28+12,28)&&originalFetchedVertexCount(65536*28+12,28,28)==65536,
             "Valid unused partial tail rejected or acquired another vertex");
        // A stride includes padding. Four position/UV rows need only their
        // last consumed byte, not all the last record's unused padding.
        need(originalFetchedVertexCount(3*64+28,64,28)==4,"Original final padding became a whitelist");
        need(originalFetchedVertexCount(3*64+24,64,28)==3,"Missing final selected attribute acquired a vertex");
        need(originalFetchedVertexCount(3*64+20,64,20)==4,"Consumed-input-specific extent was ignored");
        for(const size_t end:std::array<size_t,3>{0,65,std::numeric_limits<size_t>::max()})
            need(!originalFetchedVertexCount(3*64+28,64,end),"Malformed consumed attribute extent was admitted");
        // All six current decoded host layouts: sky, mono, shadow, rigid,
        // zprepass and skin. These bounds depend on bytes, not index width.
        for(const size_t width:std::array<size_t,6>{28,44,52,68,116,160}) {
            need(validNativeMeshBufferExtent(65536,width),"Valid native65536 owner rejected");
            const auto maxCount=std::min(nativeMeshMaxBufferElements,size_t{UINT32_MAX}/width);
            need(validNativeMeshBufferExtent(maxCount,width),"Representable native maximum rejected");
            need(!validNativeMeshBufferExtent(maxCount+1,width),"Native ByteWidth or element overflow admitted");
            need(!validNativeMeshBufferExtent(0,width),"Empty native owner admitted");
        }
        need(validNativeMeshBufferExtent(nativeMeshMaxBufferElements,2),"D3D R16 element maximum rejected");
        need(validNativeMeshBufferExtent(0x2000001,2),"Old64MiB index upload whitelist survived");
        need(!validNativeMeshBufferExtent(nativeMeshMaxBufferElements+1,2),"D3D element overflow admitted");
        for(const size_t bytes:std::array<size_t,6>{2,3,16*1024*1024+1,16*1024*1024+2,0x04000002,UINT32_MAX})
            need(originalFetchedIndexCount(bytes)==bytes/2,"Original full-word index extent or inert tail rejected");
        for(const size_t bytes:std::array<size_t,3>{0,1,std::numeric_limits<size_t>::max()})
            need(!originalFetchedIndexCount(bytes),"Index owner without a complete representable word admitted");
        need(!validNativeMeshBufferExtent(1,0),"Zero native element width admitted");
        need(!validNativeMeshBufferExtent(std::numeric_limits<size_t>::max(),160),"Native count multiplication overflow admitted");
        need(!validNativeMeshBufferExtent(2,std::numeric_limits<size_t>::max()),"Native element multiplication overflow admitted");
        std::printf("PASS geometry extents: %zu original byte-field/native ByteWidth checks; no vertex-count whitelist\n",checks);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL geometry extents: %s\n",error.what());return 1;}
}
