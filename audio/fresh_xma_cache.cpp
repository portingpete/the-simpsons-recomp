#include "fresh_xma_cache.h"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <condition_variable>
#include <list>
#include <thread>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace Simpsons::Audio {
namespace {
using Key=std::array<uint8_t,32>;
void need(bool ok,const char* message) {if(!ok)throw XmaSourceError(message);}
struct IdentityUnavailable {};
void identityNeed(bool ok) {if(!ok)throw IdentityUnavailable{};}
struct KeyHash {
    size_t operator()(const Key& key) const noexcept {
        size_t value=0;for(const auto byte:key)value=(value*131)^byte;return value;
    }
};
Key identity(std::span<const XmaFormat> formats,const XmaSource::Segment& segment,XmaSource::Limits limits) {
    BCRYPT_HASH_HANDLE handle{};
    identityNeed(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,&handle,nullptr,0,nullptr,0,0)>=0);
    struct Close {BCRYPT_HASH_HANDLE handle;~Close(){BCryptDestroyHash(handle);}} close{handle};
    const auto word=[&](uint64_t value) {
        std::array<uint8_t,8> bytes{};for(unsigned i=0;i<8;++i)bytes[i]=uint8_t(value>>(56-8*i));
        identityNeed(BCryptHashData(handle,bytes.data(),ULONG(bytes.size()),0)>=0);
    };
    // Only equivalent fresh input recipes can share decoded floats. Opaque
    // caller generation, sequence and slot are deliberately local to each owner.
    word(0x4E61746976654632ull);word(uint64_t(segment.continuity));
    word(segment.declaredFrames);word(segment.initialConsumedFrames);word(formats.size());
    word(limits.maxSources);word(limits.maxTickets);word(limits.maxQuotaFrames);
    word(limits.maxBufferedFramesPerLayer);word(limits.maxDeclaredFrames);word(limits.maxCompressedBytes);
    for(const auto& format:formats) {word(format.channels);word(format.sampleRate);word(uint64_t(format.variant));}
    word(segment.layers.size());
    for(const auto& layer:segment.layers) {
        word(layer.skipFrames);word(layer.packets.size());
        identityNeed(layer.packets.size()<=std::numeric_limits<ULONG>::max()/sizeof(XmaSource::Packet));
        if(!layer.packets.empty())
            identityNeed(BCryptHashData(handle,reinterpret_cast<PUCHAR>(const_cast<XmaSource::Packet*>(layer.packets.data())),
                                       ULONG(layer.packets.size()*sizeof(XmaSource::Packet)),0)>=0);
    }
    Key key{};identityNeed(BCryptFinishHash(handle,key.data(),ULONG(key.size()),0)>=0);
    return key;
}
}
struct FreshXmaCache::State {
    struct Entry {Key key;std::shared_ptr<const XmaSource::FreshImage> image;uint64_t bytes;};
    mutable std::mutex mutex;
    const uint64_t maxBytes;
    const uint32_t maxEntries;
    std::list<Entry> lru;
    std::unordered_map<Key,std::list<Entry>::iterator,KeyHash> lookup;
    Snapshot counts;
    // Incrementally decoding misses (Limits::prepareFrames) are published once their
    // codec has seen every packet, so later plays reuse the decoded floats as before.
    struct Pending {
        Key key;std::shared_ptr<XmaSource> source;std::vector<XmaSource::LayerInput> input;
        XmaSource::Receipt origin;uint32_t declaredFrames{},initialConsumedFrames{};
    };
    static constexpr size_t kPending=8;
    std::list<Pending> pending;
    // Background completion of pending incremental decodes (see the class comment).
    std::condition_variable wake;
    std::thread worker;
    bool stopping=false;
    void run() {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        std::unique_lock lock(mutex);
        for(;;) {
            wake.wait(lock,[&]{return stopping || !pending.empty();});
            if(stopping)return;
            const auto entry=pending.front();
            lock.unlock();
            // Decoding and the image copy run without the cache mutex, so the audio
            // thread's prepare() never waits for them.
            std::shared_ptr<const XmaSource::FreshImage> image;
            try {
                while(entry.source->decodeAhead(4))std::this_thread::yield();
                image=entry.source->completedFreshImage(entry.input,entry.origin,entry.declaredFrames,entry.initialConsumedFrames);
            } catch(...) {} // The owner's snapshot keeps any terminal failure; nothing is published.
            lock.lock();
            for(auto it=pending.begin();it!=pending.end();++it) if(it->source==entry.source) {
                if(image) {try {publish(entry.key,std::move(image));} catch(const std::bad_alloc&) {++counts.bypasses;}}
                pending.erase(it);break;
            }
        }
    }
    ~State() {
        {std::lock_guard lock(mutex);stopping=true;}
        wake.notify_all();
        if(worker.joinable())worker.join();
    }
    void publish(const Key& key,std::shared_ptr<const XmaSource::FreshImage> image) {
        const auto bytes=image->memoryBytes+sizeof(Entry)+sizeof(Key)+512;
        if(lookup.contains(key) || !room(bytes)){++counts.bypasses;return;}
        lru.push_front({key,std::move(image),bytes});
        try {lookup.emplace(key,lru.begin());}
        catch(...) {lru.pop_front();throw;}
        counts.bytes+=bytes;
    }

