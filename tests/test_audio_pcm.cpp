// Execute the original CPU-only XMA ring converter against independent signed16
// arithmetic. Synthetic ring data is a semantic fixture, not original audio.
#include "runtime/engine_cpu_calls.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
constexpr uint32_t memory=0x40000,extent=0x10000,ring=0x41000,ringBytes=0x1800;
constexpr uint32_t left=0x45000,right=0x4a000;
size_t cases=0,samples=0;
uint16_t sampleBits(uint32_t frame,uint32_t channel,uint32_t seed) {
    const uint32_t value=seed+frame;
    return uint16_t(channel?value*257+12345:value);
}
void storeExpected(std::vector<uint8_t>& bytes,uint32_t address,uint32_t bits) {
    const uint32_t at=address-memory;
    for(unsigned i=0;i<4;++i) bytes.at(at+i)=uint8_t(bits>>(24-i*8));
}
void checkCase(Simpsons::Runtime& rt,Simpsons::EngineCpuCalls& cpu,uint32_t channels,
               uint32_t tail,uint32_t head,uint32_t lAlign,uint32_t rAlign,uint32_t seed,bool wrap) {
    auto* base=rt.base;
    std::memset(rt.pointer(memory,extent,true),0xa5,extent);
    const uint32_t frameBytes=channels*2,capacity=ringBytes/frameBytes;
    if(tail>capacity || head>capacity || (tail+head)*4+128>0x4000)
        throw Simpsons::Failure("Invalid PCM test extent");
    for(uint32_t frame=0;frame<capacity;++frame)
        for(uint32_t channel=0;channel<channels;++channel)
            PPC_STORE_U16(ring+frame*frameBytes+channel*2,sampleBits(frame,channel,seed));
    const uint32_t offset=wrap && tail?ringBytes-tail*frameBytes:0;
    std::vector<uint8_t> expected(rt.pointer(memory,extent,false),rt.pointer(memory,extent,false)+extent);
    // These original words encode plain dcbz (32 bytes), not dcbzl (128 bytes).
    // Addresses advance by 128: one chunk per 32 mono or 16 stereo samples, at
    // LEFT only. Stereo may therefore zero chunks beyond the requested left
    // plane, with untouched gaps between them. Preserve the exact CPU footprint.
    if(((left+lAlign)&127)==0) {
        const uint32_t chunks=(tail+head)/(channels==1?32:16);
        for(uint32_t chunk=0;chunk<chunks;++chunk)
            std::fill_n(expected.begin()+left+lAlign-memory+128*chunk,32,uint8_t(0));
    }
    for(uint32_t frame=0;frame<tail+head;++frame) {
        const uint32_t input=frame<tail?offset/frameBytes+frame:frame-tail;
        for(uint32_t channel=0;channel<channels;++channel) {
            const int16_t value=std::bit_cast<int16_t>(sampleBits(input,channel,seed));
            const float converted=float(value)*(1.0f/32768.0f);
            storeExpected(expected,(channel?right+rAlign:left+lAlign)+4*frame,std::bit_cast<uint32_t>(converted));
            ++samples;
        }
    }
    auto& ctx=cpu.registers();
    const std::array registers={&ctx.r14,&ctx.r15,&ctx.r16,&ctx.r17,&ctx.r18,&ctx.r19,&ctx.r20,&ctx.r21,&ctx.r22,
                               &ctx.r23,&ctx.r24,&ctx.r25,&ctx.r26,&ctx.r27,&ctx.r28,&ctx.r29,&ctx.r30,&ctx.r31};
    for(size_t i=0;i<registers.size();++i) registers[i]->u64=0xfedcba9800000000ull+i;
    const uint64_t sp=ctx.r1.u64,lr=ctx.lr;
    ctx.r3.u64=0;ctx.r4.u64=left+lAlign;ctx.r5.u64=right+rAlign;
    ctx.r6.u64=ring;ctx.r7.u64=offset;ctx.r8.u64=channels;ctx.r9.u64=tail;ctx.r10.u64=head;
    cpu.invoke(0x8233F250);
    ++cases;
    if(ctx.r1.u64!=sp || ctx.lr!=lr) throw Simpsons::Failure("Original PCM converter changed its caller SP/LR");
    for(size_t i=0;i<registers.size();++i)
        if(registers[i]->u64!=0xfedcba9800000000ull+i) throw Simpsons::Failure("Original PCM converter changed a nonvolatile GPR");
    const auto* actual=rt.pointer(memory,extent,false);
    if(std::memcmp(actual,expected.data(),extent)) {
        uint32_t at=0;while(at<extent && actual[at]==expected[at]) ++at;
        char message[256];std::snprintf(message,sizeof(message),
            "PCM mismatch ch=%u tail=%u head=%u leftAlign=%u rightAlign=%u seed=%u wrap=%u address=%08X actual=%02X expected=%02X",
            channels,tail,head,lAlign,rAlign,seed,unsigned(wrap),memory+at,unsigned(actual[at]),unsigned(expected[at]));
        throw Simpsons::Failure(message);
    }
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=2) throw Simpsons::Failure("Original image path required");
        Simpsons::Runtime rt;rt.load(argv[1]);
        rt.map(0x10000,0x10000,true,"PCM conversion fixture stack");
        rt.map(memory,extent,true,"PCM conversion fixture ring and planes");
        PPCContext entry{};entry.r1.u64=0x20000;entry.lr=0x11223344;
        Simpsons::EngineCpuCalls cpu(entry,rt.base);
        auto* base=rt.base;
        if(PPC_LOAD_U32(0x8233F250)!=0x7d8802a6 || PPC_LOAD_U32(0x821dd2ac)!=0x38000000)
            throw Simpsons::Failure("Original PCM converter/scale pin changed");
        for(uint32_t channels:{1u,2u}) {
            // Every signed16 input bit pattern, independently on both channels.
            for(uint32_t seed=0;seed<65536;seed+=256)
                checkCase(rt,cpu,channels,256,0,0,0,seed,false);
            const std::array<std::array<uint32_t,2>,19> spans={{{0,0},{1,0},{0,1},{15,0},{16,0},{17,0},
                {31,1},{1,31},{32,0},{0,32},{33,0},{63,1},{1,63},{127,1},{1,127},{255,1},{511,1},{512,0},{1,512}}};
            for(auto counts:spans)
                for(uint32_t alignment:{0u,4u,12u,124u})
                    checkCase(rt,cpu,channels,counts[0],counts[1],alignment,alignment?0:4,0x7ff0,true);
        }
        std::printf("PASS original PCM converter: %zu cases, %zu exact samples; mono/stereo, signed16 domain, vector/scalar tails, wrap, guards and GPR ABI\n",cases,samples);
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL original PCM converter: %s\n",error.what());return 1;
    }
}
