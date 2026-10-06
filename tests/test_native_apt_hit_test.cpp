// Real Apt CIH bounds, ancestor transforms, original point classifier and
// native MovieClip.hitTest and original mouse event domains. A hidden background
// window supplies the renderer's frozen aspect; no game loop or GPU draw runs.
#include "runtime/runtime.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include "runtime/native_controllers.h"
#include "runtime/native_window.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

void SimpsonsNativeAptHitTest(PPCContext&,uint8_t*);
void SimpsonsNativeMenuMouse(PPCContext&,uint8_t*);
void SimpsonsNativeMainMenuExit(PPCContext&,uint8_t*);
bool SimpsonsNativeMainMenuExitQueryCallback(PPCContext&,uint8_t*);

namespace {
using namespace Simpsons;
constexpr uint32_t text=0x20000,row=0x20100,parent=0x20200,
                   textData=0x21000,rowData=0x21100,parentData=0x21200,
                   rowList=0x21300,parentList=0x21310,renderContext=0x22000,
                   matrixOwner=0x23000,matrixOutput=0x23100,gc=0x24000,
                   boolean=0x24100,items=0x25000,xValue=0x25100,yValue=0x25110,
                   shapeValue=0x25120,undefined=0x25130,out=0x26000,
                   pointerRecord=0x26100,asStack=0x82E02840;
constexpr std::array<float,6> identity{1,0,0,1,0,0};
size_t checks{};
void need(bool value,const char* why){++checks;if(!value)throw Failure(why);}
void setFloat(uint8_t* base,uint32_t at,float value){PPC_STORE_U32(at,std::bit_cast<uint32_t>(value));}
float getFloat(uint8_t* base,uint32_t at){return std::bit_cast<float>(PPC_LOAD_U32(at));}
void matrix(uint8_t* base,uint32_t at,const std::array<float,6>& values) {
    for(uint32_t i=0;i<values.size();++i)setFloat(base,at+4*i,values[i]);
}
void pinSource(uint8_t* base) {
    constexpr std::array<std::array<uint32_t,2>,3> leaf{{
        {0x827E39B8,0x3D6082E0},{0x827E39BC,0x806B1C7C},{0x827E39C0,0x4E800020}}};
    for(const auto word:leaf)need(PPC_LOAD_U32(word[0])==word[1],"Original Apt hitTest placeholder identity changed");
    // Pin actual original affine/bounds pipeline calls and the independent
    // original mouse point classifier's call into global bounds.
    constexpr std::array<std::array<uint32_t,2>,6> calls{{
        {0x827E9774,0x827F01A8},{0x827E978C,0x827F0100},
        {0x827E9798,0x827F0278},{0x827E97C8,0x827E95B0},
        {0x827E97D0,0x827F0148},{0x827EC114,0x827E9720}}};
    for(const auto call:calls)
        need(PPC_LOAD_U32(call[0])==(0x48000001u|((call[1]-call[0])&0x03FFFFFCu)),
             "Original Apt bounds or point classifier call changed");
}
PPCContext seeded(const PPCContext& entry,uint32_t receiver,uint32_t argc=3) {
    auto c=entry;
#define SEED(n) c.r##n.u64=0x7193000000000000ull+n;c.f##n.f64=double(n)+0.25
    SEED(14);SEED(15);SEED(16);SEED(17);SEED(18);SEED(19);SEED(20);SEED(21);SEED(22);
    SEED(23);SEED(24);SEED(25);SEED(26);SEED(27);SEED(28);SEED(29);SEED(30);SEED(31);
#undef SEED
    c.v0.u32[0]=0xF17893A5;c.v13.u32[3]=0x517E2604;c.v31.u32[1]=0xC729B351;
    c.r3.u64=receiver;c.r4.u64=argc;c.lr=0x827D6220;c.lastFunction=0x827E39B8;
    return c;
}
struct Fixture {
    Runtime rt;PPCContext entry{};uint8_t* base;
    explicit Fixture(const char* image):base(nullptr) {
        rt.load(image);rt.initialize(entry);base=rt.base;pinSource(base);
        rt.map(0x20000,0x7000,true,"original Apt hitTest CIH fixture");
        // Original global matrix callback writes only the mapped proxy matrix.
        // The actual affine pipeline and push/pop callbacks remain unchanged.
        PPC_STORE_U32(0x82E01CC0,renderContext);
        matrix(base,renderContext+32,identity);
        matrix(base,0x82E01CC4,identity); // Original startup's identity affine source.
        PPC_STORE_U32(0x82E01C30,0x827F4EF8);
        PPC_STORE_U32(0x82D08BF4,matrixOwner);PPC_STORE_U32(matrixOwner+4,matrixOutput);
        setFloat(base,0x82CF7D74,1);setFloat(base,0x82CF7D78,1);
        PPC_STORE_U32(0x82E01BA0,gc); // Original Boolean free-list fast path, no heap expansion.
        PPC_STORE_U32(0x82E01C7C,undefined);PPC_STORE_U32(undefined+4,0x08000001);
        PPC_STORE_U32(asStack,3);PPC_STORE_U32(asStack+4,3);PPC_STORE_U32(asStack+8,items);
        PPC_STORE_U32(items,shapeValue);PPC_STORE_U32(items+4,yValue);PPC_STORE_U32(items+8,xValue);
        for(const auto value:{xValue,yValue})PPC_STORE_U32(value+4,0x08000006); // Real Apt float kind6.
        PPC_STORE_U32(shapeValue+4,0x08000007);PPC_STORE_U32(shapeValue+8,0);
        cih(text,textData,15);cih(row,rowData,13);cih(parent,parentData,13);
        // Movie character display-list indirection retains original ED4F0
        // traversal. A child with negative mask depth contributes its bounds.
        PPC_STORE_U32(rowData+36,rowList);PPC_STORE_U32(rowList,row);PPC_STORE_U32(row+80,text);
        PPC_STORE_U32(parentData+36,parentList);PPC_STORE_U32(parentList,parent);PPC_STORE_U32(parent+80,row);
        PPC_STORE_U32(textData+4,0xFFFFFFFF);PPC_STORE_U32(rowData+4,0xFFFFFFFF);
        localRect({-10,-5,20,10});
    }
    void cih(uint32_t object,uint32_t data,uint32_t kind) {
        PPC_STORE_U32(object+4,0x08000000|kind);PPC_STORE_U32(object+72,data);
        PPC_STORE_U32(object+88,0x800);matrix(base,object+12,identity);
    }
    void localRect(const std::array<float,4>& rect) {
        for(uint32_t i=0;i<rect.size();++i)setFloat(base,textData+80+4*i,rect[i]);
    }
    std::array<uint32_t,6> currentMatrix() {
        std::array<uint32_t,6> result{};
        for(uint32_t i=0;i<result.size();++i)result[i]=PPC_LOAD_U32(renderContext+32+4*i);
        return result;
    }
    std::array<float,4> bounds(uint32_t receiver) {
        const auto before=currentMatrix();const auto depth=PPC_LOAD_U32(renderContext+956);
        EngineCpuCalls cpu(entry,base);const auto sp=cpu.registers().r1.u64,lr=cpu.registers().lr;
        cpu.invoke(0x827E9720,receiver,out);
        need(cpu.registers().r1.u64==sp&&cpu.registers().lr==lr,"Original Apt bounds did not restore stack/LR");
        need(before==currentMatrix()&&PPC_LOAD_U32(renderContext+956)==depth,"Original Apt bounds leaked its transform stack");
        return {getFloat(base,out),getFloat(base,out+4),getFloat(base,out+8),getFloat(base,out+12)};
    }
    bool originalPoint(uint32_t receiver,int32_t x,int32_t y) {
        PPC_STORE_U32(pointerRecord+84,uint32_t(x));PPC_STORE_U32(pointerRecord+88,uint32_t(y));
        EngineCpuCalls cpu(entry,base);const auto result=cpu.invoke(0x827EC0F8,pointerRecord,receiver);
        need(result<=1,"Original Apt point classifier returned a non-Boolean result");return result!=0;
    }
    bool hit(uint32_t receiver,float x,float y,bool generated=false,uint32_t argc=3,bool expectUndefined=false) {
        setFloat(base,xValue+8,x);setFloat(base,yValue+8,y);
        PPC_STORE_U32(boolean+4,0x08000005);PPC_STORE_U32(boolean+8,0);
        PPC_STORE_U32(0x82E01D08,boolean);
        auto c=seeded(entry,receiver,argc);auto expected=c;
        const auto previousContext=currentContext;const auto hostFp=PPCFPSCRRegister::getcsr();
        const auto before=currentMatrix();const auto depth=PPC_LOAD_U32(renderContext+956);
        const std::array stackBefore{PPC_LOAD_U32(asStack),PPC_LOAD_U32(asStack+4),PPC_LOAD_U32(asStack+8),
                                    PPC_LOAD_U32(items),PPC_LOAD_U32(items+4),PPC_LOAD_U32(items+8)};
        std::array<uint8_t,32> argsBefore{};
        std::memcpy(argsBefore.data(),rt.pointer(items,uint32_t(argsBefore.size()),false),argsBefore.size());
        // The caller's live frame is above the scratch frame reserved by the
        // native-to-original call adapter and must retain every byte.
        std::array<uint8_t,0xC0> frameBefore{};
        std::memcpy(frameBefore.data(),rt.pointer(c.r1.u32,uint32_t(frameBefore.size()),false),frameBefore.size());
        SetLastError(0x6192);
        if(generated)PPCSafeIndirect(c,base,0x827E39B8);else SimpsonsNativeAptHitTest(c,base);
        const auto result=c.r3.u32;expected.r3.u64=c.r3.u64;
        // The generated entry records exactly one ordinary AOT trace sample.
        // Original calls inside the native callback use an isolated context.
        if(generated){expected.lastFunction=0x827E39B8;expected.trace[expected.traceIndex++&63]=0x827E39B8;}
        if(std::memcmp(&c,&expected,sizeof(c))!=0) {
            const auto* actual=reinterpret_cast<const uint8_t*>(&c);
            const auto* wanted=reinterpret_cast<const uint8_t*>(&expected);
            size_t offset=0;while(offset<sizeof(c)&&actual[offset]==wanted[offset])++offset;
            std::fprintf(stderr,"[APT ABI] receiver=%08X point=%.9g,%.9g generated=%u argc=%u first_byte=%zu actual=%02X expected=%02X\n",
                receiver,double(x),double(y),unsigned(generated),argc,offset,unsigned(actual[offset]),unsigned(wanted[offset]));
        }
        need(std::memcmp(&c,&expected,sizeof(c))==0,"Apt hitTest changed unrelated PPC registers or FP lanes");
        need(currentContext==previousContext&&PPCFPSCRRegister::getcsr()==hostFp&&GetLastError()==0x6192,
             "Apt hitTest changed host FP, error or current-context state");
        need(std::memcmp(frameBefore.data(),rt.pointer(c.r1.u32,uint32_t(frameBefore.size()),false),frameBefore.size())==0,
             "Apt hitTest overwrote the caller's live stack frame");
        need(before==currentMatrix()&&depth==PPC_LOAD_U32(renderContext+956),"Apt hitTest leaked its bounds transform scope");
        need(stackBefore==std::array{PPC_LOAD_U32(asStack),PPC_LOAD_U32(asStack+4),PPC_LOAD_U32(asStack+8),
                                    PPC_LOAD_U32(items),PPC_LOAD_U32(items+4),PPC_LOAD_U32(items+8)},
             "Native Apt callback popped or overwrote AVM arguments");
        need(std::memcmp(argsBefore.data(),rt.pointer(items,uint32_t(argsBefore.size()),false),argsBefore.size())==0,
             "Native Apt callback overwrote another live AVM argument");
        if(argc!=3||expectUndefined){need(result==undefined,"Unimplemented Apt hitTest overload did not retain undefined");return false;}
        need(result==boolean&&(PPC_LOAD_U32(result+4)&0x0800007F)==0x08000005,
             "Native Apt hitTest did not use original Boolean allocation");
        need(PPC_LOAD_U8(result+8)<=1,"Native Apt hitTest returned a noncanonical Boolean");
        return PPC_LOAD_U8(result+8)!=0;
    }
};
void exerciseConstantReload(const char* image) {
    Fixture f(image);auto* base=f.base;
    constexpr uint32_t apt=0x30000,cons=0x31000,values=0x32000,counter=0x33000;
    f.rt.map(apt,0x4000,true,"original Apt constant unload/reload fixture");
    // The original walker handles Push and ConstantPool with the same path,
    // scans into DefineFunction bodies, and keeps a caller-owned ordinal.
    // Pin its actual writes, its reload table lookup, and the public unload
    // wrapper's argument shift rather than reproducing that algorithm here.
    constexpr std::array<std::array<uint32_t,2>,14> source{{
        {0x827D5BD4,0x817D0004},{0x827D5BD8,0x81580000},{0x827D5BDC,0x7D5F592E},
        {0x827D5BE0,0x81780000},{0x827D5BE4,0x396B0001},{0x827D5BE8,0x91780000},
        {0x827D5ACC,0x81780000},{0x827D5AE0,0x91380000},{0x827D5AE4,0x815A001C},
        {0x827D6040,0x7CA62B78},{0x827D6044,0x38A00000},{0x827D6048,0x4BFFF890},
        {0x8215A804,0x827C2D30},{0x8215A808,0x827C2D60}}};
    for(const auto word:source)need(PPC_LOAD_U32(word[0])==word[1],"Original Apt constant relocation identity changed");
    PPC_STORE_U8(apt,0x96);PPC_STORE_U32(apt+4,2);PPC_STORE_U32(apt+8,0x80);
    PPC_STORE_U8(apt+12,0x9B);
    PPC_STORE_U32(apt+16,0xE0);PPC_STORE_U32(apt+20,1);PPC_STORE_U32(apt+24,0xC0);
    PPC_STORE_U32(apt+28,13);PPC_STORE_U32(apt+32,0x98765432);PPC_STORE_U32(apt+36,0x12345678);
    PPC_STORE_U8(apt+40,0x88);PPC_STORE_U32(apt+44,1);PPC_STORE_U32(apt+48,0x88);
    PPC_STORE_U8(apt+52,0x3E); // Function return is not the relocation stream's End.
    PPC_STORE_U8(apt+53,0x96);PPC_STORE_U32(apt+56,1);PPC_STORE_U32(apt+60,0x8C);
    PPC_STORE_U8(apt+64,0);PPC_STORE_U32(apt+0xC0,0xF0);
    std::memcpy(f.rt.pointer(apt+0xE0,7,true),"helper",7);
    std::memcpy(f.rt.pointer(apt+0xF0,3,true),"mx",3);
    constexpr std::array<uint32_t,4> authored{3,1,2,0},firstLoad{404,202,303,101},natural{101,202,303,404};
    for(uint32_t i=0;i<authored.size();++i)PPC_STORE_U32(apt+0x80+4*i,authored[i]);
    PPC_STORE_U32(cons+24,4);PPC_STORE_U32(cons+28,cons+32);
    auto setCons=[&](const std::array<uint32_t,4>& numbers) {
        for(uint32_t i=0;i<numbers.size();++i){PPC_STORE_U32(cons+32+8*i,7);PPC_STORE_U32(cons+36+8*i,numbers[i]);}
    };
    auto prepareValues=[&] {
        for(uint32_t i=0;i<4;++i) {
            const auto value=values+16*i;
            // Real integer free-list nodes retain substantial references, so
            // the original retain/release methods run without heap/free work.
            PPC_STORE_U32(value,0x8215A804);PPC_STORE_U32(value+4,0x0C000007|(100u<<14));
            PPC_STORE_U32(value+8,i==3?0:value+16);
        }
        PPC_STORE_U32(0x82E01D00,values);
    };
    auto invoke=[&](bool load) {
        EngineCpuCalls cpu(f.entry,base);const auto sp=cpu.registers().r1.u64,lr=cpu.registers().lr;
        if(load){prepareValues();cpu.invoke(0x827D6050,apt,apt,cons,counter);}
        else cpu.invoke(0x827D6040,apt,apt,counter);
        need(cpu.registers().r1.u64==sp&&cpu.registers().lr==lr,"Original Apt constant walker leaked stack/LR");
    };
    auto loadedNumbers=[&] {
        std::array<uint32_t,4> result{};
        for(uint32_t i=0;i<result.size();++i) {
            const auto value=PPC_LOAD_U32(apt+0x80+4*i);
            need(value>=values&&value<values+64&&(PPC_LOAD_U32(value+4)&0x0800007F)==0x08000007,
                 "Original Apt reload did not create its original integer values");
            result[i]=PPC_LOAD_U32(value+8);
        }
        return result;
    };
    setCons(natural);PPC_STORE_U32(counter,0);invoke(true);
    need(loadedNumbers()==firstLoad&&PPC_LOAD_U32(counter)==4,"Original first load ignored authored constant indices");
    need(PPC_LOAD_U32(apt+24)==apt+0xC0&&PPC_LOAD_U32(apt+0xC0)==apt+0xF0,
         "Original DefineFunction named parameters did not relocate");
    // Executed DefineFunction may overwrite these pool-scope words. Unload
    // restores their sentinels independently of the literal table ordering.
    PPC_STORE_U32(apt+32,0xDEADBEEF);PPC_STORE_U32(apt+36,0xCAFEBABE);
    PPC_STORE_U32(counter,0);invoke(false);
    for(uint32_t i=0;i<4;++i)need(PPC_LOAD_U32(apt+0x80+4*i)==i,"Original unload did not replace authored indices with traversal ordinals");
    need(PPC_LOAD_U32(counter)==4&&PPC_LOAD_U32(apt+8)==0x80&&PPC_LOAD_U32(apt+48)==0x88&&PPC_LOAD_U32(apt+60)==0x8C,
         "Original Apt unload did not restore relative Push/ConstantPool tables");
    need(PPC_LOAD_U32(apt+32)==0x98765432&&PPC_LOAD_U32(apt+36)==0x12345678&&PPC_LOAD_U32(apt+0xC0)==0xF0,
         "Original DefineFunction unload retained stale pool scope or parameters");
    PPC_STORE_U32(counter,0);invoke(true);
    need(loadedNumbers()==natural&&loadedNumbers()!=firstLoad,"Original reload did not expose the noncanonical constant-order defect");
    PPC_STORE_U32(counter,0);invoke(false);setCons(firstLoad);
    for(uint32_t round=0;round<2;++round) {
        PPC_STORE_U32(counter,0);invoke(true);
        need(loadedNumbers()==firstLoad&&PPC_LOAD_U32(counter)==4,"Traversal-ordered constants changed across original Apt reload");
        PPC_STORE_U32(counter,0);invoke(false);
    }
    // A following action shares the counter; each stream does not restart it.
    constexpr uint32_t tail=apt+0x100;
    PPC_STORE_U8(tail,0x96);PPC_STORE_U32(tail+4,2);PPC_STORE_U32(tail+8,apt+0x120);
    PPC_STORE_U32(apt+0x120,values);PPC_STORE_U32(apt+0x124,values+16);
    EngineCpuCalls cpu(f.entry,base);cpu.invoke(0x827D6040,tail,apt,counter);
    need(PPC_LOAD_U32(apt+0x120)==4&&PPC_LOAD_U32(apt+0x124)==5&&PPC_LOAD_U32(counter)==6,
         "Original constant ordinal restarted at a later action stream");
}
void exerciseCloseFailurePriority(const char* image) {
    const auto checkFailure=[&](const char* stop,const char* expected,bool laterClose=false) {
        Fixture f(image);auto c=seeded(f.entry,0x29);c.r29.u64=0;c.r28.u64=0xFFFFFFFF;
        c.lastFunction=0x827C2D30;c.lr=0x827EBEC8;
        const auto before=c;
        if(stop)f.rt.requestStop(stop);
        if(laterClose)f.rt.requestStop("Native window closed");
        bool rejected=false;
        try{PPCSafeIndirect(c,f.base,0xFFFFFFFF);}
        catch(const Failure& error){rejected=std::string(error.what())==expected;}
        need(rejected,"Apt invalid-target guard changed live-fault or explicit-close classification");
        need(std::memcmp(&c,&before,sizeof(c))==0,"Apt invalid-target cancellation changed guest registers");
        if(stop)need(f.rt.stopReason==stop,"A late close replaced the original runtime stop reason");
    };
    constexpr auto invalid="missing or invalid indirect function target";
    checkFailure(nullptr,invalid);
    checkFailure("Native window closed","Native window closed");
    checkFailure("prior worker failure",invalid,true);
}
void exercise(const char* image) {
    Fixture f(image);auto* base=f.base;
    need(f.bounds(text)==std::array<float,4>{-10,-5,20,10},"Original identity text bounds differ");
    need(f.bounds(row)==std::array<float,4>{-10,-5,20,10},"Original MovieClip display-list bounds differ");
    for(const auto receiver:{text,row})for(const auto point:std::array<std::array<int32_t,2>,7>{{
        {-10,-5},{20,10},{0,0},{-11,0},{21,0},{0,-6},{0,11}}})
        need(f.hit(receiver,float(point[0]),float(point[1]))==f.originalPoint(receiver,point[0],point[1]),
             "Native hitTest differs from original Apt mouse point classification");
    // Three nested transforms deliberately include nonzero local min offsets,
    // nonuniform scale and a 90-degree parent rotation.
    matrix(base,text+12,{1,0,0,1,5,7});matrix(base,row+12,{2,0,0,3,100,200});
    matrix(base,parent+12,{0,1,-1,0,500,10});
    PPC_STORE_U32(text+68,row);PPC_STORE_U32(row+68,parent);
    constexpr std::array<float,4> textTransformed{1066,215,1096,305},rowTransformed{249,100,294,160};
    // Original global bounds compose ancestors nearest-first. Recursive movie
    // display-list bounds compose their children while descending. Preserve
    // each original ordering, including their different noncommuting results.
    need(f.bounds(text)==textTransformed&&f.bounds(row)==rowTransformed,"Original nested affine/movie bounds differ");
    // Original raster output scales are consumed by the proxy matrix callback,
    // while CPU hit bounds stay in authored Apt stage coordinates.
    setFloat(base,0x82CF7D74,2.5f);setFloat(base,0x82CF7D78,720.0f/448.0f);
    need(f.bounds(text)==textTransformed&&f.bounds(row)==rowTransformed,
         "Original hit bounds incorrectly changed with Apt raster output scaling");
    for(const auto receiver:{text,row}) {
        const auto r=receiver==text?textTransformed:rowTransformed;
        const auto xmin=int32_t(r[0]),ymin=int32_t(r[1]),xmax=int32_t(r[2]),ymax=int32_t(r[3]);
        const auto x=(xmin+xmax)/2,y=(ymin+ymax)/2;
        for(const auto point:std::array<std::array<int32_t,2>,7>{{
            {xmin,ymin},{xmax,ymax},{x,y},{xmin-1,y},{xmax+1,y},{x,ymin-1},{x,ymax+1}}})
        need(f.hit(receiver,float(point[0]),float(point[1]))==f.originalPoint(receiver,point[0],point[1]),
             "Native transformed row hit differs from original point classifier");
    }
    need(f.hit(row,249,100)&&f.hit(row,294,160)&&!f.hit(row,248.5f,120)&&!f.hit(row,294.5f,120),
         "Apt fractional point hitTest or inclusive bounds edges differ");
    for(const auto hidden:{text,row,parent}) {
        PPC_STORE_U32(hidden+88,0);need(!f.hit(text,1080,250),"Hidden row or ancestor retained mouse hit");
        PPC_STORE_U32(hidden+88,0x800);need(f.hit(text,1080,250),"Visible ancestor did not restore mouse hit");
    }
    need(!f.hit(text,std::numeric_limits<float>::quiet_NaN(),250)&&
         !f.hit(text,1080,std::numeric_limits<float>::infinity()),"Nonfinite Apt mouse arguments were admitted");
    PPC_STORE_U32(asStack,2);need(!f.hit(text,1080,250),"Short Apt argument stack was admitted");PPC_STORE_U32(asStack,3);
    PPC_STORE_U32(asStack+4,2);need(!f.hit(text,1080,250),"Apt stack count beyond capacity was admitted");PPC_STORE_U32(asStack+4,3);
    PPC_STORE_U32(asStack+8,0);need(!f.hit(text,1080,250),"Null Apt argument storage was admitted");
    PPC_STORE_U32(asStack+8,0xFFFFFFFC);need(!f.hit(text,1080,250),"Wrapped Apt argument storage was admitted");
    PPC_STORE_U32(asStack+8,items);PPC_STORE_U32(asStack,0x40000000);PPC_STORE_U32(asStack+4,0x40000000);
    need(!f.hit(text,1080,250),"Overflowing Apt argument count was admitted");
    PPC_STORE_U32(asStack,3);PPC_STORE_U32(asStack+4,3);
    // Native calls share the AVM operand stack; unrelated entries may precede
    // the topmost x/y/shape arguments and must neither change order nor pop.
    PPC_STORE_U32(items,undefined);PPC_STORE_U32(items+4,text);
    PPC_STORE_U32(items+8,shapeValue);PPC_STORE_U32(items+12,yValue);PPC_STORE_U32(items+16,xValue);
    PPC_STORE_U32(asStack,5);PPC_STORE_U32(asStack+4,5);
    need(f.hit(text,1080,250)&&!f.hit(text,1065,250),"Apt hitTest did not consume the top three shared-stack arguments");
    PPC_STORE_U32(items,shapeValue);PPC_STORE_U32(items+4,yValue);PPC_STORE_U32(items+8,xValue);
    PPC_STORE_U32(asStack,3);PPC_STORE_U32(asStack+4,3);
    for(const uint32_t argc:{0u,1u,2u,4u})f.hit(text,1080,250,false,argc);
    PPC_STORE_U32(shapeValue+8,1);f.hit(text,1080,250,false,3,true);PPC_STORE_U32(shapeValue+8,0);
    PPC_STORE_U32(parent+68,text);need(!f.hit(text,1080,250),"Cyclic Apt parent chain was admitted");PPC_STORE_U32(parent+68,0);
    f.localRect({0,0,0,0});need(!f.hit(text,1080,250),"Empty Apt rectangle retained a mouse hit");f.localRect({-10,-5,20,10});
    PPC_STORE_U32(textData+4,0);need(!f.hit(row,270,120),"Original MovieClip mask child incorrectly contributed mouse bounds");
    PPC_STORE_U32(textData+4,0xFFFFFFFF);
    // Finally exercise the regenerated retail callback entry rather than only
    // its host implementation. This catches a missing/wrong return hook.
    need(f.hit(row,270,120,true)&&!f.hit(row,248,120,true),"Generated original Apt hitTest hook is missing or differs");
    f.hit(row,270,120,true,1);
}
constexpr uint32_t menuOwner=0x30000,menuManager=0x30100,newestMovie=0x30200,
                   olderMovie=0x30300,wrongDomainMovie=0x30400,newestReceiver=newestMovie+100,
                   olderReceiver=olderMovie+100,wrongDomainReceiver=wrongDomainMovie+100,targetWords=0x30500,
                   newestPath=0x30600,olderPath=0x30700,eventRows=0x31000,driverMode=0x32000;
uint32_t bridgeCalls{},bridgeTarget{},bridgeExpectedTarget{},bridgeHit=106;
uint32_t bridgeUpdateMode{},bridgeExpectedUpdateMode{};
bool bridgeUpdate{};
uint32_t pressCalls{},pressExpectedMovie{},pressExpectedEvent{};
void moviePress(PPCContext& c,uint8_t*) {
    ++pressCalls;
    need(c.r3.u32==pressExpectedMovie&&c.r4.u32==0&&c.r5.u32==pressExpectedEvent,
         "Original typed receiver press thunk lost its movie owner/domain/event");
}
void mouseScriptBridge(PPCContext& c,uint8_t* base) {
    ++bridgeCalls;bridgeTarget=c.r5.u32;
    need(bridgeTarget==bridgeExpectedTarget,"Mouse query selected another receiver's Apt target");
    need(!std::strcmp(reinterpret_cast<const char*>(PPCGuestPointer(base,c.r3.u32,16,false)),"nativeMouseQuery")&&c.r6.u32==4,
         "Mouse query changed its original string-argument bridge ABI");
    // Exercise the original bridge's outgoing register spills before reading
    // our argument strings, without initializing the full AVM/asset runtime.
    const std::array<uint64_t,4> arguments{c.r7.u64,c.r8.u64,c.r9.u64,c.r10.u64};
    for(uint32_t i=0;i<arguments.size();++i)PPC_STORE_U64(c.r1.u32+48+8*i,arguments[i]);
    std::array<double,4> values{};
    for(uint32_t i=0;i<arguments.size();++i) {
        const auto address=PPC_LOAD_U32(c.r1.u32+52+8*i);
        const auto* value=reinterpret_cast<const char*>(PPCGuestPointer(base,address,16,false));
        char* end{};values[i]=std::strtod(value,&end);
        need(end!=value&&*end==0&&std::isfinite(values[i]),"Mouse argument string overlapped the original outgoing ABI area");
    }
    need(values[2]>=0&&values[2]<=3&&std::floor(values[2])==values[2],"Mouse bridge update mode is outside the query/hover/wheel/press protocol");
    bridgeUpdateMode=uint32_t(values[2]);bridgeUpdate=bridgeUpdateMode!=0;
    need(bridgeUpdateMode==bridgeExpectedUpdateMode,"Mouse bridge did not distinguish pointer press from query, hover or wheel input");
    need(values[3]==256,"Mouse query lost its authored stage center");
    if(bridgeUpdate)need(values[0]==256&&values[1]==224,"Mouse pointer was not mapped to authored Apt coordinates");
    char result[16]{};std::snprintf(result,sizeof(result),"%u",bridgeUpdate?bridgeHit:1u);
    std::memcpy(PPCGuestPointer(base,c.r4.u32,uint32_t(std::strlen(result)+1),true),result,std::strlen(result)+1);
}
void exerciseMouseDomains(const char* image) {
    Runtime rt;PPCContext entry{};rt.load(image);rt.initialize(entry);auto* base=rt.base;
    rt.map(menuOwner,0x4000,true,"original Apt controller-domain fixture");
    // Pin the actual selected-slot mapper and digital publisher call chain.
    constexpr std::array<std::array<uint32_t,2>,11> words{{
        {0x823A1970,0x546B063E},{0x823A202C,0x92410064},
        {0x823A203C,0x4BFFF935},{0x823A2050,0x83810064},{0x823A214C,0x7F84E378},
        {0x827F4090,0x3B9F0064},{0x827F4108,0x93FF007C},{0x827F2008,0x80630018},
        {0x827F2010,0x816B0064},{0x8215BCDC,0x827F2008},{0x82002C6C,0x823A6C78}}};
    for(const auto word:words)need(PPC_LOAD_U32(word[0])==word[1],"Original physical-slot to Apt domain/publisher source changed");
    {
        EngineCpuCalls cpu(entry,base);const auto pair=cpu.registers().r1.u32+0x80;
        for(uint32_t slot=0;slot<4;++slot) {
            PPC_STORE_U32(pair,0xA193E527);PPC_STORE_U32(pair+4,0x617BD048);
            const auto before=cpu.registers();cpu.invoke(0x823A1970,slot,pair,pair+4);
            need(PPC_LOAD_U32(pair)==slot&&PPC_LOAD_U32(pair+4)==slot+2,"Original Apt mapper does not retain digital/analog domains for slots zero through three");
            need(cpu.registers().r1.u64==before.r1.u64&&cpu.registers().lr==before.lr&&cpu.registers().r31.u64==before.r31.u64,
                 "Original Apt domain mapper changed nonvolatile stack/LR state");
        }
        PPC_STORE_U32(pair,8);PPC_STORE_U32(pair+4,10);cpu.invoke(0x823A1970,6,pair,pair+4);
        need(PPC_LOAD_U32(pair)==8&&PPC_LOAD_U32(pair+4)==10,"Original unselected slot changed default Apt event domains");
        need(cpu.invoke(0x823A1B88,menuOwner)==menuOwner,"Original Apt mouse owner constructor failed");
        cpu.invoke(0x823A1C40,menuOwner);
    }
    struct BackgroundWindow {
        std::string previous;bool existed{};
        BackgroundWindow(){if(const auto value=std::getenv("SIMPSONS_BACKGROUND_WINDOW")){previous=value;existed=true;}need(!_putenv_s("SIMPSONS_BACKGROUND_WINDOW","1"),"Background mouse fixture environment failed");}
        ~BackgroundWindow(){_putenv_s("SIMPSONS_BACKGROUND_WINDOW",existed?previous.c_str():"");}
    } background;
    PPC_STORE_U32(0x82D0CA68,0x33000); // Mapped original engine owner; no console SDK device.
    rt.window=std::make_unique<NativeWindow>();ShowWindow(rt.window->handle(),SW_HIDE);
    rt.engineDriver=std::make_shared<EngineDriver>(rt,rt.window->width.load(),rt.window->height.load());
    need(rt.engineDriver->renderAspect()==16.0/9.0,"Mouse fixture frozen render aspect differs");
    rt.controllers=std::make_shared<Platform::NativeControllers>();
    rt.controllers->ignorePhysicalDevices();rt.controllers->attachKeyboard(rt.window->keyboard);
    auto keys=rt.window->keyboard;keys->focus(true);
    setFloat(base,0x82CF7D7C,512);setFloat(base,0x82CF7D80,448);
    PPC_STORE_U32(0x82D08DB0,menuManager);PPC_STORE_U32(0x82D08BA8,driverMode);
    PPC_STORE_U32(menuManager+4,olderReceiver);PPC_STORE_U32(menuManager+8,newestReceiver);
    PPC_STORE_U32(menuManager+12,wrongDomainReceiver);PPC_STORE_U32(menuManager+68,3);
    PPC_STORE_U32(menuManager+72,eventRows);PPC_STORE_U32(menuManager+76,16);
    PPC_STORE_U8(menuManager+88,1);
    // UIxMovie has a distinct receiver subobject, constructed by original
    // 827F1448 then specialized at movie+100 by source-pinned 827F4028.
    // Its real press thunk independently verifies the owner at receiver+24.
    for(const auto movie:{newestMovie,olderMovie,wrongDomainMovie}) {
        EngineCpuCalls cpu(entry,base);cpu.invoke(0x827F1448,movie+100,1);
        PPC_STORE_U32(movie,0x82002C08);PPC_STORE_U32(movie+100,0x8215BCD8);
        PPC_STORE_U32(movie+124,movie);
        PPC_STORE_U32(movie+196,targetWords+8); // Reproduces the bad target obtained from the receiver itself.
    }
    PPC_STORE_U32(targetWords+8,0x82002C08);
    for(const auto receiver:{newestReceiver,olderReceiver,wrongDomainReceiver})PPC_STORE_U32(receiver+8,0x7FFF);
    PPC_STORE_U32(newestReceiver+12,1);PPC_STORE_U32(olderReceiver+12,1);PPC_STORE_U32(wrongDomainReceiver+12,1u<<8);
    PPC_STORE_U32(newestMovie+96,targetWords);PPC_STORE_U32(targetWords,newestPath);
    PPC_STORE_U32(olderMovie+96,targetWords+4);PPC_STORE_U32(targetWords+4,olderPath);
    std::memcpy(rt.pointer(newestPath,16,true),"_level0.newest",14);
    std::memcpy(rt.pointer(olderPath,16,true),"_level0.older",13);
    struct BridgeDispatch {
        uint8_t* base;PPCFunc* previous;PPCFunc* previousPress;
        explicit BridgeDispatch(uint8_t* memory):base(memory),previous(PPC_LOOKUP_FUNC(base,0x827BF8F8)),previousPress(PPC_LOOKUP_FUNC(base,0x823A6C78)) {
            need(previous!=nullptr,"Original Apt script bridge dispatch is absent");PPC_LOOKUP_FUNC(base,0x827BF8F8)=mouseScriptBridge;
            need(previousPress!=nullptr,"Original movie press handler dispatch is absent");PPC_LOOKUP_FUNC(base,0x823A6C78)=moviePress;
        }
        ~BridgeDispatch(){PPC_LOOKUP_FUNC(base,0x827BF8F8)=previous;PPC_LOOKUP_FUNC(base,0x823A6C78)=previousPress;}
    } dispatch(base);
    bridgeCalls=pressCalls=0;bridgeExpectedTarget=newestPath;
    auto invoke=[&](uint32_t expectedUpdate=0) {
        bridgeExpectedUpdateMode=expectedUpdate;
        auto c=seeded(entry,menuOwner);c.r26.u64=menuOwner;c.r28.u64=8;const auto before=c;
        const auto previousContext=currentContext;const auto hostFp=PPCFPSCRRegister::getcsr();SetLastError(0x6192);
        std::array<uint8_t,0xC0> frame{};std::memcpy(frame.data(),rt.pointer(c.r1.u32,uint32_t(frame.size()),false),frame.size());
        SimpsonsNativeMenuMouse(c,base);
        need(!std::memcmp(&c,&before,sizeof(c)),"Native mouse adapter changed original dispatcher PPC state");
        need(currentContext==previousContext&&PPCFPSCRRegister::getcsr()==hostFp&&GetLastError()==0x6192,
             "Native mouse adapter changed host FP/error/current context");
        need(!std::memcmp(frame.data(),rt.pointer(c.r1.u32,uint32_t(frame.size()),false),frame.size()),"Mouse script/domain calls overwrote the original dispatcher frame");
    };
    auto clearQueue=[&]{EngineCpuCalls cpu(entry,base);cpu.invoke(0x827F1B98,menuManager);};
    auto press=[&](uint32_t button){keys->pointerMove(640,360,1280,720);keys->mouseButton(button,true);keys->mouseButton(button,false);invoke(3);};
    auto queued=[&](uint32_t event) {
        need(PPC_LOAD_U32(menuManager+84)==1&&PPC_LOAD_U32(eventRows)==0&&PPC_LOAD_U32(eventRows+4)==event&&!PPC_LOAD_U32(eventRows+8),
             "Mouse adapter did not publish exactly one original slot-zero Apt event");
        pressExpectedMovie=bridgeExpectedTarget==newestPath?newestMovie:olderMovie;pressExpectedEvent=event;
        const auto before=pressCalls;EngineCpuCalls cpu(entry,base);cpu.invoke(0x827F1BA8,menuManager);
        need(pressCalls==before+1&&!PPC_LOAD_U32(menuManager+84),"Original queue dispatcher did not deliver exactly one typed movie press");
        clearQueue();
    };
    // Slot zero belongs to native keyboard/mouse even when the dispatcher has
    // not yet selected a physical pad or the driver selection mode changes.
    for(uint32_t mode:{0u,1u,2u}) {
        PPC_STORE_U32(driverMode+148,mode);PPC_STORE_U8(menuOwner+9,mode?3:6);
        rt.window->setMenuMouse(false);invoke();
        need(rt.window->isMenuMouse()&&!bridgeUpdate&&!PPC_LOAD_U32(menuManager+84),"Mouse query did not bootstrap through original slot-zero domain");
        press(VK_LBUTTON);need(bridgeUpdateMode==3,"Active mouse click lost the distinct Apt pointer-press mode");queued(6);
        invoke();need(!bridgeUpdate&&!PPC_LOAD_U32(menuManager+84),"Mouse click repeated after its original queue consumption");
    }
    // Hovering an Accept hit must update selection without a pointer-press
    // marker or the original Select event. Only a physical click uses mode3;
    // the authored AVM normalizes that mode to hover while retaining its flag.
    const auto beforeHoverPresses=pressCalls;
    keys->pointerMove(639,360,1280,720);keys->pointerMove(640,360,1280,720);invoke(1);
    need(bridgeUpdateMode==1&&!PPC_LOAD_U32(menuManager+84)&&pressCalls==beforeHoverPresses,
         "Hover over an Accept hit became a pointer press or queued menu acceptance");
    invoke();need(bridgeUpdateMode==0&&!PPC_LOAD_U32(menuManager+84),"Stationary pointer query replayed an Accept hover");
    // A stationary wheel uses mode two, so the AVM retains the selection moved
    // by the previous original navigation event. Settings still classify the
    // hovered editable row and publish the original left/right events.
    for(int32_t delta:{-120,-120,120}) {
        keys->pointerWheel(delta);invoke(2);
        need(bridgeUpdateMode==2,"Stationary list wheel was incorrectly sent as a new hover");
        queued(delta>0?4:5);
        invoke();need(bridgeUpdateMode==0&&!PPC_LOAD_U32(menuManager+84),"Wheel input repeated after its original queue consumption");
    }
    bridgeHit=103;
    for(int32_t delta:{-120,120}) {
        keys->pointerWheel(delta);invoke(2);
        need(bridgeUpdateMode==2,"Stationary setting wheel lost its wheel query mode");queued(delta>0?3:2);
    }
    bridgeHit=106;invoke();
    const auto blockedCalls=bridgeCalls,blockedPresses=pressCalls;
    PPC_STORE_U32(newestReceiver+20,1);press(VK_LBUTTON);
    need(bridgeCalls==blockedCalls&&pressCalls==blockedPresses&&rt.window->isMenuMouse()&&!PPC_LOAD_U32(menuManager+84),
         "Blocked newest Apt receiver entered the AVM or leaked input to an older receiver");
    PPC_STORE_U32(newestReceiver+20,0);invoke();
    need(bridgeCalls==blockedCalls+1&&!bridgeUpdate&&pressCalls==blockedPresses&&!PPC_LOAD_U32(menuManager+84),
         "Mouse click consumed by a blocked Apt receiver was replayed after unblock");
    press(VK_RBUTTON);queued(7);
    // A generic subscriber can own input without being a UIxMovie. Preserve
    // its stack priority but do not dereference it using a movie's layout.
    PPC_STORE_U32(newestReceiver,0x8215BCBC);auto calls=bridgeCalls;invoke();
    need(bridgeCalls==calls&&!rt.window->isMenuMouse(),"Untyped input subscriber was treated as a UIxMovie");
    PPC_STORE_U32(newestReceiver,0x8215BCD8);PPC_STORE_U32(newestReceiver+24,olderMovie);invoke();
    need(bridgeCalls==calls&&!rt.window->isMenuMouse(),"Apt receiver accepted another movie's owner pointer");
    PPC_STORE_U32(newestReceiver+24,newestMovie);invoke();
    PPC_STORE_U32(newestReceiver+12,0);bridgeExpectedTarget=olderPath;press(VK_LBUTTON);queued(6);
    PPC_STORE_U32(olderReceiver+12,0);calls=bridgeCalls;invoke();
    need(bridgeCalls==calls&&!rt.window->isMenuMouse()&&!PPC_LOAD_U32(menuManager+84),"Adapter queried or published the obsolete hardcoded domain eight");
    rt.engineDriver.reset();rt.window.reset();
}
constexpr uint32_t exitMenuText=0x35000,exitManager=0x36000,exitRequest=0x36100,
                   exitStringClasses=0x36200,exitQuestionStorage=0x36300,
                   exitStringPool=0x82DFD8D0,exitStringClass=exitStringClasses+32;
constexpr char exitQuestion[]="Are you sure you want to exit the game?";
uint32_t exitAllocations{},exitPrompts{},exitFrees{};
bool exitAllocationFails{},exitSynchronousDecline{};
void exitAllocate(PPCContext& c,uint8_t* base) {
    ++exitAllocations;
    need(c.r3.u32==96,"Exit confirmation changed the original popup payload size");
    if(exitAllocationFails){c.r3.u64=0;return;}
    need(PPC_LOAD_U32(exitStringClass+12)==exitQuestionStorage,
         "Previous exit question was not returned to the original string pool");
    std::memset(PPCGuestPointer(base,exitRequest,96,true),0xA5,96);
    c.r3.u64=exitRequest;
}
void exitFree(PPCContext& c,uint8_t* base) {
    ++exitFrees;
    need(c.r3.u32==exitRequest,"Exit confirmation freed another popup payload");
    need(PPC_LOAD_U32(exitStringClass+12)==exitQuestionStorage,
         "Exit confirmation did not run the original question-string destructor before freeing its payload");
}
void exitEnqueue(PPCContext& c,uint8_t* base) {
    ++exitPrompts;
    need(c.r3.u32==exitRequest,"Exit confirmation enqueued another popup payload");
    need(active&&active->nativeMainMenuExitQuery==exitRequest&&!active->nativeMainMenuExitQueryQueued,
         "Exit confirmation accepted synchronous completion before its popup was queued");
    need(PPC_LOAD_U32(exitRequest)==0x88&&PPC_LOAD_U32(exitRequest+4)==0&&
         PPC_LOAD_U32(exitRequest+8)==0&&PPC_LOAD_U32(exitRequest+12)==2&&PPC_LOAD_U32(exitRequest+16)==0,
         "Exit confirmation changed the original Yes/No type, Yes result, head type or pause-popup flags");
    need(PPC_LOAD_U32(exitRequest+20)==exitQuestionStorage&&
         PPC_LOAD_U16(exitRequest+24)==sizeof(exitQuestion)-1&&PPC_LOAD_U16(exitRequest+26)==64&&
         !std::strcmp(reinterpret_cast<const char*>(PPCGuestPointer(base,exitQuestionStorage,sizeof(exitQuestion),false)),exitQuestion),
         "Exit confirmation did not construct its question using the original guest string setter");
    need(PPC_LOAD_U32(exitRequest+92)==0x8239C7B0,"Exit confirmation lost its original free-only callback entry");
    if(exitSynchronousDecline) {
        // The real popup owner reports GetSelected(+8) immediately when its
        // movie is not ready. This unqueued result must release the token.
        c.r3.u64=PPC_LOAD_U32(exitRequest+8);c.r4.u64=exitRequest;c.r5.u64=0;
        PPCSafeIndirect(c,base,0x8239C7B0);
    }
}
void exerciseMainMenuExit(const char* image) {
    Runtime rt;PPCContext entry{};rt.load(image);rt.initialize(entry);auto* base=rt.base;
    rt.map(exitMenuText,0x2000,true,"MainMenu exit confirmation fixture");
    need(PPC_LOAD_U32(0x8239F6F4)==0x3D608200&&PPC_LOAD_U32(0x8239F6F8)==0x38610050&&
         PPC_LOAD_U32(0x8239F6FC)==0x388B1F7C&&PPC_LOAD_U32(0x8239F700)==0x483A2539,
         "Retail MainMenu accepted-selection string comparison changed");
    // Keep the real popup constructor, setters and destructor. The question
    // uses the real 64-byte small-string free-list path, without a game heap.
    PPC_STORE_U32(exitStringPool,exitRequest);PPC_STORE_U32(exitStringPool+12,128);
    PPC_STORE_U32(exitStringPool+16,exitStringClasses);
    PPC_STORE_U32(exitStringClass,64);PPC_STORE_U32(exitStringClass+12,exitQuestionStorage);
    PPC_STORE_U32(exitQuestionStorage,0);
    struct ExitDispatch {
        uint8_t* base;std::array<PPCFunc*,3> previous;
        const std::array<uint32_t,3> addresses{0x8269BD70,0x823A8748,0x8269BEB0};
        explicit ExitDispatch(uint8_t* memory):base(memory) {
            constexpr std::array<PPCFunc*,3> mocks{exitAllocate,exitEnqueue,exitFree};
            for(size_t i=0;i<addresses.size();++i) {
                previous[i]=PPC_LOOKUP_FUNC(base,addresses[i]);
                need(previous[i]!=nullptr,"Original exit popup dispatch is absent");
                PPC_LOOKUP_FUNC(base,addresses[i])=mocks[i];
            }
        }
        ~ExitDispatch(){for(size_t i=0;i<addresses.size();++i)PPC_LOOKUP_FUNC(base,addresses[i])=previous[i];}
    } dispatch(base);
    exitAllocations=exitPrompts=exitFrees=0;exitAllocationFails=exitSynchronousDecline=false;
    const auto previous=std::getenv("SIMPSONS_BACKGROUND_WINDOW");
    const bool existed=previous!=nullptr;const std::string saved=previous?previous:"";
    need(!_putenv_s("SIMPSONS_BACKGROUND_WINDOW","1"),"MainMenu Exit fixture environment failed");
    rt.window=std::make_unique<NativeWindow>();ShowWindow(rt.window->handle(),SW_HIDE);
    need(!_putenv_s("SIMPSONS_BACKGROUND_WINDOW",existed?saved.c_str():""),"MainMenu Exit fixture environment restore failed");
    auto c=seeded(entry,0);
    auto invoke=[&](const char* text) {
        // Place the terminator at the final byte of this guest mapping. Short
        // stock IDs must not trigger a full native-exit-literal extent probe.
        const auto length=uint32_t(std::strlen(text)+1),address=exitMenuText+0x1000-length;
        std::memcpy(rt.pointer(address,length,true),text,length);PPC_STORE_U32(c.r1.u32+80,address);
        const auto before=c;const auto hostFp=PPCFPSCRRegister::getcsr();const auto previousContext=currentContext;SetLastError(0x6192);
        std::array<uint8_t,0xC0> frame{};std::memcpy(frame.data(),rt.pointer(c.r1.u32,uint32_t(frame.size()),false),frame.size());
        SimpsonsNativeMainMenuExit(c,base);
        need(!std::memcmp(&c,&before,sizeof(c)),"MainMenu Exit hook changed original dispatcher registers");
        need(currentContext==previousContext&&PPCFPSCRRegister::getcsr()==hostFp&&GetLastError()==0x6192,
             "MainMenu Exit hook changed host FP/error/current context");
        need(!std::memcmp(frame.data(),rt.pointer(c.r1.u32,uint32_t(frame.size()),false),frame.size()),"MainMenu Exit hook changed original dispatcher frame");
    };
    for(const auto text:{"Continue","Options","Extras","ExitGame","NativeExitGameExtra","nativeexitgame",""}) {
        invoke(text);need(!rt.stopping.load()&&!rt.window->closed.load(),"Unrelated menu ID closed the native game");
    }
    need(!exitAllocations&&!exitPrompts,"Unrelated menu ID constructed an exit confirmation");
    PPC_STORE_U32(0x82D08E60,0);invoke("NativeExitGame");
    need(!exitAllocations&&!exitPrompts&&!rt.stopping.load(),"Missing popup owner created an unowned request or closed the game");
    PPC_STORE_U32(0x82D08E60,exitManager);PPC_STORE_U32(exitManager+128,5);invoke("NativeExitGame");
    need(!exitAllocations&&!exitPrompts&&!rt.stopping.load(),"Full popup queue created an unowned request or closed the game");
    PPC_STORE_U32(exitManager+128,0);exitAllocationFails=true;invoke("NativeExitGame");
    need(exitAllocations==1&&!exitPrompts&&!rt.stopping.load(),"Failed popup allocation closed the game or enqueued a null request");
    exitAllocationFails=false;invoke("NativeExitGame");
    need(exitAllocations==2&&exitPrompts==1&&!exitFrees&&rt.nativeMainMenuExitQuery==exitRequest&&rt.nativeMainMenuExitQueryQueued&&
         !rt.stopping.load()&&!rt.window->closed.load(),
         "MainMenu Exit closed immediately instead of retaining its confirmation");
    invoke("NativeExitGame");
    need(exitAllocations==2&&exitPrompts==1,"Repeated Exit selection stacked a duplicate confirmation");
    auto reply=[&](uint32_t choice,uint32_t aborted,uint32_t request=exitRequest,bool generated=true) {
        auto callback=seeded(entry,choice);callback.r4.u64=request;callback.r5.u64=aborted;
        const auto before=callback;const auto hostFp=PPCFPSCRRegister::getcsr();const auto previousContext=currentContext;SetLastError(0x6192);
        std::array<uint8_t,0xC0> frame{};std::memcpy(frame.data(),rt.pointer(callback.r1.u32,uint32_t(frame.size()),false),frame.size());
        bool handled=true;
        if(generated)PPCSafeIndirect(callback,base,0x8239C7B0);
        else handled=SimpsonsNativeMainMenuExitQueryCallback(callback,base);
        need(!std::memcmp(&callback,&before,sizeof(callback)),"Exit confirmation callback changed original popup PPC state");
        need(currentContext==previousContext&&PPCFPSCRRegister::getcsr()==hostFp&&GetLastError()==0x6192,
             "Exit confirmation callback changed host FP/error/current context");
        need(!std::memcmp(frame.data(),rt.pointer(callback.r1.u32,uint32_t(frame.size()),false),frame.size()),
             "Exit confirmation callback changed original popup frame");
        return handled;
    };
    need(!reply(0,0,exitRequest+128,false)&&!exitFrees&&rt.nativeMainMenuExitQuery==exitRequest&&rt.nativeMainMenuExitQueryQueued&&!rt.stopping.load(),
         "Exit confirmation accepted or freed another popup's callback");
    reply(1,0);
    need(exitFrees==1&&!rt.nativeMainMenuExitQuery&&!rt.nativeMainMenuExitQueryQueued&&!rt.stopping.load()&&!rt.window->closed.load(),
         "No closed the game or retained its exit request");
    need(!reply(0,0,exitRequest,false)&&exitFrees==1&&!rt.stopping.load(),
         "A stale exit callback closed the game or freed its request twice");
    invoke("NativeExitGame");reply(0,1);
    need(exitPrompts==2&&exitFrees==2&&!rt.nativeMainMenuExitQuery&&!rt.nativeMainMenuExitQueryQueued&&!rt.stopping.load()&&!rt.window->closed.load(),
         "Back/abort on Yes closed the game or retained its exit request");
    exitSynchronousDecline=true;invoke("NativeExitGame");
    need(exitPrompts==3&&exitFrees==3&&!rt.nativeMainMenuExitQuery&&!rt.nativeMainMenuExitQueryQueued&&!rt.stopping.load()&&!rt.window->closed.load(),
         "A popup movie that was not ready treated its synchronous result as an exit");
    exitSynchronousDecline=false;invoke("NativeExitGame");
    need(exitPrompts==4&&exitFrees==3&&rt.nativeMainMenuExitQueryQueued&&!rt.stopping.load(),
         "Declining synchronously prevented a later confirmation");
    reply(0,0x100); // The original abort argument is its low byte.
    need(exitFrees==4&&!rt.nativeMainMenuExitQuery&&!rt.nativeMainMenuExitQueryQueued&&rt.stopping.load()&&rt.stopReason=="Native window closed",
         "Yes did not free its request then synchronously cancel through the native window-close path");
    for(unsigned i=0;i<100&&!rt.window->closed.load();++i)Sleep(10);
    need(rt.window->closed.load(),"MainMenu Exit did not deliver WM_CLOSE to its owned window thread");
    rt.window.reset();
}
}
int main(int argc,char** argv) {
    try {
        need(argc==2,"Original image required");exercise(argv[1]);exerciseConstantReload(argv[1]);exerciseMouseDomains(argv[1]);exerciseCloseFailurePriority(argv[1]);exerciseMainMenuExit(argv[1]);
        std::printf("PASS original Apt hitTest: %zu checks; real text/movie bounds, original point oracle, nested transforms, visibility, edges, original Boolean allocation, original constant unload/reload, original controller mapper/publisher, receiver ownership, exit confirmation, arguments and PPC/host ABI\n",checks);return 0;
    } catch(const std::exception& error){std::fprintf(stderr,"FAIL original Apt hitTest: %s\n",error.what());return 1;}
}
