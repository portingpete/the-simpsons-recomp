// Regression of original zero-submesh binding, unused pointer fields and later valid use.
// Whole original zero-bone rigid dispatch plus original pool/declaration/FX cleanup.
// Mapped material objects and borrowed driver resources retain the parent fixture's scope.
#define main OriginalRigidBaselineEntrypoint
#include "test_rigid_dual_pass.cpp"
#undef main
#include "effect_draw_cleanup_helpers.h"
#include "runtime/engine_recording.h"
#include <chrono>
#include <set>

extern "C" void __imp__sub_82701220(PPCContext&,uint8_t*);
PPC_FUNC(sub_82701220){forward(0x82701220,ctx,base,__imp__sub_82701220);}
extern "C" void __imp__sub_826FF250(PPCContext&,uint8_t*);
extern "C" void __imp__sub_826FE668(PPCContext&,uint8_t*);
extern "C" void __imp__sub_82700498(PPCContext&,uint8_t*);
PPC_FUNC(sub_826FF250){forward(0x826FF250,ctx,base,__imp__sub_826FF250);}
PPC_FUNC(sub_826FE668){forward(0x826FE668,ctx,base,__imp__sub_826FE668);}
PPC_FUNC(sub_82700498){forward(0x82700498,ctx,base,__imp__sub_82700498);}
// Existing native validation adapter; C++ linkage, production HostState guard.
void SimpsonsNativeRigidImmediateMeshEntry(PPCContext&,uint8_t*);

