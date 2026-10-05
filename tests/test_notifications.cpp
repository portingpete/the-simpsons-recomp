// Synthetic notifications below are fixtures, not native UI/profile events or
// evidence of the original platform's startup notification policy. This target
// links SimpsonsRuntime and calls real PPC imports without loading game assets.
#include "runtime/runtime.h"
#include "runtime/native_notifications.h"
#include "ppc_recomp_shared.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <thread>
#include <vector>

namespace {
using Simpsons::Runtime;
using Simpsons::KernelHandle;
using Simpsons::Platform::NativeNotifications;
using Simpsons::Platform::Notification;
using Simpsons::Platform::NotificationListener;

void require(bool ok,const char* message) {
    if(!ok) throw std::runtime_error(message);
}
template<class Error,class F> void rejects(F&& action,const char* message,const char* reason="") {
    try {action();}
    catch(const Error& error) {
        require(std::string(error.what()).find(reason)!=std::string::npos,"Unexpected rejection reason");
        return;
    }
    throw std::runtime_error(message);
}
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE handle):value(handle) {require(value && value!=INVALID_HANDLE_VALUE,"Fixture handle creation failed");}
    ~Handle() {CloseHandle(value);}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
};
void signal(HANDLE event) {require(SetEvent(event)!=FALSE,"Fixture event signalling failed");}
void waitState(HANDLE event,bool ready) {
    require(WaitForSingleObject(event,0)==(ready?WAIT_OBJECT_0:WAIT_TIMEOUT),"Notification event/queue state mismatch");
}
void expectRead(const std::shared_ptr<NotificationListener>& listener,Notification expected,uint32_t match=0) {
    require(listener->read(match)==expected,"Notification payload/order mismatch");
}
void expectQueue(const std::shared_ptr<NotificationListener>& listener,const std::vector<Notification>& expected) {
    require(listener->pendingCount()==expected.size(),"Subscription included/excluded the wrong notifications");
    Handle event(listener->duplicateWaitHandle());
    waitState(event.value,!expected.empty());
    for(const auto notification:expected) expectRead(listener,notification);
    require(!listener->read(),"Subscription had extra notifications");
    waitState(event.value,false);
}

