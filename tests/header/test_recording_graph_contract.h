#pragma once
// Integration fixture for a caller with the real catalog, main camera and
// uploaded shadow copies already active. Supply three independently constructed
// rigid packets: A and C share an object but have distinct metadata; B belongs
// to another object. Each packet must produce one genuine recorded draw.
// Invoke before any other recording. This intentionally leaves three completed
// CPU records alive; use terminal runtime teardown, not the empty-owner fixture.
#include "runtime/engine_cpu_calls.h"
#include "runtime/engine_driver.h"
#include "runtime/engine_recording.h"
#include "renderer/native_backend.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace Simpsons::Tests {
inline void recordingGraphContracts(Runtime& runtime,EngineCpuCalls& cpu,
                                    const std::array<uint32_t,3>& packets,
                                    Graphics::NativeBackend& backend) {
    auto* base=runtime.base;auto& owners=runtime.engineDriver->recordingOwners();
    const auto need=[](bool value,const char* why){if(!value)throw Failure(why);};
    const uint32_t owner=PPC_LOAD_U32(0x82D09784),pool=owner+0xC;
    runtime.pointer(owner,0x7C,true);
    need(owner&&PPC_LOAD_U32(pool+0xC)==2000&&!PPC_LOAD_U32(pool+0x10)&&
         !PPC_LOAD_U32(owner+0x44)&&!PPC_LOAD_U32(owner+0x50),"Graph fixture requires an untouched real recording manager");
    std::array<uint32_t,3> objects{},metadata{},nodes{},payloads{};
    for(size_t i=0;i<packets.size();++i) {
        runtime.pointer(packets[i],0x24,false);objects[i]=PPC_LOAD_U32(packets[i]+4);metadata[i]=PPC_LOAD_U32(packets[i]);
        runtime.pointer(objects[i],0xC0,true);
        need(!PPC_LOAD_U32(objects[i]+0xB4),"Graph fixture packet cache was already populated");
    }
    need(objects[0]==objects[2]&&objects[0]!=objects[1]&&metadata[0]!=metadata[2],
         "Graph fixture needs separate-object and shared-object distinct-metadata cases");
    const uint32_t block=PPC_LOAD_U32(pool+0x30),freeBefore=PPC_LOAD_U32(pool+0x34);
    const uint64_t recordedBefore=backend.recordingDrawCount(),executedBefore=backend.recordingExecutedDrawCount();
    const auto invoke=[&](size_t i) {
        // This retains the complete dispatcher, build/finish, LRU helper,
        // original two-bank upload and real native execution. No graph stores.
        cpu.invoke(0x8273B4D0,packets[i]);
    };
    const auto checkLru=[&](std::initializer_list<uint32_t> expected) {
        uint32_t at=PPC_LOAD_U32(owner+0x44),previous=0;
        for(uint32_t node:expected) {
            need(at==node&&PPC_LOAD_U32(node+4)==previous,"Original LRU order/backlink differs from dispatch order");
            previous=node;at=PPC_LOAD_U32(node);
        }
        need(!at&&PPC_LOAD_U32(owner+0x48)==previous,"Original LRU tail/end link differs");
    };
    const auto record=[&](size_t i,uint32_t count) {
        const uint32_t borrowed=PPC_LOAD_U32(pool+0x34),next=PPC_LOAD_U32(borrowed);
        invoke(i);nodes[i]=PPC_LOAD_U32(objects[i]+0xB4);payloads[i]=PPC_LOAD_U32(nodes[i]+0x28);
        need(nodes[i]==borrowed&&PPC_LOAD_U32(pool+0x34)==next&&PPC_LOAD_U32(pool+0xC)==2000-count&&
             PPC_LOAD_U32(pool+0x10)==count&&PPC_LOAD_U32(owner+0x50)==count,
             "Original publication did not borrow precisely one existing free slot");
        need(PPC_LOAD_U32(nodes[i]+0x1C)==metadata[i]&&PPC_LOAD_U32(nodes[i]+0x2C)==2&&
             !PPC_LOAD_U32(nodes[i]+0x10)&&PPC_LOAD_U32(nodes[i]+0x14),"Original finish did not publish successful native owned-data accounting");
        owners.requireReplay(packets[i],payloads[i]);
    };
    record(0,1);need(nodes[0]==freeBefore,"First original pool borrow changed");checkLru({nodes[0]});
    record(1,2);checkLru({nodes[1],nodes[0]});
    need(payloads[0]!=payloads[1],"Distinct recordings reused a native payload identity");
    invoke(0);checkLru({nodes[0],nodes[1]});
    record(2,3);checkLru({nodes[2],nodes[0],nodes[1]});
    need(payloads[2]!=payloads[0]&&payloads[2]!=payloads[1]&&PPC_LOAD_U32(objects[0]+0xB4)==nodes[2]&&
         PPC_LOAD_U32(nodes[2]+0xC)==nodes[0]&&PPC_LOAD_U32(nodes[2]+8)==objects[0]+0xB4&&
         PPC_LOAD_U32(nodes[0]+8)==nodes[2]+0xC,"Original shared-object insertion lost the old node's slot backlink");
    // A now has both an older native payload and a non-head CPU cache node.
    invoke(0);checkLru({nodes[0],nodes[2],nodes[1]});
    need(backend.recordingDrawCount()==recordedBefore+3&&backend.recordingExecutedDrawCount()==executedBefore+5,
         "A/B/A/C/A must record three real draws and execute five");
    const uint32_t byteTotal=PPC_LOAD_U32(nodes[0]+0x14)+PPC_LOAD_U32(nodes[1]+0x14)+PPC_LOAD_U32(nodes[2]+0x14);
    need(PPC_LOAD_U32(owner+0x4C)==byteTotal&&PPC_LOAD_U32(pool+0x30)==block&&
         PPC_LOAD_U32(pool+0xC)==1997&&PPC_LOAD_U32(pool+0x10)==3,"Completed cache/pool totals changed during old-node replay");

    // Independent corruptions exercise production validation before GPU work.
    // Restore each isolated byte mutation even on failure; never repair lists
    // as part of the successful original CPU path.
    const auto corrupt=[&](uint32_t address,uint32_t value) {
        const uint32_t saved=PPC_LOAD_U32(address);const auto executions=backend.recordingExecutedDrawCount();
        struct Restore {uint8_t* base;uint32_t address,value;~Restore(){PPC_STORE_U32(address,value);}} restore{base,address,saved};
        PPC_STORE_U32(address,value);bool rejected=false;
        try {owners.requireReplay(packets[0],payloads[0]);}catch(const Failure&){rejected=true;}
        need(rejected&&backend.recordingExecutedDrawCount()==executions,"Corrupt original recording graph reached native execution");
    };
    const uint32_t free=PPC_LOAD_U32(pool+0x34);
    corrupt(free,free);                         // Free-list cycle.
    corrupt(nodes[0]+8,objects[0]+0xB4);        // Non-head slot backlink.
    corrupt(nodes[0]+0x28,payloads[1]);         // Foreign owned payload.
    corrupt(nodes[2]+0xC,nodes[2]);             // Object-chain cycle.
    corrupt(nodes[1]+4,0);                     // LRU reciprocal link.
    corrupt(owner+0x50,0);                     // Unobserved event reset.
    corrupt(owner+0x4C,byteTotal+4);            // Incorrect owned-data total.
    owners.requireReplay(packets[0],payloads[0]);

    std::array<uint8_t,0x7C> manager{};std::memcpy(manager.data(),runtime.pointer(owner,manager.size(),false),manager.size());
    const size_t blockSize=2000*0x34+4+0x13;auto* blockBytes=runtime.pointer(block,blockSize,false);
    const std::vector<uint8_t> poolBytes(blockBytes,blockBytes+blockSize);
    const auto recorded=backend.recordingDrawCount(),executed=backend.recordingExecutedDrawCount();
    need(PPC_LOAD_U32(0x82D61DD0)!=0,"Graph fixture needs the genuine registered counter event");
    cpu.invoke(0x826F3988,owner+4); // Whole original receiver; its sole store clears O+50.
    std::fill(manager.begin()+0x50,manager.begin()+0x54,0);
    need(!std::memcmp(manager.data(),runtime.pointer(owner,manager.size(),false),manager.size())&&
         !std::memcmp(poolBytes.data(),blockBytes,blockSize)&&backend.recordingDrawCount()==recorded&&
         backend.recordingExecutedDrawCount()==executed,"Original event reset changed resident records, manager history or GPU work");
    owners.requireReplay(packets[0],payloads[0]);owners.requireReplay(packets[1],payloads[1]);owners.requireReplay(packets[2],payloads[2]);
    invoke(1);checkLru({nodes[1],nodes[0],nodes[2]});
    need(!PPC_LOAD_U32(owner+0x50)&&PPC_LOAD_U32(owner+0x4C)==byteTotal&&
         backend.recordingDrawCount()==recorded&&backend.recordingExecutedDrawCount()==executed+1,
         "Post-event cached replay borrowed or recorded new work");
}
}
