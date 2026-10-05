#include "audio/native_xma_codec.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <string>
#include <vector>
#include <xmmintrin.h>

using namespace Simpsons::Audio;
static std::atomic_uint64_t checks{};
static void need(bool value,const char* message) {++checks;if(!value) throw std::runtime_error(message);}
template<class F> static void rejects(F&& call) {
    bool rejected=false;try{call();}catch(const CodecError&){rejected=true;}
    need(rejected,"Invalid codec operation was not rejected");
}
static std::string sha(std::span<const uint8_t> bytes) {
    std::array<uint8_t,32> digest{};
    need(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),
                    digest.data(),ULONG(digest.size()))==0,"SHA256 failed");
    std::string result;for(auto b:digest){result+="0123456789abcdef"[b>>4];result+="0123456789abcdef"[b&15];}
    return result;
}
static std::string sha(const std::vector<float>& pcm) {
    return sha({reinterpret_cast<const uint8_t*>(pcm.data()),pcm.size()*sizeof(float)});
}
struct Fixture {const char* name;size_t packets,samples;const char* packetHash;const char* pcmHash;std::vector<uint8_t> bytes;};
static Fixture fixtures[]{
    {"short",2,8704,"312a9c2a63c7744f84df913da544e3fce047289cd42aa5ae76808eb2117a9996",
     "79c1b75fc3dc434099bd6b31e1dcec0da6edcfc05d7061c143ff365ae589aedd"},
    {"documented",11,55296,"9a837e71c7b71887a182202ada01c003a99515308967f55bf115e9ba8230014f",
     "c7364922a185b457206b94e9173eaa0d206ca7af5bd134586d0ba521576fa6f9"}
};
struct FloatingPoint {
    const uint32_t saved=_mm_getcsr();
    explicit FloatingPoint(uint32_t mode) {_mm_setcsr(((saved|0x1f80u|0x8040u|0x21u)&~0x6000u)|mode);}
    ~FloatingPoint(){_mm_setcsr(saved);}
};
static uint32_t pull(NativeXmaCodec& decoder,std::vector<float>& result,size_t capacity) {
    constexpr uint32_t sentinel=0x4b123456;
    std::vector<float> output(capacity+2,std::bit_cast<float>(sentinel));
    const auto csr=_mm_getcsr();
    const auto count=decoder.read({output.data()+1,capacity});
    need(_mm_getcsr()==csr,"Read changed caller floating-point state");
    need(count<=capacity && count<=512,"Read exceeded destination/frame capacity");
    need(std::bit_cast<uint32_t>(output.front())==sentinel,"Read underflowed destination");
    for(size_t index=count+1;index<output.size();++index)
        need(std::bit_cast<uint32_t>(output[index])==sentinel,"Read touched unreturned destination samples");
    result.insert(result.end(),output.begin()+1,output.begin()+1+count);
    return count;
}
static PacketResult push(NativeXmaCodec& decoder,std::span<const uint8_t> bytes) {
    // The original buffer is destroyed/poisoned immediately after acceptance.
    // A decoder retaining a caller pointer will fail the pinned output hash.
    std::vector<uint8_t> transient(bytes.begin(),bytes.end());
    const auto csr=_mm_getcsr();const auto result=decoder.send(transient);
    need(_mm_getcsr()==csr,"Send changed caller floating-point state");
    std::fill(transient.begin(),transient.end(),0xa5);return result;
}
static std::vector<float> decode(NativeXmaCodec& decoder,const Fixture& f,unsigned pattern,bool greedy) {
    const std::array<size_t,8> widths{1,3,127,511,512,513,19,997};
    size_t readCount=0;uint32_t backpressure=0;std::vector<float> output;
    need(pull(decoder,output,37)==0,"Fresh/reset decoder emitted invented samples");
    auto read=[&]{return pull(decoder,output,pattern?widths[readCount++%widths.size()]:512);};
    auto drain=[&]{size_t total=0;for(unsigned attempt=0;attempt<1024;++attempt){auto n=read();if(!n)return total;total+=n;}
        throw std::runtime_error("Drain exceeded bounded progress");};
    for(size_t packet=0;packet<f.packets;++packet) {
        for(unsigned attempt=0;;++attempt) {
            need(attempt<1024,"Packet retry exceeded bounded progress");
            const auto result=push(decoder,{f.bytes.data()+packet*2048,2048});
            if(result==PacketResult::Accepted) break;
            ++backpressure;need(read()!=0,"Backpressure did not permit output progress");
        }
        if(!greedy) drain();
    }
    drain();
    for(unsigned attempt=0;attempt<3;++attempt) need(read()==0,"Temporary starvation synthesized output");
    need(output.size()==f.samples,"Raw sample count changed");
    need(sha(output)==f.pcmHash,"Raw output differs from independently recorded original-packet oracle");
    if(greedy && f.packets>2) need(backpressure!=0,"Greedy test never exercised packet backpressure");
    return output;
}
static void runCase(const Fixture& f,XmaVariant variant,uint32_t rounding) {
    FloatingPoint floatingPoint(rounding);const auto csr=_mm_getcsr();
    {
        NativeXmaCodec decoder({1,48000,variant});need(_mm_getcsr()==csr,"Constructor changed caller FP state");
        need(decoder.format().channels==1 && decoder.format().sampleRate==48000 && decoder.format().variant==variant,
             "Declared format changed");
        rejects([&]{decoder.send({});});
        rejects([&]{decoder.send({f.bytes.data(),2047});});
        rejects([&]{decoder.send({f.bytes.data(),2049});});
        need(decoder.read({})==0,"Empty read emitted output");
        decode(decoder,f,0,false);
        decoder.reset();need(_mm_getcsr()==csr,"Reset changed caller FP state");
        decode(decoder,f,1,true);
        decoder.reset();
        need(push(decoder,{f.bytes.data(),2048})==PacketResult::Accepted,"Partial reset packet was not accepted");
        std::vector<float> partial;need(pull(decoder,partial,3)==3,"Partial reset did not retain a frame");
        decoder.reset();
        decode(decoder,f,1,false);
    }
    need(_mm_getcsr()==csr,"Destructor changed caller FP state");
}
int main(int argc,char** argv) {
    try {
        static_assert(sizeof(float)==4 && std::endian::native==std::endian::little);
        need(argc==2,"Original fixture directory required");
        std::array<wchar_t,32768> modulePath{};
        need(GetModuleFileNameW(nullptr,modulePath.data(),DWORD(modulePath.size()))!=0,"Executable path unavailable");
        const auto executableDirectory=std::filesystem::canonical(modulePath.data()).parent_path();
        for(const auto* name:{L"avcodec-simpsonsxma-62.dll",L"avutil-simpsonsxma-60.dll",L"libwinpthread-1.dll"}) {
            const auto module=GetModuleHandleW(name);need(module!=nullptr,"Required owned codec DLL is not loaded");
            need(GetModuleFileNameW(module,modulePath.data(),DWORD(modulePath.size()))!=0,"Codec DLL path unavailable");
            need(std::filesystem::canonical(modulePath.data())==std::filesystem::canonical(executableDirectory/name),
                 "Codec DLL loaded from outside the verified executable directory");
            std::cout<<"module "<<std::filesystem::path(modulePath.data()).string()<<'\n';
        }
        for(auto& f:fixtures) {
            const auto path=std::filesystem::path(argv[1])/(std::string(f.name)+".packets");
            std::ifstream file(path,std::ios::binary|std::ios::ate);
            need(bool(file) && file.tellg()==std::streamoff(f.packets*2048),"Fixture size/read failed");
            f.bytes.resize(f.packets*2048);file.seekg(0);
            need(bool(file.read(reinterpret_cast<char*>(f.bytes.data()),f.bytes.size())),"Fixture read failed");
            need(sha(f.bytes)==f.packetHash,"Fixture original-packet identity changed");
        }
        for(auto format:{XmaFormat{0,48000,XmaVariant::Xma1},XmaFormat{3,48000,XmaVariant::Xma1},
                         XmaFormat{1,22050,XmaVariant::Xma1},XmaFormat{1,48000,XmaVariant(9)}})
            rejects([&]{NativeXmaCodec decoder(format);});
        // Factory coverage is not stereo/rate waveform evidence.
        for(auto variant:{XmaVariant::Xma1,XmaVariant::Xma2})
            for(auto rate:{24000u,32000u,44100u,48000u}) {
                NativeXmaCodec decoder({2,rate,variant});std::array<float,3> invalid{};
                rejects([&]{decoder.read(invalid);});
                std::array<float,4> output{1,2,3,4};need(decoder.read(output)==0,"Empty stereo decoder invented samples");
                need(output==std::array<float,4>{1,2,3,4},"Empty stereo read modified destination");
            }
        for(auto& f:fixtures) {
            for(auto variant:{XmaVariant::Xma1,XmaVariant::Xma2})
                for(auto mode:{0u,0x2000u,0x4000u,0x6000u}) runCase(f,variant,mode);
            std::cout<<f.name<<" raw_samples "<<f.samples<<" sha256 "<<f.pcmHash<<'\n';
        }
        // Actual independent owners decode concurrently on separate native
        // threads. Construction/destruction can occur on another thread.
        std::array<std::future<void>,4> workers;
        for(unsigned i=0;i<workers.size();++i) workers[i]=std::async(std::launch::async,[i]{
            runCase(fixtures[i%2],i<2?XmaVariant::Xma1:XmaVariant::Xma2,(i%4)*0x2000u);
        });
        for(auto& worker:workers) worker.get();
        NativeXmaFactory factory;
        auto transferred=factory.create({1,48000,XmaVariant::Xma1});
        auto use=std::async(std::launch::async,[&]{decode(*transferred,fixtures[1],1,true);});use.get();
        std::cout<<"Native XMA ownership PASS: "<<checks<<" checks; no output device or EOF packet\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"Native XMA ownership FAIL: "<<error.what()<<'\n';return 1;}
}
