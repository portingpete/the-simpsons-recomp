#include "runtime/runtime.h"
#include "runtime/guest_read_memo.h"
#include "runtime/guest_memory.h"
#include <cstdio>
#include <stdexcept>

namespace {
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
}

int main() try {
    Simpsons::Runtime rt;
    rt.map(0x10000,0x10000,true,"memo fixture");
    Simpsons::GuestReadMemo memo;
    memo.setWatch(rt.writeWatch());
    need(!memo.valid(),"A fresh memo was valid");

    const auto record=[&](std::initializer_list<uint32_t> addresses) {
        memo.begin();
        need(!memo.valid(),"A memo was valid while recording");
        for(const auto address:addresses)memo.arm(rt.probe(address,4,false),4);
        memo.commit();
    };

    // Unchanged pages keep the memo valid, across many hits.
    record({0x10010,0x12020,0x14030});
    for(int i=0;i<100;++i)need(memo.valid(),"Memo lost validity with no store");

    // A guest-code style checked store to any recorded page invalidates it.
    for(const uint32_t address:{0x10010u,0x12024u,0x14FFCu}) {
        record({0x10010,0x12020,0x14030});
        need(memo.valid(),"Fresh memo was not valid");
        PPCStoreU32(rt.base,address,0xA5A5A5A5);
        need(!memo.valid(),"A checked store to a recorded page did not invalidate the memo");
    }
    // A native write through pointer(write=true) invalidates it too.
    record({0x10010,0x12020});
    *reinterpret_cast<volatile uint8_t*>(rt.pointer(0x12020,1,true))=7;
    need(!memo.valid(),"A native pointer write did not invalidate the memo");
    // A permission probe is not a store: it must not invalidate.
    record({0x10010,0x12020});
    (void)rt.probe(0x12020,4,true);(void)rt.probe(0x10010,64,true);
    need(memo.valid(),"A write-permission probe invalidated the memo");
    // A store to an unrelated page does not.
    PPCStoreU32(rt.base,0x1A000,1);
    need(memo.valid(),"A store to an unrelated page invalidated the memo");
    // Permission changes invalidate an armed page even when its bytes are unchanged.
    record({0x10010});
    rt.pageAccess[0x10000>>12]=1;
    need(!memo.valid(),"A watched page permission change did not invalidate the memo");
    rt.pageAccess[0x10000>>12]=3;
    need(!memo.valid(),"Restoring permissions revived a stale memo");

    // A whole-allocation permission proof also depends on pages whose contents the
    // validator never compares. Only trackPermissions ties those unarmed pages in.
    memo.begin();memo.trackPermissions();
    memo.arm(rt.probe(0x10010,4,false),4);
    (void)rt.probe(0x1A000,4,false);memo.commit();
    need(memo.valid(),"An unchanged permission proof was not valid");
    rt.pageAccess[0x1A000>>12]=1;
    need(!memo.valid(),"An unarmed page permission change escaped the permission proof");
    rt.pageAccess[0x1A000>>12]=3;
    need(!memo.valid(),"Restoring unarmed permissions revived a stale proof");
    record({0x10010});
    rt.pageAccess[0x1A000>>12]=1;
    need(memo.valid(),"begin did not clear the previous permission dependency");
    rt.pageAccess[0x1A000>>12]=3;

    // Replacing the mapping at the same address must not revive a byte proof.
    rt.map(0x50000,0x1000,true,"memo remap fixture");
    record({0x50010});
    rt.unmap(0x50000);
    need(!memo.valid(),"Unmapping a watched page did not invalidate the memo");
    rt.map(0x50000,0x1000,true,"memo replacement mapping");
    need(!memo.valid(),"A replacement mapping revived a stale memo");
    record({0x50010});
    need(memo.valid(),"A replacement mapping could not be proven afresh");
    memo.begin();memo.trackPermissions();
    memo.arm(rt.probe(0x10010,4,false),4);memo.commit();
    rt.unmap(0x50000);
    need(!memo.valid(),"Unmapping an unarmed page escaped the permission proof");
    memo.begin();memo.trackPermissions();
    memo.arm(rt.probe(0x10010,4,false),4);memo.commit();
    rt.map(0x50000,0x1000,true,"memo unarmed mapping");
    need(!memo.valid(),"Mapping an unarmed page escaped the permission proof");

    // An outer proof can stay byte-valid while an inner proof is re-established
    // with new content. Its saved generation must reject the new inner result.
    Simpsons::GuestReadMemo inner;inner.setWatch(rt.writeWatch());
    const auto proveInner=[&] {
        inner.begin();inner.arm(rt.probe(0x16010,4,false),4);
        const auto value=PPCLoadU32(rt.base,0x16010);inner.commit();return value;
    };
    PPCStoreU32(rt.base,0x16010,1);
    const auto oldValue=proveInner();
    const auto oldGeneration=inner.generation();
    record({0x10010});
    need(inner.valid()&&memo.valid(),"The nested proof fixture was not valid");
    PPCStoreU32(rt.base,0x16010,2);
    need(!inner.valid()&&memo.valid(),"An inner store invalidated the wrong proof");
    const auto newValue=proveInner();
    need(newValue!=oldValue&&inner.valid()&&memo.valid(),"The inner proof did not recover with new content");
    need(inner.generation()!=oldGeneration,"Re-proving an inner result reused its generation");
    const auto dependencyHolds=[&](uint64_t generation) {
        return memo.valid()&&inner.valid()&&inner.generation()==generation;
    };
    need(!dependencyHolds(oldGeneration),"The outer dependency admitted a re-proven inner result");
    need(dependencyHolds(inner.generation()),"The outer dependency rejected the current inner proof");
    const auto currentGeneration=inner.generation();
    inner.invalidate();inner.begin();inner.abandon();
    need(inner.generation()==currentGeneration,"Invalidation or abandonment published a new proof generation");
    // The same-page store barrier also covers a store that starts in the previous 64 KiB block
    // and straddles into a recorded page: arm the first page of a block and write across it.
    rt.map(0x20000,0x20000,true,"memo block fixture");
    memo.begin();memo.arm(rt.probe(0x30000,4,false),4);memo.commit();
    PPCStoreU64(rt.base,0x2FFFC,0x1122334455667788ull);
    need(!memo.valid(),"A store straddling into an armed block start did not invalidate the memo");
    // Re-verification is bounded: after enough hits the memo asks to be rebuilt.
    record({0x10010});
    bool expired=false;
    for(int i=0;i<20000&&!expired;++i)expired=!memo.valid();
    need(expired,"The memo was never re-verified");
    // An abandoned recording never becomes valid. Repeated pages consume one slot.
    memo.begin();memo.arm(rt.probe(0x10010,4,false),4);memo.abandon();
    need(!memo.valid(),"An abandoned recording was valid");
    memo.begin();
    for(uint32_t page=0;page<300;++page)memo.arm(rt.probe(0x10000+(page%16)*0x1000,4,false)+0,4);
    memo.commit();
    need(memo.valid(),"A repeated-page recording within capacity was not valid");
    // The maximum distinct-page proof is accepted; one more page refuses caching.
    rt.map(0x100000,257*0x1000,true,"memo capacity fixture");
    memo.begin();memo.arm(rt.probe(0x100000,256*0x1000,false),256*0x1000);memo.commit();
    need(memo.valid(),"A 256-page recording within capacity was not valid");
    memo.begin();memo.arm(rt.probe(0x100000,257*0x1000,false),257*0x1000);memo.commit();
    need(!memo.valid(),"A 257-page overflowing recording became valid");
    record({0x10010});
    need(memo.valid(),"A fresh recording did not recover after overflow");
    std::puts("PASS guest read memo: stores, permissions, remapping, dependent generations, capacity, block straddle, bounded re-verification");
    return 0;
} catch(const std::exception& error) {std::fprintf(stderr,"FAIL guest read memo: %s\n",error.what());return 1;}
