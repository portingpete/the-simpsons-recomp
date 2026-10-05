// First-capture regression through genuine original65536-row pool/setup/draw paths.
// Fresh process per logical opaque/alpha phase; the large table is the FIRST rigid draw.
// Expected bytes are preserved before that call; raw captures survive every outcome.
// Mapped material objects and borrowed driver resources retain the parent fixture's scope.
#define main OriginalRigidBaselineEntrypoint
#include "test_rigid_dual_pass.cpp"
#undef main
#include "effect_draw_cleanup_helpers.h"
#include "runtime/engine_recording.h"
#include <chrono>
#include <bcrypt.h>
#include <charconv>
#include <set>
#include <span>

extern "C" void __imp__sub_82701220(PPCContext&,uint8_t*);
PPC_FUNC(sub_82701220){forward(0x82701220,ctx,base,__imp__sub_82701220);}
// Existing native validation adapter; C++ linkage, production HostState guard.
void SimpsonsNativeRigidImmediateMeshEntry(PPCContext&,uint8_t*);

namespace {
std::filesystem::path rigidAuditPath,captureDirectory;
uint32_t firstPhase{};
// Independent process modes. success: the unchanged 65,536-row first capture.
// budget: a valid original table above the 64 MiB diagnostic budget (never a
// rendering limit). io-raw / io-draw: a real directory occupies one capture
// output name, so the owned open/close genuinely fails.
std::string captureMode="success";
uint32_t largeRows=65536;
constexpr uint32_t budgetRows=1864136; // 36*rows=67,108,896: 64 MiB + 32 bytes.
constexpr uint32_t captureBudgetBytes=64u*1024*1024;
const char* firstAction="capture-first65536-opaque";
DWORD fixtureOwnerThread{};
// The destructor uses only the already-qualified fixed views. It restores the
// actual current PPC object; no TLS/currentContext or guest ownership is swapped.
struct HostRestore {
    uint32_t csr=PPCFPSCRRegister::getcsr();DWORD error=GetLastError();
    ~HostRestore() noexcept {PPCFPSCRRegister::restoreHostCSR(csr);SetLastError(error);}
};
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
        auto* base=rt.base;need(rows==1||rows==largeRows,"Submesh candidate count differs");
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
                        uint32_t typed,const char* action,const char* event,bool production=false) {
    need(auditString(line,"event")==event&&auditString(line,"asset")=="source:"+auditHex(source)&&
         line.find("\"caller\":"+std::to_string(0x827402B0u)+",")!=std::string::npos&&
         auditString(line,"mission")=="synthetic-rigid-first-capture-fixture"&&auditString(line,"last_action")==action,
         "Submesh pre-validation receipt lost its source/caller/mission/action");
    const auto parameters=auditString(line,"parameters"),ownership=auditString(line,"ownership"),instance=auditString(line,"instance");
    need(auditWord(parameters,"boundary",production?"rigid_mesh_entry":"rigid_fixture_mesh_entry")&&auditWord(parameters,"source",auditHex(source))&&
         auditWord(parameters,"requested_technique","00000000")&&auditWord(parameters,"argument5_role","typed-mesh-owner")&&
         auditWord(parameters,"submesh_count",std::to_string(count))&&auditWord(parameters,"bones","0")&&
         auditWord(parameters,"submesh_owner","observed-live")&&auditWord(ownership,"admission","unvalidated")&&
         auditWord(ownership,"runtime_match","1")&&auditWord(ownership,"thread_match","1")&&auditWord(ownership,"cpu_match","1"),
         "Submesh pre-validation receipt lost its raw count or unvalidated owner state");
    need(auditWord(instance,"r3",auditHex(metadata))&&auditWord(instance,"r4",auditHex(object))&&
         auditWord(instance,"r5",auditHex(typed))&&auditWord(instance,"r7",auditHex(count))&&
         auditWord(instance,"submeshes",auditHex(rows))&&auditWord(instance,"submesh_owner_base",auditHex(pool.first))&&
         auditWord(instance,"submesh_owner_extent",std::to_string(pool.firstBytes))&&
         auditWord(instance,"submesh_owner_generation",std::to_string(pool.firstGeneration)),
         "Submesh receipt hid the actual array pointer/logical extent/generation");
    const auto group=auditString(line,"group");
    need(group.find("submeshes=")==std::string::npos&&group.find("submesh_owner_base=")==std::string::npos&&
         group.find("submesh_owner_extent=")==std::string::npos&&group.find("submesh_owner_generation=")==std::string::npos,
         "Submesh instance addresses/generation leaked into the stable combination group");
}
#include "rigid_row_audit_checks.h"

void observeRigidEntry(Runtime& rt,PPCContext& c,const OriginalSubmeshPool& pool) {
    HostRestore host;
    need(active==&rt&&currentContext==&c&&GetCurrentThreadId()==fixtureOwnerThread,
         "Rigid fixture receipt is outside its actual Runtime/caller thread");
    auto* base=rt.base;
    const auto rawCount=PPC_LOAD_U32(metadata+0x10),rows=PPC_LOAD_U32(metadata+0x14);
    const auto span=rt.engineAudio->allocationSpan(rows);
    const bool owned=span&&span->address==pool.first&&span->extent==pool.firstBytes&&span->generation==pool.firstGeneration;
    const auto parameters=std::string("boundary=rigid_fixture_mesh_entry source=")+auditHex(source)+
        " requested_technique=00000000 argument5_role=typed-mesh-owner submesh_count="+std::to_string(rawCount)+
        " bones="+std::to_string(PPC_LOAD_U32(metadata+0x24))+" submesh_owner="+(owned?"observed-live":"unobserved");
    const auto instance=std::string("r3=")+auditHex(c.r3.u32)+" r4="+auditHex(c.r4.u32)+
        " r5="+auditHex(c.r5.u32)+" r7="+auditHex(c.r7.u32)+" submeshes="+auditHex(rows)+
        " submesh_owner_base="+auditHex(pool.first)+" submesh_owner_extent="+std::to_string(pool.firstBytes)+
        " submesh_owner_generation="+std::to_string(pool.firstGeneration);
    rt.resourceAudit.observe("rigid_fixture_entry","source:"+auditHex(source),uint32_t(c.lr),parameters,
        std::string("admission=unvalidated runtime_match=")+(active==&rt?"1":"0")+
        " thread_match="+(GetCurrentThreadId()==fixtureOwnerThread?"1":"0")+
        " cpu_match="+(currentContext==&c?"1":"0"),0,instance);
}

