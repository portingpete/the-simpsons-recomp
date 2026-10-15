#include "renderer/skin_detail.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>

using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void need(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
using Float3=std::array<float,3>;
Float3 sub(Float3 a,Float3 b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
Float3 cross(Float3 a,Float3 b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
float dot(Float3 a,Float3 b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
SkinVertex vertex(Float3 position,Float3 normal,float bone=1) {
    SkinVertex v{};v.position=position;v.normal=normal;v.indices={bone,0,0,0};v.weights={1,0,0,0};
    v.color={1,1,1,1};v.uv={position[0],position[1]};return v;
}
Float3 faceNormal(const SkinDetailGeometry& g,size_t triangle) {
    const auto& a=g.vertices[g.indices[3*triangle]].position;
    return cross(sub(g.vertices[g.indices[3*triangle+1]].position,a),sub(g.vertices[g.indices[3*triangle+2]].position,a));
}

// A flat strip with perpendicular normals stays flat; every segment count
// gives n*n triangles per source triangle with the source winding.
void flatStrip() {
    const std::vector<SkinVertex> v{vertex({0,0,0},{0,0,1}),vertex({1,0,0},{0,0,1}),vertex({0,1,0},{0,0,1}),vertex({1,1,0},{0,0,1})};
    const std::vector<uint16_t> strip{0,1,2,3};
    const auto smoothing=skinSmoothingNormals(v);
    for(uint32_t n=2;n<=4;++n) {
        const auto g=buildSkinDetail(v,smoothing,strip,0,4,0,n);
        need(g.sourceTriangles==2&&g.indices.size()==size_t(2*n*n*3),"Flat strip triangle count");
        // Shared edge 1-2 and the corners are reused, so vertices are the
        // (n+1)^2 lattice of the square.
        need(g.vertices.size()==size_t((n+1)*(n+1)),"Flat strip did not share its diagonal or corners");
        for(const auto& out:g.vertices) {
            need(out.position[2]==0&&out.normal==Float3{0,0,1},"Flat strip left its plane");
            need(std::abs(out.uv[0]-out.position[0])<1e-6f&&std::abs(out.uv[1]-out.position[1])<1e-6f,"Flat strip UV is not linear");
        }
        for(size_t t=0;t<g.indices.size()/3;++t)need(faceNormal(g,t)[2]>0,"Flat strip winding changed");
    }
}

// Strip parity, restart and degenerate stitching.
void stripOrder() {
    std::vector<SkinVertex> v;
    for(int i=0;i<8;++i)v.push_back(vertex({float(i/2),float(i%2),0},{0,0,1}));
    // (0,1,2),(2,1,3) | restart | (4,5,6),(6,5,7): odd strip triangles swap
    // their first two vertices, so all four face the same side.
    const std::vector<uint16_t> strip{0,1,2,3,0xFFFF,4,5,6,7};
    const auto smoothing=skinSmoothingNormals(v);
    const auto g=buildSkinDetail(v,smoothing,strip,0,uint32_t(strip.size()),0,2);
    need(g.sourceTriangles==4,"Strip triangles or restart differ");
    for(size_t t=0;t<g.indices.size()/3;++t)need(faceNormal(g,t)[2]<0,"Strip parity winding is inconsistent");
    // Degenerate stitching draws nothing but still advances the parity, as
    // the GPU does: 4,4,5,6 yields only (5,4,6), facing the other side.
    const std::vector<uint16_t> stitched{4,4,5,6};
    const auto d=buildSkinDetail(v,smoothing,stitched,0,4,0,2);
    need(d.sourceTriangles==1&&faceNormal(d,0)[2]>0,"Degenerate strip triangle was drawn or kept the wrong parity");
    // A sub-range starts its own strip.
    const auto tail=buildSkinDetail(v,smoothing,strip,1,3,0,3);
    need(tail.sourceTriangles==1&&tail.indices.size()==9*3,"Sub-range did not start a new strip");
    // A sub-range drawn with baseVertex offsets selects later vertices.
    const std::vector<uint16_t> shifted{0,1,2};
    const auto offset=buildSkinDetail(v,smoothing,shifted,0,3,4,2);
    need(offset.sourceTriangles==1&&offset.vertices[0].position==v[4].position,"Base vertex was not applied");
    bool rejected=false;try{buildSkinDetail(v,smoothing,shifted,0,3,6,2);}catch(const std::invalid_argument&){rejected=true;}
    need(rejected,"Out-of-range base vertex was accepted");
}

// A curved patch: an octahedron face fan with radial normals bulges outward,
// shared edges are identical for neighbouring triangles and for UV-seam
// duplicates, and positions stay finite.
void curvedSurface() {
    const Float3 top{0,0,1},px{1,0,0},py{0,1,0},nx{-1,0,0};
    std::vector<SkinVertex> v{vertex(top,top),vertex(px,px),vertex(py,py),vertex(nx,nx),
                              vertex(py,py)}; // 4: seam duplicate of 2 with another UV
    v[4].uv={9,9};
    const std::vector<uint16_t> fan{0,1,2,0xFFFF,0,4,3};
    const auto smoothing=skinSmoothingNormals(v);
    const auto g=buildSkinDetail(v,smoothing,fan,0,uint32_t(fan.size()),0,4);
    need(g.sourceTriangles==2&&g.indices.size()==2*16*3,"Curved fan triangle count");
    size_t bulged=0;
    for(const auto& out:g.vertices) {
        for(float c:out.position)need(std::isfinite(c),"Curved patch is not finite");
        const float radius=std::sqrt(dot(out.position,out.position));
        // Flat faces would put interior points at radius < 1; PN lifts them.
        if(radius>0.75f)++bulged;
        need(std::abs(std::sqrt(dot(out.normal,out.normal))-1)<1e-4f,"Curved normal is not unit length");
    }
    need(bulged==g.vertices.size(),"PN patch did not lift towards the sphere");
    // Edge 0-2 (first triangle) and edge 0-4 (seam duplicate) carry the same curve.
    std::map<std::array<uint32_t,3>,int> positions;
    for(const auto& out:g.vertices) {
        if(out.position[0]!=0||out.position[1]<=0||out.position[2]<=0)continue;
        std::array<uint32_t,3> bits{};std::memcpy(bits.data(),out.position.data(),12);++positions[bits];
    }
    size_t shared=0;for(const auto& [_,count]:positions)if(count==2)++shared;
    need(shared==3,"Seam duplicates do not share bitwise-identical edge points");
}

// Bone influences blend linearly, keep the four strongest and keep their total.
void boneBlend() {
    auto a=vertex({0,0,0},{0,0,1});a.indices={1,2,3,4};a.weights={.4f,.3f,.2f,.1f};
    auto b=vertex({1,0,0},{0,0,1});b.indices={5,6,1,7};b.weights={.5f,.25f,.15f,.1f};
    auto c=vertex({0,1,0},{0,0,1});c.indices={8,0,0,0};c.weights={1,0,0,0};
    const std::vector<SkinVertex> v{a,b,c};const std::vector<uint16_t> tri{0,1,2};
    const auto g=buildSkinDetail(v,skinSmoothingNormals(v),tri,0,3,0,2);
    bool found=false;
    for(const auto& out:g.vertices) {
        float total=0;for(float w:out.weights)total+=w;
        need(std::abs(total-1)<1e-5f,"Blended weights lost their total");
        for(float bone:out.indices)need(bone>=0&&bone<=63&&std::floor(bone)==bone,"Blended bone index is not an original index");
        need(out.weights[0]>=out.weights[1]&&out.weights[1]>=out.weights[2]&&out.weights[2]>=out.weights[3],"Influences are not strongest first");
        if(out.position[1]==0&&out.position[0]>.4f&&out.position[0]<.6f) {
            // Midpoint of a-b: bone1 .4/2+.15/2=.275, bone5 .25, bone2 .15, bone6 .125.
            found=true;
            need(out.indices[0]==1&&out.indices[1]==5&&out.indices[2]==2&&out.indices[3]==6,"Midpoint kept the wrong bones");
        }
    }
    need(found,"Edge midpoint missing");
}

// Hard edges weld: duplicates at one position share the averaged normal.
void smoothingWeld() {
    const std::vector<SkinVertex> v{vertex({0,0,0},{1,0,0}),vertex({0,0,0},{0,1,0}),vertex({-0.0f,0,0},{0,0,1}),vertex({1,0,0},{0,0,1})};
    const auto s=skinSmoothingNormals(v);
    const float k=1/std::sqrt(3.0f);
    for(size_t i=0;i<3;++i)need(std::abs(s[i][0]-k)<1e-6f&&std::abs(s[i][1]-k)<1e-6f&&std::abs(s[i][2]-k)<1e-6f,"Hard edge normals did not weld");
    need(s[3]==Float3{0,0,1},"Separate vertex normal changed");
    need(skinDetailSegments(0)==1&&skinDetailSegments(1)==2&&skinDetailSegments(3)==4&&skinDetailSegments(9)==1,"Detail level mapping differs");
}
}

// The worker builds the same geometry as a direct call, computes the mesh's
// smoothing normals once, reports bad ranges as errors and can be destroyed
// with jobs still queued.
void worker() {
    auto source=std::make_shared<SkinDetailSource>();
    source->vertices={vertex({0,0,1},{0,0,1}),vertex({1,0,0},{1,0,0}),vertex({0,1,0},{0,1,0}),vertex({-1,0,0},{-1,0,0})};
    source->indices={0,1,2,3};
    const auto direct=buildSkinDetail(source->vertices,skinSmoothingNormals(source->vertices),source->indices,0,4,0,3);
    std::vector<std::shared_ptr<SkinDetailJob>> jobs;
    {
        SkinDetailWorker builder;
        for(int32_t base:{0,0,5}) {
            auto job=std::make_shared<SkinDetailJob>();job->source=source;job->startIndex=0;job->indexCount=4;job->baseVertex=base;job->segments=3;
            builder.submit(job);jobs.push_back(job);
        }
        for(const auto& job:jobs)SkinDetailWorker::wait(*job);
        need(source->smoothing.size()==source->vertices.size(),"Worker did not keep the mesh smoothing normals");
        for(size_t i=0;i<2;++i) {
            const auto& g=jobs[i]->geometry;
            need(jobs[i]->error.empty()&&g.indices==direct.indices&&g.vertices.size()==direct.vertices.size()&&
                 std::memcmp(g.vertices.data(),direct.vertices.data(),g.vertices.size()*sizeof(SkinVertex))==0,"Worker geometry differs from a direct build");
        }
        need(!jobs[2]->error.empty()&&jobs[2]->geometry.indices.empty(),"Worker accepted an out-of-range job");
        for(int i=0;i<64;++i){auto job=std::make_shared<SkinDetailJob>();job->source=source;job->indexCount=4;job->segments=4;builder.submit(job);}
    }
}
int main() try {
    flatStrip();stripOrder();curvedSurface();boneBlend();smoothingWeld();worker();
    std::printf("PASS character detail smoothing: %zu checks\n",checks);
    return 0;
} catch(const std::exception& error) {
    std::fprintf(stderr,"FAIL %s\n",error.what());return 1;
}
