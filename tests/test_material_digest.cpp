// CPU-only identity contract: real production matcher, independent exact goldens.
// Full catalog admissions are exercised separately by material/effect fixtures.
#include "renderer/material_resources.h"
#include "header/test_material_digest_vectors.h"
#include <windows.h>
#include <immintrin.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>
namespace Simpsons::Graphics::detail {
bool matchesOriginalRecordDigest(std::span<const uint8_t>,std::string_view);
}
namespace {
using namespace Simpsons::Graphics;
size_t checks=0,digestCalls=0;
void need(bool okay,const char* why) {
    ++checks;
    if(!okay) throw std::runtime_error(why);
}
struct HostRestore {
    unsigned csr=_mm_getcsr();DWORD error=GetLastError();
    ~HostRestore() noexcept {_mm_setcsr(csr);SetLastError(error);}
};
uint8_t nibble(char c) {
    if(c>='0'&&c<='9') return static_cast<uint8_t>(c-'0');
    if(c>='a'&&c<='f') return static_cast<uint8_t>(c-'a'+10);
    throw std::runtime_error("Invalid frozen vector hex");
}
std::vector<uint8_t> unhex(std::string_view s) {
    need(s.size()%2==0,"Odd frozen vector hex");
    std::vector<uint8_t> result(s.size()/2);
    for(size_t i=0;i<result.size();++i) result[i]=static_cast<uint8_t>(16*nibble(s[2*i])+nibble(s[2*i+1]));
    return result;
}
void digest(std::span<const uint8_t> bytes,std::string_view expected,bool wanted) {
    const HostRestore restore;
    constexpr std::array<unsigned,4> sticky={0u,1u,0x20u,0x3fu};
    for(unsigned rounding=0;rounding<4;++rounding) {
        for(size_t i=0;i<sticky.size();++i) {
            const unsigned csr=0x1f80u|(rounding<<13)|sticky[i]|((i&1u)?0x8040u:0u);
            const DWORD error=0xa17c0000u|static_cast<DWORD>(rounding*4+i);
            _mm_setcsr(csr);SetLastError(error);
            const bool result=detail::matchesOriginalRecordDigest(bytes,expected);
            const unsigned afterCsr=_mm_getcsr();const DWORD afterError=GetLastError();
            ++digestCalls;
            need(result==wanted,"Digest admission differs from independent expected outcome");
            need(afterCsr==csr,"Digest changed full host MXCSR");
            need(afterError==error,"Digest changed LastError");
        }
    }
}
struct GuardPages {
    uint8_t* allocation{};size_t page{};
    GuardPages() {
        SYSTEM_INFO info{};GetSystemInfo(&info);page=info.dwPageSize;
        allocation=static_cast<uint8_t*>(VirtualAlloc(nullptr,page*3,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS));
        need(allocation!=nullptr,"Guard allocation failed");
        DWORD old{};
        if(!VirtualProtect(allocation+page,page,PAGE_READWRITE,&old)) {
            VirtualFree(allocation,0,MEM_RELEASE);allocation=nullptr;
            throw std::runtime_error("Guard middle-page protection failed");
        }
    }
    ~GuardPages() {if(allocation) VirtualFree(allocation,0,MEM_RELEASE);}
    GuardPages(const GuardPages&)=delete;GuardPages& operator=(const GuardPages&)=delete;
    uint8_t* end() const noexcept {return allocation+2*page;}
};
void independentVectors() {
    GuardPages pages;
    for(const auto& golden:MaterialDigestTest::goldens) {
        auto bytes=unhex(golden.hex);const auto original=bytes;
        digest(bytes,golden.sha256,true);
        need(bytes==original,"Hash modified immutable input");
        digest(bytes,golden.sha256.substr(0,63),false);
        auto nonhex=std::string(golden.sha256);nonhex[0]='g';digest(bytes,nonhex,false);
        auto wrong=std::string(golden.sha256);wrong[0]=wrong[0]=='0'?'1':'0';digest(bytes,wrong,false);
        auto uppercase=std::string(golden.sha256);
        for(auto& c:uppercase) if(c>='a'&&c<='f') c=static_cast<char>(c-'a'+'A');
        need(uppercase!=golden.sha256,"Frozen expected hash lacks alpha hex");
        digest(bytes,uppercase,false);
        for(size_t offset=0;offset<32;++offset) {
            std::vector<uint8_t> padded(offset+bytes.size()+32,0xa5);
            std::copy(bytes.begin(),bytes.end(),padded.begin()+static_cast<ptrdiff_t>(offset));
            const auto saved=padded;
            digest(std::span<const uint8_t>(padded).subspan(offset,bytes.size()),golden.sha256,true);
            need(padded==saved,"Unaligned input or surrounding sentinels changed");
        }
        need(bytes.size()<pages.page,"Golden is not bounded to middle guard page");
        uint8_t* endInput=pages.end()-bytes.size();
        if(!bytes.empty()) std::memcpy(endInput,bytes.data(),bytes.size());
        digest(std::span<const uint8_t>(endInput,bytes.size()),golden.sha256,true);
        need(std::equal(bytes.begin(),bytes.end(),endInput),"Guarded exact-end bytes changed");
        if(bytes.empty()) digest({},golden.sha256,true);
        else {
            for(size_t at:std::array<size_t,3>{0,bytes.size()/2,bytes.size()-1}) {
                auto mutation=bytes;mutation[at]^=1;
                digest(mutation,golden.sha256,false);
            }
        }
    }
}
}
int main() {
    const HostRestore restore;
    try {
        static_assert(MaterialDigestTest::goldens.size()==77);
        independentVectors();
        std::printf("Material digest contract passed%zu checks,77 independent vectors,%zu digest calls\n",checks,digestCalls);
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"Material digest contract failed after%zu checks: %s\n",checks,error.what());
        return 1;
    }
}