void eventAndFilterContracts() {
    NativeNotifications broker;
    broker.publish({9,0x11111111}); // Publishing before registration is not replayed.
    auto first=broker.create(1,2),second=broker.create(1,2);
    Handle event(first->duplicateWaitHandle()),duplicate(first->duplicateWaitHandle());
    require(first->pendingCount()==0 && second->pendingCount()==0,"Listener creation fabricated startup events");
    waitState(event.value,false);waitState(duplicate.value,false);
    Handle entered(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    DWORD wake=WAIT_FAILED;
    std::exception_ptr waitError;
    std::jthread waiter([&] {
        try {signal(entered.value);wake=WaitForSingleObject(event.value,2000);}
        catch(...) {waitError=std::current_exception();}
    });
    require(WaitForSingleObject(entered.value,2000)==WAIT_OBJECT_0,"Native waiter did not start");
    broker.publish({9,0x12345678});
    waiter.join();
    if(waitError) std::rethrow_exception(waitError);
    require(wake==WAIT_OBJECT_0,"Native publication failed to wake a real event waiter");
    waitState(event.value,true);waitState(event.value,true);waitState(duplicate.value,true);
    broker.publish({9,0x12345678}); // Identical duplicates are significant.
    broker.publish({10,0x89abcdef});
    require(!first->read(11) && first->pendingCount()==3,"Filter miss consumed a notification");
    waitState(event.value,true);
    expectRead(first,{10,0x89abcdef},10); // Remove a non-front match only.
    waitState(event.value,true);
    expectRead(first,{9,0x12345678},9);
    waitState(duplicate.value,true);
    expectRead(first,{9,0x12345678});
    waitState(event.value,false);waitState(duplicate.value,false);
    expectQueue(second,{{9,0x12345678},{9,0x12345678},{10,0x89abcdef}});
    broker.publish({9,7}); // The same event must wake again after being reset.
    waitState(event.value,true);expectRead(first,{9,7});waitState(event.value,false);
}

void identifierContracts() {
    NativeNotifications broker;
    auto none=broker.create(0,511),zero=broker.create(1,0),two=broker.create(1,2);
    auto version255=broker.create(1,255),category31=broker.create(1ull<<31,511);
    auto category32=broker.create(1ull<<32,511),category63=broker.create(1ull<<63,511);
    auto combined=broker.create(1|(1ull<<63),2),all=broker.create(~0ull,511);
    // Explicit field-edge IDs: version occupies nine bits, category six. The
    // local 16-bit ID may be zero when the complete identifier is nonzero.
    const std::vector<Notification> items={
        {0x00000001,1},{0x00010007,2},{0x00020009,3},{0x00030001,4},
        {0x01000003,5},{0x01ffffff,6},{0x3fff0001,7},{0x40020002,8},
        {0x7fffffff,9},{0x7e020000,10}};
    rejects<Simpsons::Platform::NotificationError>([&]{broker.create(1,512);},"Version 512 accepted");
    rejects<Simpsons::Platform::NotificationError>([&]{broker.create(1,0xffffffff);},"Oversized version accepted");
    rejects<Simpsons::Platform::NotificationError>([&]{broker.publish({0,99});},"Zero notification ID accepted");
    rejects<Simpsons::Platform::NotificationError>([&]{broker.publish({0x80000001,99});},"Reserved ID bit accepted");
    require(all->pendingCount()==0,"Invalid publication changed queues");
    for(auto item:items) broker.publish(item);
    expectQueue(none,{});expectQueue(zero,{items[0]});
    expectQueue(two,{items[0],items[1],items[2]});
    expectQueue(version255,{items[0],items[1],items[2],items[3]});
    expectQueue(category31,{items[6]});expectQueue(category32,{items[7]});
    expectQueue(category63,{items[8],items[9]});
    expectQueue(combined,{items[0],items[1],items[2],items[9]});expectQueue(all,items);
}

void independentLifetimeContracts() {
    std::shared_ptr<NotificationListener> survivor;
    std::weak_ptr<NativeNotifications> brokerWeak;
    {
        auto broker=std::make_shared<NativeNotifications>();brokerWeak=broker;
        auto transient=broker->create(1,2);
        std::weak_ptr<NotificationListener> weak=transient;
        transient.reset();
        require(weak.expired(),"Broker registration strongly owns a closed listener");
        broker->publish({9,3}); // Expired registration must be harmless.
        survivor=broker->create(1,2);broker->publish({10,4});
    }
    require(brokerWeak.expired(),"Listener unnecessarily keeps its broker alive");
    Handle event(survivor->duplicateWaitHandle());
    waitState(event.value,true);expectRead(survivor,{10,4});waitState(event.value,false);
    std::weak_ptr<NotificationListener> weak=survivor;
    survivor.reset();require(weak.expired(),"Duplicate event owns the listener queue");
    waitState(event.value,false); // Duplicate remains a valid OS object.
}

void concurrentBrokerContracts() {
    NativeNotifications broker;
    auto reader=broker.create(1,2),reference=broker.create(1,2);
    Handle event(reader->duplicateWaitHandle()),start(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    std::atomic<unsigned> finished=0;
    std::atomic<bool> abort=false;
    std::array<std::exception_ptr,4> errors{};
    std::vector<Notification> received;
    std::vector<std::weak_ptr<NotificationListener>> closed;
    auto worker=[&](size_t index,auto body) {
        return std::jthread([&,index,body] {
            try {
                require(WaitForSingleObject(start.value,2000)==WAIT_OBJECT_0,"Concurrent fixture start timed out");
                body();
            } catch(...) {errors[index]=std::current_exception();abort=true;}
        });
    };
    auto publish=[&](uint32_t source) {
        for(uint32_t sequence=0;sequence<64 && !abort;++sequence) {
            broker.publish({9,(source<<16)|sequence});
            std::this_thread::yield();
        }
        ++finished;
    };
    auto first=worker(0,[&]{publish(1);});
    auto second=worker(1,[&]{publish(2);});
    auto consumer=worker(2,[&] {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!abort) {
            require(std::chrono::steady_clock::now()<deadline,"Concurrent read/publication exceeded bound");
            if(auto next=reader->read()) received.push_back(*next);
            else if(finished==2) break;
            else {
                const auto result=WaitForSingleObject(event.value,20);
                require(result==WAIT_OBJECT_0 || result==WAIT_TIMEOUT,"Concurrent event wait failed");
            }
        }
    });
    auto closer=worker(3,[&] {
        for(unsigned i=0;i<32 && !abort;++i) {
            auto transient=broker.create(1,2);closed.push_back(transient);
            (void)transient->read();
            transient.reset(); // Races publication's weak-to-strong lease.
            std::this_thread::yield();
        }
    });
    signal(start.value);
    first.join();second.join();consumer.join();closer.join();
    for(auto error:errors) if(error) std::rethrow_exception(error);
    // A producer may publish between an empty read and observing finished==2.
    while(auto next=reader->read()) received.push_back(*next);
    require(received.size()==128,"Concurrent publication lost or duplicated notifications");
    std::array<uint32_t,2> nextSequence{};
    for(auto notification:received) {
        const uint32_t source=notification.parameter>>16;
        require(notification.id==9 && source>=1 && source<=2,"Concurrent payload corrupted");
        require((notification.parameter&0xffff)==nextSequence[source-1]++,"Per-source FIFO/uniqueness broken");
    }
    require(nextSequence==std::array<uint32_t,2>{64,64},"A producer's publications disappeared");
    expectQueue(reference,received); // Both listeners must observe one total order.
    waitState(event.value,false);
    for(auto weak:closed) require(weak.expired(),"Concurrent close leaked a strong broker registration");
}

constexpr uint32_t ram=0x10000,idOutput=0x10040,paramOutput=0x10044,timeoutAddress=0x10080;
uint32_t createListener(Runtime& rt,uint64_t mask=1,uint32_t version=2) {
    PPCContext ctx{};ctx.r3.u64=mask;ctx.r4.u64=version;ctx.lr=0x82860f14;
    __imp__XamNotifyCreateListener(ctx,rt.base);
    require(ctx.r3.u64==ctx.r3.u32 && ctx.r3.u32!=0,"Create returned an invalid guest handle width/value");
    return ctx.r3.u32;
}
bool poll(Runtime& rt,uint32_t handle,uint32_t match=0,uint32_t id=idOutput,uint32_t parameter=paramOutput) {
    PPCContext ctx{};
    ctx.r3.u64=handle;ctx.r4.u64=match;ctx.r5.u64=id;ctx.r6.u64=parameter;
    ctx.r1.u64=0x10f00;ctx.r13.u64=0x10400;ctx.r14.u64=0x123456789abcdef0;ctx.lr=0x82861818;
    __imp__XNotifyGetNext(ctx,rt.base);
    require(ctx.r3.u64<=1,"Poll did not return a zero-extended Boolean");
    require(ctx.r1.u64==0x10f00 && ctx.r13.u64==0x10400 && ctx.r14.u64==0x123456789abcdef0 && ctx.lr==0x82861818,
            "Notification import changed caller stack/TLS/nonvolatile register/LR");
    return ctx.r3.u64!=0;
}
uint32_t close(Runtime& rt,uint32_t handle) {
    PPCContext ctx{};ctx.r3.u64=handle;__imp__NtClose(ctx,rt.base);return ctx.r3.u32;
}
uint32_t guestWait(Runtime& rt,uint32_t handle) {
    PPCContext ctx{};ctx.r3.u64=handle;ctx.r4.u64=1;ctx.r5.u64=0;ctx.r6.u64=timeoutAddress;
    __imp__NtWaitForSingleObjectEx(ctx,rt.base);return ctx.r3.u32;
}

void audioSubscriptionContracts() {
    Runtime rt;rt.map(ram,0x1000,true,"audio notification fixture outputs");
    auto* base=rt.base;
    // The unchanged original two-instruction wrapper sets version=2 while
    // retaining the full r3 mask. No image data or audio service is needed.
    PPCContext ctx{};ctx.r3.u64=0x20;ctx.r4.u64=0xdeadbeef;
    ctx.r1.u64=0x10f00;ctx.r13.u64=0x10400;ctx.r14.u64=0x123456789abcdef0;
    ctx.lr=0x8280a17c;
    sub_82432CC0(ctx,base);
    require(ctx.r3.u64==ctx.r3.u32 && ctx.r3.u32!=0 && ctx.r4.u64==2,
            "Original audio subscription wrapper lost mask/version ABI");
    require(ctx.r1.u64==0x10f00 && ctx.r13.u64==0x10400 &&
            ctx.r14.u64==0x123456789abcdef0 && ctx.lr==0x8280a17c,
            "Audio listener creation changed caller ABI");
    const uint32_t audio=ctx.r3.u32,system=createListener(rt);
    auto listener=rt.getHandle(audio)->notification;
    Handle event(listener->duplicateWaitHandle());
    PPC_STORE_U64(timeoutAddress,0);
    require(listener->pendingCount()==0 && guestWait(rt,audio)==0x102,
            "Audio subscription fabricated initial activity");
    require(!poll(rt,audio,0x0a000003) && PPC_LOAD_U32(idOutput)==0 &&
            PPC_LOAD_U32(paramOutput)==0,"Empty audio filter did not report no event");
    waitState(event.value,false);

    auto source=rt.notificationSource();
    source->publish({9,1}); // Synthetic system event must not reach audio.
    require(listener->pendingCount()==0 && !poll(rt,audio,0x0a000003),
            "System notification crossed audio category boundary");
    require(poll(rt,system) && PPC_LOAD_U32(idOutput)==9,"System policy regressed");
    source->publish({0x08000003,1}); // Neighbour category 4.
    source->publish({0x0c000003,1}); // Neighbour category 6.
    source->publish({0x0a030003,1}); // Version 3 exceeds original request.
    require(listener->pendingCount()==0,"Audio category/version filtering failed");
    source->publish({0x0a000001,0x55667788}); // Unrequested same-category ID.
    require(!poll(rt,audio,0x0a000003) && listener->pendingCount()==1,
            "Exact original filter consumed an unrelated audio ID");
    waitState(event.value,true);

    // Original payload is a u32 tested for zero, not seconds, sample frames,
    // a volume, pointer, or truncated byte. These are transport fixtures only;
    // no production playback-controller transitions are fabricated.
    for(uint32_t parameter:std::array<uint32_t,6>{0,1,2,0x100,0x80000000,0xffffffff}) {
        source->publish({0x0a000003,parameter});
        require(guestWait(rt,audio)==0,"Audio publication did not signal native wait");
        ctx={};ctx.r3.u64=audio;ctx.r4.u64=0x0a000003;
        ctx.r1.u64=0x10f00;ctx.r5.u64=ctx.r1.u32+0x60;ctx.r6.u64=ctx.r1.u32+0x5c;
        ctx.r13.u64=0x10400;ctx.r14.u64=0x123456789abcdef0;ctx.lr=0x8280a784;
        __imp__XNotifyGetNext(ctx,base);
        require(ctx.r3.u64==1 && PPC_LOAD_U32(0x10f60)==0x0a000003 &&
                PPC_LOAD_U32(0x10f5c)==parameter,"Original audio poll ABI/payload changed");
        require(ctx.r1.u64==0x10f00 && ctx.r13.u64==0x10400 &&
                ctx.r14.u64==0x123456789abcdef0 && ctx.lr==0x8280a784,
                "Audio poll changed original caller ABI");
        require(listener->pendingCount()==1 && !poll(rt,system),
                "Audio event leaked categories or consumed the unmatched ID");
    }
    require(poll(rt,audio,0x0a000001) && PPC_LOAD_U32(paramOutput)==0x55667788,
            "Exact-filter miss did not preserve the unmatched payload");
    waitState(event.value,false);
    source->publish({0x0a020003,0x89abcdef}); // Version 2 is admitted by mask,
    require(!poll(rt,audio,0x0a000003) && listener->pendingCount()==1,
            "Exact audio filter confused different ID versions");
    require(poll(rt,audio,0x0a020003) && PPC_LOAD_U32(paramOutput)==0x89abcdef,
            "Audio listener did not retain the requested maximum version");

    source->publish({0x0a000003,1});
    rejects<Simpsons::Failure>([&]{poll(rt,audio,0x0a000003,idOutput,0x30000);},
                              "Audio poll accepted an unmapped payload output");
    require(listener->pendingCount()==1,"Rejected audio output consumed event");
    waitState(event.value,true);
    require(poll(rt,audio,0x0a000003) && PPC_LOAD_U32(paramOutput)==1,
            "Rejected audio output damaged queued payload");
    // This extension does not grant combinations or truncate high mask bits.
    for(auto [mask,version]:std::array<std::pair<uint64_t,uint32_t>,8>{{
        {0x21,2},{0x10,2},{0x40,2},{0x100000020ull,2},
        {0x20,0},{0x20,1},{0x20,3},{0x20,512}}}) {
        const auto before=rt.handles.size();
        rejects<Simpsons::Failure>([&]{createListener(rt,mask,version);},
            "Audio extension admitted an unqualified subscription","Unsupported native notification subscription");
        require(rt.handles.size()==before,"Rejected audio subscription leaked handle");
    }
    std::weak_ptr<NotificationListener> weak=listener;
    listener.reset();
    require(close(rt,audio)==0 && weak.expired(),"Audio close retained listener ownership");
    rejects<Simpsons::Failure>([&]{poll(rt,audio,0x0a000003);},"Stale audio listener polled successfully");
    source->publish({0x0a000003,0});waitState(event.value,false);
    const auto reopened=createListener(rt,0x20);
    require(!poll(rt,reopened,0x0a000003),"Reopened audio listener replayed prior activity");
    require(close(rt,reopened)==0 && close(rt,system)==0 && rt.handles.empty(),
            "Audio subscription fixture leaked handles");
}

void runtimeImportContracts() {
    Runtime rt;rt.map(ram,0x1000,true,"notification fixture outputs");
    rt.map(0x20000,0x1000,false,"notification fixture readonly outputs");
    auto* base=rt.base;
    require(!rt.notifications,"Runtime fabricated its broker before use");
    std::array<std::shared_ptr<NativeNotifications>,2> sources;
    std::array<std::exception_ptr,2> errors{};
    std::jthread sourceA([&]{try{sources[0]=rt.notificationSource();}catch(...){errors[0]=std::current_exception();}});
    std::jthread sourceB([&]{try{sources[1]=rt.notificationSource();}catch(...){errors[1]=std::current_exception();}});
    sourceA.join();sourceB.join();
    for(auto error:errors) if(error) std::rethrow_exception(error);
    require(sources[0] && sources[0]==sources[1] && sources[0]==rt.notificationSource(),"Lazy broker identity differs across callers");
    auto source=sources[0];
    for(auto [mask,version]:std::array<std::pair<uint64_t,uint32_t>,6>{{{0,2},{2,2},{0x100000001ull,2},{1,0},{1,3},{1,512}}}) {
        const auto before=rt.handles.size();
        rejects<Simpsons::Failure>([&]{createListener(rt,mask,version);},"Unverified import subscription accepted","Unsupported native notification subscription");
        require(rt.handles.size()==before,"Rejected subscription leaked a guest handle");
    }
    const uint32_t handle=createListener(rt);
    auto object=rt.getHandle(handle);
    require(object && object->type==KernelHandle::Type::Notification && object->notification,"Import did not create an owned listener");
    auto listener=object->notification;
    require(listener->pendingCount()==0,"Import fabricated initial notifications");
    PPC_STORE_U64(timeoutAddress,0);
    require(guestWait(rt,handle)==0x102,"Empty guest notification wait did not time out");
    PPC_STORE_U32(idOutput,0xdeadbeef);PPC_STORE_U32(paramOutput,0xcafebabe);
    require(!poll(rt,handle) && PPC_LOAD_U32(idOutput)==0 && PPC_LOAD_U32(paramOutput)==0,"Empty poll did not zero both outputs");

    source->publish({0x00021234,0x89abcdef});
    require(guestWait(rt,handle)==0 && guestWait(rt,handle)==0,"Guest wait consumed a manual event signal");
    for(auto [id,parameter]:std::array<std::pair<uint32_t,uint32_t>,9>{{
        {0,paramOutput},{0x20000,paramOutput},{0x30000,paramOutput},{0x10ffe,paramOutput},
        {0xfffffffc,paramOutput},{idOutput,0x20000},{idOutput,0x30000},{idOutput,0x10ffe},{idOutput,0xfffffffc}}}) {
        PPC_STORE_U32(idOutput,0xdeadbeef);PPC_STORE_U32(paramOutput,0xcafebabe);
        std::array<uint8_t,0x1000> before{};std::memcpy(before.data(),rt.pointer(ram,4096,false),before.size());
        rejects<Simpsons::Failure>([&]{poll(rt,handle,0,id,parameter);},"Invalid output pointer accepted");
        require(std::memcmp(before.data(),rt.pointer(ram,4096,false),before.size())==0,"Rejected output partially wrote guest RAM");
        require(listener->pendingCount()==1,"Output preflight failure consumed a notification");
        waitState(object->native,true);
    }
    require(poll(rt,handle),"Valid poll lost notification after invalid outputs");
    const std::array<uint8_t,8> expected={0x00,0x02,0x12,0x34,0x89,0xab,0xcd,0xef};
    require(std::memcmp(rt.pointer(idOutput,8,false),expected.data(),expected.size())==0,"Guest ID/parameter endian or width mismatch");
    require(guestWait(rt,handle)==0x102,"Drained guest notification handle stayed signaled");

    source->publish({9,11});source->publish({10,22});source->publish({9,33});
    require(!poll(rt,handle,12) && PPC_LOAD_U32(idOutput)==0 && PPC_LOAD_U32(paramOutput)==0,"Filter miss did not return initialized false result");
    require(listener->pendingCount()==3 && guestWait(rt,handle)==0,"Filter miss hid nonempty listener");
    require(poll(rt,handle,10) && PPC_LOAD_U32(idOutput)==10 && PPC_LOAD_U32(paramOutput)==22,"Matched poll selected the wrong notification");
    require(poll(rt,handle,9) && PPC_LOAD_U32(paramOutput)==11,"Matched poll failed first-duplicate ordering");
    PPC_STORE_U32(paramOutput,0xfeed1234);
    require(poll(rt,handle,0,idOutput,0) && PPC_LOAD_U32(idOutput)==9 && PPC_LOAD_U32(paramOutput)==0xfeed1234,"Optional parameter output was accessed or wrong event consumed");
    require(!poll(rt,handle,0,idOutput,0) && PPC_LOAD_U32(idOutput)==0 && PPC_LOAD_U32(paramOutput)==0xfeed1234,"Empty optional-parameter poll changed unused output");
    source->publish({9,0xa1b2c3d4});
    require(poll(rt,handle,0,idOutput,idOutput) && PPC_LOAD_U32(idOutput)==0xa1b2c3d4,"Aliased outputs did not preserve ID-then-parameter write order");
    require(!poll(rt,handle,0,idOutput,idOutput) && PPC_LOAD_U32(idOutput)==0,"Empty aliased output was not zeroed");

    const uint32_t wrongType=rt.addHandle(std::make_shared<KernelHandle>(CreateEventW(nullptr,TRUE,FALSE,nullptr),KernelHandle::Type::Event));
    require(rt.getHandle(wrongType)->native!=nullptr,"Wrong-type fixture event creation failed");
    source->publish({9,77});
    for(auto bad:std::array<uint32_t,3>{0,0xfffffff0,wrongType}) {
        PPC_STORE_U32(idOutput,0xdeadbeef);PPC_STORE_U32(paramOutput,0xcafebabe);
        rejects<Simpsons::Failure>([&]{poll(rt,bad);},"Invalid/wrong-type notification handle accepted","owned native listener");
        require(PPC_LOAD_U32(idOutput)==0xdeadbeef && PPC_LOAD_U32(paramOutput)==0xcafebabe && listener->pendingCount()==1,"Invalid handle touched outputs or another listener");
    }
    require(close(rt,wrongType)==0,"Wrong-type fixture cleanup failed");
    expectRead(listener,{9,77});

    // Exercise the real wait import while a host producer signals its duplicate.
    PPC_STORE_U64(timeoutAddress,uint64_t(-20000000ll));
    Handle entered(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    uint32_t status=0xffffffff;std::exception_ptr waitError;
    std::jthread waiter([&]{try{signal(entered.value);status=guestWait(rt,handle);}catch(...){waitError=std::current_exception();}});
    require(WaitForSingleObject(entered.value,2000)==WAIT_OBJECT_0,"Guest wait worker did not start");
    source->publish({10,88});waiter.join();if(waitError) std::rethrow_exception(waitError);
    require(status==0 && poll(rt,handle) && PPC_LOAD_U32(paramOutput)==88,"Real guest wait/publication/poll integration failed");

    // Keep exactly the lease acquired by getHandle in the existing wait/poll
    // imports. Deterministic close after acquisition must not destroy it. This
    // does not assume we can observe a particular instruction inside an import.
    Handle duplicate(listener->duplicateWaitHandle());
    std::weak_ptr<NotificationListener> weak=listener;
    object.reset();listener.reset();
    Handle acquired(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    std::optional<Notification> afterClose;std::exception_ptr leaseError;
    std::jthread inflight([&] {
        try {
            auto lease=rt.getHandle(handle);require(lease && lease->notification,"In-flight fixture failed to acquire handle lease");
            signal(acquired.value);
            require(WaitForSingleObject(lease->native,2000)==WAIT_OBJECT_0,"Closed in-flight handle did not remain waitable");
            afterClose=lease->notification->read();
        } catch(...) {leaseError=std::current_exception();}
    });
    require(WaitForSingleObject(acquired.value,2000)==WAIT_OBJECT_0,"In-flight lease was not acquired");
    require(close(rt,handle)==0 && !rt.getHandle(handle) && !weak.expired(),"Close destroyed an in-flight listener or retained registry entry");
    PPC_STORE_U32(idOutput,0xdeadbeef);PPC_STORE_U32(paramOutput,0xcafebabe);
    rejects<Simpsons::Failure>([&]{poll(rt,handle);},"Stale notification handle polled successfully","owned native listener");
    require(PPC_LOAD_U32(idOutput)==0xdeadbeef && PPC_LOAD_U32(paramOutput)==0xcafebabe,"Stale handle changed guest outputs");
    require(guestWait(rt,handle)==0xc0000008,"Stale notification handle waited successfully");
    require(close(rt,handle)==0xc0000008,"Stale notification close succeeded");
    source->publish({9,0x10203040});inflight.join();if(leaseError) std::rethrow_exception(leaseError);
    require(afterClose==Notification{9,0x10203040} && weak.expired(),"In-flight close lost payload or leaked listener ownership");
    waitState(duplicate.value,false);
    source->publish({9,123});waitState(duplicate.value,false); // Expired registration is not an event producer.

    const uint32_t stoppedHandle=createListener(rt);
    auto stoppedListener=rt.getHandle(stoppedHandle)->notification;
    source->publish({9,55});
    PPC_STORE_U32(idOutput,0xdeadbeef);PPC_STORE_U32(paramOutput,0xcafebabe);
    rt.requestStop("notification fixture cancellation");
    rejects<Simpsons::Failure>([&]{poll(rt,stoppedHandle);},"Cancelled poll succeeded","notification fixture cancellation");
    rejects<Simpsons::Failure>([&]{rt.notificationSource();},"Cancelled runtime created/accessed broker","notification fixture cancellation");
    require(stoppedListener->pendingCount()==1,"Cancellation already set at entry consumed notification");
    // Checked PPC loads also cancel; inspect already mapped bytes directly.
    const std::array<uint8_t,8> sentinels={0xde,0xad,0xbe,0xef,0xca,0xfe,0xba,0xbe};
    require(std::memcmp(rt.pointer(idOutput,8,false),sentinels.data(),sentinels.size())==0,"Cancelled poll changed guest outputs");
    require(close(rt,stoppedHandle)==0 && rt.handles.empty(),"Notification fixture leaked guest handles");
}
}

int main() {
    try {
        eventAndFilterContracts();identifierContracts();independentLifetimeContracts();
        concurrentBrokerContracts();runtimeImportContracts();audioSubscriptionContracts();
        std::puts("NativeSystemNotifications PASS: real events, FIFO/filtering, ownership/concurrency, checked PPC imports, original audio wrapper/category-5 contracts; synthetic fixtures only");
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"NativeSystemNotifications FAIL: %s\n",error.what());return 1;
    }
}
