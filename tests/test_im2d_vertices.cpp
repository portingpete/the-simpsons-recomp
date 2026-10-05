#include "renderer/im2d_vertices.h"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <cfenv>
using namespace Simpsons::Graphics;
namespace {
size_t checks{};
void need(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& call) {
    bool rejected=false;try{call();}catch(const Im2DVertexError&){rejected=true;}
    need(rejected,"Unsupported vertex input accepted");
}
std::vector<uint8_t> declaration() {
    return {0,0,0,0,0,0x1A,0x23,0xA6,0,0,0,0,
        0,0,0,16,0,0x18,0x28,0x86,0,10,0,0,
        0,0,0,20,0,0x2C,0x23,0xA5,0,5,0,0,
        0,0xFF,0,0,0xFF,0xFF,0xFF,0xFF,0,0,0,0};
}
void put(uint8_t* p,uint32_t value) {
    p[0]=uint8_t(value>>24);p[1]=uint8_t(value>>16);p[2]=uint8_t(value>>8);p[3]=uint8_t(value);
}
}
int main()try {
    DeclarationRegistry registry({2048,UINT32_MAX});auto bytes=declaration();
    auto id=registry.create(bytes);auto record=registry.record(id);
    // Each color channel spans every input byte, in distinct non-gray order.
    // The source begins unaligned and carries untouched extent sentinels.
    std::vector<uint8_t> storage(256*28+2,0xCD);
    std::span<uint8_t> source(storage.data()+1,256*28);
    for(uint32_t i=0;i<256;++i) {
        auto* p=source.data()+i*28;
        put(p,std::bit_cast<uint32_t>(float(i)-128.25f));
        put(p+4,std::bit_cast<uint32_t>(float(i)*3.125f));
        put(p+8,std::bit_cast<uint32_t>(float(i)/255.0f));
        put(p+12,0x7FA00000u+i); // Unused signaling NaN RHW is copied as bits.
        p[16]=uint8_t(i);p[17]=uint8_t(i+31);p[18]=uint8_t(255-i);p[19]=uint8_t(i*73);
        put(p+20,std::bit_cast<uint32_t>(float(i)/128.0f-0.5f));put(p+24,0x80000000);
    }
    const auto snapshot=storage;
    const auto decoded=decodeIm2DVertices(*record,source,true);
    need(decoded.size()==256,"Vertex count changed");
    for(uint32_t i=0;i<256;++i) {
        const auto& v=decoded[i];
        need(v.position[0]==float(i)-128.25f && v.position[1]==float(i)*3.125f && v.position[2]==float(i)/255.0f,"BE position differs");
        need(std::bit_cast<uint32_t>(v.position[3])==0x7FA00000u+i,"Unused RHW bits changed");
        need(v.uv[0]==float(i)/128.0f-0.5f && std::bit_cast<uint32_t>(v.uv[1])==0x80000000,"BE UV bits differ");
        const std::array<float,4> expected={float(uint8_t(i+31))/255.0f,float(255-i)/255.0f,float(uint8_t(i*73))/255.0f,float(i)/255.0f};
        for(size_t c=0;c<4;++c)need(v.color[c]==expected[c],"Original ARGB word did not expand to RGBA");
    }
    need(storage==snapshot,"Decoder modified source bytes");
    // Repeated changes of packet size must overwrite every field, including
    // inactive NaN RHW and signed-zero UV, while retaining no caller bytes.
    std::vector<Im2DVertex> reusable;
    for(size_t count:{size_t(3),size_t(256),size_t(4),size_t(9362),size_t(3),size_t(128),size_t(9362),size_t(4)}) {
        std::vector<uint8_t> packet(count*28);
        for(size_t i=0;i<count;++i)std::memcpy(packet.data()+i*28,snapshot.data()+1+(i%256)*28,28);
        decodeIm2DVertices(*record,packet,true,reusable);
        std::fill(packet.begin(),packet.end(),0xCD);
        need(reusable.size()==count,"Reusable decoder retained a previous packet extent");
        for(size_t i=0;i<count;++i)
            need(!std::memcmp(&reusable[i],&decoded[i%256],sizeof(Im2DVertex)),"Reusable decode retained stale or borrowed vertex fields");
    }
    const auto beforeOverlap=reusable;
    rejects([&]{decodeIm2DVertices(*record,{reinterpret_cast<const uint8_t*>(reusable.data()),3*28},true,reusable);});
    need(reusable.size()==beforeOverlap.size() && !std::memcmp(reusable.data(),beforeOverlap.data(),reusable.size()*sizeof(Im2DVertex)),
        "Overlapping reusable decode changed storage before rejection");
    auto damaged=std::vector<uint8_t>(source.begin(),source.end());put(damaged.data()+255*28+24,0x7F800000);
    rejects([&]{decodeIm2DVertices(*record,damaged,true,reusable);});
    decodeIm2DVertices(*record,source,true,reusable);
    need(reusable.size()==decoded.size() && !std::memcmp(reusable.data(),decoded.data(),decoded.size()*sizeof(Im2DVertex)),
        "Reusable decoder did not recover after a rejected last vertex");
    {
        struct SavedEnvironment {
            std::fenv_t value;
            SavedEnvironment(){if(std::fegetenv(&value))throw std::runtime_error("Cannot capture floating environment");}
            ~SavedEnvironment(){std::fesetenv(&value);}
        } savedEnvironment;
        for(int mode:{FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}) {
            if(std::fesetround(mode))throw std::runtime_error("Cannot set floating test mode");
            std::feclearexcept(FE_ALL_EXCEPT);
            const auto converted=decodeIm2DVertices(*record,source,true);
            need(std::fegetround()==mode,"Decoder changed caller rounding mode");
            need(!std::fetestexcept(FE_ALL_EXCEPT),"Decoder performed floating arithmetic on original input bits");
            for(size_t i=0;i<converted.size();++i)for(size_t c=0;c<4;++c)
                need(std::bit_cast<uint32_t>(converted[i].color[c])==std::bit_cast<uint32_t>(decoded[i].color[c]),
                    "Packed color decode inherited caller rounding mode");
        }
    }
    std::fill(source.begin(),source.end(),0);
    need(decoded[0].position[0]==-128.25f && decoded[0].color[0]==31.0f/255,"Output retained source memory");
    registry.release(id);record.reset();
    need(decoded[255].color[3]==1,"Output retained declaration lifetime");
    id=registry.create(bytes);record=registry.record(id);
    for(size_t size:{size_t(0),size_t(28),size_t(56),size_t(83),size_t(85),size_t(9362*28+1),size_t(9363*28)}) {
        std::vector<uint8_t> invalid(size);rejects([&]{decodeIm2DVertices(*record,invalid,false);});
    }
    for(size_t count:{size_t(3),size_t(4),size_t(9362)}) {
        std::vector<uint8_t> valid(count*28);need(decodeIm2DVertices(*record,valid,true).size()==count,"Valid bounded input rejected");
    }
    std::vector<uint8_t> small(3*28);
    for(uint32_t bits:{0x7F800000u,0xFF800000u,0x7FC00000u,0x7FA12345u}) {
        for(size_t offset:{size_t(0),size_t(4),size_t(8),size_t(20),size_t(24)}) {
            put(small.data()+2*28+offset,bits);
            rejects([&]{decodeIm2DVertices(*record,small,true);});
            if(offset>=20) {
                const auto flat=decodeIm2DVertices(*record,small,false);
                need(std::bit_cast<uint32_t>(flat[2].uv[(offset-20)/4])==bits,"Inactive UV bits changed");
            }else rejects([&]{decodeIm2DVertices(*record,small,false);});
            put(small.data()+2*28+offset,0);
        }
    }
    // Registry-admitted layouts that are incompatible with this exact shader.
    for(size_t field:{size_t(3),size_t(7),size_t(15),size_t(21),size_t(27)}) {
        auto bad=bytes;
        if(field==7){bad[5]=0x2A;bad[7]=0xB9;} // FLOAT3 position.
        else if(field==21)bad[field]=3; // NORMAL instead of COLOR.
        else bad[field]=uint8_t(bad[field]+4);
        const auto wrong=registry.record(registry.create(bad));
        rejects([&]{decodeIm2DVertices(*wrong,small,true);});
    }
    auto omitted=bytes;omitted.erase(omitted.begin()+24,omitted.begin()+36);
    auto wrong=registry.record(registry.create(omitted));rejects([&]{decodeIm2DVertices(*wrong,small,false);});
    auto reordered=bytes;std::swap_ranges(reordered.begin(),reordered.begin()+12,reordered.begin()+12);
    wrong=registry.record(registry.create(reordered));rejects([&]{decodeIm2DVertices(*wrong,small,true);});
    for(size_t element=0;element<4;++element)for(uint32_t value=0;value<256;++value) {
        auto opaque=bytes;opaque[element*12+11]=uint8_t(value);
        const auto opaqueId=registry.create(opaque);
        need(decodeIm2DVertices(*registry.record(opaqueId),small,true).size()==3,"Opaque original stack byte affected decoding");
        registry.release(opaqueId);
    }
    need(registry.referenceCount(id)==1,"Decoder changed original declaration references");
    std::printf("PASS: %zu Im2D packed vertex checks; bounded owned conversion, original color order, finite active fields and unchanged declaration ownership\n",checks);
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL after %zu checks: %s\n",checks,e.what());return 1;}
