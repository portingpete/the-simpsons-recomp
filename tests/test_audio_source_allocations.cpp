// Host regression for the audio owner's free check: "does a live source still decode from
// this allocation generation?" must answer exactly what the scan of every live source
// answered, for any sequence of publications (including repeated keys and shared banks)
// and retirements (including absent keys).
#include "runtime/audio_source_allocations.h"
#include <cstdio>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>
using Simpsons::AudioSourceAllocations;
namespace {
void need(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
struct Bank {uint64_t generation;};
struct Amx {uint64_t generation;};
struct Source {
    std::shared_ptr<const Bank> bank;
    std::shared_ptr<const Amx> amx;
};
using Sources=AudioSourceAllocations::Sources<Source>;
// The check the owner made before the index existed (one require per live source).
bool scanDecodesFrom(const Sources& sources,uint64_t generation) {
    for(const auto& [key,source]:sources)
        if(!((!source->bank||source->bank->generation!=generation)&&(!source->amx||source->amx->generation!=generation)))
            return true;
    return false;
}
void agree(const Sources& sources,const AudioSourceAllocations& index,uint64_t generations,const char* why) {
    for(uint64_t generation=0;generation<generations;++generation)
        need(index.decodesFrom(generation)==scanDecodesFrom(sources,generation),why);
}
}
int main() {
    try {
        // Shared banks: two sources from one bank keep it live until both retire.
        {
            Sources sources;AudioSourceAllocations index;
            const auto bank=std::make_shared<const Bank>(Bank{7});
            index.publish(sources,1,std::make_shared<Source>(Source{bank,nullptr}));
            index.publish(sources,2,std::make_shared<Source>(Source{bank,nullptr}));
            need(index.decodesFrom(7)&&!index.decodesFrom(8),"a published bank generation was not found");
            index.retire(sources,1);
            need(index.decodesFrom(7),"retiring one of two sources released their shared bank");
            index.retire(sources,2);
            need(!index.decodesFrom(7)&&index.size()==0&&sources.empty(),"retiring both sources kept the bank generation");
        }
        // A repeated key keeps the existing receipt (emplace semantics); its replacement is not counted.
        {
            Sources sources;AudioSourceAllocations index;
            const auto first=std::make_shared<Source>(Source{std::make_shared<const Bank>(Bank{3}),nullptr});
            index.publish(sources,10,first);
            index.publish(sources,10,std::make_shared<Source>(Source{nullptr,std::make_shared<const Amx>(Amx{4})}));
            need(sources.at(10)==first&&index.decodesFrom(3)&&!index.decodesFrom(4),"a repeated key replaced or counted a second receipt");
            index.retire(sources,11); // Absent key: no change.
            need(sources.size()==1&&index.decodesFrom(3),"retiring an absent key changed the index");
            index.retire(sources,10);
            need(!index.decodesFrom(3)&&!index.decodesFrom(4)&&index.size()==0,"retirement left a stale generation");
        }
        // Streamed sources (no bank, no AMX) never hold an allocation generation.
        {
            Sources sources;AudioSourceAllocations index;
            index.publish(sources,0x82000000u,std::make_shared<Source>());
            agree(sources,index,4,"a streamed source changed the free check");
            need(index.size()==0,"a streamed source was counted");
        }
        // Randomized publish/retire sequences over a small generation pool (many shared banks,
        // sources with both a bank and an AMX payload, repeated and absent keys).
        {
            std::mt19937 random(0xA0D10);
            constexpr uint64_t generations=24,keys=64;
            std::vector<std::shared_ptr<const Bank>> banks;std::vector<std::shared_ptr<const Amx>> payloads;
            for(uint64_t generation=0;generation<generations;++generation) {
                banks.push_back(std::make_shared<const Bank>(Bank{generation}));
                payloads.push_back(std::make_shared<const Amx>(Amx{generation}));
            }
            for(unsigned round=0;round<200;++round) {
                Sources sources;AudioSourceAllocations index;
                for(unsigned step=0;step<400;++step) {
                    const uint64_t key=random()%keys;
                    if(random()%3) {
                        auto source=std::make_shared<Source>();
                        const unsigned kind=random()%4;
                        if(kind&1)source->bank=banks[random()%generations];
                        if(kind&2)source->amx=payloads[random()%generations];
                        index.publish(sources,key,source);
                    } else index.retire(sources,key);
                    agree(sources,index,generations,"indexed free check differs from a scan of every live source");
                }
                for(uint64_t key=0;key<keys;++key)index.retire(sources,key);
                need(sources.empty()&&index.size()==0,"retiring every source left counted generations");
            }
        }
        std::puts("PASS audio source allocations: indexed free check equals a scan of every live source");
        return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
