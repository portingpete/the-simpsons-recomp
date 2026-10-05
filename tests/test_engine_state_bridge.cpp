#include "runtime/engine_state_bridge.h"
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_cache_transaction.h"
#include <cstdio>
#include <bit>
#include <functional>
#include <vector>

namespace {
size_t checks{};
void require(bool ok,const char* message) {++checks;if(!ok) throw Simpsons::Failure(message);}
void rejects(const std::function<void()>& operation) {
    ++checks;try {operation();} catch(const std::exception&) {return;}
    throw Simpsons::Failure("Expected unsupported state to fail");
}
std::vector<uint8_t> copy(uint32_t address,uint32_t size) {
    auto* data=Simpsons::active->pointer(address,size,false);return {data,data+size};
}
void verify(uint32_t address,const std::vector<uint8_t>& before) {
    require(copy(address,uint32_t(before.size()))==before,"Rejected state changed original cache/records");
}
void cacheTransactions() {
    constexpr std::array<uint32_t,3> regions{0x82D0D000,0x82E3D000,0x82D50000};
    constexpr std::array<uint32_t,3> regionSizes{0x4000,0x1000,0x1000};
    constexpr std::array<uint32_t,3> windows{0x82D0D170,0x82E3D160,0x82D501E0};
    constexpr std::array<uint32_t,3> sizes{0x2FBC,0xB24,0x140};
    // Reuse storage across runtime destruction as well as nested transactions.
    for(unsigned owner=0;owner<2;++owner) {
        Simpsons::Runtime rt;
        for(size_t i=0;i<regions.size();++i) {
            rt.map(regions[i],regionSizes[i],true,"cache transaction contract");
            auto* p=rt.pointer(regions[i],regionSizes[i],true);
            for(uint32_t j=0;j<regionSizes[i];++j)p[j]=uint8_t(j*23+owner*79+i);
        }
        auto snapshot=[&] {
            std::array<std::vector<uint8_t>,3> result;
            for(size_t i=0;i<regions.size();++i)result[i]=copy(regions[i],regionSizes[i]);
            return result;
        };
        auto matches=[&](const auto& expected) {
            for(size_t i=0;i<regions.size();++i)verify(regions[i],expected[i]);
        };
        auto mutate=[&](unsigned seed) {
            for(size_t i=0;i<windows.size();++i) {
                auto* p=rt.pointer(windows[i],sizes[i],true);
                for(uint32_t j=0;j<sizes[i];++j)p[j]=uint8_t(j*37+seed*17+i);
            }
        };
        for(unsigned sample=0;sample<64;++sample) {
            const auto before=snapshot();auto expected=before;
            {
                Simpsons::EngineCacheTransaction outer;
                mutate(sample*3);auto intermediate=snapshot();
                {
                    Simpsons::EngineCacheTransaction inner;
                    mutate(sample*3+1);
                    if(sample&1){intermediate=snapshot();inner.publish();}
                }
                matches(intermediate);
                mutate(sample*3+2);
                if(sample&2){expected=snapshot();outer.publish();}
            }
            matches(expected);
            rejects([&] {
                Simpsons::EngineCacheTransaction transaction;
                mutate(sample+193);
                throw Simpsons::Failure("Injected exception after cache mutation");
            });
            matches(expected);
        }
        const auto before=snapshot();
        for(uint32_t page:{0x82D0Du,0x82D0Eu,0x82D0Fu,0x82D10u,0x82E3Du,0x82D50u})
            for(uint8_t access:{uint8_t(0),uint8_t(1)}) {
                rt.pageAccess[page]=access;
                rejects([&]{Simpsons::EngineCacheTransaction transaction;});
                rt.pageAccess[page]=3;
                matches(before);
            }
        rejects([&] {
            Simpsons::EngineCacheTransaction transaction;
            mutate(239);
            rt.stopping=true;rt.stopReason="cache transaction cancellation";
            rt.checkRunning();
        });
        rt.stopping=false;rt.stopReason.clear();matches(before);
    }
    puts("PASS full cache snapshots: reuse, nested commit/rollback, exception recovery, every-page permissions, cancellation and runtime replacement");
}
void emptyCommits(Simpsons::EngineRenderState& state,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    constexpr std::array<uint32_t,3> windows{0x82D0D170,0x82E3D160,0x82D501E0};
    constexpr std::array<uint32_t,3> sizes{0x2FBC,0xB24,0x140};
    auto& rt=*Simpsons::active;
    const auto savedHost=state.effective();const auto savedContext=cpu.registers();
    const auto snapshot=[&] {
        std::array<std::vector<uint8_t>,3> bytes;
        for(size_t i=0;i<windows.size();++i)bytes[i]=copy(windows[i],sizes[i]);
        return bytes;
    };
    const auto restore=[&](const auto& bytes) {
        for(size_t i=0;i<windows.size();++i)std::memcpy(base+windows[i],bytes[i].data(),sizes[i]);
    };
    const auto savedMemory=snapshot();
    struct Outcome {std::string error;uint32_t csr{};bool operator==(const Outcome&)const=default;};
    const auto run=[&](bool reference) {
        Outcome result;
        PPCGuestFloatingPointScope floating(cpu.registers().fpscr);
        try {
            if(reference) {
                // The previous empty-queue implementation: full snapshots,
                // no loop iterations/callbacks, two stores and owner publication.
                require(PPC_LOAD_U32(0x82D10114)==0 && PPC_LOAD_U32(0x82D10118)==0,"Reference requires empty queues");
                Simpsons::EngineCacheTransaction transaction;
                const auto next=state.effective();
                PPC_STORE_U32(0x82D10114,0);PPC_STORE_U32(0x82D10118,0);
                state.publishScreenState(next);transaction.publish();
            } else state.commit(cpu,base);
        } catch(const Simpsons::Failure& error) {result.error=error.what();}
        result.csr=PPCFPSCRRegister::getcsr();return result;
    };
    const auto compare=[&](uint32_t page=0,uint8_t access=3,bool cancelled=false) {
        const auto initial=snapshot();const auto context=cpu.registers();
        const auto oldAccess=page?rt.pageAccess[page].load():uint8_t(3);
        const auto execute=[&](bool reference) {
            restore(initial);cpu.registers()=context;
            if(page)rt.pageAccess[page]=access;
            rt.stopping=cancelled;if(cancelled)rt.stopReason="empty commit cancellation";
            const auto outcome=run(reference);
            rt.stopping=false;rt.stopReason.clear();
            if(page)rt.pageAccess[page]=oldAccess;
            return outcome;
        };
        const auto expected=execute(true);const auto expectedContext=cpu.registers();const auto expectedMemory=snapshot();
        const auto actual=execute(false);
        require(actual==expected,"Empty commit changed rejection or native floating-point status");
        require(!std::memcmp(&cpu.registers(),&expectedContext,sizeof(PPCContext)),"Empty commit changed original registers or trace");
        require(snapshot()==expectedMemory,"Empty commit changed cache bytes or partial-failure behavior");
        for(const auto& field:Simpsons::Graphics::scalarStateEvidence())
            require(state.effective().scalar(field.id)==savedHost.scalar(field.id),"Empty commit changed a scalar");
        for(uint32_t stage=0;stage<16;++stage)
            for(const auto& field:Simpsons::Graphics::samplerStateEvidence())
                require(state.effective().sampler(stage,field.id)==savedHost.sampler(stage,field.id),"Empty commit changed a sampler");
        for(uint32_t target=0;target<4;++target)
            require(state.effective().effectiveBlend(target)==savedHost.effectiveBlend(target),"Empty commit changed packed blend publication");
    };
    for(unsigned sample=0;sample<32;++sample) {
        // Stale/unreferenced entries must remain byte-for-byte untouched.
        for(size_t i=0;i<windows.size();++i)
            for(uint32_t j=0;j<sizes[i];++j)base[windows[i]+j]=uint8_t(j*37+sample*79+i);
        PPC_STORE_U32(0x82D10114,0);PPC_STORE_U32(0x82D10118,0);
        cpu.registers().fpscr.csr=PPCFPSCRRegister::DefaultCSR|
            ((sample%4)<<PPCFPSCRRegister::RoundShift)|((sample&4)?PPCFPSCRRegister::FlushMask:0);
        compare();
    }
    for(uint32_t page:{0x82D0Du,0x82D0Eu,0x82D0Fu,0x82D10u,0x82E3Du,0x82D50u})
        for(uint8_t access:{uint8_t(0),uint8_t(1),uint8_t(2)})compare(page,access);
    compare(0,3,true);
    restore(savedMemory);state.publishScreenState(savedHost);cpu.registers()=savedContext;
    puts("PASS empty commits versus prior full snapshots: all bytes/registers/FP, stale entries, six-page permissions and cancellation");
}
void originalQueries(Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    const auto original=copy(0x82D0E3B0,0x1400);
    // Independently reviewed full136-word leaf and30-byte switch table:
    // build/unbuffered-assets/render-state-query-evidence.json. These fields
    // are retained RenderWare requests, not effective native/SDK values.
    constexpr uint32_t offsets[]={0xF048,0,0xF04C,0xF050,0,0xF004,0xF044,0xF000,0xF054,0xF108,0xF10C,
        0xF028,0xF058,0xF034,0xF040,0xF038,0xF03C,0,0,0xF030,0xF008,0xF00C,0xF010,
        0xF014,0xF018,0xF01C,0xF020,0xF024,0xF110,0x0320};
    std::array<uint32_t,30> values{};
    for(uint32_t i=0;i<30;++i)if(offsets[i]) {
        values[i]=i==16?0x3FC90FDB:(0x01234567^(i*0x00110107));
        PPC_STORE_U32(0x82D0F3B0+uint32_t(int32_t(int16_t(offsets[i]))),values[i]);
    }
    const auto before=copy(0x82D0E3B0,0x1400);
    auto call=[&](uint32_t selector,uint32_t status,uint32_t value) {
        constexpr uint32_t out=0x18004;
        PPC_STORE_U32(out-4,0x89ABCDEF);PPC_STORE_U32(out,0xDEADBEEF);PPC_STORE_U32(out+4,0x76543210);
        auto& c=cpu.registers();const auto saved=c;
        require(cpu.invoke(0x82401260,selector,out)==status,"Original RenderWare query status differs");
        require(PPC_LOAD_U32(out)==value,"Original RenderWare query did not return its retained CPU field");
        require(PPC_LOAD_U32(out-4)==0x89ABCDEF&&PPC_LOAD_U32(out+4)==0x76543210,"RenderWare query overwrote caller output extent");
        require(c.r1.u64==saved.r1.u64&&c.lr==saved.lr,"RenderWare query changed caller stack/link");
        constexpr size_t first=offsetof(PPCContext,r14),last=offsetof(PPCContext,r31)+sizeof(PPCRegister);
        require(!std::memcmp(reinterpret_cast<const uint8_t*>(&c)+first,reinterpret_cast<const uint8_t*>(&saved)+first,last-first),
            "RenderWare query changed nonvolatile registers");
    };
    for(uint32_t i=0;i<30;++i) {
        if(i==1)continue;
        call(i+1,offsets[i]?1:0,offsets[i]?values[i]:0xDEADBEEF);
    }
    for(uint32_t selector:{0u,31u,32u,255u,UINT32_MAX})call(selector,0,0xDEADBEEF);
    verify(0x82D0E3B0,before);
    PPC_STORE_U32(0x82D0E3FC,3);PPC_STORE_U32(0x82D0E400,4);call(2,0,0);
    PPC_STORE_U32(0x82D0E400,3);call(2,1,3);
    // Unsupported selectors never dereference even an invalid output pointer.
    require(cpu.invoke(0x82401260,5,0)==0&&cpu.invoke(0x82401260,UINT32_MAX,0)==0,
        "Unsupported query touched caller output");
    std::memcpy(Simpsons::active->pointer(0x82D0E3B0,0x1400,true),original.data(),original.size());
    puts("PASS original RenderWare query: all30 selectors, U/V disagreement, invalid selectors, caller extent and ABI; no native driver required");
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=2) throw Simpsons::Failure("Original flat image path required");
        cacheTransactions();
        Simpsons::Runtime runtime;runtime.load(argv[1]);
        runtime.map(0x10000,0x10000,true,"state test stack");
        auto* base=runtime.base;
        PPCContext entry{};entry.r1.u32=0x20000;entry.lr=0x11223344;
        Simpsons::currentContext=&entry;
        {
            Simpsons::EngineCpuCalls cpu(entry,base);
            require(Simpsons::currentContext==&cpu.registers(),"Original callback TLS context missing");
            require(PPC_LOAD_U32(cpu.registers().r1.u32)==entry.r1.u32,"Callback ABI backchain mismatch");
            cpu.invoke(0x8240EC28); // Original CPU pipeline default records.
            originalQueries(cpu,base); // Original leaf runs without a native state owner.
            PPC_STORE_U32(0x82E3DFE0,0x200);
            Simpsons::EngineRenderState state;
            rejects([&]{cpu.invoke(0x82400040);});
            cpu.invoke(0x824008E0); // Actual generated engine-entry hook.
            require(state.effective().initialized(),"State initialization missing");
            // Differential check against the actual original CPU setter body.
            // These are isolated test bytes, never a live console device.
            runtime.map(0x30000,0x20000,true,"original state differential fixtures");
            cpu.invoke(0x82725848,0x40000); // Original application tables/defaults.
            {
                const auto before=state.effective();
                const auto fields=Simpsons::Graphics::scalarStateEvidence();
                const auto missing=fields.back().id/4-9;
                state.beginRecording(0x00600001);
                rejects([&]{state.beginRecording(0x00600001);});
                for(const auto& field:fields)
                    state.applicationScalar(base,0x40000,field.id/4-9,before.scalar(field.id),false);
                rejects([&]{state.requireRecordingSeed(0x00600001);}); // Preflight is not a submission.
                for(const auto& field:fields)if(field.id/4-9!=missing)
                    state.applicationScalar(base,0x40000,field.id/4-9,before.scalar(field.id),true);
                for(uint32_t stage=0;stage<16;++stage)
                    for(const auto& field:Simpsons::Graphics::samplerStateEvidence())
                        state.applicationSampler(base,0x40000,stage,field.id/4+1,before.sampler(stage,field.id),true);
                const auto rank=PPC_LOAD_U32(0x82D6D498+4*missing);
                PPC_STORE_U32(0x82D6D6A0+4*rank,fields.front().id/4-9);
                rejects([&]{state.requireRecordingSeed(0x00600001);}); // Duplicate cannot hide the omitted field.
                PPC_STORE_U32(0x82D6D6A0+4*rank,missing);
                state.applicationScalar(base,0x40000,missing,before.scalar(fields.back().id),true);
                state.requireRecordingSeed(0x00600001);
                rejects([&]{state.requireRecordingSeed(0x00600002);});
                state.endRecording(0x00600001);
                for(const auto& field:fields)
                    require(state.effective().scalar(field.id)==before.scalar(field.id),"Recording restore changed main scalar state");
                for(uint32_t stage=0;stage<16;++stage)
                    for(const auto& field:Simpsons::Graphics::samplerStateEvidence())
                        require(state.effective().sampler(stage,field.id)==before.sampler(stage,field.id),"Recording restore changed main sampler state");
            }
            for(uint32_t byte=0;byte<256;++byte) {
                const uint32_t argb=(byte<<24)|((255-byte)<<16)|((byte^0xA5)<<8)|(byte^0x5A);
                cpu.invoke(0x8243A4D0,0x30000,argb);
                state.applicationScalar(base,0x40000,8,argb,true);
                const auto rgba=state.effective().blendConstant();
                for(uint32_t lane=0;lane<4;++lane)
                    require(std::bit_cast<uint32_t>(rgba[lane])==PPC_LOAD_U32(0x328E0+4*lane),
                            "Native blend constant differs from original AOT conversion");
                const uint32_t prior=0x5AA5AA5A;
                PPC_STORE_U32(0x3293C,prior);
                cpu.invoke(0x8243B7A8,0x30000,byte&1);
                cpu.invoke(0x8243B7D8,0x30000,byte);
                state.applicationScalar(base,0x40000,0x4B,byte&1,true);
                state.applicationScalar(base,0x40000,0x4C,byte,true);
                require(PPC_LOAD_U32(0x3293C)==((prior&0x00FFFFEF)|((byte&1)<<4)|(byte<<24)),
                        "Original alpha-to-mask setters have unexpected adjacent effects");
                require(state.effective().scalar(0x150)==((PPC_LOAD_U32(0x3293C)>>4)&1) &&
                        state.effective().scalar(0x154)==(PPC_LOAD_U32(0x3293C)>>24),
                        "Native alpha-to-mask state differs from original AOT setters");
            }
            state.applicationScalar(base,0x40000,8,0xFFFFFFFF,true);
            state.applicationScalar(base,0x40000,0x4B,0,true);
            state.applicationScalar(base,0x40000,0x4C,0x87,true);
            for(const auto& field:Simpsons::Graphics::scalarStateEvidence()) {
                if(field.id!=0x70 && (field.id<0x90 || field.id>0xA8)) continue;
                const uint32_t table=0x82CD28B8+3*field.id;
                cpu.invoke(field.setterAddress,0x30000,field.startupValue);
                const uint32_t result=cpu.invoke(PPC_LOAD_U32(table),0x30000);
                const uint32_t mask=field.id>=0xA0?255:(field.id==0x70?1:7);
                require(result==(field.startupValue&mask),"Original back-stencil setter/getter disagrees with retained state");
                state.applicationScalar(base,0x40000,field.id/4-9,field.startupValue,true);
                require(state.effective().scalar(field.id)==field.startupValue,"Native back-stencil state lost its original raw request");
            }
            for(uint32_t id:{0x84u,0x88u,0x8Cu,0xA0u,0xA4u,0xA8u}) {
                const uint32_t table=0x82CD28B8+3*id;
                for(uint32_t value=0;value<256;++value) {
                    cpu.invoke(PPC_LOAD_U32(table+4),0x30000,value);
                    const uint32_t actual=cpu.invoke(PPC_LOAD_U32(table),0x30000);
                    state.applicationScalar(base,0x40000,id/4-9,value,true);
                    require(actual==value && state.effective().scalar(id)==value,
                            "Canonical stencil byte differs from original AOT setter/getter");
                }
                const uint32_t restore=PPC_LOAD_U32(table+8);
                state.applicationScalar(base,0x40000,id/4-9,restore,true);
            }
            // Remaining startup registrations: actual original AOT getter/setter
            // pairs, restricted to the observed SDK and application values.
            constexpr uint32_t remainingScalarIds[]={0x168,0x16C,0x170,0x130,0xC8,0x144,0x158,0x15C,
                0xAC,0xC0,0xC4,0xB0,0xB4,0xB8,0xBC,0xE4,0xE8,0xEC,0x178,
                0xF0,0xF4,0xF8,0xFC,0x100,0x104,0x108,0x10C,0x110,0x114,0x118,0x11C,0x120,0x124,0x128,0x12C,0x148,0x14C};
            for(uint32_t id:remainingScalarIds) {
                const uint32_t table=0x82CD28B8+3*id,sdk=PPC_LOAD_U32(table+8);
                uint32_t app=sdk;
                if(id==0x144 || id==0x148 || id==0x178) app=1;
                if(id==0x158 || id==0x15C || id==0xBC) app=0x3F800000;
                if(id==0xC4) app=0xFFFF;
                if(id==0xE4) app=0;
                for(uint32_t value:{sdk,app}) {
                    // Scissor's original tail calls a console submission helper.
                    // Its disabled request is checked natively; never run that
                    // mixed CPU/submission function on fixture or live SDK data.
                    if(id!=0xC8) {
                        cpu.invoke(PPC_LOAD_U32(table+4),0x30000,value);
                        const uint32_t actual=cpu.invoke(PPC_LOAD_U32(table),0x30000);
                        require(actual==(id==0xC4?(value&0xFFFF):value),
                                "Retained startup scalar differs from original AOT setter/getter");
                    }
                    state.applicationScalar(base,0x40000,id/4-9,value,true);
                    require(state.effective().scalar(id)==value,"Native startup scalar lost raw SDK/application distinction");
                }
                state.applicationScalar(base,0x40000,id/4-9,sdk,true);
            }
            // Compare the accepted sampler subset against original SDK setter /
            // getter pairs on isolated memory, including application slots 8..15.
            constexpr uint32_t acceptedSamplerIds[]={0,4,8,0xC,0x10,0x14,0x18,0x1C,0x20,0x24,0x28,0x2C,0x30,0x34,0x38,0x3C,0x40,0x44,0x48,0x4C};
            for(uint32_t stage=0;stage<16;++stage) {
                for(uint32_t id:acceptedSamplerIds) {
                    const uint32_t table=0x82CD2D78+3*id;
                    const uint32_t first=(id==0x24 || id==0x4C)?1:id==0x34?13:0;
                    const uint32_t last=id<=8?7:(id==0x10 || id==0x14 || id==0x28 || id==0x2C || id==0x30)?1:id==0x18?2:first;
                    for(uint32_t value=first;value<=last;++value) {
                        cpu.invoke(PPC_LOAD_U32(table+4),0x30000,stage,value);
                        const uint32_t actual=cpu.invoke(PPC_LOAD_U32(table),0x30000,stage);
                        state.applicationSampler(base,0x40000,stage,id/4+1,value,true);
                        // The anisotropy-bias getter negates float zero. The
                        // application cache/native scalar still retain raw +0.
                        const uint32_t expectedGetter=id==0x3C?0x80000000:value;
                        require(actual==expectedGetter && state.effective().sampler(stage,id)==value,
                                "Native sampler differs from original AOT setter/getter");
                    }
                    const auto f=Simpsons::Graphics::samplerStateEvidence()[id/4];
                    state.applicationSampler(base,0x40000,stage,id/4+1,stage<8?f.startupValue:f.sdkDefault,true);
                }
            }
            // Original normal and separate Z filter setters recompute the same
            // effective two-bit volume filter. Exercise each intermediate update
            // across all five Boolean requests at all application stages.
            constexpr uint32_t filters[]={0x10,0x14,0x28,0x2C,0x30};
            for(uint32_t stage=0;stage<16;++stage) {
                for(uint32_t id:filters) {
                    cpu.invoke(PPC_LOAD_U32(0x82CD2D78+3*id+4),0x30000,stage,0);
                    state.applicationSampler(base,0x40000,stage,id/4+1,0,true);
                }
                for(uint32_t bits=0;bits<32;++bits) for(uint32_t k=0;k<5;++k) {
                    const uint32_t id=filters[k],value=(bits>>k)&1;
                    cpu.invoke(PPC_LOAD_U32(0x82CD2D78+3*id+4),0x30000,stage,value);
                    state.applicationSampler(base,0x40000,stage,id/4+1,value,true);
                    require(state.effective().effectiveVolumeFilter(stage)==(PPC_LOAD_U32(0x30480+24*stage+16)&3),
                            "Native effective volume filter differs from original coupled setter behavior");
                }
                for(uint32_t id:filters) {
                    const auto f=Simpsons::Graphics::samplerStateEvidence()[id/4];
                    state.applicationSampler(base,0x40000,stage,id/4+1,stage<8?f.startupValue:f.sdkDefault,true);
                }
            }
            require(PPC_LOAD_U32(0x82D0CAF8)==0,"Native state wrote a console device pointer");
            require(PPC_LOAD_U32(0x82D10114)==0 && PPC_LOAD_U32(0x82D10118)==0,"Startup queues not empty");
            emptyCommits(state,cpu,base);
            for(uint32_t i=0;i<425;++i) {
                require(PPC_LOAD_U32(0x82D0F3B0+8*i)==PPC_LOAD_U32(0x82E3D580+4*i),"Scalar pending/applied mismatch");
                require(PPC_LOAD_U32(0x82D0F3B4+8*i)==0,"Scalar startup dirty flag not cleared");
            }
            for(uint32_t stage=0;stage<8;++stage) {
                require(PPC_LOAD_U32(0x82D501EC+40*stage)==(stage?1:3),"Original color operation mismatch");
                require(PPC_LOAD_U32(0x82D501FC+40*stage)==(stage?1:3),"Original alpha operation mismatch");
                for(uint32_t id=0;id<33;++id) {
                    uint32_t index=33*stage+id;
                    require(PPC_LOAD_U32(0x82D0DB70+8*index)==PPC_LOAD_U32(0x82E3D160+4*index),"Stage pending/applied mismatch");
                    require(PPC_LOAD_U32(0x82D0DB74+8*index)==0,"Stage dirty flag not cleared");
                }
                require(PPC_LOAD_U32(0x82D0D170+320*stage+4*0x18)==2,"Inline original base-only sampler write lost");
                require(PPC_LOAD_U32(0x82D0D170+320*stage+4*8)==0xFFFFFFFF,"Unwritten sampler cache was invented");
                require(state.effective().sampler(stage,8)==0,"SDK inherited W repeat missing");
                require(state.effective().sampler(stage,0x10)==1,"Linear sampler baseline missing");
            }
            require(PPC_LOAD_U32(0x82D0E3EC)==0x3F800000,"Original float word not retained");
            auto initial=copy(0x82D0D170,0x2FBC);
            rejects([&]{cpu.invoke(0x824008E0);});verify(0x82D0D170,initial);
            // Original queue helper overwrites without a duplicate entry.
            cpu.invoke(0x82400170,0x28,0);cpu.invoke(0x82400170,0x28,1);cpu.invoke(0x82400170,0x28,0);
            require(PPC_LOAD_U32(0x82D10114)==1,"Original scalar queue did not deduplicate");
            cpu.invoke(0x82400040);
            require(state.effective().scalar(0x28)==0 && PPC_LOAD_U32(0x82E3D580+4*0x28)==0,"Scalar native/guest commit mismatch");
            // Invalid SDK state after a valid one: all pending/applied state retained.
            cpu.invoke(0x82400170,0x28,1);cpu.invoke(0x82400170,0,123);
            auto pending=copy(0x82D0D170,0x2FBC),applied=copy(0x82E3D160,0xB24),records=copy(0x82D501E0,0x140);
            rejects([&]{cpu.invoke(0x82400040);});
            verify(0x82D0D170,pending);verify(0x82E3D160,applied);verify(0x82D501E0,records);
            require(state.effective().scalar(0x28)==0,"Rejected scalar transaction changed host state");
            // Cancel the unknown queued value by restoring its applied sentinel.
            cpu.invoke(0x82400170,0,0xFFFFFFFF);cpu.invoke(0x82400040);
            require(state.effective().scalar(0x28)==1,"Recovered scalar transaction failed");
            // First original CPU stage mutates, second rejects; rollback both.
            cpu.invoke(0x824001E0,0,1,4);cpu.invoke(0x824001E0,1,1,0);
            pending=copy(0x82D0D170,0x2FBC);applied=copy(0x82E3D160,0xB24);records=copy(0x82D501E0,0x140);
            rejects([&]{cpu.invoke(0x82400040);});
            verify(0x82D0D170,pending);verify(0x82E3D160,applied);verify(0x82D501E0,records);
            cpu.invoke(0x824001E0,1,1,1);cpu.invoke(0x82400040);
            require(PPC_LOAD_U32(0x82D501EC)==4,"Original stage update did not reach CPU record");
            // Engine-only IDs retain their full raw value without an SDK call.
            cpu.invoke(0x82400170,0x1A8,0xCAFEBABE);cpu.invoke(0x82400040);
            require(PPC_LOAD_U32(0x82E3D580+4*0x1A8)==0xCAFEBABE,"High engine state was discarded");
            cpu.invoke(0x82400278,3,0x10,0);
            require(state.effective().sampler(3,0x10)==0 && PPC_LOAD_U32(0x82D0D170+3*320+0x40)==0,"Sampler commit mismatch");
            pending=copy(0x82D0D170,0x2FBC);
            rejects([&]{cpu.invoke(0x82400278,3,0x10,2);});
            rejects([&]{cpu.invoke(0x82400278,8,0x10,0);});
            verify(0x82D0D170,pending);
            require(state.effective().sampler(3,0x10)==0,"Rejected sampler changed host state");
        }
        require(Simpsons::currentContext==&entry,"Callback TLS context not restored");
        {
            Simpsons::EngineCpuCalls cpu(entry,base);
            rejects([&]{cpu.invoke(0x82400040);}); // No native owner survives scope.
            Simpsons::EngineRenderState state;
            PPC_STORE_U32(0x82E3DFE0,0);
            cpu.invoke(0x824008E0);
            require(state.effective().sampler(0,0x10)==0 && PPC_LOAD_U32(0x82D0E404)==1,"Capability-selected point branch was lost");
        }
        std::printf("PASS: original engine state bridge / %zu checks\n",checks);
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL after %zu checks: %s\n",checks,error.what());return 1;}
}
