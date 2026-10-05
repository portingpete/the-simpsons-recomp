#include "renderer/native_material_compiler.h"
#include "header/native_translated_materials.h"
#include <fstream>
#include <iterator>
#include <cstdio>
#include <bit>
#include <cstring>
#include <limits>

using namespace Simpsons::Graphics;
void require(bool value,const char* reason) {if(!value) throw MaterialError(reason);}
int main(int argc,char** argv) {
    try {
        if(argc<2 || argc>3 || (argc==3 && std::string(argv[2])!="--hardware"))
            throw MaterialError("Supply original image path and optional --hardware");
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
        require(bytes.size()==15466496,"Wrong original image size");
        MaterialRegistry owner;
        NativeBackend backend(argc!=3);NativeMaterialCompiler compiler(backend);
        size_t compiled=0,unsupported=0;
        for(const auto& original:originalMaterialIdentities()) {
            auto id=owner.create(original.originalAddress,std::span<const uint8_t>(bytes).subspan(original.originalAddress-0x82000000,original.recordBytes));
            require(owner.capability(id)==MaterialCapability::Uncompiled,"Creation falsely claimed native compilation");
            if(nativeTranslatedMaterial(original.originalAddress)) {
                const auto* artifact=&owner.prepareForBind(id,compiler);
                require(artifact->originalAddress()==original.originalAddress && artifact->stage()==original.stage,"Wrong native material artifact");
                require(owner.capability(id)==MaterialCapability::Compiled,"Successful native object creation not published");
                require(&owner.prepareForBind(id,compiler)==artifact,"Compiled native artifact was recreated");
                ++compiled;
            } else {
                bool failed=false;
                try {owner.prepareForBind(id,compiler);} catch(const UnsupportedMaterial&) {failed=true;}
                require(failed && owner.capability(id)==MaterialCapability::Unsupported,"Untranslated material accepted at native bind");
                failed=false;
                try {owner.requireCompiled(id);} catch(const UnsupportedMaterial&) {failed=true;}
                require(failed,"Unsupported resource became compiled");++unsupported;
            }
            owner.release(id);require(!owner.contains(id),"Released artifact ID remained live");
        }
        const auto expected=nativeTranslatedMaterials.size();
        if(compiled!=expected || unsupported+compiled!=originalMaterialIdentities().size())
            std::fprintf(stderr,"Native material population: compiled=%zu unsupported=%zu\n",compiled,unsupported);
        require(compiled==expected && unsupported+compiled==originalMaterialIdentities().size() && owner.liveCount()==0,
                "Wrong native material population or ownership leak");
        std::array<MaterialId,2> edge{};
        for(const auto& original:originalMaterialIdentities()) {
            if(original.originalAddress!=0x8202E6F0 && original.originalAddress!=0x8202E840)continue;
            const auto index=original.stage==MaterialStage::Vertex?0u:1u;
            edge[index]=owner.create(original.originalAddress,std::span<const uint8_t>(bytes).subspan(original.originalAddress-0x82000000,original.recordBytes));
        }
        const auto& vertex=owner.prepareForBind(edge[0],compiler);const auto& pixel=owner.prepareForBind(edge[1],compiler);
        backend.bindEdgeShaders(vertex,pixel);backend.requireEdgeShaders(vertex,pixel);
        bool rejected=false;try{backend.bindEdgeShaders(pixel,vertex);}catch(const Error&){rejected=true;}
        require(rejected,"Edge bind accepted reversed shader stages");backend.requireEdgeShaders(vertex,pixel);
        {NativeBackend other(true);rejected=false;try{other.bindEdgeShaders(vertex,pixel);}catch(const Error&){rejected=true;}
         require(rejected,"Edge bind accepted another device's shaders");}
        backend.requireEdgeShaders(vertex,pixel);for(auto id:edge)owner.release(id);
        require(owner.liveCount()==0,"Native edge compiler owner leaked");
        std::array<MaterialId,2> rigid{};
        for(const auto& original:originalMaterialIdentities()) {
            if(original.originalAddress!=0x8200D3AC&&original.originalAddress!=0x8200D9E8)continue;
            rigid[original.stage==MaterialStage::Vertex?0:1]=owner.create(original.originalAddress,
                std::span<const uint8_t>(bytes).subspan(original.originalAddress-0x82000000,original.recordBytes));
        }
        const auto& rv=owner.prepareForBind(rigid[0],compiler);const auto& rp=owner.prepareForBind(rigid[1],compiler);
        backend.bindRigidShaders(rv,rp);backend.requireRigidShaders(rv,rp);
        rejected=false;try{backend.bindRigidShaders(rp,rv);}catch(const Error&){rejected=true;}
        require(rejected,"Rigid bind accepted reversed stages");backend.requireRigidShaders(rv,rp);
        RigidVertexConstants vc{};RigidPixelConstants pc{};
        for(size_t row=0;row<vc.size();++row)for(size_t lane=0;lane<4;++lane)
            vc[row][lane]=float(int(row*4+lane)-37)*0.03125f;
        for(size_t row=0;row<pc.size();++row)for(size_t lane=0;lane<4;++lane)
            pc[row][lane]=float(int(row*4+lane)-143)*0.125f;
        vc[1][2]=std::bit_cast<float>(0x80000000u);pc[49][3]=std::bit_cast<float>(0x80000000u);
        auto committed=backend.commitRigid(rv,rp,vc,pc);
        auto vg=backend.readbackRigidVertexConstants(committed);auto pg=backend.readbackRigidPixelConstants(committed);
        require(!std::memcmp(vg.data(),vc.data(),sizeof(vc))&&!std::memcmp(pg.data(),pc.data(),sizeof(pc)),
            "Rigid GPU constant banks differ bitwise or overlap across stages");
        {NativeBackend other(true);rejected=false;try{other.bindRigidShaders(rv,rp);}catch(const Error&){rejected=true;}
         require(rejected,"Rigid bind accepted another device's shaders");
         rejected=false;try{other.requireRigidCommit(committed);}catch(const Error&){rejected=true;}
         require(rejected,"Rigid constants accepted another device");}
        auto bad=pc;bad[47][0]=std::numeric_limits<float>::infinity();
        rejected=false;try{backend.commitRigid(rv,rp,vc,bad);}catch(const Error&){rejected=true;}
        require(rejected,"Rigid commit accepted infinity");backend.requireRigidCommit(committed);
        bad[47][0]=std::numeric_limits<float>::quiet_NaN();
        rejected=false;try{backend.commitRigid(rv,rp,vc,bad);}catch(const Error&){rejected=true;}
        require(rejected,"Rigid commit accepted NaN");backend.requireRigidCommit(committed);
        vc[0][0]=91;auto next=backend.commitRigid(rv,rp,vc,pc);
        rejected=false;try{backend.requireRigidCommit(committed);}catch(const Error&){rejected=true;}
        require(rejected,"Rigid previous commit accepted stale bindings");backend.requireRigidCommit(next);
        vg=backend.readbackRigidVertexConstants(next);require(vg[0][0]==91,"Rigid replacement constants not uploaded");
        for(auto id:rigid)owner.release(id);
        backend.requireRigidCommit(next); // Native constants retain both actual shader owners.
        require(owner.liveCount()==0,"Native rigid compiler owner leaked");
        require(backend.presentationCount()==0 && backend.screenDrawCount()==0,"Shader object creation counted as a game frame");
        std::printf("%zu records created real native shader artifacts; %zu untranslated records rejected. Edge/rigid shader ownership and rigid GPU constant banks passed. No draws.\n",compiled,unsupported);
        return 0;
    } catch(const std::exception& e) {fprintf(stderr,"Native material compiler: %s\n",e.what());return 1;}
}