// These files are expectations copied from the already-qualified live inputs,
// not actual capture-completion evidence. Every file is retained on failure.
std::string captureJsonString(const std::string& text) {
    std::string result="\"";
    for(const unsigned char byte:text) {
        if(byte=='"'||byte=='\\'){result+='\\';result+=char(byte);}
        else if(byte<0x20){char escaped[7];std::snprintf(escaped,sizeof(escaped),"\\u%04x",unsigned(byte));result+=escaped;}
        else result+=char(byte);
    }
    return result+'"';
}
struct CaptureHash {
    BCRYPT_HASH_HANDLE value{};
    CaptureHash(){need(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,&value,nullptr,0,nullptr,0,0)>=0,"Capture hash creation failed");}
    ~CaptureHash(){if(value)BCryptDestroyHash(value);}
    CaptureHash(const CaptureHash&)=delete;CaptureHash& operator=(const CaptureHash&)=delete;
};
std::string captureDigest(std::span<const uint8_t> bytes) {
    HostRestore host;CaptureHash hash;
    need(bytes.size()<=ULONG_MAX&&BCryptHashData(hash.value,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),0)>=0,
         "Capture hash update failed");
    std::array<uint8_t,32> digest{};
    need(BCryptFinishHash(hash.value,digest.data(),ULONG(digest.size()),0)>=0,"Capture hash completion failed");
    std::string result;for(const auto byte:digest){result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}return result;
}
std::vector<uint8_t> captureRead(const std::filesystem::path& path) {
    HostRestore host;std::ifstream file(path,std::ios::binary|std::ios::ate);
    need(bool(file),"Preserved first-capture file is missing");const auto end=file.tellg();
    need(end>0&&end<=64*1024*1024,"Preserved first-capture file extent differs");
    std::vector<uint8_t> bytes(size_t(end),uint8_t{});file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));
    need(bool(file),"Preserved first-capture file is truncated");return bytes;
}
void captureWrite(const std::filesystem::path& path,std::span<const uint8_t> bytes) {
    HostRestore host;need(!std::filesystem::exists(path),"Expected capture path is already occupied");
    std::ofstream file(path,std::ios::binary);need(bool(file),"Cannot create expected capture file");
    file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));file.flush();
    need(bool(file),"Expected capture write/flush failed");file.close();need(bool(file),"Expected capture close failed");
    need(captureRead(path)==std::vector<uint8_t>(bytes.begin(),bytes.end()),"Expected capture reread differs");
}
// Production draw metadata is written by a TEXT ofstream: Windows CRLF is
// expected. Normalize only the parser view; hashes/comparisons retain raw bytes.
std::string captureDrawText(const std::vector<uint8_t>& bytes) {
    std::string text;text.reserve(bytes.size());
    for(size_t i=0;i<bytes.size();++i) {
        need(bytes[i]!=0,"Captured draw metadata contains a NUL");
        if(bytes[i]=='\r') {
            need(i+1<bytes.size()&&bytes[i+1]=='\n',"Captured draw metadata has a truncated/bare CR");continue;
        }
        text+=char(bytes[i]);
    }
    return text;
}
uint64_t captureNumber(const std::string& text,const char* field) {
    const auto marker=std::string("\"")+field+"\": ";const auto at=text.find(marker);
    need(at!=std::string::npos&&text.find(marker,at+marker.size())==std::string::npos,"Captured draw scalar absent or duplicated");
    uint64_t value{};const auto first=text.data()+at+marker.size(),last=text.data()+text.size();
    const auto parsed=std::from_chars(first,last,value);
    need(parsed.ec==std::errc{}&&parsed.ptr!=first&&parsed.ptr!=last&&(*parsed.ptr==','||*parsed.ptr=='\n'),
         "Captured draw scalar malformed or truncated");return value;
}
std::vector<uint32_t> captureWords(const std::string& text,const char* field) {
    const auto marker=std::string("\"")+field+"\": [";const auto at=text.find(marker);
    need(at!=std::string::npos&&text.find(marker,at+marker.size())==std::string::npos,"Captured draw words absent or duplicated");
    const auto last=text.data()+text.size();const char* cursor=text.data()+at+marker.size();std::vector<uint32_t> result;
    for(;;) {
        while(cursor!=last&&*cursor==' ')++cursor;need(cursor!=last,"Captured draw words truncated");
        uint32_t value{};const auto parsed=std::from_chars(cursor,last,value);
        need(parsed.ec==std::errc{}&&parsed.ptr!=cursor&&parsed.ptr!=last,"Captured draw word malformed");
        result.push_back(value);cursor=parsed.ptr;
        if(*cursor==']')return result;
        need(*cursor==','&&result.size()<32,"Captured draw word separator/extent differs");++cursor;
    }
}
struct FirstCapture {
    struct Raw {const char* stem;uint32_t address;std::vector<uint8_t> expected,first;};
    std::array<Raw,5> raw;
    uint32_t selected{},id{},typed{},camera{},color{},depth{},materialFlags{};
    std::array<uint32_t,9> selectedWords{};std::array<uint32_t,6> viewport{};
    std::vector<uint8_t> firstDraw;std::vector<std::string> diagnosticErrors;
    bool firstObserved=false;
    FirstCapture(Runtime& rt,const OriginalSubmeshPool& owner,uint32_t effect,uint32_t typedOwner,const NativeCameraBinding& binding)
        :selected(owner.row()+36*(owner.count-1)),id(effect),typed(typedOwner),camera(binding.camera),
         color(binding.colorIdentity),depth(binding.depthIdentity),viewport(binding.viewport) {
        HostRestore host;auto* base=rt.base;
        raw={{{"geometry",geometry,snapshot(rt,geometry,0x78),{}},
              {"vertices",PPC_LOAD_U32(geometry+0x10),snapshot(rt,PPC_LOAD_U32(geometry+0x10),PPC_LOAD_U32(geometry)),{}},
              {"indices",PPC_LOAD_U32(geometry+0x1C),snapshot(rt,PPC_LOAD_U32(geometry+0x1C),PPC_LOAD_U32(geometry+0x14)),{}},
              {"elements",PPC_LOAD_U32(geometry+0xC),snapshot(rt,PPC_LOAD_U32(geometry+0xC),12*PPC_LOAD_U32(geometry+8)),{}},
              {"submeshes",owner.row(),snapshot(rt,owner.row(),36*owner.count),{}}}};
        need(owner.count==largeRows&&raw[4].expected.size()==36ull*largeRows&&owner.firstBytes==0x80+36ull*largeRows&&
             (captureMode=="budget")==(raw[4].expected.size()>captureBudgetBytes),
             "First-capture logical row owner differs");
        need(PPC_LOAD_U32(metadata+0x10)==owner.count&&PPC_LOAD_U32(metadata+0x14)==owner.row()&&
             PPC_LOAD_U32(metadata+8)==1&&PPC_LOAD_U8(packet+0xC)==firstPhase,
             "Expected capture receipt is not the actual published first-call tuple");
        need(std::equal(raw[4].expected.begin(),raw[4].expected.end(),owner.relocatedFirst.begin()+OriginalSubmeshPool::rowOffset),
             "First-capture raw rows differ from the original relocated pool suffix");
        for(uint32_t i=0;i<selectedWords.size();++i)selectedWords[i]=PPC_LOAD_U32(selected+4*i);
        materialFlags=PPC_LOAD_U16(headers+PPC_LOAD_U32(offsets+8));
        for(const auto& item:raw)need(!std::filesystem::exists(captureDirectory/(std::string("rigid-first-")+item.stem+".bin")),
            "A previous rigid call consumed the fresh first-capture state");
        need(!std::filesystem::exists(captureDirectory/"rigid-first-draw.json"),"A previous rigid draw consumed the fresh capture state");
        std::string receipt="{\n  \"schema\":1,\n  \"event\":\"fixture_expected_before_original_call\",\n  \"kind\":\"rigid_first_capture\",\n  \"asset\":"+
            captureJsonString("source:"+auditHex(source))+",\n  \"caller_helper\":\"first_capture_expected\",\n  \"original_entry\":\"8273B4D0\",\n  \"expected_mesh_caller\":\"827402B0\",\n  \"mission\":\"synthetic-rigid-first-capture-fixture\",\n  \"last_action\":"+captureJsonString(firstAction)+
            ",\n  \"ownership\":\"original_relocated_row_pool_live; geometry_material_borrowed; SDK_declaration_live\",\n  \"logical_phase\":"+
            std::to_string(firstPhase)+",\n  \"capture_mode\":"+captureJsonString(captureMode)+",\n  \"submesh_count\":"+std::to_string(largeRows)+
            ",\n  \"selected\":"+std::to_string(selected)+",\n  \"expected_files\":[";
        for(size_t i=0;i<raw.size();++i) {
            const auto& item=raw[i];const auto path=captureDirectory/(std::string("expected-rigid-first-")+item.stem+".bin");
            // An over-budget table is not written as an expectation file (the preserved
            // reader caps at the same 64 MiB); only its precall digest is recorded.
            const bool written=item.expected.size()<=captureBudgetBytes;
            if(written)captureWrite(path,item.expected);
            if(i)receipt+=',';receipt+="\n    {\"stem\":"+captureJsonString(item.stem)+",\"path\":"+captureJsonString(written?path.generic_string():std::string{})+
                ",\"written\":"+(written?"true":"false")+",\"guest_address\":"+std::to_string(item.address)+",\"bytes\":"+std::to_string(item.expected.size())+
                ",\"sha256\":"+captureJsonString(captureDigest(item.expected))+"}";
        }
        receipt+="\n  ],\n  \"credit\":\"expectations_only; no capture/draw/retirement result\"\n}\n";
        const auto path=captureDirectory/"expected-first-capture.json";
        captureWrite(path,std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(receipt.data()),receipt.size()));
        std::printf("AUDIT_RIGID_FIRST_CAPTURE_EXPECTED phase=%u mode=%s source=%08X count=%u row_bytes=%llu path=%s\n",
            firstPhase,captureMode.c_str(),source,largeRows,36ull*largeRows,path.string().c_str());need(std::fflush(stdout)==0,"Expected first-capture marker flush failed");
    }
    void remember(bool condition,const char* message) {if(!condition)diagnosticErrors.emplace_back(message);}
    void observeCompletedFirst() {
        HostRestore host;
        // Diagnostics are delayed until real normal CPU cleanup. Missing/bad
        // files do not make a later original call or release disappear.
        try {
            for(auto& item:raw) {
                item.first=captureRead(captureDirectory/(std::string("rigid-first-")+item.stem+".bin"));
                remember(item.first==item.expected,"First capture raw bytes differ from the exact pre-call snapshot");
            }
            firstDraw=captureRead(captureDirectory/"rigid-first-draw.json");
            const auto text=captureDrawText(firstDraw);
            remember(text.starts_with("{\n")&&text.ends_with("\n}\n"),"First draw JSON framing differs");
            remember(captureNumber(text,"payload")==0&&captureNumber(text,"effect")==id&&captureNumber(text,"typed")==typed&&
                captureNumber(text,"metadata")==metadata&&captureNumber(text,"object")==object&&captureNumber(text,"geometry")==geometry&&
                captureNumber(text,"submesh")==selected&&captureNumber(text,"material")==material&&captureNumber(text,"materialFlags")==materialFlags,
                "First draw metadata does not identify the actual final selected original row/owner");
            remember(captureNumber(text,"camera")==camera&&captureNumber(text,"colorIdentity")==color&&captureNumber(text,"depthIdentity")==depth&&
                captureNumber(text,"vertexCount")==4&&captureNumber(text,"stride")==
                ((uint32_t(raw[0].expected[4])<<24)|(uint32_t(raw[0].expected[5])<<16)|(uint32_t(raw[0].expected[6])<<8)|raw[0].expected[7])&&
                captureNumber(text,"primitiveType")==6&&captureNumber(text,"indexCount")==4&&
                captureNumber(text,"baseVertex")==0&&captureNumber(text,"startIndex")==0,
                "First draw camera/selected geometry range differs");
            remember(captureWords(text,"submeshWords")==std::vector<uint32_t>(selectedWords.begin(),selectedWords.end())&&
                captureWords(text,"viewportWords")==std::vector<uint32_t>(viewport.begin(),viewport.end()),
                "First draw raw row words or actual camera viewport differs");
            firstObserved=true;
        } catch(const std::exception& error){diagnosticErrors.push_back(std::string("First capture diagnostic: ")+error.what());}
    }
    // Isolated modes: a real directory occupies one output name (io-*), or the
    // valid table exceeds the diagnostic budget. Primary draws are unaffected.
    std::filesystem::path obstacle;
    static std::filesystem::path firstPath(const char* stem) {return captureDirectory/(std::string("rigid-first-")+stem+".bin");}
    static bool absent(const std::filesystem::path& path) {std::error_code error;return !std::filesystem::exists(path,error)&&!error;}
    static bool isDirectory(const std::filesystem::path& path) {std::error_code error;return std::filesystem::is_directory(path,error)&&!error;}
    // Raw files closed before the contained stop: all five, the two before the occupied indices name, or none.
    size_t closedRawFiles() const {return captureMode=="io-raw"?2:captureMode=="budget"?0:5;}
    void observeIsolatedFirst() {
        HostRestore host;
        try {
            const auto drawPath=captureDirectory/"rigid-first-draw.json";
            for(size_t i=0;i<raw.size();++i) {
                const auto path=firstPath(raw[i].stem);
                if(i<closedRawFiles()) {
                    raw[i].first=captureRead(path);
                    remember(raw[i].first==raw[i].expected,"A closed raw capture differs from the exact pre-call snapshot");
                } else remember(captureMode=="io-raw"&&i==2?isDirectory(path):absent(path),
                                "A raw capture output exists after the group was skipped or stopped before it");
            }
            remember(captureMode=="io-draw"?isDirectory(drawPath):absent(drawPath),"First draw metadata state differs from the contained outcome");
            // Releasing an occupied name must not invite a silent retry from later draws.
            if(!obstacle.empty()){std::error_code error;remember(std::filesystem::remove(obstacle,error)&&!error,"Cannot release the occupied capture name");}
            firstObserved=true;
        } catch(const std::exception& error){diagnosticErrors.push_back(std::string("Isolated first capture diagnostic: ")+error.what());}
    }
    void assertIsolatedAfterCleanup() {
        HostRestore host;
        try {
            for(size_t i=0;i<raw.size();++i) {
                const auto path=firstPath(raw[i].stem);
                if(i<closedRawFiles())remember(captureRead(path)==raw[i].first&&raw[i].first==raw[i].expected,
                    "A later original/malformed call changed a closed first raw capture");
                else remember(absent(path),"A later call retried or created a skipped/failed first raw capture");
            }
            remember(absent(captureDirectory/"rigid-first-draw.json"),"A later call retried or created first draw metadata after a contained outcome");
        }catch(const std::exception& error){diagnosticErrors.push_back(std::string("Retained isolated capture diagnostic: ")+error.what());}
        for(const auto& message:diagnosticErrors)std::fprintf(stderr,"AUDIT_RIGID_FIRST_CAPTURE_DIAGNOSTIC %s\n",message.c_str());
        need(firstObserved&&diagnosticErrors.empty(),"Isolated first capture outcome/one-shot preservation differs after original cleanup");
        for(size_t i=0;i<closedRawFiles();++i)std::printf("AUDIT_RIGID_FIRST_CAPTURE_FILE phase=%u stem=%s bytes=%zu sha256=%s path=%s\n",
            firstPhase,raw[i].stem,raw[i].expected.size(),captureDigest(raw[i].expected).c_str(),firstPath(raw[i].stem).string().c_str());
        need(std::fflush(stdout)==0,"Isolated capture identity marker flush failed");
    }
    void assertAfterOriginalCleanup() {
        HostRestore host;
        try {
            for(const auto& item:raw) {
                const auto bytes=captureRead(captureDirectory/(std::string("rigid-first-")+item.stem+".bin"));
                remember(bytes==item.first&&bytes==item.expected,"Later original/malformed calls overwrote the first raw capture");
            }
            remember(captureRead(captureDirectory/"rigid-first-draw.json")==firstDraw,"Later calls overwrote first draw-state capture");
        }catch(const std::exception& error){diagnosticErrors.push_back(std::string("Retained capture diagnostic: ")+error.what());}
        for(const auto& message:diagnosticErrors)std::fprintf(stderr,"AUDIT_RIGID_FIRST_CAPTURE_DIAGNOSTIC %s\n",message.c_str());
        need(firstObserved&&diagnosticErrors.empty(),"First capture files/metadata/one-shot preservation differ after original cleanup");
        for(const auto& item:raw)std::printf("AUDIT_RIGID_FIRST_CAPTURE_FILE phase=%u stem=%s bytes=%zu sha256=%s path=%s\n",
            firstPhase,item.stem,item.expected.size(),captureDigest(item.expected).c_str(),
            (captureDirectory/(std::string("rigid-first-")+item.stem+".bin")).string().c_str());
        std::printf("AUDIT_RIGID_FIRST_CAPTURE_DRAW phase=%u bytes=%zu sha256=%s path=%s\n",firstPhase,firstDraw.size(),
            captureDigest(firstDraw).c_str(),(captureDirectory/"rigid-first-draw.json").string().c_str());
        need(std::fflush(stdout)==0,"Completed capture identity marker flush failed");
    }
};

