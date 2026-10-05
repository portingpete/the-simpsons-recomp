#pragma once
#include "runtime.h"
#include <array>
#include <cstring>
#include <memory>

namespace Simpsons {
// Full, fresh backups of the original engine cache/record windows. Only the
// allocated byte storage is reused; destinations and bytes are read anew.
class EngineCacheTransaction {
    static constexpr std::array<uint32_t,3> addresses{0x82D0D170,0x82E3D160,0x82D501E0};
    static constexpr std::array<size_t,3> sizes{0x2FBC,0xB24,0x140};
    struct Storage {
        std::array<uint8_t,0x2FBC+0xB24+0x140> bytes;
        std::unique_ptr<Storage> next;
    };
    static std::unique_ptr<Storage>& spare() {
        static thread_local std::unique_ptr<Storage> value;
        return value;
    }
    struct Lease {
        std::unique_ptr<Storage> storage;
        Lease():storage(std::move(spare())) {
            if(storage)spare()=std::move(storage->next);
            else storage=std::make_unique<Storage>();
        }
        ~Lease() {
            storage->next=std::move(spare());
            spare()=std::move(storage);
        }
    } backup;
    std::array<uint8_t*,3> destinations{};
    bool committed{};
public:
    // Even a proven no-change operation must validate every original window.
    static void preflight() {
        for(size_t i=0;i<sizes.size();++i)active->pointer(addresses[i],sizes[i],true);
    }
    EngineCacheTransaction() {
        size_t offset=0;
        for(size_t i=0;i<sizes.size();++i) {
            destinations[i]=active->pointer(addresses[i],sizes[i],true);
            std::memcpy(backup.storage->bytes.data()+offset,destinations[i],sizes[i]);
            offset+=sizes[i];
        }
    }
    ~EngineCacheTransaction() {
        if(committed)return;
        size_t offset=0;
        for(size_t i=0;i<sizes.size();++i) {
            std::memcpy(destinations[i],backup.storage->bytes.data()+offset,sizes[i]);
            offset+=sizes[i];
        }
    }
    EngineCacheTransaction(const EngineCacheTransaction&)=delete;
    EngineCacheTransaction& operator=(const EngineCacheTransaction&)=delete;
    void publish() noexcept {committed=true;}
};
}
