#include "runtime/itxd_rgba_preparation.h"
#include "renderer/itxd_blocks.h"
#include "renderer/native_backend.h"
#ifdef small
#undef small
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

using Simpsons::ITXDRGBAPreparation;
using namespace std::chrono_literals;
namespace {
size_t checks{};
void need(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class Predicate>void eventually(Predicate predicate,const char* why) {
    const auto deadline=std::chrono::steady_clock::now()+3s;
    while(!predicate()) {
        if(std::chrono::steady_clock::now()>=deadline)need(false,why);
        std::this_thread::sleep_for(1ms);
    }
    need(true,why);
}
struct Fixture {
    uint32_t width=512,height=512;
    std::array<uint32_t,6> descriptor{};
    std::vector<uint8_t> input;
    explicit Fixture(uint32_t extent=512,uint32_t maxLevel=0):width(extent),height(extent) {
        descriptor={0x80000002u|((width/32)<<22),0x86,((height-1)<<13)|(width-1),0xC14,
                    maxLevel<<6,maxLevel?(width*height*4|0xA00u):0x200u};
        size_t bytes{};
        for(uint32_t level=0;level<=maxLevel;++level)
            bytes+=size_t(std::max(32u,width>>level))*std::max(32u,height>>level)*4;
        input.resize(bytes);
        for(size_t i=0;i<input.size();++i)input[i]=uint8_t((i*37)^(i>>7)^(i>>13));
    }
    auto decode() const {return Simpsons::Graphics::decodeITXDCandyRGBA8(descriptor,width,height,input);}
    size_t charge() const {size_t output{};for(const auto& level:decode())output+=level.size();return input.size()+output+4096;}
};
struct Gate {
    std::mutex mutex;
    std::condition_variable wake;
    bool open{};
    size_t entered{},active{},maxActive{};
    void wait() {
        std::unique_lock lock(mutex);++entered;++active;maxActive=std::max(maxActive,active);wake.notify_all();
        wake.wait(lock,[&]{return open;});--active;
    }
    void release(){std::lock_guard lock(mutex);open=true;wake.notify_all();}
    bool started(){std::lock_guard lock(mutex);return entered!=0;}
};
struct ReleaseGate {std::shared_ptr<Gate> gate;~ReleaseGate(){gate->release();}};
auto waitReady(ITXDRGBAPreparation& preparation,const ITXDRGBAPreparation::Handle& handle,
               ITXDRGBAPreparation::Key key) {
    std::optional<ITXDRGBAPreparation::Ready> result;
    eventually([&]{auto ready=preparation.takeReady(handle,key);if(ready)result.emplace(std::move(*ready));return result.has_value();},
               "Prepared result did not become ready");
    return result;
}

struct BC2Fixture {
    uint32_t width,height;
    std::array<uint32_t,6> descriptor{};
    std::vector<uint8_t> input;
    BC2Fixture(uint32_t w=1024,uint32_t h=1024,uint32_t storedPitch=0):width(w),height(h) {
        const uint32_t pitch=storedPitch?storedPitch:((w+127)&~127u);
        descriptor={0x80000002u|((pitch/32)<<22),0x53,((h-1)<<13)|(w-1),0xD10,0,0x200};
        const size_t columns=(((pitch+3)/4)+31)&~size_t(31);
        const size_t rows=(((h+3)/4)+31)&~size_t(31);
        input.resize((columns*rows*16+4095)&~size_t(4095));
        for(size_t i=0;i<input.size();++i)input[i]=uint8_t((i*43)^(i>>5)^(i>>11));
    }
    auto decode() const {return Simpsons::Graphics::decodeITXDBCMipChain(descriptor,0x53,width,height,input);}
    size_t charge() const {size_t output{};for(const auto& level:decode())output+=level.size();return input.size()+output+4096;}
};
auto waitBC2Ready(ITXDRGBAPreparation& preparation,const ITXDRGBAPreparation::Handle& handle,
                 ITXDRGBAPreparation::Key key,const std::array<uint32_t,6>& descriptor,
                 uint32_t width,uint32_t height) {
    std::optional<ITXDRGBAPreparation::Ready> result;
    eventually([&]{auto ready=preparation.takeReadyBC2Base(handle,key,descriptor,width,height);
        if(ready){result.emplace(std::move(*ready));}
        return result.has_value();},"Prepared BC2 base did not become ready");
    return result;
}
void bc2ExactOwnedResult() {
    for(const auto shape:std::array<std::array<uint32_t,3>,3>{{{1024,1024,1024},{2048,512,2048},{1024,1024,2048}}}) {
        BC2Fixture fixture(shape[0],shape[1],shape[2]);
        const auto descriptor=fixture.descriptor;
        const auto expected=fixture.decode();
        auto gate=std::make_shared<Gate>();
        ITXDRGBAPreparation preparation({},[gate](auto){gate->wait();});ReleaseGate release{gate};
        const ITXDRGBAPreparation::Key key{101,1};
        auto handle=preparation.submitBC2Base(key,descriptor,fixture.width,fixture.height,fixture.input);
        need(!handle.expired(),"Qualified large BC2 base snapshot was rejected");
        eventually([&]{return gate->started();},"BC2 worker did not start");
        const auto charge=preparation.stats().reservedBytes;
        need(charge==fixture.charge(),"BC2 source/output reservation differs from admitted decoder bytes");
        std::fill(fixture.input.begin(),fixture.input.end(),0);fixture.descriptor.fill(0);
        auto demand=std::async(std::launch::async,[&]{
            return preparation.takeReadyBC2Base(handle,key,descriptor,fixture.width,fixture.height).has_value();});
        need(demand.wait_for(1s)==std::future_status::ready&&!demand.get(),"BC2 demand waited or consumed unfinished work");
        gate->release();eventually([&]{return preparation.stats().ready==1;},"BC2 result was not ready");
        need(!preparation.takeReady(handle,key),"RGBA API consumed BC2 output");
        need(!preparation.takeReadyBC2Base(handle,{101,2},descriptor,fixture.width,fixture.height),"Old BC2 request consumed a new generation");
        for(size_t field=0;field<descriptor.size();++field) {
            auto wrong=descriptor;wrong[field]^=1;
            need(!preparation.takeReadyBC2Base(handle,key,wrong,fixture.width,fixture.height),"Wrong BC2 descriptor consumed ready output");
        }
        need(!preparation.takeReadyBC2Base(handle,key,descriptor,fixture.width/2,fixture.height),"Wrong BC2 width consumed ready output");
        need(!preparation.takeReadyBC2Base(handle,key,descriptor,fixture.width,fixture.height/2),"Wrong BC2 height consumed ready output");
        need(preparation.stats().ready==1&&preparation.stats().taken==0,"Mismatched BC2 request retired real ready output");
        auto ready=waitBC2Ready(preparation,handle,key,descriptor,fixture.width,fixture.height);
        need(ready->levels==expected,"BC2 bytes differ from existing admitted pair-swap decoder/owned snapshot");
        need(preparation.stats().reservedBytes==charge&&preparation.stats().outstandingJobs==1,"BC2 ready lost its pinned reservation");
        need(!preparation.takeReadyBC2Base(handle,key,descriptor,fixture.width,fixture.height),"BC2 output was consumed twice");
        preparation.stop();
        need(ready->levels==expected&&preparation.stats().reservedBytes==charge,"Stop invalidated pinned BC2 bytes or budget");
        ready.reset();need(preparation.stats().reservedBytes==0&&preparation.stats().outstandingJobs==0,"BC2 pinned reservation did not retire");
    }
}
void bc2ProfilesAndFallback() {
    BC2Fixture fixture;
    std::atomic<size_t> calls{};
    ITXDRGBAPreparation preparation({},[&](auto){++calls;});
    need(!preparation.qualifiesProfile(fixture.descriptor,fixture.width,fixture.height,fixture.input.size()),
         "Existing RGBA-only profile query broadened to BC2");
    need(preparation.submit({1,1},fixture.descriptor,fixture.width,fixture.height,fixture.input).expired(),"Existing RGBA submission admitted BC2");
    for(size_t field=0;field<fixture.descriptor.size();++field) {
        auto wrong=fixture.descriptor;wrong[field]^=1;
        need(preparation.submitBC2Base({uint32_t(field+2),1},wrong,fixture.width,fixture.height,fixture.input).expired(),
             "Malformed BC2 descriptor reached worker");
    }
    for(const uint32_t format:{0x52u,0x54u}) {
        auto wrong=fixture.descriptor;wrong[1]=format;
        need(preparation.submitBC2Base({10+format,1},wrong,fixture.width,fixture.height,fixture.input).expired(),"BC1/BC3 entered BC2-only worker path");
    }
    auto wrong=fixture.descriptor;wrong[1]|=0x1000;
    need(preparation.submitBC2Base({20,1},wrong,fixture.width,fixture.height,fixture.input).expired(),"Nonzero BC2 base address reached worker");
    wrong=fixture.descriptor;wrong[4]=1<<6;wrong[5]=uint32_t(fixture.input.size())|0xA00;
    need(preparation.submitBC2Base({21,1},wrong,fixture.width,fixture.height,fixture.input).expired(),"BC2 mip chain reached base-only worker");
    Fixture rgba;
    need(preparation.submitBC2Base({22,1},rgba.descriptor,rgba.width,rgba.height,rgba.input).expired(),"RGBA entered BC2-only submission");
    for(const auto shape:std::array<std::array<uint32_t,2>,6>{{{0,1024},{1024,0},{1023,1024},{1024,1023},{4096,1024},{1024,4096}}})
        need(preparation.submitBC2Base({23,1},fixture.descriptor,shape[0],shape[1],fixture.input).expired(),"Invalid BC2 dimensions reached worker");
    BC2Fixture small(512,512);
    need(preparation.submitBC2Base({24,1},small.descriptor,small.width,small.height,small.input).expired(),"Below-1MiB BC2 entered default queue");
    BC2Fixture shortPitch(1024,1024,512);
    need(preparation.submitBC2Base({25,1},shortPitch.descriptor,shortPitch.width,shortPitch.height,shortPitch.input).expired(),"Short BC2 pitch reached worker");
    wrong=fixture.descriptor;wrong[0]=0x80000002u|((1056/32)<<22);
    need(preparation.submitBC2Base({26,1},wrong,fixture.width,fixture.height,fixture.input).expired(),"Non-128-aligned BC2 pitch reached worker");
    need(preparation.submitBC2Base({0,1},fixture.descriptor,fixture.width,fixture.height,fixture.input).expired()&&
         preparation.submitBC2Base({27,0},fixture.descriptor,fixture.width,fixture.height,fixture.input).expired(),"Null BC2 ownership key reached worker");
    need(preparation.submitBC2Base({28,1},fixture.descriptor,fixture.width,fixture.height,std::span(fixture.input).first(fixture.input.size()-1)).expired(),
         "Truncated BC2 input reached worker");
    auto longer=fixture.input;longer.push_back(0);
    need(preparation.submitBC2Base({29,1},fixture.descriptor,fixture.width,fixture.height,longer).expired(),"BC2 input padding reached worker");
    need(!calls.load()&&!preparation.stats().outstandingJobs,"Rejected BC2 profiles started work or retained reservation");
    ITXDRGBAPreparation above({64u*1024u*1024u,8,1024u*1024u+1});
    need(above.submitBC2Base({30,1},fixture.descriptor,fixture.width,fixture.height,fixture.input).expired(),"BC2 ignored custom minimum output threshold");
    wrong=fixture.descriptor;wrong[3]^=1;
    bool rejected=false;
    try {(void)Simpsons::Graphics::decodeITXDBCMipChain(wrong,0x53,fixture.width,fixture.height,fixture.input);}
    catch(const Simpsons::Graphics::Error& error) {rejected=std::string(error.what())=="ITXD BC chain descriptor has unqualified format, address, mip or sampler controls";}
    need(rejected,"Rejected BC2 preparation changed original synchronous descriptor error");

    const auto expected=fixture.decode();
    ITXDRGBAPreparation failed({},[](auto){throw std::runtime_error("deterministic BC2 CPU failure");});
    auto failure=failed.submitBC2Base({31,1},fixture.descriptor,fixture.width,fixture.height,fixture.input);
    need(!failure.expired(),"BC2 failed-worker control was not admitted");
    eventually([&]{const auto stats=failed.stats();return stats.failed==1&&stats.outstandingJobs==0;},"BC2 background error did not retire buffers");
    need(!failed.takeReadyBC2Base(failure,{31,1},fixture.descriptor,fixture.width,fixture.height)&&fixture.decode()==expected,
         "Failed BC2 worker published output or changed synchronous fallback bytes");
    auto gate=std::make_shared<Gate>();
    ITXDRGBAPreparation incomplete({},[gate](auto){gate->wait();});ReleaseGate release{gate};
    auto pending=incomplete.submitBC2Base({32,1},fixture.descriptor,fixture.width,fixture.height,fixture.input);
    need(!pending.expired(),"BC2 incomplete control was not admitted");
    eventually([&]{return gate->started();},"BC2 incomplete worker did not start");
    need(!incomplete.takeReadyBC2Base(pending,{32,1},fixture.descriptor,fixture.width,fixture.height),"Incomplete BC2 work was consumed");
    incomplete.cancel(pending);
    need(fixture.decode()==expected,"Nonblocking BC2 fallback differs from admitted decoder");
    gate->release();incomplete.stop();
    need(!incomplete.stats().ready&&!incomplete.stats().reservedBytes,"Cancelled BC2 fallback later published or leaked budget");
}
void bc2SharedBudgetsAndKinds() {
    Fixture rgba;BC2Fixture bc;
    const size_t rgbaCharge=rgba.charge(),bcCharge=bc.charge();
    auto gate=std::make_shared<Gate>();
    ITXDRGBAPreparation preparation({rgbaCharge+bcCharge,2,1024u*1024u},[gate](auto){gate->wait();});ReleaseGate release{gate};
    auto first=preparation.submit({41,1},rgba.descriptor,rgba.width,rgba.height,rgba.input);
    eventually([&]{return gate->started();},"Mixed worker did not start RGBA control");
    auto second=preparation.submitBC2Base({42,1},bc.descriptor,bc.width,bc.height,bc.input);
    need(!first.expired()&&!second.expired(),"RGBA and BC2 did not share exact two-job/byte budget");
    need(preparation.stats().reservedBytes==rgbaCharge+bcCharge&&preparation.stats().outstandingJobs==2,
         "Mixed CPU reservations do not charge both original owned snapshots/results");
    need(preparation.submitBC2Base({42,1},bc.descriptor,bc.width,bc.height,bc.input).expired()&&
         preparation.submit({41,1},rgba.descriptor,rgba.width,rgba.height,rgba.input).expired(),"Duplicate mixed preparation keys changed the shared pool");
    preparation.cancel(second);
    need(preparation.stats().reservedBytes==rgbaCharge&&preparation.stats().outstandingJobs==1,"Queued BC2 cancellation did not release shared charge");
    auto replacement=preparation.submitBC2Base({42,2},bc.descriptor,bc.width,bc.height,bc.input);
    need(!replacement.expired(),"Retired BC2 budget could not admit new generation");
    gate->release();eventually([&]{return preparation.stats().ready==2;},"Mixed ready results were not published");
    need(!preparation.takeReadyBC2Base(first,{41,1},bc.descriptor,bc.width,bc.height)&&
         !preparation.takeReady(replacement,{42,2}),"Cross-kind ready consumption was admitted");
    auto rgbaReady=waitReady(preparation,first,{41,1});
    auto bcReady=waitBC2Ready(preparation,replacement,{42,2},bc.descriptor,bc.width,bc.height);
    need(rgbaReady->levels==rgba.decode()&&bcReady->levels==bc.decode(),"Mixed preparation changed either admitted decoder's bytes");
    preparation.stop();
    need(preparation.stats().reservedBytes==rgbaCharge+bcCharge,"Stop freed pinned mixed ready leases");
    rgbaReady.reset();bcReady.reset();need(!preparation.stats().reservedBytes&&!preparation.stats().outstandingJobs,"Mixed pinned leases did not retire");

    void* inaccessible=VirtualAlloc(nullptr,bc.input.size(),MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
    need(inaccessible!=nullptr,"BC2 no-access budget control could not allocate");
    struct Free {void* value;~Free(){VirtualFree(value,0,MEM_RELEASE);}} free{inaccessible};
    ITXDRGBAPreparation tooSmall({bcCharge-1,8,1024u*1024u});
    need(tooSmall.submitBC2Base({45,1},bc.descriptor,bc.width,bc.height,
        {static_cast<const uint8_t*>(inaccessible),bc.input.size()}).expired(),"BC2 budget rejection copied inaccessible input");
    need(!tooSmall.stats().reservedBytes&&!tooSmall.stats().outstandingJobs,"BC2 budget rejection retained a lease");
}
void bc2RunningCancellationAndGeneration() {
    BC2Fixture fixture;const auto expected=fixture.decode();const auto charge=fixture.charge();
    auto gate=std::make_shared<Gate>();
    ITXDRGBAPreparation preparation({charge,1,1024u*1024u},[gate](auto){gate->wait();});ReleaseGate release{gate};
    auto old=preparation.submitBC2Base({51,1},fixture.descriptor,fixture.width,fixture.height,fixture.input);
    need(!old.expired(),"Running BC2 cancel control was not admitted");
    eventually([&]{return gate->started();},"Running BC2 cancel control did not start");
    preparation.cancel(old);
    need(preparation.stats().reservedBytes==charge&&preparation.stats().outstandingJobs==1,"Running BC2 cancellation freed live owned input");
    need(preparation.submitBC2Base({51,2},fixture.descriptor,fixture.width,fixture.height,fixture.input).expired(),"Cancelled running BC2 escaped shared bounds");
    need(!preparation.takeReadyBC2Base(old,{51,1},fixture.descriptor,fixture.width,fixture.height),"Cancelled BC2 generation was consumed");
    gate->release();eventually([&]{return preparation.stats().outstandingJobs==0;},"Cancelled running BC2 buffers did not retire");
    auto fresh=preparation.submitBC2Base({51,2},fixture.descriptor,fixture.width,fixture.height,fixture.input);
    need(!fresh.expired(),"Fresh BC2 generation did not admit after retirement");
    eventually([&]{return preparation.stats().ready==1;},"Fresh BC2 generation was not ready");
    need(!preparation.takeReadyBC2Base(fresh,{51,1},fixture.descriptor,fixture.width,fixture.height)&&
         !preparation.takeReadyBC2Base(old,{51,2},fixture.descriptor,fixture.width,fixture.height),"Stale BC2 key or ticket consumed replacement");
    ITXDRGBAPreparation foreign;
    need(!foreign.takeReadyBC2Base(fresh,{51,2},fixture.descriptor,fixture.width,fixture.height),"Foreign CPU worker consumed BC2 owner");
    foreign.cancel(fresh);need(preparation.stats().ready==1,"Foreign worker cancelled live BC2 owner");
    auto ready=waitBC2Ready(preparation,fresh,{51,2},fixture.descriptor,fixture.width,fixture.height);
    need(ready->levels==expected,"Fresh BC2 generation retained stale bytes");
}

void bc2QueuedReplacementAndRGBAPriority() {
    Fixture rgba;BC2Fixture bc;
    const auto rgbaGold=rgba.decode(),bcGold=bc.decode();const size_t charge=bc.charge();
    need(rgba.charge()==charge,"Replacement control requires equal actual decoder charges");
    auto gate=std::make_shared<Gate>();
    ITXDRGBAPreparation preparation({},[gate](auto){gate->wait();});ReleaseGate release{gate};
    auto running=preparation.submit({900,100},rgba.descriptor,rgba.width,rgba.height,rgba.input);
    need(!running.expired(),"Replacement running RGBA control was not admitted");
    eventually([&]{return gate->started();},"Replacement running RGBA did not enter gate");
    std::array<ITXDRGBAPreparation::Handle,7> old;
    for(size_t i=0;i<old.size();++i) {
        old[i]=preparation.submitBC2Base({uint32_t(800-i),uint64_t(70-i)},bc.descriptor,bc.width,bc.height,bc.input);
        need(!old[i].expired(),"Full pool lost an initial queued BC2 ticket");
    }
    need(preparation.stats().outstandingJobs==8&&preparation.stats().reservedBytes==charge*8,
         "Replacement control did not fill the exact shared pool");
    auto fresh=preparation.submitBC2Base({999,900},bc.descriptor,bc.width,bc.height,bc.input);
    need(!fresh.expired()&&old[0].expired()&&!old[1].expired(),"Late BC2 did not replace only the oldest queued BC2");
    auto priority=preparation.submit({998,1},rgba.descriptor,rgba.width,rgba.height,rgba.input);
    need(!priority.expired()&&old[1].expired()&&!old[2].expired()&&!running.expired()&&!fresh.expired(),
         "RGBA did not recover BC2 capacity or evicted protected/newer work");
    const auto stats=preparation.stats();
    need(stats.outstandingJobs==8&&stats.reservedBytes==charge*8&&stats.cancelled==2&&stats.submitted==10,
         "Queued retirements changed bounded reservation accounting");
    preparation.cancel(old[0]);preparation.cancel(old[1]);
    need(preparation.stats().cancelled==2&&!preparation.takeReadyBC2Base(old[0],{800,70},bc.descriptor,bc.width,bc.height),
         "Retired BC2 handle was consumed or cancelled twice");
    need(bc.decode()==bcGold,"Retired BC2 demand fallback changed synchronous bytes");
    gate->release();
    auto ready=waitBC2Ready(preparation,fresh,{999,900},bc.descriptor,bc.width,bc.height);
    auto first=waitReady(preparation,running,{900,100});
    auto rgbaReady=waitReady(preparation,priority,{998,1});
    need(ready->levels==bcGold&&first->levels==rgbaGold&&rgbaReady->levels==rgbaGold,"Replacement changed owned output bytes");
    preparation.stop();
    need(preparation.stats().outstandingJobs==3&&preparation.stats().reservedBytes==charge*3,"Stop freed taken replacement leases");
    ready.reset();first.reset();rgbaReady.reset();
    need(!preparation.stats().outstandingJobs&&!preparation.stats().reservedBytes,"Replacement pinned leases leaked");
}
void bc2ReadyReplacementKeepsPinnedLease() {
    BC2Fixture bc;const auto gold=bc.decode();const size_t charge=bc.charge();
    ITXDRGBAPreparation preparation;
    std::array<ITXDRGBAPreparation::Handle,8> old;
    for(size_t i=0;i<old.size();++i) {
        old[i]=preparation.submitBC2Base({uint32_t(100-i),uint64_t(90-i)},bc.descriptor,bc.width,bc.height,bc.input);
        need(!old[i].expired(),"Ready replacement control lost an initial ticket");
    }
    eventually([&]{return preparation.stats().ready==8;},"Ready replacement control did not finish eight jobs");
    auto pinned=waitBC2Ready(preparation,old[0],{100,90},bc.descriptor,bc.width,bc.height);
    auto fresh=preparation.submitBC2Base({200,1},bc.descriptor,bc.width,bc.height,bc.input);
    need(!fresh.expired()&&old[1].expired()&&!old[2].expired(),"Ready replacement did not skip taken lease and select oldest Ready");
    need(pinned->levels==gold&&preparation.stats().outstandingJobs==8&&preparation.stats().reservedBytes==charge*8,
         "Ready replacement invalidated or uncharged taken output");
    need(!preparation.takeReadyBC2Base(old[1],{99,89},bc.descriptor,bc.width,bc.height)&&bc.decode()==gold,
         "Retired Ready remained consumable or changed synchronous fallback");
    auto ready=waitBC2Ready(preparation,fresh,{200,1},bc.descriptor,bc.width,bc.height);
    need(ready->levels==gold,"Late Ready replacement produced wrong bytes");
    preparation.stop();
    need(preparation.stats().reservedBytes==charge*2&&pinned->levels==gold&&ready->levels==gold,"Stop invalidated retained outputs");
    pinned.reset();ready.reset();
    need(!preparation.stats().reservedBytes&&!preparation.stats().outstandingJobs,"Ready replacement lease accounting leaked");
}
void bc2AdmissionProtectedAndNoRead() {
    Fixture rgba;BC2Fixture bc;const size_t charge=bc.charge();
    void* inaccessible=VirtualAlloc(nullptr,bc.input.size(),MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
    need(inaccessible!=nullptr,"Protected admission input allocation failed");
    struct Free {void* value;~Free(){VirtualFree(value,0,MEM_RELEASE);}} free{inaccessible};
    const std::span<const uint8_t> unreadable(static_cast<const uint8_t*>(inaccessible),bc.input.size());
    auto gate=std::make_shared<Gate>();
    ITXDRGBAPreparation running({charge,1,1024u*1024u},[gate](auto){gate->wait();});ReleaseGate release{gate};
    auto active=running.submitBC2Base({300,1},bc.descriptor,bc.width,bc.height,bc.input);
    need(!active.expired(),"Protected running BC2 was not admitted");
    eventually([&]{return gate->started();},"Protected running BC2 did not start");
    need(running.submitBC2Base({301,1},bc.descriptor,bc.width,bc.height,unreadable).expired()&&
         running.submit({302,1},rgba.descriptor,rgba.width,rgba.height,unreadable).expired(),"Running BC2 was evicted or rejected input read");
    running.cancel(active);
    need(running.submitBC2Base({300,2},bc.descriptor,bc.width,bc.height,unreadable).expired()&&
         running.stats().reservedBytes==charge&&running.stats().running==1,"Cancelled running reservation was reclaimed early");
    gate->release();running.stop();need(!running.stats().reservedBytes,"Running protected cancellation did not retire");
    ITXDRGBAPreparation pinnedOwner({charge,1,1024u*1024u});
    auto taken=pinnedOwner.submitBC2Base({310,1},bc.descriptor,bc.width,bc.height,bc.input);
    auto pinned=waitBC2Ready(pinnedOwner,taken,{310,1},bc.descriptor,bc.width,bc.height);
    need(pinnedOwner.submitBC2Base({311,1},bc.descriptor,bc.width,bc.height,unreadable).expired()&&
         pinnedOwner.submit({312,1},rgba.descriptor,rgba.width,rgba.height,unreadable).expired()&&
         pinnedOwner.stats().reservedBytes==charge,"Taken lease was reclaimed or rejected input read");
    pinnedOwner.stop();need(pinned->levels==bc.decode(),"Protected taken bytes depended on worker");
    pinned.reset();need(!pinnedOwner.stats().reservedBytes,"Protected taken lease did not retire");
    ITXDRGBAPreparation foreignKind({charge,1,1024u*1024u});
    auto rgbaHandle=foreignKind.submit({320,1},rgba.descriptor,rgba.width,rgba.height,rgba.input);
    eventually([&]{return foreignKind.stats().ready==1;},"Protected RGBA Ready did not finish");
    need(foreignKind.submitBC2Base({321,1},bc.descriptor,bc.width,bc.height,unreadable).expired()&&
         foreignKind.submit({322,1},rgba.descriptor,rgba.width,rgba.height,unreadable).expired()&&
         !rgbaHandle.expired()&&!foreignKind.stats().cancelled,"Ready RGBA was evicted by pressure");
    ITXDRGBAPreparation other;other.cancel(rgbaHandle);
    need(foreignKind.stats().ready==1&&!foreignKind.stats().cancelled,"Foreign worker retired protected RGBA");
    auto retained=waitReady(foreignKind,rgbaHandle,{320,1});need(retained->levels==rgba.decode(),"Protected RGBA bytes changed");
}
void bc2AdmissionImpossibleDuplicateAndStop() {
    Fixture rgba,large(1024);BC2Fixture bc;const size_t charge=rgba.charge(),largeCharge=large.charge();
    void* inaccessible=VirtualAlloc(nullptr,large.input.size(),MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
    need(inaccessible!=nullptr,"Impossible admission input allocation failed");
    struct Free {void* value;~Free(){VirtualFree(value,0,MEM_RELEASE);}} free{inaccessible};
    const std::span<const uint8_t> shortUnreadable(static_cast<const uint8_t*>(inaccessible),bc.input.size());
    const std::span<const uint8_t> largeUnreadable(static_cast<const uint8_t*>(inaccessible),large.input.size());
    auto gate=std::make_shared<Gate>();
    ITXDRGBAPreparation preparation({largeCharge+charge-1,8,1024u*1024u},[gate](auto){gate->wait();});ReleaseGate release{gate};
    auto protectedJob=preparation.submit({400,1},rgba.descriptor,rgba.width,rgba.height,rgba.input);
    need(!protectedJob.expired(),"Impossible admission protected owner was not admitted");
    eventually([&]{return gate->started();},"Impossible admission worker did not start");
    auto speculative=preparation.submitBC2Base({401,1},bc.descriptor,bc.width,bc.height,bc.input);
    need(!speculative.expired(),"Impossible admission control lost speculative owner");
    const auto before=preparation.stats();
    need(preparation.submit({402,1},large.descriptor,large.width,large.height,largeUnreadable).expired(),
         "Insufficient reclaimable charge copied rejected input");
    need(preparation.submitBC2Base({401,1},bc.descriptor,bc.width,bc.height,shortUnreadable).expired()&&
         preparation.submit({400,1},rgba.descriptor,rgba.width,rgba.height,shortUnreadable).expired(),"Duplicate active key retired work or read input");
    auto wrong=bc.descriptor;wrong[3]^=1;
    need(preparation.submitBC2Base({403,1},wrong,bc.width,bc.height,shortUnreadable).expired(),"Malformed profile retired work or read input");
    const auto after=preparation.stats();
    need(after.reservedBytes==before.reservedBytes&&after.outstandingJobs==before.outstandingJobs&&
         after.cancelled==before.cancelled&&after.submitted==before.submitted&&!speculative.expired(),
         "Impossible/duplicate/malformed rejection destroyed speculative BC2 needlessly");
    auto limitGate=std::make_shared<Gate>();
    ITXDRGBAPreparation individual({charge*2,8,1024u*1024u},[limitGate](auto){limitGate->wait();});ReleaseGate limitRelease{limitGate};
    auto protectedLimit=individual.submit({410,1},rgba.descriptor,rgba.width,rgba.height,rgba.input);
    eventually([&]{return limitGate->started();},"Individual-charge worker did not start");
    auto retained=individual.submitBC2Base({411,1},bc.descriptor,bc.width,bc.height,bc.input);
    need(!protectedLimit.expired()&&!retained.expired(),"Individual-charge control failed to fill pool");
    need(individual.submit({412,1},large.descriptor,large.width,large.height,largeUnreadable).expired()&&
         !retained.expired()&&!individual.stats().cancelled,"Over-limit individual charge retired BC2 or copied input");
    limitGate->release();individual.stop();
    gate->release();preparation.stop();
    need(preparation.submitBC2Base({405,1},bc.descriptor,bc.width,bc.height,shortUnreadable).expired()&&
         preparation.submit({406,1},rgba.descriptor,rgba.width,rgba.height,shortUnreadable).expired()&&
         !preparation.stats().reservedBytes,"Stopped worker copied input or retained reservation");
    ITXDRGBAPreparation zero({64u*1024u*1024u,0,1024u*1024u});
    need(zero.submitBC2Base({407,1},bc.descriptor,bc.width,bc.height,shortUnreadable).expired(),"Zero-slot admission read input");
}
void bc2AdmissionMultipleVictimsForBytes() {
    Fixture rgba,large(1024);BC2Fixture bc;
    const size_t smallCharge=bc.charge(),largeCharge=large.charge();
    const auto smallGold=rgba.decode(),largeGold=large.decode();
    need(rgba.charge()==smallCharge&&3*smallCharge<largeCharge&&largeCharge<=4*smallCharge,
         "Multi-victim control does not require exactly four real decoder charges");
    auto gate=std::make_shared<Gate>();
    ITXDRGBAPreparation preparation({smallCharge*8,8,1024u*1024u},[gate](auto){gate->wait();});ReleaseGate release{gate};
    auto running=preparation.submit({700,1},rgba.descriptor,rgba.width,rgba.height,rgba.input);
    need(!running.expired(),"Multi-victim RGBA owner was not admitted");
    eventually([&]{return gate->started();},"Multi-victim worker did not enter gate");
    std::array<ITXDRGBAPreparation::Handle,7> old;
    for(size_t i=0;i<old.size();++i) {
        old[i]=preparation.submitBC2Base({uint32_t(600-i),uint64_t(90-i)},bc.descriptor,bc.width,bc.height,bc.input);
        need(!old[i].expired(),"Multi-victim control lost initial BC2 ticket");
    }
    auto fresh=preparation.submit({799,99},large.descriptor,large.width,large.height,large.input);
    need(!fresh.expired()&&!running.expired(),"Large RGBA admission did not reclaim enough speculative bytes");
    for(size_t i=0;i<old.size();++i)
        need(old[i].expired()==(i<4),"Byte-pressure reclamation did not retire exactly the four oldest BC2 tickets");
    const auto stats=preparation.stats();
    need(stats.cancelled==4&&stats.submitted==9&&stats.outstandingJobs==5&&
         stats.reservedBytes==smallCharge*4+largeCharge&&stats.reservedBytes<=smallCharge*8,
         "Multi-victim byte/job reservation accounting differs");
    std::fill(large.input.begin(),large.input.end(),0);
    gate->release();
    auto ready=waitReady(preparation,fresh,{799,99});auto retained=waitReady(preparation,running,{700,1});
    need(ready->levels==largeGold&&retained->levels==smallGold,"Multi-victim admission changed owned decoded output");
    preparation.stop();
    need(preparation.stats().outstandingJobs==2&&preparation.stats().reservedBytes==smallCharge+largeCharge,
         "Multi-victim stop did not preserve exactly taken leases");
    ready.reset();retained.reset();
    need(!preparation.stats().reservedBytes&&!preparation.stats().outstandingJobs,"Multi-victim leases leaked");
}

void bc2AdmissionConcurrentProducers() {
    Fixture rgba;BC2Fixture bc;const size_t charge=bc.charge();
    auto gate=std::make_shared<Gate>();
    ITXDRGBAPreparation preparation({},[gate](auto){gate->wait();});ReleaseGate release{gate};
    auto protectedJob=preparation.submit({500,1},rgba.descriptor,rgba.width,rgba.height,rgba.input);
    need(!protectedJob.expired(),"Concurrent replacement protected owner was not admitted");
    eventually([&]{return gate->started();},"Concurrent replacement worker did not enter gate");
    std::array<ITXDRGBAPreparation::Handle,7> old;
    for(size_t i=0;i<old.size();++i) {
        old[i]=preparation.submitBC2Base({uint32_t(501+i),1},bc.descriptor,bc.width,bc.height,bc.input);
        need(!old[i].expired(),"Concurrent replacement control lost queued ticket");
    }
    need(preparation.stats().outstandingJobs==8,"Concurrent replacement control did not fill pool");
    std::array<ITXDRGBAPreparation::Handle,2> result;
    std::atomic<unsigned> entered{};std::atomic<bool> start{};
    std::jthread a([&]{++entered;while(!start.load())std::this_thread::yield();
        result[0]=preparation.submitBC2Base({510,1},bc.descriptor,bc.width,bc.height,bc.input);});
    std::jthread b([&]{++entered;while(!start.load())std::this_thread::yield();
        result[1]=preparation.submit({511,1},rgba.descriptor,rgba.width,rgba.height,rgba.input);});
    struct StartOnFailure {std::atomic<bool>& value;~StartOnFailure(){value=true;}} startOnFailure{start};
    eventually([&]{return entered.load()==2;},"Concurrent replacement producers did not reach start gate");
    start=true;a.join();b.join();
    const auto stats=preparation.stats();
    need(stats.submitted>=9&&stats.submitted<=10&&stats.outstandingJobs<=8&&
         stats.reservedBytes==stats.outstandingJobs*charge&&!protectedJob.expired(),"Concurrent reclamation exceeded caps or retired RGBA");
    need(stats.outstandingJobs==stats.submitted-stats.cancelled,"Concurrent reservation accounting differs from committed tickets");
    gate->release();
    auto retained=waitReady(preparation,protectedJob,{500,1});need(retained->levels==rgba.decode(),"Concurrent admission changed protected RGBA");
    if(!result[0].expired()) {
        auto ready=waitBC2Ready(preparation,result[0],{510,1},bc.descriptor,bc.width,bc.height);
        need(ready->levels==bc.decode(),"Concurrent admitted BC2 bytes changed");
    }
    if(!result[1].expired()) {
        auto ready=waitReady(preparation,result[1],{511,1});need(ready->levels==rgba.decode(),"Concurrent admitted RGBA bytes changed");
    }
    preparation.stop();retained.reset();
    need(!preparation.stats().reservedBytes&&!preparation.stats().outstandingJobs,"Concurrent replacement leaked reservation");
}

void exactOwnedResult() {
    for(const auto [extent,maxLevel]:std::array<std::pair<uint32_t,uint32_t>,3>{{{512,0},{512,5},{1024,0}}}) {
        Fixture fixture(extent,maxLevel);
        const auto expected=fixture.decode();
        auto gate=std::make_shared<Gate>();
        ITXDRGBAPreparation preparation({},[gate](auto){gate->wait();});
        ReleaseGate release{gate};
        const ITXDRGBAPreparation::Key key{17,maxLevel+1};
        auto handle=preparation.submit(key,fixture.descriptor,fixture.width,fixture.height,fixture.input);
        need(!handle.expired(),"Qualified large RGBA snapshot was rejected");
        eventually([&]{return gate->started();},"Worker did not start");
        const auto charge=preparation.stats().reservedBytes;
        std::fill(fixture.input.begin(),fixture.input.end(),0);
        fixture.descriptor.fill(0);
        auto demand=std::async(std::launch::async,[&]{return preparation.takeReady(handle,key).has_value();});
        need(demand.wait_for(1s)==std::future_status::ready,"Demand waited for unfinished conversion");
        need(!demand.get(),"An unfinished job was published");
        gate->release();
        auto ready=waitReady(preparation,handle,key);
        need(ready->levels==expected,"Prepared pixels differ from the existing decoder/owned snapshot");
        need(preparation.stats().reservedBytes==charge&&preparation.stats().outstandingJobs==1,
             "Taken pixels lost their pinned reservation");
        need(!preparation.takeReady(handle,key),"A prepared result was consumed twice");
        ready.reset();
        need(preparation.stats().reservedBytes==0&&preparation.stats().outstandingJobs==0,
             "Consumed pixels retained their reservation");
    }
}

void boundsAndPinnedResults() {
    Fixture fixture;
    const size_t charge=fixture.charge();
    auto gate=std::make_shared<Gate>();
    ITXDRGBAPreparation preparation({charge*2,8,1024u*1024u},[gate](auto){gate->wait();});
    ReleaseGate release{gate};
    auto first=preparation.submit({1,1},fixture.descriptor,512,512,fixture.input);
    eventually([&]{return gate->started();},"Bounded worker did not start");
    auto second=preparation.submit({2,1},fixture.descriptor,512,512,fixture.input);
    need(!first.expired()&&!second.expired(),"Two reserved jobs did not fit exact byte bound");
    need(preparation.submit({3,1},fixture.descriptor,512,512,fixture.input).expired(),"Byte budget admitted excess work");
    need(preparation.stats().reservedBytes==charge*2&&preparation.stats().outstandingJobs==2,"Pending job accounting differs");
    gate->release();
    auto pinned=waitReady(preparation,first,{1,1});
    eventually([&]{return preparation.stats().ready==1;},"Second result did not become ready");
    need(preparation.submit({3,1},fixture.descriptor,512,512,fixture.input).expired(),"Pinned result escaped the byte bound");
    preparation.cancel(second);
    need(preparation.stats().reservedBytes==charge&&preparation.stats().outstandingJobs==1,"Ready cancellation did not release charge");
    auto replacement=preparation.submit({3,1},fixture.descriptor,512,512,fixture.input);
    need(!replacement.expired(),"Freed ready reservation was not reusable");
    preparation.stop();preparation.stop();
    need(preparation.stats().reservedBytes==charge&&preparation.stats().outstandingJobs==1,"Stop released pinned pixels too early");
    pinned.reset();
    need(preparation.stats().reservedBytes==0,"Pinned pixels did not release after stop");
    need(preparation.submit({4,1},fixture.descriptor,512,512,fixture.input).expired(),"Stopped worker accepted a job");

    // A no-access CPU allocation proves reservation rejection happens before
    // snapshot copying. No GPU object or guest memory participates in this test.
    void* inaccessible=VirtualAlloc(nullptr,fixture.input.size(),MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
    need(inaccessible!=nullptr,"No-access input allocation failed");
    struct Free {void* value;~Free(){VirtualFree(value,0,MEM_RELEASE);}} free{inaccessible};
    ITXDRGBAPreparation tooSmall({charge-1,8,1024u*1024u});
    const std::span<const uint8_t> unreadable(static_cast<const uint8_t*>(inaccessible),fixture.input.size());
    need(tooSmall.submit({5,1},fixture.descriptor,512,512,unreadable).expired(),"Bounds check copied inaccessible input");
    need(tooSmall.stats().outstandingJobs==0,"Rejected snapshot retained a reservation");
}

void generationsAndCancellation() {
    Fixture fixture;
    auto gate=std::make_shared<Gate>();
    ITXDRGBAPreparation preparation({fixture.charge(),1,1024u*1024u},[gate](auto){gate->wait();});
    ReleaseGate release{gate};
    auto old=preparation.submit({17,1},fixture.descriptor,512,512,fixture.input);
    eventually([&]{return gate->started();},"Cancellation worker did not start");
    const auto charge=preparation.stats().reservedBytes;
    preparation.cancel(old);
    need(preparation.stats().reservedBytes==charge&&preparation.stats().outstandingJobs==1,"Running cancellation freed live input reservation");
    need(preparation.submit({17,2},fixture.descriptor,512,512,fixture.input).expired(),"Cancelled running job escaped job/byte bounds");
    need(!preparation.takeReady(old,{17,1}),"Cancelled generation was published");
    gate->release();
    eventually([&]{return preparation.stats().outstandingJobs==0;},"Cancelled running buffers were not retired");
    auto fresh=preparation.submit({17,2},fixture.descriptor,512,512,fixture.input);
    eventually([&]{return preparation.stats().ready==1;},"Fresh generation did not become ready");
    need(!preparation.takeReady(fresh,{17,1}),"Old generation consumed new pixels");
    need(!preparation.takeReady(old,{17,2}),"Cancelled ticket consumed replacement pixels");
    ITXDRGBAPreparation foreign;
    need(!foreign.takeReady(fresh,{17,2}),"Another helper consumed foreign pixels");
    foreign.cancel(fresh);
    need(preparation.stats().ready==1,"Foreign helper cancelled the real owner");
    auto ready=waitReady(preparation,fresh,{17,2});
    need(ready->levels==fixture.decode(),"New generation pixels differ");
}

void errorsAndQualification() {
    Fixture fixture;
    ITXDRGBAPreparation failed({},[](auto){throw std::runtime_error("deterministic CPU failure");});
    auto handle=failed.submit({1,1},fixture.descriptor,512,512,fixture.input);
    eventually([&]{const auto stats=failed.stats();return stats.failed==1&&stats.outstandingJobs==0;},
               "Background exception was not contained");
    need(!failed.takeReady(handle,{1,1}),"Failed background work became ready");
    need(failed.stats().reservedBytes==0&&failed.stats().outstandingJobs==0,"Failed job retained buffers");
    need(!fixture.decode().empty(),"Background exception changed the legacy demand decoder");

    std::atomic<size_t> calls{};
    ITXDRGBAPreparation qualified({},[&](auto){++calls;});
    for(size_t field=0;field<fixture.descriptor.size();++field) {
        auto bad=fixture.descriptor;bad[field]^=1;
        need(qualified.submit({uint32_t(field+1),1},bad,512,512,fixture.input).expired(),"Malformed descriptor reached worker");
    }
    need(qualified.submit({20,1},fixture.descriptor,512,512,std::span(fixture.input).first(fixture.input.size()-1)).expired(),
         "Truncated owned input reached worker");
    Fixture small(256);
    need(qualified.submit({21,1},small.descriptor,256,256,small.input).expired(),"Small texture entered large preparation queue");
    need(calls.load()==0&&qualified.stats().outstandingJobs==0,"Rejected profiles started worker conversion");
}

void profileQueryIsIndependent() {
    Fixture fixture,full(512,5),smallerFull(256,4);
    auto gate=std::make_shared<Gate>();
    std::atomic<size_t> calls{};
    ITXDRGBAPreparation preparation({fixture.charge(),1,1024u*1024u},[&](auto){++calls;gate->wait();});
    ReleaseGate release{gate};
    auto handle=preparation.submit({71,1},fixture.descriptor,512,512,fixture.input);
    need(!handle.expired(),"Query fixture failed to admit its existing job");
    eventually([&]{return gate->started();},"Query fixture worker did not enter its gate");
    const auto sameStats=[](const auto& a,const auto& b) {
        return a.reservedBytes==b.reservedBytes&&a.outstandingJobs==b.outstandingJobs&&
            a.queued==b.queued&&a.running==b.running&&a.ready==b.ready&&
            a.submitted==b.submitted&&a.rejected==b.rejected&&a.completed==b.completed&&
            a.failed==b.failed&&a.taken==b.taken&&a.cancelled==b.cancelled&&a.stopped==b.stopped;
    };
    const auto before=preparation.stats();
    need(before.reservedBytes==fixture.charge()&&before.outstandingJobs==1&&before.running==1,
         "Query fixture did not occupy its exact byte and job budgets");
    static_assert(noexcept(preparation.qualifiesProfile(fixture.descriptor,512,512,fixture.input.size())));
    need(preparation.qualifiesProfile(fixture.descriptor,512,512,fixture.input.size()),
         "Exact default 1MiB output threshold was rejected while worker was busy");
    need(preparation.qualifiesProfile(full.descriptor,512,512,full.input.size()),
         "Supported larger mip output was rejected while worker was busy");
    need(!preparation.qualifiesProfile(smallerFull.descriptor,256,256,smallerFull.input.size()),
         "256 full mips incorrectly reached the default 1MiB output threshold");
    for(size_t field=0;field<fixture.descriptor.size();++field) {
        auto bad=fixture.descriptor;bad[field]^=1;
        need(!preparation.qualifiesProfile(bad,512,512,fixture.input.size()),
             "Query accepted a malformed descriptor word");
    }
    for(const size_t bytes:{size_t(0),fixture.input.size()-1,fixture.input.size()+1,
                           std::numeric_limits<size_t>::max()})
        need(!preparation.qualifiesProfile(fixture.descriptor,512,512,bytes),
             "Query accepted a non-exact input byte count");
    for(const auto [width,height]:std::array<std::pair<uint32_t,uint32_t>,6>{{
            {0,512},{512,0},{16,512},{512,16},{513,512},{512,2048}}})
        need(!preparation.qualifiesProfile(fixture.descriptor,width,height,fixture.input.size()),
             "Query accepted invalid or mismatched dimensions");
    Fixture nonPowerOfTwo(513),tooSmall(16),tooDeep(512,6);
    need(!preparation.qualifiesProfile(nonPowerOfTwo.descriptor,513,513,nonPowerOfTwo.input.size()),
         "Query accepted a self-consistent non-power-of-two profile");
    need(!preparation.qualifiesProfile(tooSmall.descriptor,16,16,tooSmall.input.size()),
         "Query accepted a self-consistent below-minimum extent");
    need(!preparation.qualifiesProfile(tooDeep.descriptor,512,512,tooDeep.input.size()),
         "Query accepted the unsupported deeper shared-tail layout");
    need(sameStats(before,preparation.stats())&&calls.load()==1,
         "Profile queries changed an existing job, counters, reservations or decoder hook");

    // Known authored mip payload sums, distinct from padded source input bytes.
    ITXDRGBAPreparation exactFull({0,0,1397760}),aboveFull({0,0,1397761}),smallMinimum({0,0,349184});
    const auto exactBefore=exactFull.stats(),aboveBefore=aboveFull.stats(),smallBefore=smallMinimum.stats();
    need(!exactFull.qualifiesProfile(fixture.descriptor,512,512,fixture.input.size()),
         "Query ignored its custom minimum output threshold");
    need(exactFull.qualifiesProfile(full.descriptor,512,512,full.input.size()),
         "Query rejected the exact 512 full-mip output threshold with zero job budget");
    need(!aboveFull.qualifiesProfile(full.descriptor,512,512,full.input.size()),
         "Query used padded input bytes or rounded down a one-byte-higher output threshold");
    need(smallMinimum.qualifiesProfile(smallerFull.descriptor,256,256,smallerFull.input.size()),
         "Query ignored a lower custom exact full-mip output threshold");
    need(sameStats(exactBefore,exactFull.stats())&&sameStats(aboveBefore,aboveFull.stats())&&
         sameStats(smallBefore,smallMinimum.stats()),"Custom-threshold queries submitted or charged work");

    gate->release();preparation.stop();
    const auto stopped=preparation.stats();
    need(stopped.stopped&&stopped.outstandingJobs==0&&stopped.reservedBytes==0,
         "Query fixture did not retire its original job before stopped-state checks");
    need(preparation.qualifiesProfile(fixture.descriptor,512,512,fixture.input.size())&&
         preparation.qualifiesProfile(full.descriptor,512,512,full.input.size())&&
         !preparation.qualifiesProfile(smallerFull.descriptor,256,256,smallerFull.input.size()),
         "Stopping the worker changed its supported profile classification");
    need(sameStats(stopped,preparation.stats())&&calls.load()==1,
         "Stopped-worker profile queries changed counters or invoked the decoder hook");
}

void concurrentProducersAndStop() {
    Fixture fixture;
    auto gate=std::make_shared<Gate>();
    ITXDRGBAPreparation preparation({fixture.charge()*3,3,1024u*1024u},[gate](auto){gate->wait();});
    ReleaseGate release{gate};
    std::array<ITXDRGBAPreparation::Handle,12> handles;
    std::array<std::jthread,12> producers;
    for(size_t i=0;i<producers.size();++i)producers[i]=std::jthread([&,i]{
        handles[i]=preparation.submit({uint32_t(i+1),1},fixture.descriptor,512,512,fixture.input);});
    for(auto& producer:producers)producer.join();
    eventually([&]{return gate->started();},"Concurrent producer worker did not start");
    need(preparation.stats().submitted==3&&preparation.stats().outstandingJobs==3,
         "Concurrent admission exceeded or lost the exact job budget");
    need(preparation.stats().reservedBytes==fixture.charge()*3,"Concurrent snapshot byte accounting differs");
    std::jthread stopper([&]{preparation.stop();});
    ReleaseGate stopFailureRelease{gate};
    eventually([&]{return preparation.stats().stopped;},"Stop did not abandon queued work");
    need(preparation.stats().queued==0&&preparation.stats().ready==0,"Stop retained queued/ready jobs");
    gate->release();stopper.join();preparation.stop();
    need(preparation.stats().outstandingJobs==0&&preparation.stats().reservedBytes==0,"Stopped worker retained any reservation");
    need(gate->maxActive==1,"More than one CPU decoder ran concurrently");
    for(size_t i=0;i<handles.size();++i)need(!preparation.takeReady(handles[i],{uint32_t(i+1),1}),"Stop published stale queued work");
}

void pinnedLifetime() {
    Fixture fixture;
    const auto expected=fixture.decode();
    std::optional<ITXDRGBAPreparation::Ready> pinned;
    {
        ITXDRGBAPreparation preparation;
        auto handle=preparation.submit({1,1},fixture.descriptor,512,512,fixture.input);
        auto ready=waitReady(preparation,handle,{1,1});
        pinned.emplace(std::move(*ready));
    }
    need(pinned->levels==expected,"Ready value depended on a destroyed worker/helper");
    pinned.reset();
}

void hardProductionCaps() {
    for(const uint32_t extent:{512u,1024u}) {
        Fixture fixture(extent,extent==1024?6u:0u);
        const size_t charge=fixture.charge();
        auto gate=std::make_shared<Gate>();
        ITXDRGBAPreparation preparation({std::numeric_limits<size_t>::max(),1000,1},[gate](auto){gate->wait();});
        ReleaseGate release{gate};
        for(uint32_t i=0;i<12;++i)
            preparation.submit({i+1,1},fixture.descriptor,fixture.width,fixture.height,fixture.input);
        const size_t maximum=std::min(size_t(8),size_t(64u*1024u*1024u)/charge);
        const auto stats=preparation.stats();
        need(stats.submitted==maximum&&stats.outstandingJobs==maximum,"Oversized caller limits escaped production hard caps");
        need(stats.reservedBytes==maximum*charge&&stats.reservedBytes<=64u*1024u*1024u,
             "Oversized caller byte limit escaped 64MiB hard cap");
        gate->release();preparation.stop();
    }
}
}

int main(){try {
    exactOwnedResult();boundsAndPinnedResults();generationsAndCancellation();
    errorsAndQualification();profileQueryIsIndependent();concurrentProducersAndStop();pinnedLifetime();hardProductionCaps();
    bc2ExactOwnedResult();bc2ProfilesAndFallback();bc2SharedBudgetsAndKinds();bc2RunningCancellationAndGeneration();
    bc2QueuedReplacementAndRGBAPriority();bc2ReadyReplacementKeepsPinnedLease();bc2AdmissionProtectedAndNoRead();
    bc2AdmissionImpossibleDuplicateAndStop();bc2AdmissionMultipleVictimsForBytes();bc2AdmissionConcurrentProducers();
    std::printf("PASS: %zu bounded owned ITXD preparation checks; pure CPU, no GPU or guest ownership substitution\n",checks);
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL after %zu checks: %s\n",checks,error.what());return 1;}}