uint64_t auditSequence(const std::string& row) {
    const std::string marker="\"sequence\":";const auto at=row.find(marker);need(at!=std::string::npos,"Audit sequence field is absent");
    uint64_t value{};const auto first=row.data()+at+marker.size();
    const auto parsed=std::from_chars(first,row.data()+row.size(),value);
    need(parsed.ec==std::errc{}&&parsed.ptr!=first,"Audit sequence is malformed");return value;
}
std::vector<std::string> captureDiagnosticRows() {
    std::ifstream input(rigidAuditPath);need(bool(input),"Preserved capture diagnostic log is missing");
    std::vector<std::string> result;std::string line;
    while(std::getline(input,line))if(line.find("\"kind\":\"rigid_capture\"")!=std::string::npos)result.push_back(line);
    return result;
}
// Each capture attempt reports exactly one non-attributing diagnostic row, inside
// the first call's receipt window (after its pre-validation entry, before the next).
void verifyCaptureDiagnostics(const OriginalSubmeshPool& pool,uint32_t typed) {
    struct Expect {const char* phase,*status,*reasonCode,*step;};
    std::vector<Expect> expected;
    if(captureMode=="success")expected={{"first_raw","complete","none","written"},{"first_draw","complete","none","written"}};
    else if(captureMode=="budget")expected={{"first_raw","skipped","budget_exceeded","extent"}};
    else if(captureMode=="io-raw")expected={{"first_raw","failed","exception","write_indices"}};
    else expected={{"first_raw","complete","none","written"},{"first_draw","failed","exception","write_draw"}};
    const auto rows=captureDiagnosticRows();need(rows.size()==expected.size(),"Capture diagnostic rows missing, repeated or unexpected");
    const auto fixture=rigidAuditRows(),production=rigidAuditRows(true);
    need(fixture.size()>=2&&production.size()>=1,"Capture diagnostic ordering anchors are missing");
    uint64_t previous=auditSequence(production[0]);
    need(auditSequence(fixture[0])<previous,"Fixture entry receipt does not precede its production entry");
    for(size_t i=0;i<rows.size();++i) {
        const auto& row=rows[i];const auto& e=expected[i];
        const auto sequence=auditSequence(row);
        need(sequence>previous&&sequence<auditSequence(fixture[1]),"Capture diagnostic is outside the first call's receipt window");previous=sequence;
        need(auditString(row,"event")=="diagnostic"&&auditString(row,"kind")=="rigid_capture"&&auditString(row,"asset")=="source:"+auditHex(source)&&
             row.find("\"caller\":"+std::to_string(0x827402B0u)+",")!=std::string::npos&&
             auditString(row,"mission")=="synthetic-rigid-first-capture-fixture"&&auditString(row,"last_action")==firstAction,
             "Capture diagnostic lost its source/caller/mission/action");
        const auto parameters=auditString(row,"parameters"),ownership=auditString(row,"ownership"),instance=auditString(row,"instance");
        const bool draw=std::string_view(e.phase)=="first_draw";
        need(auditWord(parameters,"phase",e.phase)&&auditWord(parameters,"status",e.status)&&auditWord(parameters,"reason_code",e.reasonCode)&&
             auditWord(parameters,"capture_step",e.step),"Capture diagnostic phase/status/reason/step differs");
        if(draw)need(parameters.find("submesh_count=")==std::string::npos&&parameters.find("budget_bytes=")==std::string::npos,
                     "Draw diagnostic carries unrelated row fields");
        else need(auditWord(parameters,"submesh_count",std::to_string(largeRows))&&
                  auditWord(parameters,"submesh_bytes",std::to_string(36ull*largeRows))&&
                  auditWord(parameters,"budget_bytes",std::to_string(captureBudgetBytes)),"Raw capture diagnostic lost its count/bytes/budget");
        need(auditWord(ownership,"diagnostic_only","1")&&auditWord(ownership,"primary_outcome","unchanged"),"Capture diagnostic claims a primary effect");
        need(auditWord(instance,"typed",auditHex(typed))&&auditWord(instance,"metadata",auditHex(metadata))&&auditWord(instance,"object",auditHex(object))&&
             auditWord(instance,"geometry",auditHex(geometry))&&auditWord(instance,"payload","00000000"),"Capture diagnostic instance identity differs");
        need(draw||(auditWord(instance,"submeshes",auditHex(pool.row()))&&auditWord(instance,"submesh_owner_base",auditHex(pool.first))&&
             auditWord(instance,"submesh_owner_extent",std::to_string(pool.firstBytes))&&
             auditWord(instance,"submesh_owner_generation",std::to_string(pool.firstGeneration))),"Raw capture diagnostic lost its row owner");
        const auto marker=instance.find("capture_directory=");
        need(marker!=std::string::npos&&instance.substr(marker+18)==captureDirectory.string(),"Capture diagnostic lost its output directory");
        const auto group=auditString(row,"group");
        need(group.find("submeshes=")==std::string::npos&&group.find("capture_directory=")==std::string::npos&&
             group.find("submesh_owner_generation=")==std::string::npos,"Capture diagnostic instance data leaked into its stable group");
        const auto reason=row.find("\"reason\":");
        if(std::string_view(e.status)=="complete")need(reason==std::string::npos,"A complete capture carries a failure reason");
        else if(std::string_view(e.status)=="skipped")
            need(auditString(row,"reason").find("exceeds the 67108864 byte diagnostic budget")!=std::string::npos,"Skipped capture lost its budget reason");
        else need(auditString(row,"reason")==(draw?"Cannot write original first rigid draw-state capture":"Cannot write rigid capture file"),
                  "Failed capture lost its bounded I/O reason");
    }
}

