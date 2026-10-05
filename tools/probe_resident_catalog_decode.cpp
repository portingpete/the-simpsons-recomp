#include "audio/native_xma_codec.h"

#include <array>
#include <bit>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Simpsons::Audio::NativeXmaCodec;
using Simpsons::Audio::PacketResult;
using Simpsons::Audio::XmaFormat;
using Simpsons::Audio::XmaVariant;

uint32_t word(std::istream& input) {
    std::array<unsigned char,4> bytes{};
    if(!input.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))
        throw std::runtime_error("Truncated probe manifest");
    return uint32_t(bytes[0]) | uint32_t(bytes[1])<<8 | uint32_t(bytes[2])<<16 | uint32_t(bytes[3])<<24;
}

struct Result {uint64_t frames=0, hash=14695981039346656037ull;};

Result decode(NativeXmaCodec& codec,std::span<const uint8_t> packets,uint32_t channels) {
    Result result;
    std::array<float,NativeXmaCodec::frameSamples*2> output{};
    const auto destination=std::span<float>(output).first(NativeXmaCodec::frameSamples*channels);
    auto drain=[&] {
        uint64_t gained=0;
        for(unsigned attempt=0;attempt<4096;++attempt) {
            const uint32_t frames=codec.read(destination);
            if(!frames) return gained;
            gained+=frames;
            result.frames+=frames;
            for(size_t i=0;i<size_t(frames)*channels;++i) {
                const uint32_t bits=std::bit_cast<uint32_t>(output[i]);
                for(unsigned byte=0;byte<4;++byte) {
                    result.hash^=uint8_t(bits>>(byte*8));
                    result.hash*=1099511628211ull;
                }
            }
        }
        throw std::runtime_error("Native decoder did not drain within bound");
    };
    if(codec.read(destination)) throw std::runtime_error("Native decoder produced frames before input");
    for(size_t at=0;at<packets.size();at+=NativeXmaCodec::packetBytes) {
        const auto packet=packets.subspan(at,NativeXmaCodec::packetBytes);
        bool accepted=false;
        for(unsigned attempt=0;attempt<4096;++attempt) {
            if(codec.send(packet)==PacketResult::Accepted) {accepted=true;break;}
            if(!drain()) throw std::runtime_error("Packet backpressure without decode progress");
        }
        if(!accepted) throw std::runtime_error("Native decoder never accepted packet");
        drain();
    }
    for(unsigned i=0;i<3;++i)
        if(codec.read(destination)) throw std::runtime_error("Native decoder emitted after input exhaustion");
    return result;
}

std::string singleLine(std::string value) {
    for(char& c:value) if(c=='\t' || c=='\r' || c=='\n') c=' ';
    return value;
}
}

int main(int argc,char** argv) {
    try {
        if(argc!=2) throw std::runtime_error("Provide one binary probe manifest");
        std::ifstream input(argv[1],std::ios::binary);
        if(!input) throw std::runtime_error("Cannot open probe manifest");
        std::array<char,8> magic{};
        if(!input.read(magic.data(),magic.size()) || magic!=std::array<char,8>{'R','D','E','C','0','0','0','1'})
            throw std::runtime_error("Unknown probe manifest");
        const uint32_t count=word(input);
        if(!count || count>100000) throw std::runtime_error("Probe manifest record count outside bound");
        std::cout<<"id\tstatus\traw_frames\traw_hash\trestart_frames\trestart_hash\treason\n";
        for(uint32_t index=0;index<count;++index) {
            const uint32_t id=word(input),channels=word(input),rate=word(input);
            const uint32_t packets=word(input),restart=word(input);
            if(id!=index || (channels!=1 && channels!=2) || !packets || packets>8192 || restart>1)
                throw std::runtime_error("Probe record framing outside bound");
            std::vector<uint8_t> bytes(size_t(packets)*NativeXmaCodec::packetBytes);
            if(!input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))
                throw std::runtime_error("Truncated probe packet payload");
            try {
                NativeXmaCodec codec({channels,rate,XmaVariant::Xma2});
                const auto first=decode(codec,bytes,channels);
                Result again;
                if(restart) {codec.reset();again=decode(codec,bytes,channels);}
                std::cout<<id<<"\tOK\t"<<first.frames<<"\t"<<first.hash<<"\t"
                         <<again.frames<<"\t"<<again.hash<<"\t\n";
            } catch(const std::exception& error) {
                std::cout<<id<<"\tFAIL\t0\t0\t0\t0\t"<<singleLine(error.what())<<'\n';
            }
        }
        if(input.peek()!=std::char_traits<char>::eof())
            throw std::runtime_error("Trailing probe manifest bytes");
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"Resident decode probe failed: "<<error.what()<<'\n';
        return 1;
    }
}