    State(uint64_t bytes,uint32_t entries):maxBytes(bytes),maxEntries(entries) {}
    bool room(uint64_t bytes) {
        if(bytes>maxBytes || !maxEntries)return false;
        while(lru.size()>=maxEntries || counts.bytes>maxBytes-bytes) {
            auto candidate=lru.end();
            for(auto it=lru.end();it!=lru.begin();) {
                --it;if(it->image.use_count()==1){candidate=it;break;}
            }
            // Pinned PCM still counts against both caps. Cache pressure never
            // changes original guest admission: the caller uses real decoding.
            if(candidate==lru.end())return false;
            counts.bytes-=candidate->bytes;lookup.erase(candidate->key);lru.erase(candidate);++counts.evictions;
        }
        return true;
    }
};
FreshXmaCache::FreshXmaCache(uint64_t bytes,uint32_t entries):state(std::make_unique<State>(bytes,entries)) {}
FreshXmaCache::~FreshXmaCache()=default;
FreshXmaCache::Result FreshXmaCache::prepare(NativeXmaFactory& factory,uint64_t generation,
                                          std::span<const XmaFormat> formats,const XmaSource::Segment& segment,
                                          XmaSource::Limits limits) {
    need(segment.continuity==XmaSource::Continuity::FreshContext,"Fresh XMA reuse does not authorize continuation");
    auto& s=*state;
    std::optional<Key> key;
    if(s.maxBytes && s.maxEntries) {
        try {key=identity(formats,segment,limits);}
        catch(const IdentityUnavailable&) {
            // A disabled/unavailable optional identity cannot reject original
            // admission. Normal preparation below still validates every input.
        }
    }
    std::shared_ptr<const XmaSource::FreshImage> image;
    {
        std::lock_guard lock(s.mutex);
        const auto found=key?s.lookup.find(*key):s.lookup.end();
        if(found!=s.lookup.end()) {
            image=found->second->image;s.lru.splice(s.lru.begin(),s.lru,found->second);++s.counts.hits;
        }else {
            ++s.counts.misses;
            if(!key)++s.counts.bypasses;
        }
    }
    if(image) {
        auto source=std::shared_ptr<XmaSource>(new XmaSource(generation,std::move(image),formats,segment,limits));
        const auto snapshot=source->snapshot();
        return {source,snapshot.sources.front().receipt,true};
    }
    // Codec work remains outside the cache mutex. A failed actual decode keeps
    // its normal terminal provenance; optional cache allocation cannot reject it.
    auto source=std::make_shared<XmaSource>(factory,generation,formats,limits);
    const auto accepted=source->prepare(segment);
    need(accepted.status==XmaSource::PrepareStatus::Accepted,"Fresh native XMA source admission has no capacity");
    if(!key)return {source,accepted.receipt,false};
    if(!source->decodedAllInput()) {
        // Incremental decode: publish its image once every packet has been decoded.
        std::lock_guard lock(s.mutex);
        bool known=s.lookup.contains(*key);
        for(const auto& entry:s.pending)known|=entry.key==*key;
        if(!known) {
            if(s.pending.size()>=State::kPending)s.pending.pop_back(); // The worker owns the front entry.
            s.pending.push_back({*key,source,segment.layers,accepted.receipt,segment.declaredFrames,segment.initialConsumedFrames});
            if(!s.worker.joinable())s.worker=std::thread([&s]{s.run();});
            s.wake.notify_one();
        }
        return {source,accepted.receipt,false};
    }
    // Retained raw capacities and packet copies dominate storage. Also charge
    // conservative per-entry control/list/hash-node/bucket/allocation overhead.
    const auto bytes=source->freshImageBytes()+sizeof(State::Entry)+sizeof(Key)+512;
    {
        std::lock_guard lock(s.mutex);
        if(!s.lookup.contains(*key) && s.room(bytes)) {
            try {
                image=source->shareFreshImage();
                s.lru.push_front({*key,image,bytes});
                try {s.lookup.emplace(*key,s.lru.begin());}
                catch(...) {s.lru.pop_front();throw;}
                s.counts.bytes+=bytes;
            }catch(const std::bad_alloc&) {++s.counts.bypasses;}
        }else ++s.counts.bypasses;
    }
    return {source,accepted.receipt,false};
}
FreshXmaCache::Snapshot FreshXmaCache::snapshot() const {
    const auto& s=*state;std::lock_guard lock(s.mutex);auto result=s.counts;
    result.entries=uint32_t(s.lru.size());
    for(const auto& entry:s.lru)result.pinned+=entry.image.use_count()>1;
    return result;
}
}