void rangeRun(Runtime& rt,const PPCContext& entry,const char* dictionaryPath) {
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
    std::array<OriginalSubmeshPool,2> submeshPools;
    stage="original streamed one-row and65536-row allocation/relocation";
    submeshPools[0].read(rt,cpu,area+0xD900,1,options);
    submeshPools[1].read(rt,cpu,area+0xD940,largeRows,options);
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
        // Independent Boolean cases own their baseline. The inherited
        // sequence keeps the preceding original alpha cleanup unchanged.
        for(const auto& row:states)driver.directScalar(base,row[0],row[1]);
        cpu.invoke(0x826B7968,PPC_LOAD_U32(0x82E06F80+0x38),1,1);
    };
    resetPassState();

    const auto bound=driver.cameraBinding();uint32_t dispatcherCalls=0,fallbackCalls=0,vectorCalls=0,textureCalls=0,expectedAlpha=0,meshEntries=0;
    const OriginalSubmeshPool* activeSubmeshes=nullptr;
    bool armMalformed=false,malformedComplete=false;
    Observation observation([&](uint32_t pc,PPCContext& c) {
        HostRestore observerHost;
        if(pc==0x82740680){++dispatcherCalls;need(uint32_t(c.lr)==0x8273B4E0&&c.r3.u32==packet,"Original rigid dispatcher caller differs");}
        else if(pc==0x827400F8){++fallbackCalls;need(uint32_t(c.lr)==0x82740B28&&c.r3.u32==packet&&
            c.r4.u32==expectedAlpha&&c.r5.u32==0&&c.r6.u32==0,"Original rigid fallback flags/caller differ");}
        else if(pc==0x82701220) {
            ++meshEntries;
            std::fprintf(stderr,"AUDIT_RIGID_FIXTURE_RAW_ENTRY lr=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X expected_typed=%08X expected_count=%u expected_alpha=%u rows=%08X expected_rows=%08X cpu_match=%u\n",
                uint32_t(c.lr),c.r3.u32,c.r4.u32,c.r5.u32,c.r6.u32,c.r7.u32,c.r8.u32,typed,
                activeSubmeshes?activeSubmeshes->count:0,expectedAlpha,PPC_LOAD_U32(metadata+0x14),
                activeSubmeshes?activeSubmeshes->row():0,unsigned(currentContext==&c));
            need(activeSubmeshes&&currentContext==&c&&uint32_t(c.lr)==0x827402B0&&c.r3.u32==metadata&&
                 c.r4.u32==object&&c.r5.u32==typed&&!c.r6.u32&&c.r7.u32==activeSubmeshes->count&&
                 c.r8.u32==PPC_LOAD_U8(packet+0xC)&&PPC_LOAD_U32(metadata+0x14)==activeSubmeshes->row(),
                 "Original rigid mesh lost its actual zero-bone caller/count/typed owner");
            observeRigidEntry(rt,c,*activeSubmeshes);
            if(armMalformed&&!malformedComplete) {
                need(activeSubmeshes->count==largeRows&&expectedAlpha==1,"Malformed rigid controls were armed before the valid large opaque call");
                const auto row=activeSubmeshes->row(),last=row+36*(largeRows-1);
                const auto valuesBefore=effects.view(id).defaultVectorWords;
                const auto maskBefore=effects.privateModifiedMask(id);
                const auto recordingBefore=driver.recordingOwners().count();
                const auto metadataBefore=snapshot(rt,metadata,0x38);
                const auto reject=[&](const std::function<void()>& mutate,const char* reason,const char* action) {
                    HostRestore host; // First: protects all pointer/receipt/exception paths.
                    bool failed=false;std::string actual;uint32_t returnedCsr=0;DWORD returnedError=0;
                    bool contextPreserved=false;
                    {
                        ProbeRestore restore(c,rt.pointer(metadata+0x10,8,true),rt.pointer(last,36,true));
                        c.lastFunction=pc;mutate();rt.resourceAudit.action(action);
                        std::array<uint8_t,sizeof(PPCContext)> submitted{};std::memcpy(submitted.data(),&c,sizeof(c));
                        const auto probeCsr=(host.csr&~0x6000u)|0x4000u;constexpr DWORD probeError=0xA17D6536;
                        PPCFPSCRRegister::restoreHostCSR(probeCsr);SetLastError(probeError);
                        observeRigidEntry(rt,c,*activeSubmeshes);
                        try {SimpsonsNativeRigidImmediateMeshEntry(c,base);}
                        catch(const Failure& error) {
                            returnedCsr=PPCFPSCRRegister::getcsr();returnedError=GetLastError();failed=true;
                            contextPreserved=!std::memcmp(submitted.data(),&c,sizeof(c));actual=error.what();
                            rt.resourceAudit.failure(actual);
                        }
                        need(failed,"Malformed rigid range input was admitted");
                        need(actual==reason,"Malformed rigid range input rejected at the wrong frontier");
                        need(contextPreserved&&returnedCsr==probeCsr&&returnedError==probeError,
                             "Rejected rigid range input changed full PPC/host CSR/LastError");
                    }
                    need(effects.view(id).defaultVectorWords==valuesBefore&&effects.privateModifiedMask(id)==maskBefore&&
                         driver.recordingOwners().count()==recordingBefore,"Rejected rigid range changed FX/dirty/payload ownership");
                    same(rt,metadata,metadataBefore,"Rejected rigid range changed ordinary metadata");
                    activeSubmeshes->unchanged(rt);
                };
                // Controls enter the native adapter from the actual original
                // caller context; each restores metadata, rows and all PPC state.
                reject([&]{PPC_STORE_U32(metadata+0x10,largeRows+1);c.r7.u32=largeRows+1;},
                    "Original rigid submesh rows exceed their observed allocation owner","malformed-owner-count65537");
                reject([&]{PPC_STORE_U32(metadata+0x14,row+36);},
                    "Original rigid submesh rows exceed their observed allocation owner","malformed-shifted-row-view");
                reject([&]{PPC_STORE_U32(last+4,PPC_LOAD_U32(collection));},
                    "Rigid submesh material index is out of range","malformed-compiled-material-index");
                // Same control, same action: the stable row keys are now cached, so only the failure handler can
                // still bind this rejection to the offending row instead of the entry receipt.
                reject([&]{PPC_STORE_U32(last+4,PPC_LOAD_U32(collection));},
                    "Rigid submesh material index is out of range","malformed-compiled-material-index");
                reject([&]{PPC_STORE_U32(last,UINT32_MAX);},
                    "Rigid material offset table overflows","malformed-wrapped-material-selector");
                rt.resourceAudit.action("valid-streamed65536-alpha-after-malformed");
                observeRigidEntry(rt,c,*activeSubmeshes);malformedComplete=true;
            }
        }
        else if(pc==0x8270BBC0)++vectorCalls;else if(pc==0x8270BE50)++textureCalls;
    });
    PPC_STORE_U32(area+0x80,0x00FF00FF);need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original rigid main clear failed");
    const auto clearColor=driver.readbackColor(bound.colorIdentity);const auto mesh=snapshot(rt,geometry,0x700);const auto collectionCount=PPC_LOAD_U32(collection);
    // This is before the FIRST actual rigid call. No one-row draw or private flag reset occurs.
    stage="preserve exact source snapshots before first real large rigid capture";
    // Publish only the ordinary fixture metadata/packet fields that call()
    // would otherwise write. This makes the preserved pre-call tuple actual.
    PPC_STORE_U32(metadata+0x10,submeshPools[1].count);PPC_STORE_U32(metadata+0x14,submeshPools[1].row());
    PPC_STORE_U32(metadata+8,1);PPC_STORE_U8(packet+0xC,uint8_t(firstPhase));
    submeshPools[1].unchanged(rt);
    FirstCapture firstCapture(rt,submeshPools[1],id,typed,bound);
    // A real directory occupies one output name before the first call; the
    // owned open/close then fails exactly as it would for any blocked path.
    if(captureMode=="io-raw"||captureMode=="io-draw") {
        firstCapture.obstacle=captureDirectory/(captureMode=="io-raw"?"rigid-first-indices.bin":"rigid-first-draw.json");
        need(std::filesystem::create_directory(firstCapture.obstacle),"Cannot occupy the capture output name with a directory");
    }
    const auto inputs=materialInputs();
    const auto call=[&](const OriginalSubmeshPool& owner,uint32_t alpha,const char* action) {
        expectedAlpha=alpha;activeSubmeshes=&owner;
        owner.unchanged(rt);
        PPC_STORE_U32(metadata+0x10,owner.count);PPC_STORE_U32(metadata+0x14,owner.row());
        rt.resourceAudit.action(action);
        // Only the ordinary borrowed table view and packet alpha byte vary.
        // Genuine827400F8 supplies the full count, typed owner and alpha argument.
        PPC_STORE_U32(metadata+8,1);PPC_STORE_U8(packet+0xC,uint8_t(alpha));
        resetPassState();
        need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original variant clear failed");
        EngineCpuCalls draw(entry,base);const auto saved=fullAbi(draw.registers());try {draw.invoke(0x8273B4D0,packet);}
        catch(const Failure& error) {rt.resourceAudit.failure(error.what());
            if(std::string_view(action)==firstAction) {
                std::fprintf(stderr,"AUDIT_RIGID_FIRST_CAPTURE_FRONTIER phase=%u source=%08X count=%u reason=%s expected_snapshot=%s raw_receipts=%s\n",
                    firstPhase,source,owner.count,error.what(),(captureDirectory/"expected-first-capture.json").string().c_str(),rigidAuditPath.string().c_str());
                std::fflush(stderr);
            }
            throw;}
        need(fullAbi(draw.registers())==saved,"Whole original dual wrapper changed nonvolatile ABI");
        need(effects.privateModifiedMask(id)==std::array<uint8_t,128>{},"Dual dirty bank remains after commit");
        need(!PPC_LOAD_U32(0x82D0CAF8),"Dual draw published a console device");
        need(PPC_LOAD_U32(manager+8)==id&&PPC_LOAD_U32(manager+0xC)==(alpha?0x0007FFFCu:0x0003FFFCu),
             "Original rigid family selected the wrong owner or technique");
        const auto committed=effects.view(id).defaultVectorWords;
        for(uint32_t i=0;i<inputs.size();++i) {
            const auto& input=inputs[i];
            if(input.texture!=noTexture)need(committed[input.word]==textures[input.texture],
                "Original family callback did not store its real texture header");
            else for(uint32_t lane=0;lane<4;++lane)need(committed[input.word+lane]==PPC_LOAD_U32(values+16*i+4*lane),
                "Original family callback did not retain its authored vector lane");
        }
        const auto pixels=driver.readbackColor(bound.colorIdentity);
        need((pixel(pixels,640,360)!=pixel(clearColor,640,360))&&pixel(pixels,1100,360)==pixel(clearColor,1100,360),
             "Original dual draw produced no center pixel or escaped geometry bounds");
        same(rt,geometry,mesh,"Rigid range dispatcher changed immutable input geometry");owner.unchanged(rt);return pixels;
    };

    // First valid large call deliberately exposes the capture-only frozen guard.
    // No small reference, malformed probe or synthetic recovery runs first.
    stage="whole original FIRST65536 capture before small references or malformed controls";
    const auto firstLarge=call(submeshPools[1],firstPhase,firstAction);
    if(captureMode=="success")firstCapture.observeCompletedFirst();else firstCapture.observeIsolatedFirst();
    stage="whole original later one-row opaque reference";const auto oneOpaque=call(submeshPools[0],0,"valid-streamed1-opaque-reference");
    stage="whole original later one-row alpha reference";const auto oneAlpha=call(submeshPools[0],1,"valid-streamed1-alpha-reference");
    need(firstLarge==(firstPhase?oneAlpha:oneOpaque),"First65536 capture pixels differ from the later matching one-row reference");
    stage="whole original65536 opaque after first capture";
    const auto manyOpaque=call(submeshPools[1],0,"valid-streamed65536-opaque-after-capture");
    need(manyOpaque==oneOpaque,"Whole original65536 opaque pixels differ from one selected row");
    armMalformed=true;
    stage="whole original65536 alpha with isolated native-only prevalidation controls";
    const auto manyAlpha=call(submeshPools[1],1,"valid-streamed65536-alpha-preflight");
    need(malformedComplete&&manyAlpha==oneAlpha,"Whole original65536 alpha/later-valid pixels or malformed frontiers differ");
    stage="whole original ordinary one-row call after large/malformed inputs";
    need(call(submeshPools[0],0,"valid-streamed1-later-opaque")==oneOpaque,"Later original one-row pixels differ");
    const auto textureCount=uint32_t(std::count_if(inputs.begin(),inputs.end(),[](const auto& input){return input.texture!=noTexture;}));
    const auto vectorCount=uint32_t(inputs.size())-textureCount;
    need(dispatcherCalls==6&&fallbackCalls==6&&meshEntries==6&&vectorCalls==6*vectorCount&&textureCalls==6*textureCount,
         "Original rigid65536 skipped-row/material traversal did not select exactly one callback sequence per call");
    same(rt,source,sourceBytes,"Dual regression changed original serialized shader record");
    cpu.invoke(0x826B4B18,wrapper);need(cpu.invoke(0x823F1A08,camera)==camera,"Original main camera end failed");
    stage="original range rigid declaration and effect retirement";cpu.invoke(0x82700A78);
    rejects([&]{rt.engineAudio->allocationGeneration(declaration,0x50);},"Original range rigid declaration retained ownership");
    if(alternate)rejects([&]{rt.engineAudio->allocationGeneration(alternate,0x50);},"Original range rigid alternate retained ownership");
    cleanup.release(rt,cpu,manager);same(rt,source,sourceBytes,"Original range rigid cleanup changed serialized source");

    stage="original rigid row pools and immutable stream-source retirement";
    for(const auto& owner:submeshPools)owner.release(rt,cpu);
    const auto beforeRestore=fullAbi(cpu.registers());cpu.invoke(0x8282F4D0,previousPoolManager,previousPoolArgument);
    need(fullAbi(cpu.registers())==beforeRestore&&PPC_LOAD_U32(0x82E06F74)==previousPoolManager&&
         PPC_LOAD_U32(0x82E06F78)==previousPoolArgument,"Original rigid pool publication restoration differs");
    // Receipt assertions are diagnostic comparisons and happen only after real
    // camera/declaration/FX/cache/pool/source frees and publication restoration.
    const auto rows=rigidAuditRows();
    struct Expected {const OriginalSubmeshPool* pool;uint32_t count,view;const char* action,*event,*reason;};
    const std::array<Expected,11> expected{{
        {&submeshPools[1],largeRows,submeshPools[1].row(),firstAction,"encounter",nullptr},
        {&submeshPools[0],1,submeshPools[0].row(),"valid-streamed1-opaque-reference","encounter",nullptr},
        {&submeshPools[0],1,submeshPools[0].row(),"valid-streamed1-alpha-reference","encounter",nullptr},
        {&submeshPools[1],largeRows,submeshPools[1].row(),"valid-streamed65536-opaque-after-capture","encounter",nullptr},
        {&submeshPools[1],largeRows,submeshPools[1].row(),"valid-streamed65536-alpha-preflight","encounter",nullptr},
        {&submeshPools[1],largeRows+1,submeshPools[1].row(),"malformed-owner-count65537","encounter",nullptr},
        {&submeshPools[1],largeRows,submeshPools[1].row()+36,"malformed-shifted-row-view","encounter",nullptr},
        {&submeshPools[1],largeRows,submeshPools[1].row(),"malformed-compiled-material-index","encounter",nullptr},
        {&submeshPools[1],largeRows,submeshPools[1].row(),"malformed-wrapped-material-selector","encounter",nullptr},
        {&submeshPools[1],largeRows,submeshPools[1].row(),"valid-streamed65536-alpha-after-malformed","encounter",nullptr},
        {&submeshPools[0],1,submeshPools[0].row(),"valid-streamed1-later-opaque","encounter",nullptr}}};
    need(rows.size()==expected.size(),"Rigid fixture entry receipts missing, repeated or unexpected");
    for(size_t i=0;i<expected.size();++i) {
        const auto& e=expected[i];verifyRigidAudit(rows[i],*e.pool,e.count,e.view,typed,e.action,e.event);
        if(e.reason)need(auditString(rows[i],"reason")==e.reason,"Rigid fixture failure receipt lost its exact frontier");
    }
    const auto productionRows=rigidAuditRows(true);
    const std::array<Expected,12> productionExpected{{
        {&submeshPools[1],largeRows,submeshPools[1].row(),firstAction,"encounter",nullptr},
        {&submeshPools[0],1,submeshPools[0].row(),"valid-streamed1-opaque-reference","encounter",nullptr},
        {&submeshPools[0],1,submeshPools[0].row(),"valid-streamed1-alpha-reference","encounter",nullptr},
        {&submeshPools[1],largeRows,submeshPools[1].row(),"valid-streamed65536-opaque-after-capture","encounter",nullptr},
        {&submeshPools[1],largeRows+1,submeshPools[1].row(),"malformed-owner-count65537","encounter",nullptr},
        {&submeshPools[1],largeRows+1,submeshPools[1].row(),"malformed-owner-count65537","failure","Original rigid submesh rows exceed their observed allocation owner"},
        {&submeshPools[1],largeRows,submeshPools[1].row()+36,"malformed-shifted-row-view","encounter",nullptr},
        {&submeshPools[1],largeRows,submeshPools[1].row()+36,"malformed-shifted-row-view","failure","Original rigid submesh rows exceed their observed allocation owner"},
        {&submeshPools[1],largeRows,submeshPools[1].row(),"malformed-compiled-material-index","encounter",nullptr},
        {&submeshPools[1],largeRows,submeshPools[1].row(),"malformed-wrapped-material-selector","encounter",nullptr},
        {&submeshPools[1],largeRows,submeshPools[1].row(),"valid-streamed65536-alpha-after-malformed","encounter",nullptr},
        {&submeshPools[0],1,submeshPools[0].row(),"valid-streamed1-later-opaque","encounter",nullptr}}};
    need(productionRows.size()==productionExpected.size(),"Production rigid entry receipts missing, repeated or unexpected");
    for(size_t i=0;i<productionExpected.size();++i) {
        const auto& e=productionExpected[i];verifyRigidAudit(productionRows[i],*e.pool,e.count,e.view,typed,e.action,e.event,true);
        if(e.reason) {
            need(auditString(productionRows[i],"reason")==e.reason,"Production rigid failure lost its exact frontier");
            need(i&&auditString(productionRows[i],"group")==auditString(productionRows[i-1],"group")&&
                 auditString(productionRows[i],"instance")==auditString(productionRows[i-1],"instance"),
                 "Production rigid failure lost its pre-validation grouping/instance");
        }
    }
    // Row-level prevalidation receipts: every offending row's nine raw words are copied before
    // any offset/header/collection read, so the two row controls fail with ROW attribution while
    // the span controls (before any valid row copy) keep entry attribution above.
    stage="native per-row nine-word prevalidation receipts after complete original CPU cleanup";
    {
        const auto rowOf=[&](const OriginalSubmeshPool& pool,uint32_t count,uint32_t ordinal,const std::array<uint32_t,9>& words,
                             const char* action,const char* event="encounter",const char* reason=nullptr) {
            RigidRowCase row;row.poolFirst=pool.first;row.poolRow=pool.row();row.poolBytes=pool.firstBytes;row.poolGeneration=pool.firstGeneration;
            // The original dispatcher clears the alpha eligibility byte before the mesh call, so the raw entry argument recorded as `variant` is zero for every call.
            row.count=count;row.ordinal=ordinal;row.variant=0;row.words=words;row.action=action;row.event=event;row.reason=reason;return row;
        };
        const std::array<uint32_t,9> skipped{7,0,0,6,0,0,4,0,0},selected{2,0,0,6,0,0,4,0,0};
        auto badIndex=selected;badIndex[1]=collectionCount;auto badSelector=selected;badSelector[0]=UINT32_MAX;
        const auto& large=submeshPools[1];const auto& one=submeshPools[0];const uint32_t last=largeRows-1;
        const std::vector<RigidRowCase> expectedRows{
            rowOf(large,largeRows,0,skipped,firstAction),rowOf(large,largeRows,last,selected,firstAction),
            rowOf(one,1,0,selected,"valid-streamed1-opaque-reference"),
            rowOf(one,1,0,selected,"valid-streamed1-alpha-reference"),
            rowOf(large,largeRows,0,skipped,"valid-streamed65536-opaque-after-capture"),
            rowOf(large,largeRows,last,selected,"valid-streamed65536-opaque-after-capture"),
            rowOf(large,largeRows,0,skipped,"malformed-compiled-material-index"),
            rowOf(large,largeRows,last,badIndex,"malformed-compiled-material-index"),
            rowOf(large,largeRows,last,badIndex,"malformed-compiled-material-index","failure","Rigid submesh material index is out of range"),
            rowOf(large,largeRows,last,badIndex,"malformed-compiled-material-index","failure","Rigid submesh material index is out of range"),
            rowOf(large,largeRows,0,skipped,"malformed-wrapped-material-selector"),
            rowOf(large,largeRows,last,badSelector,"malformed-wrapped-material-selector"),
            rowOf(large,largeRows,last,badSelector,"malformed-wrapped-material-selector","failure","Rigid material offset table overflows"),
            rowOf(large,largeRows,0,skipped,"valid-streamed65536-alpha-after-malformed"),
            rowOf(large,largeRows,last,selected,"valid-streamed65536-alpha-after-malformed"),
            rowOf(one,1,0,selected,"valid-streamed1-later-opaque")};
        const auto rowRows=rigidRowAuditRows(rigidAuditPath);
        verifyRigidRows(rowRows,expectedRows,"synthetic-rigid-first-capture-fixture",source,metadata,object,typed,collectionCount,collection);
        // Independent field changes distinguish a row from the otherwise identical valid final row.
        const auto replaced=[](std::string text,const std::string& from,const std::string& to){
            const auto at=text.find(from);need(at!=std::string::npos&&text.find(from,at+1)==std::string::npos,"Row parameter to vary is absent or duplicated");
            return text.replace(at,from.size(),to);};
        const auto validLast=auditString(rowRows[14],"parameters");
        need(auditString(rowRows[7],"parameters")==replaced(validLast,"compiled_index=0","compiled_index="+std::to_string(collectionCount))&&
             auditString(rowRows[11],"parameters")==replaced(validLast,"rw_selector="+auditHex(2),"rw_selector=FFFFFFFF"),
             "A changed compiled index or selector did not alter only its own stable row field");
        need(auditString(rowRows[7],"group")!=auditString(rowRows[11],"group")&&auditString(rowRows[7],"group")!=auditString(rowRows[14],"group"),
             "Independent row field changes did not form distinct stable groups");
    }
    stage="exact capture comparison after complete original CPU cleanup";
    if(captureMode=="success")firstCapture.assertAfterOriginalCleanup();else firstCapture.assertIsolatedAfterCleanup();
    stage="non-attributing capture diagnostics after complete original CPU cleanup";verifyCaptureDiagnostics(submeshPools[1],typed);
    const auto opaqueVS=body+PPC_LOAD_U32(body+family->opaqueContext+0x48)+8;
    const auto opaquePS=body+PPC_LOAD_U32(body+family->opaqueContext+0x4C)+8;
    const bool isSuccess=captureMode=="success";
    const char* captureStatus=captureMode=="budget"?"skipped-budget":captureMode=="io-raw"?"failed-contained":"complete";
    const char* drawStatus=isSuccess?"complete":captureMode=="io-draw"?"failed-contained":"not-attempted";
    const char* rawComparison=captureMode=="budget"?"not-applicable":captureMode=="io-raw"?"closed-prefix-passed":"passed";
    std::printf("AUDIT_RIGID_FIRST_CAPTURE_LIFECYCLE source=%08X opaque_vertex=%08X opaque_pixel=%08X alpha_vertex=%08X alpha_pixel=%08X count=%u selected_rows=1 skipped_rows=%u bones=0 capture_mode=%s capture_status=%s draw_status=%s first_large_before_reference=passed first_phase=%u raw_capture_files=%zu raw_row_bytes=%llu raw_byte_comparison=%s selected_draw_metadata=%s later_capture_preserved=passed capture_diagnostics=passed row_prevalidation=passed create=passed use=passed opaque_alpha_pixels=passed malformed=4 exact_frontiers=passed later_valid=passed declaration_release=passed fx_release=passed cpu_cache_release=passed original_pool_release=passed immutable_source_free=passed stale_cpu_owner_queries=passed receipt_scope=production_and_fixture_before_validation backend_mesh_cache=owner_resident full_gpu_retirement=unproven receipts=%s\n",
        source,opaqueVS,opaquePS,family->alphaVertex,family->alphaPixel,largeRows,largeRows-1,captureMode.c_str(),captureStatus,drawStatus,firstPhase,
        captureMode=="io-raw"?size_t(2):captureMode=="budget"?size_t(0):size_t(5),36ull*largeRows,rawComparison,isSuccess?"passed":"not-applicable",rigidAuditPath.string().c_str());
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==6||argc==7,"Original image, loc_split4 ITXD dictionary, rigid family, first phase, public capture root and optional mode required");
        if(argc==7)captureMode=argv[6];
        need(captureMode=="success"||captureMode=="budget"||captureMode=="io-raw"||captureMode=="io-draw","First capture mode must be success, budget, io-raw or io-draw");
        largeRows=captureMode=="budget"?budgetRows:65536u;
        need(std::string_view(argv[4])=="opaque"||std::string_view(argv[4])=="alpha","First capture phase must be opaque or alpha");
        firstPhase=std::string_view(argv[4])=="alpha"?1u:0u;
        firstAction=firstPhase?"capture-first65536-alpha":"capture-first65536-opaque";
        const auto choice=std::find_if(families.begin(),families.end(),[&](const auto& candidate){return std::string_view(argv[3])==candidate.name;});
        need(choice!=families.end(),"Unknown original rigid first-capture family");family=&*choice;source=family->source;body=source+12;
        fixtureOwnerThread=GetCurrentThreadId();
        const auto captureRoot=std::filesystem::absolute(argv[5]);std::filesystem::create_directories(captureRoot);
        captureDirectory=captureRoot/("rigid-first-"+std::string(family->name)+"-"+argv[4]+(captureMode=="success"?"":"-"+captureMode)+"-"+
            std::to_string(GetCurrentProcessId())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        need(std::filesystem::create_directory(captureDirectory),"Cannot reserve unique public rigid capture directory");
        Runtime rt;rt.frameCaptureDirectory=captureDirectory;rt.captureOnRequest=true;
        rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        rigidAuditPath=captureDirectory/"resources.jsonl";
        const auto file=CreateFileW(rigidAuditPath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        need(file!=INVALID_HANDLE_VALUE,"Cannot reserve unique rigid fixture receipt");need(CloseHandle(file)!=0,"Cannot close rigid fixture receipt reservation");
        rt.resourceAudit.configure(rigidAuditPath);rt.resourceAudit.mission("synthetic-rigid-first-capture-fixture");
        rt.resourceAudit.action("whole-original-rigid-first-capture-setup");
        std::printf("AUDIT_RIGID_FIRST_CAPTURE_RAW phase=%u mode=%s source=%08X path=%s capture_directory=%s scope=fixture_and_production_before_validation\n",
            firstPhase,captureMode.c_str(),source,rigidAuditPath.string().c_str(),captureDirectory.string().c_str());
        need(std::fflush(stdout)==0,"Rigid capture raw marker flush failed");
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;
        try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original rigid startup boundary missing");rangeRun(rt,entry,argv[2]);
        std::printf("PASS original rigid first capture %s phase=%u mode=%s: %zu checks; first large real draw, contained capture outcome, opaque+alpha references, controls, diagnostics and scoped CPU cleanup\n",family->name,firstPhase,captureMode.c_str(),checks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original rigid first capture %s phase=%u: %zu checks stage=%s: %s\n",family->name,firstPhase,checks,stage,error.what());return 1;}
}
