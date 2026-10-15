#pragma once
// Included by generated FXC headers (tools/vulkan_shader_compile.py). Each
// header appends its checked SPIR-V twin and registers it under the 16-byte
// DXBC checksum, so the Vulkan device resolves CreateVertexShader/
// CreatePixelShader bytecode without a central shader list. Header-only and
// dependency-free: binaries that never create a Vulkan device link nothing.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Simpsons::Graphics::Vulkan {
struct SpirvShader {
    const char* name;
    std::array<uint32_t,4> dxbcChecksum;
    const uint32_t* words;
    size_t wordCount;
    const SpirvShader* next;
};
// Lock-free intrusive list built by static initialization of every linked
// shader header; read only after main starts.
inline std::atomic<const SpirvShader*>& spirvShaderList() noexcept {
    static std::atomic<const SpirvShader*> head{nullptr};
    return head;
}
struct SpirvRegistration {
    SpirvShader entry;
    SpirvRegistration(const char* name,std::array<uint32_t,4> checksum,const uint32_t* words,size_t count) noexcept
        :entry{name,checksum,words,count,nullptr} {
        auto& head=spirvShaderList();
        const SpirvShader* current=head.load(std::memory_order_relaxed);
        do entry.next=current; while(!head.compare_exchange_weak(current,&entry,std::memory_order_release,std::memory_order_relaxed));
    }
    SpirvRegistration(const SpirvRegistration&)=delete;
    SpirvRegistration& operator=(const SpirvRegistration&)=delete;
};
}
