#pragma once
namespace Simpsons::Graphics {
struct Im2DLayerStorageProbe {
    static void wrapNext(Im2DLayerStorage& storage){storage.generation=UINT32_MAX;}
};
}
// Independent retained dense-grid algorithm from before storage reuse.
void im2dLayerStorageContracts() {
    using Layer=Im2DLayerStorage::Layer;
    struct Expected {std::vector<Layer> layers;std::vector<UINT> order;};
    const auto original=[](const std::vector<D3D11_BOX>& bounds,UINT width,UINT height) {
        Expected result;
        const UINT cellWidth=std::max(8u,(width+159)/160),cellHeight=std::max(8u,(height+89)/90);
        const UINT columns=(width+cellWidth-1)/cellWidth,rows=(height+cellHeight-1)/cellHeight;
        std::vector<UINT> previous(size_t(columns)*rows,0),assigned(bounds.size());
        uint64_t work=0;bool sequential=false;
        for(UINT triangle=0;triangle<bounds.size();++triangle) {
            const auto& box=bounds[triangle];
            const UINT left=box.left/cellWidth,right=(box.right-1)/cellWidth;
            const UINT top=box.top/cellHeight,bottom=(box.bottom-1)/cellHeight;
            const uint64_t cells=uint64_t(right-left+1)*(bottom-top+1);
            if(work+2*cells>1000000)sequential=true;
            UINT layer=1;
            if(sequential)layer=UINT(result.layers.size()+1);
            else {
                work+=2*cells;
                for(UINT y=top;y<=bottom;++y)for(UINT x=left;x<=right;++x)
                    layer=std::max(layer,previous[size_t(y)*columns+x]+1);
                for(UINT y=top;y<=bottom;++y)for(UINT x=left;x<=right;++x)
                    previous[size_t(y)*columns+x]=layer;
            }
            if(layer>result.layers.size())result.layers.resize(layer);
            auto& group=result.layers[layer-1];assigned[triangle]=layer-1;
            if(!group.count)group.bounds=box;
            else {
                group.bounds.left=std::min(group.bounds.left,box.left);
                group.bounds.top=std::min(group.bounds.top,box.top);
                group.bounds.right=std::max(group.bounds.right,box.right);
                group.bounds.bottom=std::max(group.bounds.bottom,box.bottom);
            }
            ++group.count;
        }
        result.order.resize(bounds.size());UINT first=0;
        for(auto& layer:result.layers){layer.first=first;first+=layer.count;layer.count=0;}
        for(UINT triangle=0;triangle<assigned.size();++triangle) {
            auto& layer=result.layers[assigned[triangle]];
            result.order[layer.first+layer.count++]=triangle;
        }
        return result;
    };
    Im2DLayerStorage actual;
    const auto compare=[&](UINT width,UINT height) {
        const auto expected=original(actual.triangles,width,height);
        actual.prepare(width,height);
        need(actual.order==expected.order && actual.layers.size()==expected.layers.size(),
            "Reused Im2D layers changed original triangle ordering/group count");
        static_assert(sizeof(Layer)==32);
        for(size_t i=0;i<actual.layers.size();++i)
            need(std::memcmp(&actual.layers[i],&expected.layers[i],sizeof(Layer))==0,
                "Reused Im2D layer changed snapshot bounds or triangle range");
    };
    // Seed epoch one, then force the true wrap branch over that stale storage.
    actual.triangles.assign(80,D3D11_BOX{0,0,0,1280,720,1});compare(1280,720);
    Im2DLayerStorageProbe::wrapNext(actual);actual.beginPacket();
    actual.triangles.push_back({0,0,0,8,8,1});compare(1280,720);
    uint32_t random=0xB81A5173;
    const auto next=[&] {random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    constexpr std::array<std::array<UINT,2>,7> extents={{{2,2},{19,7},{32,24},{640,720},{1280,720},{16384,16384},{159,89}}};
    for(UINT packet=0;packet<700;++packet) {
        actual.beginPacket();const auto [width,height]=extents[packet%extents.size()];
        const UINT count=packet%13==0?9360:packet%7==0?2:packet%5==0?0:next()%250;
        for(UINT triangle=0;triangle<count;++triangle) {
            const UINT x=next()%width,y=next()%height;
            const UINT right=x+1+next()%std::min(64u,width-x),bottom=y+1+next()%std::min(64u,height-y);
            actual.triangles.push_back({x,y,0,right,bottom,1});
        }
        if(packet%137==0)Im2DLayerStorageProbe::wrapNext(actual);
        compare(width,height);
    }
    // Large intersecting bounds reach the unchanged million-cell fallback.
    for(const auto [width,height]:extents) {
        actual.beginPacket();actual.triangles.assign(80,D3D11_BOX{0,0,0,width,height,1});compare(width,height);
    }
    actual.beginPacket();actual.triangles={{0,0,0,8,8,1},{0,0,0,33,24,1}};
    bool rejected=false;try{actual.prepare(32,24);}catch(const Error&){rejected=true;}
    need(rejected,"Invalid reused layer bounds were accepted");
    actual.beginPacket();actual.triangles={{1,1,0,7,7,1}};compare(32,24);
}
