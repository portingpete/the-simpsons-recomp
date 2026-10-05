#include "runtime/rigid_packet_owner.h"
#include "runtime/skin_profile.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {
size_t checks{};
void need(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
struct Fixture {
    uint32_t packet=0x1000,typed=0x2000;
    Simpsons::RigidPacketRecord record{0x00500031,0x8205D2D8,0x4000,0x3000,0x00900001};
    std::map<uint32_t,uint32_t> memory{
        {0x1018,0x2000},{0x1014,0x00900001},{0x2000,0x820616C0},
        {0x2010,0x4000},{0x2018,0x3000},{0x201C,0x00500031}};
    std::vector<uint32_t> reads;
    Simpsons::RigidPacketOwner resolve() {
        return Simpsons::resolveRigidPacketOwner(packet,[&](uint32_t address){reads.push_back(address);return memory.at(address);},
            [&](uint32_t identity){if(identity!=record.identity)throw std::runtime_error("unknown identity");return record;});
    }
};
void rejected(Fixture fixture,const char* step) {
    try {fixture.resolve();}catch(const std::runtime_error& error){
        need(std::string(error.what()).find(std::string("step=")+step)!=std::string::npos,"Incorrect failed owner step");
        need(std::string(error.what()).find("packet=")!=std::string::npos,"Missing packet evidence");return;
    }
    throw std::runtime_error("Invalid owner was accepted");
}
}
int main() try {
    Fixture fixture;const auto owner=fixture.resolve();
    need(owner.packet==fixture.packet&&owner.typed==fixture.typed,"Packet/typed not retained");
    need(owner.source==0x8205D2D8&&owner.identity==0x00500031,"Wrong canonical source");
    need(owner.context==fixture.record.context&&owner.wrapper==fixture.record.wrapper,"Wrong context/wrapper");
    // No wrapper memory is supplied: source lookup uses typed+1C and the live
    // registry, never a previous selection or the diagnostic's old meta+1C.
    need(fixture.reads.size()==6,"Unexpected guest reads");
    for(auto address:fixture.reads)need(address<0x3000,"Source lookup dereferenced mutable wrapper storage");
    for(auto source:{0x8200CCB8u,0x820168F8u,0x8202AD78u,0x82019988u,0x820547E8u,0x82057E08u,0x82036448u,0x8205D2D8u,0x820465E8u,0x82051808u}) {
        Fixture f;f.record.source=source;need(f.resolve().source==source,"Rigid family source was changed");
    }
    {Fixture f;f.record.source=0x82006348;f.memory[0x2000]=0x82061714;f.memory[0x1014]=0;
        const auto skin=f.resolve();need(skin.source==0x82006348,"Skin vtable was not admitted");
        need(!skin.packetContext&&skin.context==f.record.context,"Skin zero packet context did not use the live creation context");}
    {Fixture f;f.record.source=0x82006348;f.memory[0x2000]=0x82061714;
        const auto skin=f.resolve();need(skin.packetContext==f.record.context&&skin.context==f.record.context,
            "Skin matching packet context was not retained");}
    {Fixture f;f.record.source=0x82006348;f.memory[0x2000]=0x82061714;f.memory[0x1014]=0x00900002;rejected(f,"record.association");}
    {Fixture f;f.record.source=0x82006348;f.memory[0x2000]=0x82061714;f.memory[0x1014]=0;f.record.context=0;rejected(f,"record.association");}
    for(uint32_t r4=0;r4<4;++r4)for(uint32_t r5=0;r5<4;++r5)
        need(Simpsons::rigidFallbackArgumentsQualified(0x8200CCB8u,r4,r5)==
            (r4<=1&&r5<=1),"CCB8 fallback rejected an original Boolean pair or admitted a non-Boolean selector");
    for(auto source:{0x8205D2D8u,0x82036448u,0x82006348u,0u})
        for(uint32_t r4=0;r4<4;++r4)for(uint32_t r5=0;r5<4;++r5)
            need(Simpsons::rigidFallbackArgumentsQualified(source,r4,r5)==
                ((source==0x8205D2D8u||source==0x82036448u)?(r4<=1&&r5<=1):(!r4&&!r5)),"Fallback register admission widened");
    // The original dispatcher derives both entry values as Boolean metadata
    // flags. UV supports both techniques; entry r5 is overwritten with the
    // packet's object before its first use in the real fallback function.
    for(uint32_t r4=0;r4<4;++r4)for(uint32_t r5=0;r5<4;++r5)
        need(Simpsons::rigidFallbackArgumentsQualified(0x820465E8u,r4,r5)==(r4<=1&&r5<=1),
             "UV fallback rejected a Boolean selector or admitted an invalid value");
    // Completion's ordinary dual material has a different canonical source
    // and private bank from animated UV. The genuine dispatcher supplies 1/1
    // for its alpha packet and 0/1 when packet alpha eligibility is cleared.
    for(uint32_t r4=0;r4<4;++r4)for(uint32_t r5=0;r5<4;++r5)
        need(Simpsons::rigidFallbackArgumentsQualified(0x8202AD78u,r4,r5)==(r4<=1&&r5<=1),
             "Ordinary dual fallback rejected a Boolean selector or admitted an invalid value");
    {Fixture f;f.record.source=0x8202AD78;f.record.identity=0x00500024;f.memory[0x201C]=f.record.identity;
        const auto dual=f.resolve();need(dual.source==0x8202AD78&&dual.identity==0x00500024,
            "Ordinary dual packet was resolved through a previous selected material");}
    {Fixture f;f.record.source=0x8202AD78;f.memory[0x2000]=0x82061714;rejected(f,"typed.vtable-profile");}
    {const auto alpha=Simpsons::rigidAlphaPass(0x8202AD78);need(alpha.vertex==0x8202B884&&alpha.pixel==0x8202C3BC&&
        alpha.context==0x2C30&&alpha.samplers==6,"Ordinary dual alpha profile changed");}
    // These separate original owners also expose both opaque and alpha passes.
    // Do not infer their source from a prior selection or reuse opaque shader
    // addresses when the genuine dispatcher supplies an alpha selector.
    for(const auto source:{0x82019988u,0x820547E8u,0x82057E08u}) {
        for(uint32_t r4=0;r4<4;++r4)for(uint32_t r5=0;r5<4;++r5)
            need(Simpsons::rigidFallbackArgumentsQualified(source,r4,r5)==(r4<=1&&r5<=1),
                 "Rigid family fallback rejected a Boolean selector or admitted an invalid value");
        need(Simpsons::isAlphaBeginSource(source),"Rigid family did not admit its distinct alpha begin");
        Fixture f;f.record.source=source;
        f.record.identity=source==0x82019988?0x00500025u:(source==0x820547E8?0x0050002Eu:0x0050002Fu);
        f.memory[0x201C]=f.record.identity;const auto owned=f.resolve();
        need(owned.source==source&&owned.identity==f.record.identity,"Rigid family source followed a previous selection");
        f.memory[0x2000]=0x82061714;rejected(f,"typed.vtable-profile");
    }
    {const auto alpha=Simpsons::rigidAlphaPass(0x82019988);need(alpha.vertex==0x8201A430&&alpha.pixel==0x8201B0BC&&
        alpha.context==0x2DF0&&alpha.samplers==6,"Gloss alpha profile changed");}
    {const auto alpha=Simpsons::rigidAlphaPass(0x820547E8);need(alpha.vertex==0x82055440&&alpha.pixel==0x820560B8&&
        alpha.context==0x3040&&alpha.samplers==6,"Multitone alpha profile changed");}
    {const auto alpha=Simpsons::rigidAlphaPass(0x82057E08);need(alpha.vertex==0x820589EC&&alpha.pixel==0x82059880&&
        alpha.context==0x3450&&alpha.samplers==6,"Normalmap alpha profile changed");}
    {Fixture f;f.record.source=0x820465E8;f.record.identity=0x0050002C;f.memory[0x201C]=f.record.identity;
        const auto uv=f.resolve();need(uv.source==0x820465E8&&uv.identity==0x0050002C,
            "UV packet was resolved through a previous selected material");}
    {Fixture f;f.record.source=0x820465E8;f.memory[0x2000]=0x82061714;rejected(f,"typed.vtable-profile");}
    {Fixture f;f.record.source=0x82042F58;f.record.identity=0x0050002A;f.memory[0x201C]=f.record.identity;
        const auto uv=f.resolve();need(uv.source==0x82042F58&&uv.identity==0x0050002A,
            "Single UV packet was resolved through a prior dual UV material");
        for(uint32_t r4=0;r4<4;++r4)for(uint32_t r5=0;r5<4;++r5)
            need(Simpsons::rigidFallbackArgumentsQualified(0x82042F58,r4,r5)==(r4<=1&&r5<=1),
                "Single UV fallback rejected an original Boolean pair or admitted a non-Boolean selector");
        f.memory[0x2000]=0x82061714;rejected(f,"typed.vtable-profile");}
    {Fixture f;f.record.source=0x82039208;f.record.identity=0x00500028;f.memory[0x201C]=f.record.identity;
        const auto flipbook=f.resolve();need(flipbook.source==0x82039208&&flipbook.identity==0x00500028,
            "Flipbook packet was resolved through a previous UV material");
        for(uint32_t r4=0;r4<4;++r4)for(uint32_t r5=0;r5<4;++r5)
            need(Simpsons::rigidFallbackArgumentsQualified(0x82039208,r4,r5)==(r4<=1&&r5<=1),
                "Flipbook fallback rejected a Boolean pair or admitted a non-Boolean selector");
        f.memory[0x2000]=0x82061714;rejected(f,"typed.vtable-profile");}
    {const auto alpha=Simpsons::skinAlphaPass(0x82006348);need(alpha.vertex==0x82008E20&&alpha.pixel==0x8200A4A4&&alpha.context==0x5F60&&alpha.samplers==6,
        "Base skin alpha profile changed");}
    {const auto alpha=Simpsons::skinAlphaPass(0x8201CD48);need(alpha.vertex==0x8201F984&&alpha.pixel==0x82021344&&alpha.context==0x66C0&&alpha.samplers==6,
        "Dual skin alpha profile changed");}
    const std::pair<uint32_t,const char*> fields[]={{0x1018,"packet.typed"},{0x1014,"packet.context"},
        {0x2000,"typed.vtable"},{0x2010,"typed.manager"},{0x2018,"typed.wrapper"},{0x201C,"typed.identity"}};
    for(const auto& [address,step]:fields){Fixture f;f.memory.erase(address);rejected(f,step);}
    {Fixture f;f.packet=0;rejected(f,"packet.typed");}
    {Fixture f;f.packet=0xFFFFFFF0;rejected(f,"packet.typed");}
    {Fixture f;f.memory[0x1018]=0;rejected(f,"typed.vtable");}
    {Fixture f;f.memory[0x1018]=0xFFFFFFFC;f.memory[0xFFFFFFFC]=0x820616C0;rejected(f,"typed.manager");}
    {Fixture f;f.memory[0x201C]=0x00500027;rejected(f,"record.lookup");}
    for(auto address:{0x1014u,0x2010u,0x2018u}){Fixture f;f.memory[address]=0;rejected(f,"record.association");}
    {Fixture f;f.record.wrapper+=4;rejected(f,"record.association");}
    {Fixture f;f.record.manager+=4;rejected(f,"record.association");}
    {Fixture f;f.record.context+=1;rejected(f,"record.association");}
    {Fixture f;f.record.source=0;rejected(f,"record.association");}
    {Fixture f;f.memory[0x2000]=0x82061714;rejected(f,"typed.vtable-profile");}
    {Fixture f;f.record.source=0x82006348;f.memory[0x1014]=0;rejected(f,"typed.vtable-profile");}
    {Fixture f;f.memory[0x2000]=0;rejected(f,"typed.vtable-profile");}
    {Fixture f;f.record.source=0x8205B848;f.memory[0x2000]=0x82061758;
        need(f.resolve().source==0x8205B848,"VFX rigid owner was routed as a lit rigid effect");}
    {Fixture f;f.record.source=0x8205B848;rejected(f,"typed.vtable-profile");}
    {Fixture f;f.memory[0x2000]=0x82061758;rejected(f,"typed.vtable-profile");}
    std::printf("PASS rigid packet ownership: %zu checks (synthetic checked reads; no game execution)\n",checks);return 0;
} catch(const std::exception& error) {
    std::fprintf(stderr,"FAIL rigid packet ownership after %zu checks: %s\n",checks,error.what());return 1;
}
