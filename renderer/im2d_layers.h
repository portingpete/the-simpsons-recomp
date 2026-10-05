#pragma once
#include "native_backend.h"
#include <algorithm>
#include <limits>

namespace Simpsons::Graphics {
// CPU-only preparation storage. Every packet recomputes the same conservative
// dependency layers; capacity survives, but no packet/resource result is reused.
class Im2DLayerStorage {
    friend struct Im2DLayerStorageProbe;
    std::array<uint64_t,160*90> tiles{};
    uint32_t generation{};
    std::vector<UINT> assigned;
    UINT previous(size_t at) const {
        const auto value=tiles[at];
        return uint32_t(value>>32)==generation?UINT(value):0;
    }
public:
    struct Layer {D3D11_BOX bounds{};UINT first=0,count=0;};
    std::vector<D3D11_BOX> triangles;
    std::vector<Layer> layers;
    std::vector<UINT> order;
    bool active=false;
    void beginPacket() {triangles.clear();layers.clear();order.clear();}
    void prepare(UINT width,UINT height) {
        if(!width || !height || width>16384 || height>16384 || triangles.size()>9362)
            throw Error("Native Im2D layer preparation exceeds its bounded profile");
        layers.clear();order.clear();assigned.resize(triangles.size());
        // A tile from any earlier packet has effective layer zero. The sole
        // full clear is counter wrap, preventing epoch-one data from resurfacing.
        if(++generation==0){tiles.fill(0);generation=1;}
        const uint64_t stamp=uint64_t(generation)<<32;
        const UINT cellWidth=std::max(8u,(width+159)/160);
        const UINT cellHeight=std::max(8u,(height+89)/90);
        const UINT columns=(width+cellWidth-1)/cellWidth;
        uint64_t work=0;bool sequential=false;
        for(UINT triangle=0;triangle<triangles.size();++triangle) {
            const auto& box=triangles[triangle];
            if(box.left>=box.right || box.top>=box.bottom || box.right>width || box.bottom>height)
                throw Error("Native Im2D layer bounds exceed the selected target");
            const UINT left=box.left/cellWidth,right=(box.right-1)/cellWidth;
            const UINT top=box.top/cellHeight,bottom=(box.bottom-1)/cellHeight;
            const uint64_t cells=uint64_t(right-left+1)*(bottom-top+1);
            if(work+2*cells>1000000)sequential=true;
            UINT layer=1;
            if(sequential)layer=UINT(layers.size()+1);
            else {
                work+=2*cells;
                for(UINT y=top;y<=bottom;++y)for(UINT x=left;x<=right;++x)
                    layer=std::max(layer,previous(size_t(y)*columns+x)+1);
                for(UINT y=top;y<=bottom;++y)for(UINT x=left;x<=right;++x)
                    tiles[size_t(y)*columns+x]=stamp|layer;
            }
            if(layer>layers.size())layers.resize(layer);
            auto& group=layers[layer-1];assigned[triangle]=layer-1;
            if(!group.count)group.bounds=box;
            else {
                group.bounds.left=std::min(group.bounds.left,box.left);
                group.bounds.top=std::min(group.bounds.top,box.top);
                group.bounds.right=std::max(group.bounds.right,box.right);
                group.bounds.bottom=std::max(group.bounds.bottom,box.bottom);
            }
            ++group.count;
        }
        order.resize(triangles.size());UINT first=0;
        for(auto& layer:layers){layer.first=first;first+=layer.count;layer.count=0;}
        for(UINT triangle=0;triangle<assigned.size();++triangle) {
            auto& layer=layers[assigned[triangle]];
            order[layer.first+layer.count++]=triangle;
        }
    }
};
}
