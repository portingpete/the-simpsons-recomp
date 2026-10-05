#include "runtime/runtime.h"
#include <winternl.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

PPC_EXTERN_FUNC(__imp__RtlInitAnsiString);
namespace {
size_t checks=0;
void need(bool value,const char* message){++checks;if(!value) throw Simpsons::Failure(message);}
template<class F> void rejects(F action) {
    bool rejected=false;try{action();}catch(const Simpsons::Failure&){rejected=true;}
    need(rejected,"Invalid ANSI memory accepted");
}
using InitAnsi=void(NTAPI*)(PANSI_STRING,PCSZ);
}
int main() {
    try {
        Simpsons::Runtime rt;rt.map(0x10000,0x1000,true,"ANSI descriptors");
        rt.map(0x20000,0x20000,true,"ANSI source strings");
        rt.map(0x50000,0x1000,false,"ANSI readonly descriptor fixture");
        auto* base=rt.base;
        const auto native=reinterpret_cast<InitAnsi>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlInitAnsiString"));
        need(native!=nullptr,"Native RtlInitAnsiString oracle unavailable");
        auto invoke=[&](uint32_t destination,uint32_t source) {
            PPCContext ctx{};std::memset(&ctx,0xa5,sizeof(ctx));
            ctx.r3.u64=destination;ctx.r4.u64=source;ctx.lr=0x82b750c0;
            PPCContext expected{};std::memcpy(&expected,&ctx,sizeof(ctx));
            const auto csr=PPCFPSCRRegister::getcsr();
            __imp__RtlInitAnsiString(ctx,base);
            need(PPCFPSCRRegister::getcsr()==csr,"ANSI initializer leaked host FP state");
            need(!std::memcmp(&ctx,&expected,sizeof(ctx)),"Void ANSI initializer changed CPU context");
        };
        for(uint32_t length:std::array<uint32_t,10>{0,1,7,255,4095,4096,65533,65534,65535,65536}) {
            std::vector<char> bytes(length+1,char(0x81));bytes.back()=0;
            std::memcpy(rt.pointer(0x20000,uint32_t(bytes.size()),true),bytes.data(),bytes.size());
            ANSI_STRING host{};native(&host,bytes.data());
            std::memset(rt.pointer(0x10000,16,true),0xcd,16);
            invoke(0x10004,0x20000);
            need(PPC_LOAD_U16(0x10004)==host.Length && PPC_LOAD_U16(0x10006)==host.MaximumLength,
                 "ANSI descriptor length differs from actual Windows routine");
            need(PPC_LOAD_U32(0x10008)==0x20000 && host.Buffer==bytes.data(),"ANSI source pointer was not borrowed");
            need(PPC_LOAD_U32(0x10000)==0xcdcdcdcd && PPC_LOAD_U32(0x1000c)==0xcdcdcdcd,"ANSI descriptor overstore");
            need(!std::memcmp(rt.pointer(0x20000,uint32_t(bytes.size()),false),bytes.data(),bytes.size()),"ANSI source bytes changed");
        }
        invoke(0x10004,0);
        need(PPC_LOAD_U64(0x10004)==0,"Null ANSI source did not clear the descriptor");
        // Last accessible byte is the terminator. Counting must not overread.
        PPC_STORE_U8(0x3fffe,0xfe);PPC_STORE_U8(0x3ffff,0);
        invoke(0x10004,0x3fffe);
        need(PPC_LOAD_U16(0x10004)==1 && PPC_LOAD_U16(0x10006)==2,"Page-end string count changed");
        // A terminator ends counting even with further nonzero bytes.
        const std::array<uint8_t,5> embedded{0x80,0xff,0,0x90,0};
        std::memcpy(rt.pointer(0x20000,5,true),embedded.data(),5);
        invoke(0x10004,0x20000);need(PPC_LOAD_U16(0x10004)==2,"ANSI bytes were transcoded or NUL ignored");
        // Failure before descriptor publication leaves its full footprint intact.
        for(auto [destination,source]:std::array<std::pair<uint32_t,uint32_t>,6>{{
            {0,0x20000},{0x10ffc,0x20000},{0x50000,0x20000},{0x10004,0x60000},
            {0x10004,0x3ffff},{0x10004,0xfffffffe}}}) {
            PPC_STORE_U8(0x3ffff,1);
            std::memset(rt.pointer(0x10000,0x1000,true),0xcd,0x1000);
            std::array<uint8_t,0x1000> before{};std::memcpy(before.data(),base+0x10000,before.size());
            rejects([&]{invoke(destination,source);});
            need(!std::memcmp(before.data(),base+0x10000,before.size()),"Invalid ANSI input partially published descriptor");
        }
        // The pointer remains aliased to caller-owned bytes after initialization.
        PPC_STORE_U8(0x20000,'a');PPC_STORE_U8(0x20001,0);invoke(0x10004,0x20000);
        PPC_STORE_U8(0x20000,'b');need(PPC_LOAD_U8(PPC_LOAD_U32(0x10008))=='b',"ANSI source unexpectedly copied");
        std::printf("PASS native ANSI string: %zu checks; real Windows length oracle, BE descriptor, borrowed bytes, bounds, failure atomicity and void ABI\n",checks);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL native ANSI string: %s\n",e.what());return 1;}
}
