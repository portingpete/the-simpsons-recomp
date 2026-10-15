#include "skin_detail.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <windows.h>

namespace Simpsons::Graphics {
namespace {
using Float3=std::array<float,3>;
Float3 add(Float3 a,Float3 b) {return {a[0]+b[0],a[1]+b[1],a[2]+b[2]};}
Float3 sub(Float3 a,Float3 b) {return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
Float3 scale(Float3 a,float s) {return {a[0]*s,a[1]*s,a[2]*s};}
float dot(Float3 a,Float3 b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Float3 normalized(Float3 a,Float3 fallback) {
    const float length=std::sqrt(dot(a,a));
    return length>1e-6f?scale(a,1/length):fallback;
}
// Exact position identity; +0 and -0 weld.
struct PositionKey {
    uint32_t x,y,z;
    bool operator==(const PositionKey&) const=default;
};
struct PositionHash {
    size_t operator()(const PositionKey& k) const noexcept {
        return size_t(k.x*0x9E3779B1u)^(size_t(k.y*0x85EBCA77u)<<1)^(size_t(k.z*0xC2B2AE3Du)<<2);
    }
};
uint32_t bits(float value) {return value==0.0f?0u:std::bit_cast<uint32_t>(value);}
PositionKey key(const SkinVertex& v) {return {bits(v.position[0]),bits(v.position[1]),bits(v.position[2])};}
bool samePosition(const SkinVertex& a,const SkinVertex& b) {return key(a)==key(b);}
// Canonical edge direction: by position first so seam duplicates (other
// indices, same positions) evaluate the same curve in the same order.
bool before(const SkinVertex& a,uint32_t ai,const SkinVertex& b,uint32_t bi) {
    const auto ka=key(a),kb=key(b);
    if(ka.x!=kb.x)return ka.x<kb.x;
    if(ka.y!=kb.y)return ka.y<kb.y;
    if(ka.z!=kb.z)return ka.z<kb.z;
    return ai<bi;
}
// PN-triangle edge control point next to pi on the edge towards pj.
Float3 control(Float3 pi,Float3 pj,Float3 ni) {
    const float w=dot(sub(pj,pi),ni);
    return scale(sub(add(scale(pi,2),pj),scale(ni,w)),1.0f/3);
}
Float3 edgePoint(Float3 x,Float3 nx,Float3 y,Float3 ny,float t) {
    const auto bxy=control(x,y,nx),byx=control(y,x,ny);
    const float s=1-t;
    return add(add(scale(x,s*s*s),scale(bxy,3*s*s*t)),add(scale(byx,3*s*t*t),scale(y,t*t*t)));
}
struct Patch {
    Float3 p0,p1,p2,b210,b120,b021,b012,b102,b201,b111;
    Patch(Float3 a,Float3 b,Float3 c,Float3 na,Float3 nb,Float3 nc):p0(a),p1(b),p2(c) {
        b210=control(a,b,na);b120=control(b,a,nb);b021=control(b,c,nb);
        b012=control(c,b,nc);b102=control(c,a,nc);b201=control(a,c,na);
        const auto e=scale(add(add(add(b210,b120),add(b021,b012)),add(b102,b201)),1.0f/6);
        const auto v=scale(add(add(a,b),c),1.0f/3);
        b111=add(e,scale(sub(e,v),0.5f));
    }
    Float3 evaluate(float u,float v,float w) const {
        Float3 r=add(add(scale(p0,u*u*u),scale(p1,v*v*v)),scale(p2,w*w*w));
        r=add(r,add(scale(b210,3*u*u*v),scale(b120,3*u*v*v)));
        r=add(r,add(scale(b201,3*u*u*w),scale(b021,3*v*v*w)));
        r=add(r,add(scale(b102,3*u*w*w),scale(b012,3*v*w*w)));
        return add(r,scale(b111,6*u*v*w));
    }
};
template<size_t N>void mix(std::array<float,N>& out,const std::array<float,N>& in,float weight) {
    for(size_t i=0;i<N;++i)out[i]+=in[i]*weight;
}
// Everything except the position: linear attributes and merged bone influences.
SkinVertex blend(std::span<const SkinVertex* const> source,std::span<const float> weights) {
    SkinVertex out{};
    struct Influence {float bone,weight;};
    std::array<Influence,12> influences{};size_t used=0;float total=0;size_t strongest=0;
    for(size_t k=0;k<source.size();++k) {
        const auto& v=*source[k];const float w=weights[k];
        if(w>weights[strongest])strongest=k;
        mix(out.normal,v.normal,w);mix(out.uv,v.uv,w);mix(out.color,v.color,w);mix(out.uv1,v.uv1,w);
        mix(out.morph1,v.morph1,w);mix(out.morph2,v.morph2,w);mix(out.morph3,v.morph3,w);
        mix(out.morph4,v.morph4,w);mix(out.morph5,v.morph5,w);mix(out.morph6,v.morph6,w);
        for(size_t lane=0;lane<4;++lane) {
            const float influence=v.weights[lane]*w;total+=influence;
            const auto match=std::find_if(influences.begin(),influences.begin()+used,[&](const Influence& i){return i.bone==v.indices[lane];});
            if(match!=influences.begin()+used)match->weight+=influence;
            else influences[used++]={v.indices[lane],influence};
        }
    }
    out.normal=normalized(out.normal,source[strongest]->normal);
    // Keep the four strongest influences, rescaled to the blended total.
    std::stable_sort(influences.begin(),influences.begin()+used,[](const Influence& a,const Influence& b){return a.weight>b.weight;});
    const size_t kept=std::min<size_t>(used,4);
    float keptTotal=0;for(size_t i=0;i<kept;++i)keptTotal+=influences[i].weight;
    const float rescale=keptTotal!=0?total/keptTotal:0;
    for(size_t lane=0;lane<4;++lane) {
        out.indices[lane]=lane<kept?influences[lane].bone:0;
        out.weights[lane]=lane<kept?influences[lane].weight*rescale:0;
    }
    return out;
}
}

std::vector<std::array<float,3>> skinSmoothingNormals(std::span<const SkinVertex> vertices) {
    std::unordered_map<PositionKey,Float3,PositionHash> sums;sums.reserve(vertices.size());
    for(const auto& v:vertices) {
        auto& sum=sums[key(v)];
        sum=add(sum,normalized(v.normal,{0,0,0}));
    }
    std::vector<Float3> result(vertices.size());
    for(size_t i=0;i<vertices.size();++i)result[i]=normalized(sums[key(vertices[i])],vertices[i].normal);
    return result;
}

SkinDetailGeometry buildSkinDetail(std::span<const SkinVertex> vertices,std::span<const std::array<float,3>> smoothing,
    std::span<const uint16_t> indices,uint32_t startIndex,uint32_t indexCount,int32_t baseVertex,uint32_t segments) {
    if(segments<2||segments>4)throw std::invalid_argument("Character detail needs 2..4 segments per edge");
    if(smoothing.size()!=vertices.size()||uint64_t(startIndex)+indexCount>indices.size())
        throw std::invalid_argument("Character detail range exceeds its mesh");
    SkinDetailGeometry out;
    const uint32_t n=segments;
    std::vector<uint32_t> compact(vertices.size(),UINT32_MAX);
    std::unordered_map<uint64_t,uint32_t> edges;
    const auto corner=[&](uint32_t original) {
        auto& slot=compact[original];
        if(slot==UINT32_MAX){slot=uint32_t(out.vertices.size());out.vertices.push_back(vertices[original]);}
        return slot;
    };
    // Vertex at `step` of n along original edge i->j, shared by every triangle
    // that uses the same two original vertices.
    const auto edgeVertex=[&](uint32_t i,uint32_t j,uint32_t step) {
        const bool forward=before(vertices[i],i,vertices[j],j);
        const uint32_t lo=forward?i:j,hi=forward?j:i,at=forward?step:n-step;
        const uint64_t id=(uint64_t(lo)<<36)|(uint64_t(hi)<<8)|at;
        if(const auto hit=edges.find(id);hit!=edges.end())return hit->second;
        const float t=float(at)/float(n);
        const SkinVertex* ends[]{&vertices[lo],&vertices[hi]};const float weights[]{1-t,t};
        auto v=blend(ends,weights);
        v.position=edgePoint(vertices[lo].position,smoothing[lo],vertices[hi].position,smoothing[hi],t);
        const auto index=uint32_t(out.vertices.size());out.vertices.push_back(v);edges.emplace(id,index);
        return index;
    };
    const auto triangle=[&](uint32_t a,uint32_t b,uint32_t c) {
        ++out.sourceTriangles;
        const Patch patch(vertices[a].position,vertices[b].position,vertices[c].position,smoothing[a],smoothing[b],smoothing[c]);
        // Lattice row r (0..n) from corner a, column q (0..r) from the b side
        // to the c side: weights a=(n-r)/n, b=(r-q)/n, c=q/n.
        std::array<uint32_t,15> lattice{};
        const auto at=[](uint32_t r,uint32_t q){return r*(r+1)/2+q;};
        for(uint32_t r=0;r<=n;++r)for(uint32_t q=0;q<=r;++q) {
            uint32_t index;
            if(r==0)index=corner(a);
            else if(r==n&&q==0)index=corner(b);
            else if(r==n&&q==n)index=corner(c);
            else if(q==0)index=edgeVertex(a,b,r);
            else if(q==r)index=edgeVertex(a,c,r);
            else if(r==n)index=edgeVertex(b,c,q);
            else {
                const float wa=float(n-r)/float(n),wb=float(r-q)/float(n),wc=float(q)/float(n);
                const SkinVertex* corners[]{&vertices[a],&vertices[b],&vertices[c]};const float weights[]{wa,wb,wc};
                auto v=blend(corners,weights);v.position=patch.evaluate(wa,wb,wc);
                index=uint32_t(out.vertices.size());out.vertices.push_back(v);
            }
            lattice[at(r,q)]=index;
        }
        // Both sub-triangle shapes keep the (a,b,c) orientation.
        for(uint32_t r=0;r<n;++r)for(uint32_t q=0;q<=r;++q) {
            out.indices.insert(out.indices.end(),{lattice[at(r,q)],lattice[at(r+1,q)],lattice[at(r+1,q+1)]});
            if(q<r)out.indices.insert(out.indices.end(),{lattice[at(r,q)],lattice[at(r+1,q+1)],lattice[at(r,q+1)]});
        }
    };
    // Original strip order: restart 0xFFFF begins a new strip; odd triangles
    // swap their first two vertices to keep one winding.
    uint32_t previous{},last{},length=0;
    for(uint32_t i=0;i<indexCount;++i) {
        const uint16_t raw=indices[startIndex+i];
        if(raw==0xFFFF){length=0;continue;}
        const int64_t effective=int64_t(raw)+baseVertex;
        if(effective<0||uint64_t(effective)>=vertices.size())throw std::invalid_argument("Character detail index exceeds its vertices");
        const auto v=uint32_t(effective);
        if(length>=2) {
            uint32_t a=previous,b=last;
            if((length-2)&1)std::swap(a,b);
            if(a!=b&&b!=v&&a!=v&&!samePosition(vertices[a],vertices[b])&&!samePosition(vertices[b],vertices[v])&&
               !samePosition(vertices[a],vertices[v]))triangle(a,b,v);
        }
        previous=last;last=v;++length;
    }
    return out;
}

struct SkinDetailWorker::State {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::shared_ptr<SkinDetailJob>> queue;
    bool stop=false;
    std::thread thread;
    void run() {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        SetThreadDescription(GetCurrentThread(),L"Simpsons character detail");
        for(;;) {
            std::shared_ptr<SkinDetailJob> job;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock,[&]{return stop||!queue.empty();});
                if(stop)return;
                job=std::move(queue.front());queue.pop_front();
            }
            try {
                auto& source=*job->source;
                if(source.smoothing.empty())source.smoothing=skinSmoothingNormals(source.vertices);
                job->geometry=buildSkinDetail(source.vertices,source.smoothing,source.indices,job->startIndex,job->indexCount,
                                              job->baseVertex,job->segments);
            } catch(const std::exception& error) {
                job->geometry={};job->error=error.what();
            }
            job->done.store(true,std::memory_order_release);job->done.notify_all();
        }
    }
};
SkinDetailWorker::SkinDetailWorker():state(std::make_unique<State>()) {
    state->thread=std::thread([s=state.get()]{s->run();});
}
SkinDetailWorker::~SkinDetailWorker() {
    {std::lock_guard lock(state->mutex);state->stop=true;}
    state->wake.notify_all();state->thread.join();
}
void SkinDetailWorker::submit(std::shared_ptr<SkinDetailJob> job) {
    {std::lock_guard lock(state->mutex);state->queue.push_back(std::move(job));}
    state->wake.notify_one();
}
}
