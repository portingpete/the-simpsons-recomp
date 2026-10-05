#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "../common/guest_write_watch.h"

namespace Simpsons {
// Exact proof that a validator which reads only guest memory would repeat its last
// successful result: every 4 KiB page it read still has the write-watch version it had when
// the validator armed it, so no guest or host store has touched any of those pages since.
//
// Usage: begin(); for each range about to be read, arm(hostPointer,bytes) BEFORE the read;
// commit() once the validation succeeded (a throw simply leaves the memo invalid). valid()
// is then true until a watched page changes, the memo is bounded by `kReverifyUses` hits
// (the same bounded re-verification the cached-source barrier uses for its residual
// cross-thread race), or `invalidate()` is called. A validator that also reads host state
// must compare that state itself; this proves the guest bytes only.
//
// trackPermissions() (after begin()) additionally ties the memo to guestPermissionEpoch: for
// validators that check the permissions of pages whose bytes they do not compare (those pages
// are not armed), any permission or mapping change anywhere invalidates the memo.
//
// SIMPSONS_MEMO_STATS=1 prints each named memo's hit/miss counts every 65536 queries.
class GuestReadMemo {
    static constexpr size_t kMaxPages=256;
    static constexpr uint32_t kReverifyUses=1u<<12;
    GuestWriteWatch watch_{};
    uint32_t pages_[kMaxPages]{},versions_[kMaxPages]{};
    size_t count_=0;
    bool recording_=false,overflow_=false,valid_=false,permissions_=false;
    uint64_t permissionEpoch_=0,generation_=0;
    mutable uint32_t uses_=0;
    const char* name_=nullptr;
    mutable uint64_t hits_=0,misses_=0;
    static bool stats() noexcept {
        static const bool value=[]{const char* text=std::getenv("SIMPSONS_MEMO_STATS");return text&&*text&&*text!='0';}();
        return value;
    }
    void count(bool hit) const noexcept {
        if(!name_||!stats())return;
        (hit?hits_:misses_)++;
        if(((hits_+misses_)&0xFFFF)==0)
            std::fprintf(stderr,"[READ MEMO STATS] %s hits=%llu misses=%llu\n",name_,(unsigned long long)hits_,(unsigned long long)misses_);
    }
public:
    void setName(const char* name) noexcept {name_=name;}
    void trackPermissions() noexcept {permissions_=true;permissionEpoch_=guestPermissionEpoch.load(std::memory_order_seq_cst);}
    void setWatch(const GuestWriteWatch& watch) noexcept {watch_=watch;}
    bool enabled() const noexcept {return watch_.enabled();}
    void begin() noexcept {count_=0;recording_=true;overflow_=false;valid_=false;permissions_=false;}
    void arm(const uint8_t* host,size_t size) {
        if(!recording_||!watch_.enabled()||!size){overflow_|=!watch_.enabled();return;}
        uint32_t first=0,last=0;
        if(!watch_.pages(host,size,first,last)){overflow_=true;return;}
        for(uint32_t page=first;page<=last;++page) {
            bool seen=false;
            for(size_t i=0;i<count_&&!seen;++i)seen=pages_[i]==page;
            if(seen)continue;
            if(count_==kMaxPages){overflow_=true;return;}
            pages_[count_]=page;versions_[count_]=watch_.armPage(page);++count_;  // arm, then version, before the caller's read
        }
    }
    void commit() noexcept {valid_=recording_&&!overflow_&&watch_.enabled();recording_=false;uses_=0;++generation_;}
    // Advances with every commit: a memo that depends on this one records the generation it was
    // proven with, since a re-proof can be valid again with different content.
    uint64_t generation() const noexcept {return generation_;}
    void abandon() noexcept {valid_=false;recording_=false;}
    void invalidate() noexcept {valid_=false;}
    // SIMPSONS_NO_READ_MEMO=1 turns every memo off (diagnostic: re-run the full validators).
    static bool disabled() noexcept {
        static const bool value=[]{const char* text=std::getenv("SIMPSONS_NO_READ_MEMO");return text&&*text&&*text!='0';}();
        return value;
    }
    // SIMPSONS_WATCH_VERIFY=1: validators repeat the full read on every memo hit and abort on a difference.
    static bool verify() noexcept {
        static const bool value=[]{const char* text=std::getenv("SIMPSONS_WATCH_VERIFY");return text&&*text&&*text!='0';}();
        return value;
    }
    bool valid() const noexcept {
        const bool hit=check();
        count(hit);
        return hit;
    }
private:
    bool check() const noexcept {
        if(!valid_||recording_||disabled())return false;
        if(++uses_>kReverifyUses)return false;
        if(permissions_ && guestPermissionEpoch.load(std::memory_order_acquire)!=permissionEpoch_)return false;
        for(size_t i=0;i<count_;++i)
            if(watch_.version[pages_[i]].load(std::memory_order_acquire)!=versions_[i])return false;
        return true;
    }
};
}