namespace {
std::filesystem::path rigidAuditPath;
DWORD fixtureOwnerThread{};uint32_t zeroAlpha{};std::string_view unusedMode="none";
// The destructor uses only the already-qualified fixed views. It restores the
// actual current PPC object; no TLS/currentContext or guest ownership is swapped.
struct HostRestore {
    uint32_t csr=PPCFPSCRRegister::getcsr();DWORD error=GetLastError();
    ~HostRestore() noexcept {PPCFPSCRRegister::restoreHostCSR(csr);SetLastError(error);}
};
// Only mapped metadata WORD locations are saved/restored. The unused targets
// are never dereferenced, freed or reused; original backing stays independently live.
struct UnusedFieldRestore {
    uint8_t* rows;uint8_t* collection;
    std::array<uint8_t,4> savedRows{},savedCollection{};
    UnusedFieldRestore(uint8_t* rowWord,uint8_t* collectionWord):rows(rowWord),collection(collectionWord) {
        std::memcpy(savedRows.data(),rows,savedRows.size());
        std::memcpy(savedCollection.data(),collection,savedCollection.size());
    }
    ~UnusedFieldRestore() noexcept {
        std::memcpy(rows,savedRows.data(),savedRows.size());
        std::memcpy(collection,savedCollection.data(),savedCollection.size());
    }
};
struct UnusedViews {uint32_t rows,collection;};
struct ProbeRestore {
    PPCContext& context;std::array<uint8_t,sizeof(PPCContext)> saved{};
    uint8_t* countAndView;uint8_t* finalRow;
    std::array<uint8_t,8> metadataBytes{};std::array<uint8_t,36> rowBytes{};
    ProbeRestore(PPCContext& c,uint8_t* metadataView,uint8_t* last)
        :context(c),countAndView(metadataView),finalRow(last) {
        std::memcpy(saved.data(),&context,sizeof(context));
        std::memcpy(metadataBytes.data(),countAndView,metadataBytes.size());
        std::memcpy(rowBytes.data(),finalRow,rowBytes.size());
    }
    ~ProbeRestore() noexcept {
        std::memcpy(countAndView,metadataBytes.data(),metadataBytes.size());
        std::memcpy(finalRow,rowBytes.data(),rowBytes.size());std::memcpy(&context,saved.data(),sizeof(context));
    }
};
void originalGroupPoolEvidence(uint8_t* base) {
    constexpr std::array<std::array<uint32_t,2>,39> pins={{{0x8282F4D0,0x3D6082E0},
        {0x8282F4D4,0x906B6F74},{0x8282F4D8,0x3D6082E0},{0x8282F4DC,0x908B6F78},{0x8282F4E0,0x4E800020},
        {0x82862068,0x3FC082D5},{0x82862078,0x409A000C},{0x8286207C,0x4BE2C775},
        {0x82862084,0x38800000},{0x82862088,0x4BFCD449},{0x8282F628,0x3BC00000},
        {0x8282F634,0x2B1F0000},{0x8282F644,0x2B04000C},{0x8282F654,0x4BBC9A6D},
        {0x8282F668,0x4BBC8321},{0x8282F684,0x93DF0010},{0x8282F690,0x4BFFFE59},
        {0x8282F6AC,0x4BBC9A15},{0x8282F6D0,0x4BFFFE89},{0x8282F6EC,0x4BBC99D5},{0x8282F6F8,0x48001B89},
        {0x82831280,0x81430014},{0x82831284,0x3963001C},{0x82831294,0x814B0004},
        {0x8283129C,0x814B0000},{0x828312A0,0x7D2A182E},{0x828312A8,0x7D291A14},
        {0x828312B0,0x7D292214},{0x828312B8,0x7D2A192E},{0x8273B780,0x817D0000},
        {0x8273B7B0,0x484E4301},{0x8273B7DC,0x484E436D},{0x8273B7F4,0x4BFC63E5},
        {0x8273B7F8,0x907F0030},{0x8282F8A0,0x807E6F74},{0x8282F8B0,0x4E800421},
        {0x8282F8D4,0x4E800421},{0x8282F8DC,0x917F000C},{0x8282F8E0,0x917F0010}}};
    for(const auto& pin:pins)need(PPC_LOAD_U32(pin[0])==pin[1],"Original group reader/initializer/relocation/setup/release instruction changed");
}
struct OriginalSubmeshPool {
    static constexpr uint32_t rowOffset=0x80,pointerField=0x40;
    uint32_t header{},wire{},wireBytes{},first{},firstBytes{},count{};
    uint64_t firstGeneration{},wireGeneration{};
    std::vector<uint8_t> originalWire,relocatedFirst;
    void read(Runtime& rt,EngineCpuCalls& cpu,uint32_t owner,uint32_t rows,uint32_t options) {
        auto* base=rt.base;need(rows==1,"Empty logical table must retain genuine one-row allocation capacity");
        header=owner;count=rows;
        need(uint64_t(rowOffset)+uint64_t(count)*36<=UINT32_MAX,"Submesh candidate byte owner wraps");
        firstBytes=rowOffset+36*count;wireBytes=12+firstBytes;
        std::memset(rt.pointer(header,0x20,true),0,0x20);
        const auto saved=fullAbi(cpu.registers());wire=cpu.invoke(0x8269BF70,wireBytes,options);
        need(wire&&fullAbi(cpu.registers())==saved,"Original immutable submesh stream allocation/ABI differs");
        wireGeneration=rt.engineAudio->allocationGeneration(wire,wireBytes);
        std::memset(rt.pointer(wire,wireBytes,true),0,wireBytes);
        const auto little=[&](uint32_t at,uint32_t value){for(uint32_t i=0;i<4;++i)PPC_STORE_U8(at+i,uint8_t(value>>(8*i)));};
        little(wire,16);little(wire+4,firstBytes);little(wire+8,0);
        const auto rawFirst=wire+12;
        PPC_STORE_U32(rawFirst,0xBFBFBFBF);PPC_STORE_U32(rawFirst+4,0x01000000);
        PPC_STORE_U32(rawFirst+20,1);PPC_STORE_U32(rawFirst+24,0);
        PPC_STORE_U32(rawFirst+28,pointerField);PPC_STORE_U32(rawFirst+32,0);
        PPC_STORE_U32(rawFirst+pointerField,rowOffset);
        for(uint32_t i=0;i<count;++i) {
            const auto row=rawFirst+rowOffset+36*i;
            PPC_STORE_U32(row,i+1<count?7u:2u);PPC_STORE_U32(row+4,0);
            PPC_STORE_U32(row+12,6);PPC_STORE_U32(row+24,4);
        }
        originalWire=snapshot(rt,wire,wireBytes);
        PPC_STORE_U32(area+0xD9C0,wire);PPC_STORE_U32(area+0xD9C4,wireBytes);
        const auto stream=cpu.invoke(0x823F9598,3,1,area+0xD9C0);
        need(stream&&fullAbi(cpu.registers())==saved,"Original submesh memory stream constructor/ABI differs");
        need(cpu.invoke(0x8282F618,stream,wireBytes,header,0)==stream&&fullAbi(cpu.registers())==saved,
             "Original submesh first-pool reader/relocation failed or changed ABI");
        need(cpu.invoke(0x823F94A0,stream,0)==1&&fullAbi(cpu.registers())==saved,
             "Original submesh stream close/ABI differs");
        first=PPC_LOAD_U32(header+12);
        need(first&&!(first&15)&&PPC_LOAD_U32(header)==16&&PPC_LOAD_U32(header+4)==firstBytes&&
             !PPC_LOAD_U32(header+8)&&!PPC_LOAD_U32(header+16)&&PPC_LOAD_U32(first+pointerField)==first+rowOffset,
             "Original first-pool reader did not publish exact relocated submesh owner");
        firstGeneration=rt.engineAudio->allocationGeneration(first,firstBytes);
        const auto owned=rt.engineAudio->allocationSpan(row());
        need(owned&&owned->address==first&&owned->extent==firstBytes&&owned->generation==firstGeneration&&
             uint64_t(row())-owned->address+uint64_t(36)*count==owned->extent,
             "Original submesh rows lost complete logical allocator ownership");
        rejects([&]{rt.engineAudio->allocationGeneration(first,firstBytes+1);},
                "Submesh logical owner admitted an extra byte");
        relocatedFirst=snapshot(rt,first,firstBytes);
        same(rt,wire,originalWire,"Original submesh reader changed immutable serialized bytes");
        std::printf("AUDIT_SUBMESH_POOL_CREATE count=%u row_bytes=%u first_bytes=%u reader=8282F618 relocation=82831280 selector=0 stream_close=passed logical_owner=passed ABI=passed\n",count,36*count,firstBytes);
    }
    uint32_t row() const {return first+rowOffset;}
    void unchanged(Runtime& rt) const {
        same(rt,first,relocatedFirst,"Original submesh traversal changed owned relocated rows");
        same(rt,wire,originalWire,"Original submesh traversal changed immutable serialized bytes");
        const auto owned=rt.engineAudio->allocationSpan(row());
        need(owned&&owned->address==first&&owned->extent==firstBytes&&owned->generation==firstGeneration&&
             uint64_t(row())-owned->address+uint64_t(36)*count==owned->extent&&
             rt.engineAudio->allocationGeneration(first,firstBytes)==firstGeneration&&
             rt.engineAudio->allocationGeneration(wire,wireBytes)==wireGeneration,
             "Original submesh traversal lost logical row ownership");
    }
    void release(Runtime& rt,EngineCpuCalls& cpu) const {
        auto* base=rt.base;unchanged(rt);const auto saved=fullAbi(cpu.registers());
        need(cpu.invoke(0x8282F878,header,0)==header&&fullAbi(cpu.registers())==saved,
             "Original submesh pool retirement/ABI differs");
        need(!PPC_LOAD_U32(header+12)&&!PPC_LOAD_U32(header+16)&&!PPC_LOAD_U32(header+8),
             "Original submesh pool retirement retained published storage");
        rejects([&]{rt.engineAudio->allocationGeneration(first,firstBytes);},
                "Retired original submesh row owner remained live");
        need(!rt.engineAudio->allocationSpan(first)&&!rt.engineAudio->allocationSpan(row()),
             "Retired original submesh base/interior owner remained borrowed");
        same(rt,wire,originalWire,"Original submesh retirement changed immutable stream source");
        cpu.invoke(0x8269BF10,wire);need(fullAbi(cpu.registers())==saved,"Original stream-source free damaged ABI");
        rejects([&]{rt.engineAudio->allocationGeneration(wire,wireBytes);},
                "Original submesh serialized source remained live after free");
    }
};

std::string auditString(const std::string& row,const char* field) {
    const std::string marker=std::string("\"")+field+"\":\"";
    const auto at=row.find(marker);need(at!=std::string::npos,"Submesh audit string field is absent");
    std::string result;
    for(size_t i=at+marker.size();i<row.size();++i) {
        if(row[i]=='"')return result;
        if(row[i]=='\\') {
            need(++i<row.size()&&(row[i]=='"'||row[i]=='\\'||row[i]=='/'),"Unexpected submesh audit escape");
        }
        result+=row[i];
    }
    need(false,"Unterminated submesh audit field");return {};
}
std::string auditHex(uint32_t value) {char text[9];std::snprintf(text,sizeof(text),"%08X",value);return text;}
bool auditWord(const std::string& text,const char* name,const std::string& value) {
    const auto token=std::string(name)+"="+value;const auto at=text.find(token);
    return at!=std::string::npos&&(at+token.size()==text.size()||text[at+token.size()]==' ');
}
std::vector<std::string> rigidAuditRows(bool production=false) {
    std::ifstream input(rigidAuditPath);need(bool(input),"Preserved submesh entry audit log is missing");
    std::vector<std::string> result;std::string line;
    const auto boundary=production?"boundary=rigid_mesh_entry ":"boundary=rigid_fixture_mesh_entry ";
    const auto kind=production?"\"kind\":\"effect_producer_entry\"":"\"kind\":\"rigid_fixture_entry\"";
    while(std::getline(input,line))if(line.find(boundary)!=std::string::npos&&line.find(kind)!=std::string::npos)result.push_back(line);
    return result;
}
void verifyRigidAudit(const std::string& line,const OriginalSubmeshPool& pool,uint32_t count,uint32_t rows,
                        uint32_t typed,const char* action,const char* event,bool production=false,uint32_t submittedR7=UINT32_MAX,uint32_t collectionView=0) {
    const bool rowOwned=rows==pool.row();
    need(auditString(line,"event")==event&&auditString(line,"asset")=="source:"+auditHex(source)&&
         line.find("\"caller\":"+std::to_string(0x827402B0u)+",")!=std::string::npos&&
         auditString(line,"mission")=="synthetic-rigid-zero-submesh-fixture"&&auditString(line,"last_action")==action,
         "Submesh pre-validation receipt lost its source/caller/mission/action");
    const auto parameters=auditString(line,"parameters"),ownership=auditString(line,"ownership"),instance=auditString(line,"instance");
    need(auditWord(parameters,"boundary",production?"rigid_mesh_entry":"rigid_fixture_mesh_entry")&&auditWord(parameters,"source",auditHex(source))&&
         auditWord(parameters,"requested_technique","00000000")&&auditWord(parameters,"argument5_role","typed-mesh-owner")&&
         auditWord(parameters,"submesh_count",std::to_string(count))&&auditWord(parameters,"bones","0")&&
         auditWord(parameters,"submesh_owner",rowOwned?"observed-live":"unobserved")&&auditWord(ownership,"admission","unvalidated")&&
         auditWord(ownership,"runtime_match","1")&&auditWord(ownership,"thread_match","1")&&auditWord(ownership,"cpu_match","1"),
         "Submesh pre-validation receipt lost its raw count or unvalidated owner state");
    need(auditWord(instance,"r3",auditHex(metadata))&&auditWord(instance,"r4",auditHex(object))&&
         auditWord(instance,"r5",auditHex(typed))&&auditWord(instance,"r7",auditHex(submittedR7==UINT32_MAX?count:submittedR7))&&
         auditWord(instance,"submeshes",auditHex(rows))&&auditWord(instance,"submesh_owner_base",auditHex(rowOwned?pool.first:0))&&
         auditWord(instance,"submesh_owner_extent",std::to_string(rowOwned?pool.firstBytes:0))&&
         auditWord(instance,"submesh_owner_generation",std::to_string(rowOwned?pool.firstGeneration:0)),
         "Submesh receipt hid the actual array pointer/logical extent/generation");
    // Both production and fixture preserve the unused collection pointer value.
    need(auditWord(instance,"collection",auditHex(collectionView)),
        "Zero-table fixture receipt hid its actual unused collection pointer value");
    const auto group=auditString(line,"group");
    need(group.find("submeshes=")==std::string::npos&&group.find("submesh_owner_base=")==std::string::npos&&
         group.find("submesh_owner_extent=")==std::string::npos&&group.find("submesh_owner_generation=")==std::string::npos&&
         group.find("collection=")==std::string::npos,
         "Submesh instance addresses/generation leaked into the stable combination group");
}

#include "rigid_row_audit_checks.h"

void observeRigidEntry(Runtime& rt,PPCContext& c,const OriginalSubmeshPool& pool) {
    HostRestore host;
    need(active==&rt&&currentContext==&c&&GetCurrentThreadId()==fixtureOwnerThread,
         "Rigid fixture receipt is outside its actual Runtime/caller thread");
    auto* base=rt.base;
    const auto rawCount=PPC_LOAD_U32(metadata+0x10),rows=PPC_LOAD_U32(metadata+0x14),collectionView=PPC_LOAD_U32(metadata+0x34);
    const auto span=rt.engineAudio->allocationSpan(rows);
    const bool owned=span&&span->address==pool.first&&span->extent==pool.firstBytes&&span->generation==pool.firstGeneration;
    const auto parameters=std::string("boundary=rigid_fixture_mesh_entry source=")+auditHex(source)+
        " requested_technique=00000000 argument5_role=typed-mesh-owner submesh_count="+std::to_string(rawCount)+
        " bones="+std::to_string(PPC_LOAD_U32(metadata+0x24))+" submesh_owner="+(owned?"observed-live":"unobserved");
    const auto instance=std::string("r3=")+auditHex(c.r3.u32)+" r4="+auditHex(c.r4.u32)+
        " r5="+auditHex(c.r5.u32)+" r7="+auditHex(c.r7.u32)+" submeshes="+auditHex(rows)+
        " submesh_owner_base="+auditHex(span?span->address:0)+" submesh_owner_extent="+std::to_string(span?span->extent:0)+
        " submesh_owner_generation="+std::to_string(span?span->generation:0)+" collection="+auditHex(collectionView);
    rt.resourceAudit.observe("rigid_fixture_entry","source:"+auditHex(source),uint32_t(c.lr),parameters,
        std::string("admission=unvalidated runtime_match=")+(active==&rt?"1":"0")+
        " thread_match="+(GetCurrentThreadId()==fixtureOwnerThread?"1":"0")+
        " cpu_match="+(currentContext==&c?"1":"0"),0,instance);
}
void zeroRun(Runtime& rt,const PPCContext& entry,const char* dictionaryPath) {
    auto* base=rt.base;auto& driver=*rt.engineDriver;auto& effects=driver.effects();EngineCpuCalls cpu(entry,base);
    OriginalDrawCatalogCleanup cleanup(rt);
    stage="original rigid family instruction and binding evidence";originalEvidence(base);originalGroupPoolEvidence(base);
    rt.map(area,0x10000,true,"original rigid range fixture");std::memset(rt.pointer(area,0x10000,true),0,0x10000);
    stage="real rigid family catalog construction";
    const auto context=PPC_LOAD_U32(0x82D5DA74),options=cpu.registers().r1.u32+0x60;
    PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const auto manager=cpu.invoke(0x8269BF70,0x260,options);cpu.invoke(0x826B6F60,manager,context);
    need(!cpu.invoke(0x827019E8,table,25)&&!cpu.invoke(0x827019E8,secondTable,24),"Original catalog registration failed");
    cpu.invoke(0x826B7218,manager);
    const auto typed=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(secondTable+family->row*16+4));
    const auto shadow=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(table+4*16+4));
    need(typed&&shadow&&PPC_LOAD_U32(typed)==0x820616C0,"Original dual/shared rigid typed owner missing");
    const auto id=PPC_LOAD_U32(typed+0x1C),wrapper=PPC_LOAD_U32(typed+0x18);
    need(effects.count()==49&&effects.view(id).source==source&&effects.view(id).defaultVectorWords.size()==family->words,
         "Original rigid family source or private bank differs");
    cache(rt,cpu,id,body);queries(rt,cpu,id,body);const auto sourceBytes=snapshot(rt,source,family->bytes);
    stage="original two texture dictionary load";const auto textures=loadTextures(rt,entry,dictionaryPath);
    stage="original world and character shadow copies";
    for(uint32_t slot=0;slot<2;++slot) {
        const auto camera=PPC_LOAD_U32(shadow+0x5B4+4*slot);PPC_STORE_U32(area+0x80,0x000000FF);
        need(cpu.invoke(0x823F1B80,camera,area+0x80,7)==camera,"Original shadow camera clear failed");
        EngineCpuCalls pass(entry,base);const auto saved=fullAbi(pass.registers());pass.invoke(0x82707220,shadow,slot);
        need(fullAbi(pass.registers())==saved,"Whole original empty shadow parent changed nonvolatile ABI");
        need(driver.shadowTextures().view(PPC_LOAD_U32(shadow+0xF0+4*slot)).phase==EngineShadowTextures::Phase::Uploaded,
             "Original parent did not upload its world/character depth copy");
    }
    const auto viewport=cpu.invoke(0x8269D788);need(viewport,"Original viewport manager missing");
    cpu.registers().f1.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12CC));
    cpu.registers().f2.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D0));
    cpu.registers().f3.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D4));
    cpu.registers().f4.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D8));
    cpu.invoke(0x8269D608,viewport,0);const auto camera=PPC_LOAD_U32(viewport+0x14),frame=PPC_LOAD_U32(camera+4);
    fixture(rt,camera,typed,shadow,textures);

    stage="source-qualified original first-pool allocator initialization";
    const auto beforeConstruction=fullAbi(cpu.registers());
    const auto previousPoolManager=PPC_LOAD_U32(0x82E06F74),previousPoolArgument=PPC_LOAD_U32(0x82E06F78);
    auto poolManager=PPC_LOAD_U32(0x82D57244);
    if(!poolManager)poolManager=cpu.invoke(0x8268E7F0);
    need(poolManager&&PPC_LOAD_U32(poolManager)==0x820B60B8,"Original rigid pool singleton is not initialized");
    cpu.invoke(0x8282F4D0,poolManager,0);
    need(fullAbi(cpu.registers())==beforeConstruction&&PPC_LOAD_U32(0x82E06F74)==poolManager&&!PPC_LOAD_U32(0x82E06F78),
         "Original rigid pool publication/ABI differs");
    std::array<OriginalSubmeshPool,1> submeshPools;
    stage="original streamed one-row backing allocation/relocation for logical zero";
    submeshPools[0].read(rt,cpu,area+0xD900,1,options);
    PPC_STORE_U32(offsets+28,0x70);PPC_STORE_U16(headers+0x70,0x20);
    PPC_STORE_U32(metadata+0x24,0); // Genuine zero-bone827400F8 branch.
    PPC_STORE_U32(metadata+0x14,submeshPools[0].row());
    stage="whole original8273B760 rigid geometry and declaration construction";
    need(cpu.invoke(0x8273B760,metadata)==0&&fullAbi(cpu.registers())==beforeConstruction,
         "Original rigid geometry constructor returned an error or damaged ABI");
    const auto cached=PPC_LOAD_U32(geometry+0x30);
    const auto declaration=PPC_LOAD_U32(cached+4),alternate=PPC_LOAD_U32(cached+8);
    need(declaration&&rt.engineAudio->allocationGeneration(declaration,0x50),
         "Original range rigid declaration has no exact allocator owner");
    if(alternate)need(rt.engineAudio->allocationGeneration(alternate,0x50)!=0,
         "Original range rigid alternate declaration has no exact allocator owner");
    Restore flags(rt,0x82D6CCA8,4),nativeContext(rt,0x82D6D890,4),published(rt,0x82D63028,4),
        materialRoot(rt,0x82D6D814,4),recordEnabled(rt,0x82CF0BE8,4),dirty(rt,0x82D00F80,4),
        propertyBase(rt,0x82D6C0C0,4),view(rt,0x82D0CA70,64),projection(rt,0x82CD1AB0,64),cameraMatrix(rt,frame+0x10,64);
    PPC_STORE_U32(0x82D6CCA8,0);PPC_STORE_U32(0x82D6D890,context);PPC_STORE_U32(0x82D63028,0);
    PPC_STORE_U32(0x82D6D814,headers);PPC_STORE_U32(0x82CF0BE8,0);PPC_STORE_U32(0x82D00F80,0);PPC_STORE_U32(0x82D6C0C0,0);
    need(cpu.invoke(0x823F1A18,camera)==camera,"Original main camera begin failed");
    identity(base,frame+0x10);identity(base,0x82D0CA70);identity(base,0x82CD1AB0);
    constexpr std::array<std::array<uint32_t,2>,22> states={{{0x28,1},{0x2C,6},{0x30,1},{0x34,0},{0x3C,0},{0x60,0},{0x6C,0},
        {0xAC,0},{0xC0,1},{0xC4,UINT32_MAX},{0xC8,0},{0xCC,0},{0xD0,0},{0xD4,15},{0xE4,0},{0x130,1},
        {0x144,1},{0x148,1},{0x14C,0xFFFF},{0x150,0},{0xD8,0},{0xDC,0}}};
    const auto resetPassState=[&] {
        // Independent logical-count/alpha cases own the original pass state.
        for(const auto& row:states)driver.directScalar(base,row[0],row[1]);
        cpu.invoke(0x826B7968,PPC_LOAD_U32(0x82E06F80+0x38),1,1);
    };
    resetPassState();


    need(rt.frameCaptureDirectory.empty(),"Empty-table fixture requires capture-disabled public configuration");
    const auto bound=driver.cameraBinding();
    auto& backing=submeshPools[0];
    const auto ordinaryCollection=PPC_LOAD_U32(metadata+0x34),ordinaryCollectionCount=PPC_LOAD_U32(ordinaryCollection);
    uint32_t expectedRows=backing.row(),expectedCollection=ordinaryCollection;
    uint32_t logicalCount=0,expectedAlpha=0,dispatcherCalls=0,fallbackCalls=0,meshEntries=0;
    uint32_t vectorCalls=0,textureCalls=0,bindCalls=0,objectEndCalls=0,submeshCalls=0;
    bool armAbiMismatch=false,abiMismatchComplete=false;
    Observation observation([&](uint32_t pc,PPCContext& c) {
        HostRestore observerHost;
        if(pc==0x82740680){++dispatcherCalls;need(uint32_t(c.lr)==0x8273B4E0&&c.r3.u32==packet,"Original empty-table dispatcher caller differs");}
        else if(pc==0x827400F8){++fallbackCalls;need(uint32_t(c.lr)==0x82740B28&&c.r3.u32==packet&&
            c.r4.u32==expectedAlpha&&!c.r5.u32&&!c.r6.u32,"Original empty-table fallback arguments differ");}
        else if(pc==0x82701220) {
            ++meshEntries;
            need(currentContext==&c&&uint32_t(c.lr)==0x827402B0&&c.r3.u32==metadata&&c.r4.u32==object&&
                 c.r5.u32==typed&&!c.r6.u32&&c.r7.u32==logicalCount&&c.r8.u32==PPC_LOAD_U8(packet+0xC)&&
                 PPC_LOAD_U32(metadata+0x10)==logicalCount&&PPC_LOAD_U32(metadata+0x14)==expectedRows&&
                 PPC_LOAD_U32(metadata+0x34)==expectedCollection,
                 "Original empty-table entry lost logical count, caller, typed owner or actual raw unused views");
            observeRigidEntry(rt,c,backing);
            if(armAbiMismatch&&!abiMismatchComplete) {
                need(!logicalCount,"Zero-table ABI control must follow successful original empty use");
                HostRestore host;
                const auto valuesBefore=effects.view(id).defaultVectorWords;
                const auto maskBefore=effects.privateModifiedMask(id);
                const auto recordingBefore=driver.recordingOwners().count();
                const auto metadataBefore=snapshot(rt,metadata,0x38);
                bool failed=false,preserved=false;std::string reason;
                uint32_t returnedCsr=0;DWORD returnedError=0;
                {
                    ProbeRestore restore(c,rt.pointer(metadata+0x10,8,true),rt.pointer(backing.row(),36,true));
                    c.lastFunction=pc;c.r7.u32=1; // Metadata remains0: this is a real ABI mismatch, not a dead-row error.
                    rt.resourceAudit.action("malformed-zero-metadata-nonzero-abi-count");
                    const auto probeCsr=(host.csr&~0x6000u)|0x4000u;constexpr DWORD probeError=0xA17D0000;
                    PPCFPSCRRegister::restoreHostCSR(probeCsr);SetLastError(probeError);
                    std::array<uint8_t,sizeof(PPCContext)> submitted{};std::memcpy(submitted.data(),&c,sizeof(c));
                    observeRigidEntry(rt,c,backing);
                    try {SimpsonsNativeRigidImmediateMeshEntry(c,base);}
                    catch(const Failure& error) {
                        returnedCsr=PPCFPSCRRegister::getcsr();returnedError=GetLastError();failed=true;
                        preserved=!std::memcmp(submitted.data(),&c,sizeof(c));reason=error.what();rt.resourceAudit.failure(reason);
                    }
                    need(failed&&reason=="Unqualified original rigid mesh-loop entry","Malformed zero-table ABI count rejected at wrong frontier or was admitted");
                    need(preserved&&returnedCsr==probeCsr&&returnedError==probeError,"Zero-table ABI rejection changed complete PPC/CSR/LastError");
                }
                need(effects.view(id).defaultVectorWords==valuesBefore&&effects.privateModifiedMask(id)==maskBefore&&
                     driver.recordingOwners().count()==recordingBefore,"Zero-table ABI rejection changed FX/dirty/payload ownership");
                same(rt,metadata,metadataBefore,"Zero-table ABI rejection changed metadata");backing.unchanged(rt);
                rt.resourceAudit.action("valid-zero-after-malformed-abi");observeRigidEntry(rt,c,backing);abiMismatchComplete=true;
            }
        }
        else if(pc==0x826FF250) {
            ++bindCalls;need(uint32_t(c.lr)==0x8270124C&&c.r3.u32==metadata&&c.r4.u32==object&&!c.r5.u32,
                "Original empty-table static bind caller differs");
        }
        else if(pc==0x826FE668) {
            ++objectEndCalls;need(uint32_t(c.lr)==0x82701438&&c.r3.u32==object,"Original empty-table object end was skipped or used another caller");
        }
        else if(pc==0x82700498) {
            ++submeshCalls;need(logicalCount==1&&uint32_t(c.lr)==0x82701310&&c.r3.u32==typed,
                "Original empty-table branch entered an unconsumed material/submesh row");
        }
        else if(pc==0x8270BBC0)++vectorCalls;else if(pc==0x8270BE50)++textureCalls;
    });
    const auto geometryBytes=snapshot(rt,geometry,0x700),objectBytes=snapshot(rt,object,0x40);
    const auto inputs=materialInputs();
    const auto call=[&](uint32_t count,uint32_t alpha,const char* action,const UnusedViews* unused=nullptr) {
        need(count<=1,"Empty-table fixture logical range differs from its genuine backing capacity");
        backing.unchanged(rt);logicalCount=count;expectedAlpha=alpha;
        expectedRows=unused?unused->rows:backing.row();expectedCollection=unused?unused->collection:ordinaryCollection;
        PPC_STORE_U32(metadata+0x10,count);PPC_STORE_U32(metadata+0x14,expectedRows);
        PPC_STORE_U32(metadata+0x34,expectedCollection);
        PPC_STORE_U32(metadata+8,1);PPC_STORE_U8(packet+0xC,uint8_t(alpha));
        rt.resourceAudit.action(action);resetPassState();
        PPC_STORE_U32(area+0x80,0x00FF00FF);need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original empty-table clear failed");
        const auto clearColor=driver.readbackColor(bound.colorIdentity),clearDepth=driver.readbackDepth(bound.depthIdentity);
        const auto vectorsBefore=vectorCalls,texturesBefore=textureCalls,submeshesBefore=submeshCalls;
        const auto bindsBefore=bindCalls,endsBefore=objectEndCalls;
        EngineCpuCalls draw(entry,base);const auto saved=fullAbi(draw.registers());
        try {draw.invoke(0x8273B4D0,packet);}catch(const Failure& error){rt.resourceAudit.failure(error.what());throw;}
        need(fullAbi(draw.registers())==saved,"Whole original empty-table wrapper changed nonvolatile PPC/FP ABI");
        need(bindCalls==bindsBefore+1&&objectEndCalls==endsBefore+1,"Original empty-table bind or auxiliary/object closure is incomplete");
        need(PPC_LOAD_U32(manager+8)==id&&PPC_LOAD_U32(manager+0xC)==(alpha?0x0007FFFCu:0x0003FFFCu)&&
             !PPC_LOAD_U32(0x82D0CAF8),"Original empty-table pass activation/owner changed or published a console device");
        const auto pixels=driver.readbackColor(bound.colorIdentity),depth=driver.readbackDepth(bound.depthIdentity);
        if(!count) {
            need(vectorCalls==vectorsBefore&&textureCalls==texturesBefore&&submeshCalls==submeshesBefore,
                "Original zero submeshes consumed a material callback or selected row");
            need(pixels==clearColor&&depth==clearDepth,"Original zero submeshes changed clear color/depth pixels");
            // The genuine827402E4 completion also checks actual backend draw
            // count==its pre-entry count+cursor0; there is no public rigid getter.
            // Do not assert a cleared private mask: zero executes no material commit.
        } else {
            need(submeshCalls==submeshesBefore+1&&pixel(pixels,640,360)!=pixel(clearColor,640,360)&&
                 pixel(pixels,1100,360)==pixel(clearColor,1100,360),"Later genuine one-row work did not draw within its bounds");
            need(effects.privateModifiedMask(id)==std::array<uint8_t,128>{},"Later genuine one-row material commit retained a dirty bank");
            const auto committed=effects.view(id).defaultVectorWords;
            for(uint32_t i=0;i<inputs.size();++i) {
                const auto& input=inputs[i];
                if(input.texture!=noTexture)need(committed[input.word]==textures[input.texture],"Later original material lost its texture header");
                else for(uint32_t lane=0;lane<4;++lane)need(committed[input.word+lane]==PPC_LOAD_U32(values+16*i+4*lane),"Later original material lost its numeric lane");
            }
        }
        same(rt,geometry,geometryBytes,"Original empty-table use changed immutable geometry/header backing");
        same(rt,object,objectBytes,"Original empty-table use changed its mapped object backing");backing.unchanged(rt);
        return pixels;
    };
    // First static table use in this process is logical zero. The current width0
    // native pointer rejection stops here; no later probe/cleanup success is fabricated.
    stage="first whole original logical zero table before any selected-row use";
    (void)call(0,zeroAlpha,"valid-zero-first-before-selected-row-use");
    armAbiMismatch=true;
    stage="second whole original logical zero table after isolated native ABI mismatch";
    (void)call(0,zeroAlpha,"valid-zero-preflight");need(abiMismatchComplete,"Zero-table ABI negative was not exercised");
    // The five-argument baseline adds no unused-field work. Each sixth-argument
    // mode runs exactly one positive full-wrapper case in its own fresh process.
    constexpr uint32_t unusedUnmapped=0x70000000;
    const auto selectedRows=unusedMode=="row-null"?0u:(unusedMode=="row-unmapped"?unusedUnmapped:backing.row());
    const auto selectedCollection=unusedMode=="collection-null"?0u:(unusedMode=="collection-unmapped"?unusedUnmapped:ordinaryCollection);
    const char* selectedAction=unusedMode=="row-null"?"valid-zero-unused-row-null":
        (unusedMode=="row-unmapped"?"valid-zero-unused-row-unmapped":
         (unusedMode=="collection-null"?"valid-zero-unused-collection-null":"valid-zero-unused-collection-unmapped"));
    if(unusedMode=="row-unmapped"||unusedMode=="collection-unmapped") {
        stage="prove unused-field diagnostic address is genuinely unmapped";
        HostRestore host;bool rejected=false;std::string reason;
        try {(void)rt.pointer(unusedUnmapped,1,false);}
        catch(const Failure& error){rejected=true;reason=error.what();}
        need(rejected&&reason=="Unmapped/protected guest read address=0x70000000 width=1",
             "Unused-field diagnostic address is mapped or failed at another frontier");
        need(!rt.engineAudio->allocationSpan(unusedUnmapped),"Unused unmapped pointer acquired a containing owner");
    }
    const auto unusedCall=[&](uint32_t rows,uint32_t collectionView,const char* action) {
        HostRestore host;
        const auto rowBefore=PPC_LOAD_U32(metadata+0x14),collectionBefore=PPC_LOAD_U32(metadata+0x34);
        {
            UnusedFieldRestore restore(rt.pointer(metadata+0x14,4,true),rt.pointer(metadata+0x34,4,true));
            const UnusedViews views{rows,collectionView};(void)call(0,zeroAlpha,action,&views);
            need(PPC_LOAD_U32(metadata+0x14)==rows&&PPC_LOAD_U32(metadata+0x34)==collectionView,
                 "Original zero wrapper changed the actual unused pointer WORD values");
            backing.unchanged(rt);
        }
        need(PPC_LOAD_U32(metadata+0x14)==rowBefore&&PPC_LOAD_U32(metadata+0x34)==collectionBefore&&
             rowBefore==backing.row()&&collectionBefore==ordinaryCollection,
             "Unused zero-field scope did not restore the actual borrowed table views");
        backing.unchanged(rt);
    };
    if(unusedMode=="row-null") {
        stage="whole original zero with null unused row pointer";
        unusedCall(0,ordinaryCollection,"valid-zero-unused-row-null");
    } else if(unusedMode=="row-unmapped") {
        stage="whole original zero with unmapped unused row pointer";
        unusedCall(unusedUnmapped,ordinaryCollection,"valid-zero-unused-row-unmapped");
    } else if(unusedMode=="collection-null") {
        stage="whole original zero with null unused material collection pointer";
        unusedCall(backing.row(),0,"valid-zero-unused-collection-null");
    } else if(unusedMode=="collection-unmapped") {
        stage="whole original zero with unmapped unused material collection pointer";
        unusedCall(backing.row(),unusedUnmapped,"valid-zero-unused-collection-unmapped");
    }
    stage="later whole original ordinary one-row opaque use";(void)call(1,0,"valid-one-row-opaque-after-zero");
    stage="later whole original ordinary one-row alpha use";(void)call(1,1,"valid-one-row-alpha-after-zero");
    const auto textureCount=uint32_t(std::count_if(inputs.begin(),inputs.end(),[](const auto& input){return input.texture!=noTexture;}));
    const auto vectorCount=uint32_t(inputs.size())-textureCount;
    const auto addedZeroCalls=uint32_t(unusedMode!="none"),wholeCalls=4u+addedZeroCalls;
    need(dispatcherCalls==wholeCalls&&fallbackCalls==wholeCalls&&meshEntries==wholeCalls&&bindCalls==wholeCalls&&objectEndCalls==wholeCalls&&submeshCalls==2&&
         vectorCalls==2*vectorCount&&textureCalls==2*textureCount,"Original zero/one table callback/bind/end traversal differs");
    same(rt,source,sourceBytes,"Dual regression changed original serialized shader record");
    cpu.invoke(0x826B4B18,wrapper);need(cpu.invoke(0x823F1A08,camera)==camera,"Original main camera end failed");
    stage="original zero rigid declaration and effect retirement";cpu.invoke(0x82700A78);
    rejects([&]{rt.engineAudio->allocationGeneration(declaration,0x50);},"Original zero rigid declaration retained ownership");
    if(alternate)rejects([&]{rt.engineAudio->allocationGeneration(alternate,0x50);},"Original zero rigid alternate retained ownership");
    cleanup.release(rt,cpu,manager);same(rt,source,sourceBytes,"Original zero rigid cleanup changed serialized source");

    stage="original rigid row pools and immutable stream-source retirement";
    for(const auto& owner:submeshPools)owner.release(rt,cpu);
    const auto beforeRestore=fullAbi(cpu.registers());cpu.invoke(0x8282F4D0,previousPoolManager,previousPoolArgument);
    need(fullAbi(cpu.registers())==beforeRestore&&PPC_LOAD_U32(0x82E06F74)==previousPoolManager&&
         PPC_LOAD_U32(0x82E06F78)==previousPoolArgument,"Original rigid pool publication restoration differs");

    // Diagnostic comparisons occur only after genuine scoped cleanup.
    struct Expected {uint32_t count,r7,rows,collection;const char* action,*event,*reason;};
    std::vector<Expected> fixtureExpected{
        {0,0,backing.row(),ordinaryCollection,"valid-zero-first-before-selected-row-use","encounter",nullptr},
        {0,0,backing.row(),ordinaryCollection,"valid-zero-preflight","encounter",nullptr},
        {0,1,backing.row(),ordinaryCollection,"malformed-zero-metadata-nonzero-abi-count","encounter",nullptr},
        {0,0,backing.row(),ordinaryCollection,"valid-zero-after-malformed-abi","encounter",nullptr},
        {1,1,backing.row(),ordinaryCollection,"valid-one-row-opaque-after-zero","encounter",nullptr},
        {1,1,backing.row(),ordinaryCollection,"valid-one-row-alpha-after-zero","encounter",nullptr}};
    std::vector<Expected> productionExpected{
        {0,0,backing.row(),ordinaryCollection,"valid-zero-first-before-selected-row-use","encounter",nullptr},
        {0,1,backing.row(),ordinaryCollection,"malformed-zero-metadata-nonzero-abi-count","encounter",nullptr},
        {0,1,backing.row(),ordinaryCollection,"malformed-zero-metadata-nonzero-abi-count","failure","Unqualified original rigid mesh-loop entry"},
        {0,0,backing.row(),ordinaryCollection,"valid-zero-after-malformed-abi","encounter",nullptr},
        {1,1,backing.row(),ordinaryCollection,"valid-one-row-opaque-after-zero","encounter",nullptr},
        {1,1,backing.row(),ordinaryCollection,"valid-one-row-alpha-after-zero","encounter",nullptr}};
    if(addedZeroCalls) {
        const Expected selectedExpected{0,0,selectedRows,selectedCollection,selectedAction,"encounter",nullptr};
        fixtureExpected.insert(fixtureExpected.begin()+4,selectedExpected);
        productionExpected.insert(productionExpected.begin()+4,selectedExpected);
    }
    const auto fixtureRows=rigidAuditRows(),productionRows=rigidAuditRows(true);
    need(fixtureRows.size()==fixtureExpected.size()&&productionRows.size()==productionExpected.size(),"Zero-table production/fixture receipts missing or unexpected");
    for(size_t i=0;i<fixtureExpected.size();++i) {
        const auto& e=fixtureExpected[i];verifyRigidAudit(fixtureRows[i],backing,e.count,e.rows,typed,e.action,e.event,false,e.r7,e.collection);
    }
    for(size_t i=0;i<productionExpected.size();++i) {
        const auto& e=productionExpected[i];verifyRigidAudit(productionRows[i],backing,e.count,e.rows,typed,e.action,e.event,true,e.r7,e.collection);
        if(e.reason)need(auditString(productionRows[i],"reason")==e.reason&&i&&
             auditString(productionRows[i],"group")==auditString(productionRows[i-1],"group")&&
             auditString(productionRows[i],"instance")==auditString(productionRows[i-1],"instance"),"Zero-table actual ABI failure lost its prevalidation identity");
    }
    // An empty loop never copies a row, so only the two later ordinary one-row calls emit row
    // receipts (all nine raw words, copied before any offset/header/collection read).
    {
        RigidRowCase one;one.poolFirst=backing.first;one.poolRow=backing.row();one.poolBytes=backing.firstBytes;one.poolGeneration=backing.firstGeneration;
        one.count=1;one.ordinal=0;one.variant=0;one.words={2,0,0,6,0,0,4,0,0};one.event="encounter";
        auto opaqueRow=one,alphaRow=one;opaqueRow.action="valid-one-row-opaque-after-zero";alphaRow.action="valid-one-row-alpha-after-zero";
        verifyRigidRows(rigidRowAuditRows(rigidAuditPath),{opaqueRow,alphaRow},"synthetic-rigid-zero-submesh-fixture",source,metadata,object,typed,
                        ordinaryCollectionCount,ordinaryCollection);
    }
    std::printf("AUDIT_RIGID_ZERO_SUBMESH_LIFECYCLE source=%08X selected_alpha=%u logical_count=0 backing_capacity=1 original_bind=passed auxiliary_object_closure=passed callbacks=0 native_no_draw_scope=original_completion_counter_check clear_color_depth=unchanged malformed_abi_count=passed row_receipts=only_ordinary_one_row unused_case=%.*s unused_field_use=%s unused_backing_live=%s unused_views_restored=%s zero_calls=%u whole_wrapper_calls=%u fixture_receipts=%zu production_receipts=%zu later_opaque_alpha_use=passed declaration_release=passed fx_release=passed cpu_cache_release=passed original_pool_release=passed immutable_source_free=passed stale_cpu_owner_queries=passed receipt_scope=production_and_fixture_before_validation backend_mesh_cache=owner_resident full_gpu_retirement=unproven receipts=%s\n",source,zeroAlpha,int(unusedMode.size()),unusedMode.data(),addedZeroCalls?"passed":"not-exercised",
        addedZeroCalls?"passed":"not-exercised",addedZeroCalls?"passed":"not-exercised",2u+addedZeroCalls,wholeCalls,
        fixtureExpected.size(),productionExpected.size(),rigidAuditPath.string().c_str());
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==5||argc==6,"Original image, loc_split4 ITXD dictionary, rigid family, zero opaque/alpha and optional independent unused-field case required");
        if(argc==6) {unusedMode=argv[5];need(unusedMode=="row-null"||unusedMode=="row-unmapped"||
            unusedMode=="collection-null"||unusedMode=="collection-unmapped","Unknown independent zero unused-field case");}
        const std::string_view selection(argv[4]);need(selection=="opaque"||selection=="alpha","Unknown original zero-table alpha case");zeroAlpha=selection=="alpha"?1u:0u;
        const auto choice=std::find_if(families.begin(),families.end(),[&](const auto& candidate){return std::string_view(argv[3])==candidate.name;});
        need(choice!=families.end(),"Unknown original rigid submesh family");family=&*choice;source=family->source;body=source+12;
        fixtureOwnerThread=GetCurrentThreadId();
        Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        rigidAuditPath=std::filesystem::current_path()/("rigid-zero-submeshes-"+std::string(family->name)+"-"+
            std::to_string(GetCurrentProcessId())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
        const auto file=CreateFileW(rigidAuditPath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        need(file!=INVALID_HANDLE_VALUE,"Cannot reserve unique rigid fixture receipt");need(CloseHandle(file)!=0,"Cannot close rigid fixture receipt reservation");
        rt.resourceAudit.configure(rigidAuditPath);rt.resourceAudit.mission("synthetic-rigid-zero-submesh-fixture");
        rt.resourceAudit.action("whole-original-rigid-zero-setup");
        std::printf("AUDIT_RIGID_ZERO_SUBMESH_RAW path=%s unused_case=%.*s scope=fixture_local_before_validation\n",rigidAuditPath.string().c_str(),int(unusedMode.size()),unusedMode.data());std::fflush(stdout);
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;
        try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original rigid startup boundary missing");zeroRun(rt,entry,argv[2]);
        std::printf("PASS original rigid zero submeshes %s: %zu checks; genuine one-row backing, logical zero bind/no-row use, selected_unused_case=%.*s unused_field_use=%s, later ordinary draws and paired CPU owner cleanup\n",family->name,checks,int(unusedMode.size()),unusedMode.data(),unusedMode!="none"?"passed":"not-exercised");return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original rigid zero submeshes %s: %zu checks stage=%s: %s\n",family->name,checks,stage,error.what());return 1;}
}
