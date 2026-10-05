#include "audio/ea_xma_block.h"
#include "audio/menu_xma_certificates.h"
#include "audio/native_xma_codec.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// Offline only. Exact runtime framing/parser and real codec, no output endpoint,
// XmaSource quota/ring emulation, invented EOF, encoded-length trim or PCM file.
namespace {
using namespace Simpsons::Audio;
void need(bool ok,const char* reason) {if(!ok) throw std::runtime_error(reason);}
void exact(std::istream& input,void* data,size_t count) {
    need(bool(input.read(static_cast<char*>(data),std::streamsize(count))),"Truncated manifest/source");
}
uint32_t word(std::istream& input) {
    std::array<uint8_t,4> b{};exact(input,b.data(),b.size());
    return uint32_t(b[0])|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24;
}
uint64_t wide(std::istream& input) {const uint64_t low=word(input);return low|(uint64_t(word(input))<<32);}
std::string hex(std::span<const uint8_t> bytes) {
    std::string output;output.reserve(2*bytes.size());
    for(auto b:bytes) {output+="0123456789abcdef"[b>>4];output+="0123456789abcdef"[b&15];}
    return output;
}
std::string clean(std::string value) {for(char& c:value) if(c=='\t'||c=='\r'||c=='\n')c=' ';return value;}
struct Sha256 {
    BCRYPT_HASH_HANDLE handle{};
    Sha256() {need(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,&handle,nullptr,0,nullptr,0,0)>=0,"SHA256 create failed");}
    ~Sha256() {if(handle)BCryptDestroyHash(handle);}
    void add(std::span<const uint8_t> bytes) {need(BCryptHashData(handle,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),0)>=0,"SHA256 update failed");}
    std::array<uint8_t,32> finish() {std::array<uint8_t,32> digest{};need(BCryptFinishHash(handle,digest.data(),ULONG(digest.size()),0)>=0,"SHA256 finish failed");return digest;}
};
struct File {
    std::string name;
    std::ifstream input;
    std::array<uint8_t,32> hash{};
    uint64_t bytes{};
    void select(const std::string& path,uint64_t expectedBytes,const std::array<uint8_t,32>& expectedHash) {
        if(name==path) {need(bytes==expectedBytes && hash==expectedHash,"Repeated source identity changed");return;}
        input.close();input.clear();input.open(std::filesystem::path(std::u8string(path.begin(),path.end())),std::ios::binary);
        need(bool(input),"Source open failed");Sha256 digest;std::array<uint8_t,65536> buffer{};bytes=0;
        while(input.read(reinterpret_cast<char*>(buffer.data()),buffer.size()) || input.gcount()) {
            const size_t count=size_t(input.gcount());digest.add(std::span(buffer).first(count));bytes+=count;
        }
        need(input.eof(),"Source hash read failed");hash=digest.finish();
        need(bytes==expectedBytes && hash==expectedHash,"Original source SHA256/extent changed");
        name=path;input.clear();
    }
    std::vector<uint8_t> read(uint64_t at,uint32_t count) {
        need(at<=bytes && count<=bytes-at,"Source extent outside verified file");
        input.clear();input.seekg(std::streamoff(at));need(bool(input),"Source seek failed");
        std::vector<uint8_t> result(count);exact(input,result.data(),count);return result;
    }
};
struct Block {
    uint64_t offset{};uint32_t bytes{},frames{},flag{},padding{};
    std::array<uint8_t,32> rawHash{},normalizedHash{};
    std::array<uint32_t,3> payload{},ff{};
};
struct Layer {
    std::unique_ptr<NativeXmaCodec> codec;
    uint64_t frames{},packets{},hash=14695981039346656037ull,segmentFrames{};
    void mix(uint32_t value) {for(unsigned b=0;b<4;++b) {hash^=uint8_t(value>>(8*b));hash*=1099511628211ull;}}
    uint64_t drain(uint32_t channels) {
        uint64_t gained=0;std::array<float,1024> output{};
        for(unsigned attempt=0;attempt<4096;++attempt) {
            const uint32_t count=codec->read(std::span(output).first(512*channels));
            if(!count) return gained;
            need(count<=512,"Codec exceeded destination extent");
            frames+=count;segmentFrames+=count;gained+=count;
            for(size_t i=0;i<size_t(count)*channels;++i) mix(std::bit_cast<uint32_t>(output[i]));
        }
        throw std::runtime_error("Codec drain exceeded progress bound");
    }
    void feed(const XmaSource::LayerInput& input,uint32_t channels) {
        for(const auto& packet:input.packets) {
            bool accepted=false;
            for(unsigned retry=0;retry<128;++retry) {
                if(codec->send(packet)==PacketResult::Accepted) {accepted=true;++packets;break;}
                need(drain(channels)!=0,"Codec backpressure without progress");
            }
            need(accepted,"Codec submission exceeded progress bound");drain(channels);
        }
        drain(channels);
    }
};
template<class Select> std::string layerValues(const std::vector<Layer>& layers,Select select) {
    std::string value;for(const auto& layer:layers) {if(!value.empty())value+=',';value+=std::to_string(select(layer));}return value;
}
}
int main(int argc,char** argv) {
    try {
        need(argc==2,"Provide one stream manifest");std::ifstream input(argv[1],std::ios::binary);need(bool(input),"Manifest open failed");
        std::array<char,8> magic{};exact(input,magic.data(),magic.size());
        need(magic==std::array<char,8>{'S','D','E','C','0','0','0','1'},"Unknown stream manifest");
        const uint32_t count=word(input);need(count && count<=100000,"Manifest stream count outside bound");File file;
        std::cout<<"id\tstatus\tblocks\tpackets\traw_frames\traw_hashes\tminimum_segment_surplus\tmaximum_block_frames\tmilliseconds\treason\n";
        for(uint32_t index=0;index<count;++index) {
            const uint32_t id=word(input),channels=word(input),rate=word(input),loop=word(input),blockCount=word(input);
            const uint64_t headerOffset=wide(input),fileBytes=wide(input);std::array<uint8_t,8> header{};exact(input,header.data(),header.size());
            std::array<uint8_t,32> sourceHash{};exact(input,sourceHash.data(),sourceHash.size());
            const uint32_t pathBytes=word(input);need(pathBytes && pathBytes<32768,"Manifest path bound");std::string path(pathBytes,' ');exact(input,path.data(),pathBytes);
            need(id==index && (channels==1||channels==2||channels==4||channels==6) && rate==48000 && loop<=1 && blockCount && blockCount<=100000,"Manifest stream profile outside bound");
            std::vector<Block> blocks(blockCount);
            for(auto& block:blocks) {
                block.offset=wide(input);block.bytes=word(input);block.frames=word(input);block.flag=word(input);block.padding=word(input);
                exact(input,block.rawHash.data(),32);exact(input,block.normalizedHash.data(),32);
                for(auto& p:block.payload)p=word(input);for(auto& p:block.ff)p=word(input);
                need(block.bytes>=12 && block.bytes<=1024*1024 && block.frames && block.frames<=65536 && (block.flag==0||block.flag==128),"Manifest block bounds");
            }
            const auto start=std::chrono::steady_clock::now();std::vector<Layer> layers;
            int64_t minimumSurplus=INT64_MAX;uint64_t maxBlockFrames=0;uint32_t finished=0;std::string status="OK",reason;
            try {
                file.select(path,fileBytes,sourceHash);const auto originalHeader=file.read(headerOffset,8);
                need(std::equal(header.begin(),header.end(),originalHeader.begin()),"Original stream header changed");
                const uint32_t layerCount=(channels+1)/2;layers.resize(layerCount);
                for(uint32_t i=0;i<layerCount;++i) layers[i].codec=std::make_unique<NativeXmaCodec>(XmaFormat{std::min(2u,channels-2*i),rate,XmaVariant::Xma2});
                uint64_t declaredSegment=0;unsigned segments=0;
                for(uint32_t b=0;b<blockCount;++b) {
                    const auto& block=blocks[b];
                    if(b==1 && loop) {need(blocks[0].frames==1 && blocks[0].flag==128,"Loop introduction differs");for(auto& layer:layers){layer.codec->reset();layer.segmentFrames=0;}declaredSegment=0;}
                    auto bytes=file.read(block.offset,block.bytes);Sha256 digest;digest.add(bytes);need(digest.finish()==block.rawHash,"Original block SHA256 changed");
                    need(bytes[0]==block.flag,"Original block flag changed");bytes[0]&=0x7f;
                    const auto normalizedHex=hex(block.normalizedHash);
                    const EaXmaCertificate certificate{normalizedHex,block.bytes,block.frames,block.payload,block.ff,block.padding};
                    const auto parsed=parseEaXmaBlockCertified(bytes,channels,certificate);need(parsed.layers.size()==layers.size(),"Parser layer count differs");
                    for(uint32_t i=0;i<layerCount;++i) {const auto before=layers[i].frames;layers[i].feed(parsed.layers[i],std::min(2u,channels-2*i));maxBlockFrames=std::max(maxBlockFrames,layers[i].frames-before);}
                    ++finished;declaredSegment+=block.frames;
                    if(block.flag==128) {
                        ++segments;for(const auto& layer:layers) minimumSurplus=std::min(minimumSurplus,int64_t(layer.segmentFrames)-int64_t(declaredSegment)-384);
                        need(b+1==blockCount || loop && b==0,"Unexpected interior segment boundary");
                    }
                }
                need(segments==(loop?2u:1u) && blocks.back().flag==128,"Incomplete original segment sequence");
                need(minimumSurplus>=0,"Raw segment frames cannot cover declared extent plus original initial skip");
            } catch(const std::exception& error) {status="FAIL";reason=clean(error.what());}
            const auto milliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
            std::cout<<id<<'\t'<<status<<'\t'<<finished<<'\t'<<layerValues(layers,[](const Layer& l){return l.packets;})<<'\t'
                <<layerValues(layers,[](const Layer& l){return l.frames;})<<'\t'<<layerValues(layers,[](const Layer& l){return l.hash;})<<'\t'
                <<minimumSurplus<<'\t'<<maxBlockFrames<<'\t'<<milliseconds<<'\t'<<reason<<'\n'<<std::flush;
        }
        need(input.peek()==std::char_traits<char>::eof(),"Trailing manifest bytes");return 0;
    } catch(const std::exception& error) {std::cerr<<"Stream probe failed: "<<error.what()<<'\n';return 1;}
}
