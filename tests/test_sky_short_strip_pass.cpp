// Original selected counts0/1/2 retain real nonempty buffer owners and emit no triangles.
// Real allocator, original8273B760 inline headers/declaration, original draw and paired release.
#define main OriginalSkyBaselineEntrypoint
#include "test_sky_pass.cpp"
#undef main
#include "runtime/sky_vertices.h"
#include "runtime/character_mesh.h"
#include "common/geometry_extent.h"
#include "renderer/r16_index_validation.h"

namespace {
void shortStripRun(Runtime& rt,const PPCContext& entry,uint32_t selectedCount) {
    constexpr bool partialTail=false;constexpr uint32_t indexOwnerBytes=14;
    auto* base=rt.base;auto& driver=*rt.engineDriver;EngineCpuCalls cpu(entry,base);
    OriginalDrawCatalogCleanup cleanup(rt);evidence(base);
    rt.map(area,0x10000,true,"original sky large-owner fixture");
    std::memset(rt.pointer(area,0x10000,true),0,0x10000);
    stage="large-owner original effect and image constructors";
    const auto context=PPC_LOAD_U32(0x82D5DA74),options=cpu.registers().r1.u32+0x60;
    PPC_STORE_U32(options,2);PPC_STORE_U32(options+4,16);PPC_STORE_U32(options+8,0);
    const auto manager=cpu.invoke(0x8269BF70,0x260,options);cpu.invoke(0x826B6F60,manager,context);
    need(!cpu.invoke(0x827019E8,table,25)&&!cpu.invoke(0x827019E8,secondTable,24),"Original large-owner catalogs failed");
    cpu.invoke(0x826B7218,manager);
    const auto typed=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(secondTable+13*16+4));
    const auto shadow=cpu.invoke(0x826B7088,manager,PPC_LOAD_U32(table+4*16+4));
    need(typed&&shadow,"Original large-owner typed effect parents absent");
    const auto id=PPC_LOAD_U32(typed+0x1C),wrapper=PPC_LOAD_U32(typed+0x18);
    cache(rt,cpu,id,body);queries(rt,cpu,id,body);const auto immutable=snapshot(rt,source,11712);
    need(!cpu.invoke(0x826FF0F8,context,0)&&driver.builtinTextures().complete(),"Original large-owner image parent failed");
    const auto white=PPC_LOAD_U32(0x82D63004),black=PPC_LOAD_U32(0x82D6300C);
    need(white&&black&&white!=black,"Original large-owner image owners absent");
    for(uint32_t slot=0;slot<2;++slot) {
        const auto camera=PPC_LOAD_U32(shadow+0x5B4+4*slot);PPC_STORE_U32(area+0x80,0x000000FF);
        need(cpu.invoke(0x823F1B80,camera,area+0x80,7)==camera,"Original large-owner shadow clear failed");
        cpu.invoke(0x82707220,shadow,slot);
    }
    const auto viewport=cpu.invoke(0x8269D788);need(viewport,"Original large-owner viewport missing");
    cpu.registers().f1.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12CC));cpu.registers().f2.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D0));
    cpu.registers().f3.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D4));cpu.registers().f4.f64=std::bit_cast<float>(PPC_LOAD_U32(0x82CD12D8));
    cpu.invoke(0x8269D608,viewport,0);const auto camera=PPC_LOAD_U32(viewport+0x14),frame=PPC_LOAD_U32(camera+4);
    fixture(rt,camera,typed,shadow,white);
    constexpr uint32_t ownerCount=4,stride=28,baseVertex=0;
    const uint32_t ownerBytes=ownerCount*stride+(partialTail?12u:0u);
    const auto quad=snapshot(rt,vertices,4*stride);
    stage="original large vertex allocation and inline geometry/header constructor";
    const auto payload=cpu.invoke(0x8269BF70,ownerBytes,options);
    need(payload&&!(payload&15),"Original large vertex allocator did not publish aligned storage");
    const auto generation=rt.engineAudio->allocationGeneration(payload,ownerBytes);
    rejects([&]{rt.engineAudio->allocationGeneration(payload,ownerBytes+1);},"Original large vertex allocation admitted bytes outside its owner");
    std::memset(rt.pointer(payload,ownerBytes,true),0,ownerBytes);
    if(partialTail)std::memset(rt.pointer(payload+ownerCount*stride,12,true),0xFF,12);
    std::memcpy(rt.pointer(payload+baseVertex*stride,4*stride,true),quad.data(),quad.size());
    PPC_STORE_U32(geometry,ownerBytes);PPC_STORE_U32(geometry+0x10,payload);
    constexpr std::array<uint16_t,7> words={0,1,2,UINT16_MAX,2,1,3};
    const uint32_t indexBytes=indexOwnerBytes?indexOwnerBytes:uint32_t(words.size())*2;
    const auto indexPayload=indexOwnerBytes?cpu.invoke(0x8269BF70,indexBytes,options):indices;
    uint64_t indexGeneration{};
    if(indexOwnerBytes) {
        need(indexPayload&&!(indexPayload&15),"Original independent index allocator did not publish aligned storage");
        indexGeneration=rt.engineAudio->allocationGeneration(indexPayload,indexBytes);
        rejects([&]{rt.engineAudio->allocationGeneration(indexPayload,indexBytes+1);},
                "Original index allocation admitted bytes outside its owner");
        std::memset(rt.pointer(indexPayload,indexBytes,true),0xFF,indexBytes);
    }
    for(uint32_t i=0;i<words.size();++i)PPC_STORE_U16(indexPayload+2*i,words[i]);
    PPC_STORE_U32(geometry+0x14,indexBytes);PPC_STORE_U32(geometry+0x1C,indexPayload);
    PPC_STORE_U32(submeshes+16,baseVertex);PPC_STORE_U32(submeshes+20,0);PPC_STORE_U32(submeshes+24,selectedCount);
    // The established metadata fixture has no optional material-child owner;
    //8273B760 therefore runs its actual inline VB/IB/declaration setup only.
    need(!PPC_LOAD_U32(metadata+0x30),"Original large fixture acquired an unproved material child");
    const auto setupAbi=fullAbi(cpu.registers());
    need(!cpu.invoke(0x8273B760,metadata)&&fullAbi(cpu.registers())==setupAbi,
         "Whole original large geometry setup failed or damaged nonvolatile ABI");
    const auto cached=PPC_LOAD_U32(geometry+0x30),declaration=PPC_LOAD_U32(cached+4);
    need(declaration&&rt.engineAudio->allocationGeneration(declaration,0x50),"Original large-owner declaration has no allocator owner");
    need(PPC_LOAD_U32(geometry+0x38)==1&&PPC_LOAD_U32(geometry+0x3C)==1&&
         PPC_LOAD_U32(geometry+0x50)==(payload|3)&&PPC_LOAD_U32(geometry+0x54)==(ownerBytes|0x10000002)&&
         PPC_LOAD_U32(geometry+0x58)==0x20000002&&PPC_LOAD_U32(geometry+0x5C)==1&&
         PPC_LOAD_U32(geometry+0x70)==indexPayload&&PPC_LOAD_U32(geometry+0x74)==indexBytes,
         "Original large resource byte extent/address/reference publication differs");
    const auto owned=snapshot(rt,payload,ownerBytes),decl=snapshot(rt,elements,48),ownedIndices=snapshot(rt,indexPayload,indexBytes);
    stage="malformed owned source extents and selected effective ranges";
    const auto beforeMalformed=snapshot(rt,geometry,0x700);
    rejects([&]{decodeSkyVertices({},decl,stride);},"Empty original owner extent was admitted");
    rejects([&]{decodeSkyVertices(std::span<const uint8_t>(owned).first(ownerBytes-1),decl,stride);},
            "Truncated original owner record was admitted");
    rejects([&]{decodeSkyVertices(owned,decl,stride+1);},"Unaligned original owner stride was admitted");
    need(validOriginalVertexExtent(ownerBytes,stride)&&!validOriginalVertexExtent(0x04000000,stride),
         "Original26-bit resource bound admitted an unproved upper-bit alias");
    need(originalFetchedVertexCount(ownerBytes,stride,28)==ownerCount,
         "Original large byte owner acquired a vertex from its unused tail");
    using Simpsons::Graphics::validR16DrawRange;
    need(validR16DrawRange(words,ownerCount,0,selectedCount,int32_t(baseVertex)),"Original short selected range was rejected");
    need(!validR16DrawRange(words,ownerCount,uint32_t(words.size()+1),selectedCount,0)&&
         !validR16DrawRange(words,ownerCount,UINT32_MAX,1,0)&&
         !validR16DrawRange(words,ownerCount,0,1,-1)&&
         !validR16DrawRange(words,ownerCount,0,1,int32_t(ownerCount)),
         "Malformed short selected ownership/index extent was admitted");
    const auto decodedIndices=decodeCharacterIndices(ownedIndices);
    need(decodedIndices.size()==indexBytes/2&&std::equal(words.begin(),words.end(),decodedIndices.begin())&&
         validR16DrawRange(decodedIndices,ownerCount,0,uint32_t(words.size()),int32_t(baseVertex)),
         "Independent original index byte owner lost complete words or rejected its selected prefix");
    need(!validR16DrawRange(decodedIndices,ownerCount,uint32_t(decodedIndices.size()-2),3,int32_t(baseVertex)),
         "Selected draw fetched an index beyond its actual byte owner");
    const auto truncated=decodeCharacterIndices(std::span<const uint8_t>(ownedIndices).first(words.size()*2-1));
    need(truncated.size()==words.size()-1&&!validR16DrawRange(truncated,ownerCount,0,uint32_t(words.size()),int32_t(baseVertex)),
         "Truncated selected R16 source acquired a word from an incomplete final byte");
    rejects([&]{decodeCharacterIndices(std::span<const uint8_t>(ownedIndices).first(1));},
            "Index source without a complete word was admitted");
    if(indexBytes>words.size()*2) {
        need(PPC_LOAD_U8(indexPayload+indexBytes-1)==0xFF&&decodedIndices.back()==UINT16_MAX,
             "Original unused index tail was decoded as another selected word or modified");
    }
    same(rt,geometry,beforeMalformed,"Malformed large source checks changed original geometry/header data");
    same(rt,payload,owned,"Malformed large source checks changed the allocated owner");
    same(rt,indexPayload,ownedIndices,"Malformed index source checks changed its independently owned bytes");
    Restore flags(rt,0x82D6CCA8,4),nativeContext(rt,0x82D6D890,4),published(rt,0x82D63028,4),
        materialRoot(rt,0x82D6D814,4),recordEnabled(rt,0x82CF0BE8,4),dirty(rt,0x82D00F80,4),
        propertyBase(rt,0x82D6C0C0,4),view(rt,0x82D0CA70,64),projection(rt,0x82CD1AB0,64),cameraMatrix(rt,frame+0x10,64),
        environment(rt,0x82D6C7F0,4);
    PPC_STORE_U32(0x82D6CCA8,0);PPC_STORE_U32(0x82D6D890,context);PPC_STORE_U32(0x82D63028,0);
    PPC_STORE_U32(0x82D6D814,headers);PPC_STORE_U32(0x82CF0BE8,0);PPC_STORE_U32(0x82D00F80,0);PPC_STORE_U32(0x82D6C0C0,0);
    PPC_STORE_U32(0x82D6C7F0,black);
    need(cpu.invoke(0x823F1A18,camera)==camera,"Original large-owner camera begin failed");
    identity(base,frame+0x10);identity(base,0x82D0CA70);identity(base,0x82CD1AB0);
    const auto geometryBytes=snapshot(rt,geometry,0x700);
    constexpr std::array<std::array<uint32_t,2>,22> states={{{0x28,1},{0x2C,7},{0x30,1},{0x34,0},{0x3C,0},{0x60,0},{0x6C,0},
        {0xAC,0},{0xC0,1},{0xC4,UINT32_MAX},{0xC8,0},{0xCC,0},{0xD0,0},{0xD4,15},{0xE4,0},{0x130,1},
        {0x144,1},{0x148,1},{0x14C,0xFFFF},{0x150,0},{0xD8,0},{0xDC,0}}};
    uint32_t dispatches=0,drawEntries=0,vectors=0,textures=0;
    Observation observed([&](uint32_t pc,PPCContext& c){
        if(pc==0x82740680){++dispatches;need(uint32_t(c.lr)==0x8273B4E0&&c.r3.u32==packet,"Large-owner original dispatcher differs");}
        else if(pc==0x82701220){++drawEntries;need(uint32_t(c.lr)==0x827402B0&&c.r5.u32==typed,"Large-owner original mesh producer differs");}
        else if(pc==0x8270BBC0)++vectors;else if(pc==0x8270BE50)++textures;
    });
    std::vector<uint8_t> reference;
    stage="original large-owner draw all eight Boolean passes";
    for(uint32_t alpha=0;alpha<2;++alpha)for(uint32_t bit1=0;bit1<2;++bit1)for(uint32_t bit2=0;bit2<2;++bit2) {
        if(PPC_LOAD_U32(manager+4))cpu.invoke(0x826B4B18,wrapper);
        for(const auto& row:states)driver.directScalar(base,row[0],row[1]);
        cpu.invoke(0x826B7968,PPC_LOAD_U32(0x82E06F80+0x38),1,1);
        PPC_STORE_U8(packet+12,uint8_t(alpha));PPC_STORE_U32(metadata+8,1|(bit1<<1)|(bit2<<2));
        PPC_STORE_U32(area+0x80,0x00FF00FF);need(cpu.invoke(0x823EE940,camera,area+0x80,7)==1,"Original large-owner camera clear failed");
        const auto binding=driver.cameraBinding();
        const auto clear=driver.readbackColor(binding.colorIdentity),depthClear=driver.readbackDepth(binding.depthIdentity);
        EngineCpuCalls pass(entry,base);const auto before=fullAbi(pass.registers());pass.invoke(0x8273B4D0,packet);
        need(fullAbi(pass.registers())==before,"Original large-owner draw damaged nonvolatile ABI");
        const auto pixels=driver.readbackColor(binding.colorIdentity);
        need(pixels==clear&&driver.readbackDepth(binding.depthIdentity)==depthClear,"Original short strip generated color/depth output without a triangle");
        if(reference.empty())reference=pixels;else need(reference==pixels,"Large-owner Boolean passes changed effective quad pixels");
        same(rt,geometry,geometryBytes,"Original large-owner draw mutated its owned geometry/index input");
        same(rt,payload,owned,"Original large-owner draw mutated its allocated vertex owner");
        same(rt,indexPayload,ownedIndices,"Original selected draw mutated its independent index owner or inert tail");
        need(rt.engineAudio->allocationGeneration(payload,ownerBytes)==generation,"Original large-owner draw lost allocator ownership");
        if(indexOwnerBytes)need(rt.engineAudio->allocationGeneration(indexPayload,indexBytes)==indexGeneration,
                               "Original selected draw lost independent index allocator ownership");
    }
    need(dispatches==8&&drawEntries==8&&vectors==16&&textures==24,"Original large-owner callback/draw traversal differs");
    cpu.invoke(0x826B4B18,wrapper);need(cpu.invoke(0x823F1A08,camera)==camera,"Original large-owner camera end failed");
    stage="original large-owner declaration and effect retirement";
    cpu.invoke(0x82700A78);
    rejects([&]{rt.engineAudio->allocationGeneration(declaration,0x50);},"Original large-owner declaration retained allocator ownership");
    const auto freeAbi=fullAbi(cpu.registers());cpu.invoke(0x8269BF10,payload);
    need(fullAbi(cpu.registers())==freeAbi,"Original large vertex retirement damaged nonvolatile ABI");
    rejects([&]{rt.engineAudio->allocationGeneration(payload,ownerBytes);},"Original large vertex allocator retained stale ownership");
    if(indexOwnerBytes) {
        const auto indexFreeAbi=fullAbi(cpu.registers());cpu.invoke(0x8269BF10,indexPayload);
        need(fullAbi(cpu.registers())==indexFreeAbi,"Original index retirement damaged nonvolatile ABI");
        rejects([&]{rt.engineAudio->allocationGeneration(indexPayload,indexBytes);},"Original index allocator retained stale ownership");
    }
    cleanup.release(rt,cpu,manager);same(rt,source,immutable,"Original large-owner lifecycle changed serialized source");
    std::printf("AUDIT_GEOMETRY_SHORT_STRIP source=%08X original_setup=8273B760 original_draw=82701220 sdk_draw=8244D360 vertices=4 index_bytes=14 selected_start=0 selected_count=%u opaque_vertex=82036C2C opaque_pixel=820371EC alpha_vertex=82036F08 alpha_pixel=820374E8 create=passed header_publication=passed selected_draw=passed no_triangle_output=passed declaration_release=passed vertex_owner_release=passed index_owner_release=passed fx_release=passed cpu_cache_release=passed stale_use=passed malformed=passed nonempty_buffers=required backend_mesh_cache=owner_resident full_gpu_retirement=unproven\n",source,selectedCount);
}
}

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try {
        need(argc==3&&std::string_view(argv[2]).size()==1&&argv[2][0]>='0'&&argv[2][0]<='2',
             "Original image and independent selected count0/1/2 required");
        const auto selectedCount=uint32_t(argv[2][0]-'0');
        Runtime rt;rt.load(argv[1]);PPCContext startup{};rt.initialize(startup);const auto entry=startup;
        bool observed=false;rt.graphicsStartupObserver=observeGraphics;
        try{runOriginal(startup,rt.base);}catch(const Observed&){observed=true;}
        rt.graphicsStartupObserver={};need(observed,"Original short-strip startup boundary missing");
        shortStripRun(rt,entry,selectedCount);
        std::printf("PASS original sky short strip: %zu checks; nonempty original allocator/header/declaration, eight count%u draws, unchanged color/depth, ABI, selected bounds and paired original release\n",checks,selectedCount);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL original sky short strip: %zu checks stage=%s: %s\n",checks,stage,error.what());return 1;}
}
