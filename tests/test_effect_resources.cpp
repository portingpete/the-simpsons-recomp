// Standalone pure-CPU tests. Supply the original analysis/simpsons.pe as argv[1].
// Goldens come from the completed offline catalog, not the production generator.
// No SDK execution, shader instruction decoding, compiler, device, or rendering.
#include "renderer/effect_resources.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <set>
#include <type_traits>
#include <utility>

using namespace Simpsons::Graphics;
namespace {
constexpr uint32_t imageBase=0x82000000;
constexpr size_t imageBytes=15466496;
size_t checks=0;
std::string_view currentCase="startup";

void require(bool ok,const char* message) {
    ++checks;
    if(!ok) throw std::runtime_error(std::string(currentCase)+": "+message);
}
template<class F> void rejects(F&& action,const char* reason,
                              std::initializer_list<std::string_view> categories={}) {
    ++checks;
    try {action();}
    catch(const EffectError& error) {
        const std::string_view message=error.what();
        if(categories.size()) {
            const bool expected=std::any_of(categories.begin(),categories.end(),
                [&](auto category){return message.starts_with(category);});
            if(!expected) throw std::runtime_error(std::string(currentCase)+": "+reason+
                " rejected in the wrong category: "+error.what());
        }
        return;
    }
    throw std::runtime_error(std::string(currentCase)+": accepted "+reason);
}
uint32_t be32(std::span<const uint8_t> bytes,size_t at) {
    require(at<=bytes.size() && bytes.size()-at>=4,"Test word outside bounded input");
    return uint32_t(bytes[at])<<24 | uint32_t(bytes[at+1])<<16 |
           uint32_t(bytes[at+2])<<8 | uint32_t(bytes[at+3]);
}
void put32(std::vector<uint8_t>& bytes,size_t at,uint32_t value) {
    for(size_t i=0;i<4;++i) bytes.at(at+i)=uint8_t(value>>(24-8*i));
}
bool equal(std::span<const uint8_t> a,std::span<const uint8_t> b) {
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin());
}

// Regression fingerprints of decoded metadata, not cryptographic identities.
// Scalars serialize as four BE bytes; strings as byte length followed by bytes.
// Constructor envelope SHA enforcement is tested separately with changed bytes.
struct Fingerprint {
    uint64_t value=14695981039346656037ull;
    void byte(uint8_t b) {value=(value^b)*1099511628211ull;}
    void word(uint32_t w) {for(unsigned i=0;i<4;++i) byte(uint8_t(w>>(24-8*i)));}
    void text(std::string_view s) {word(uint32_t(s.size()));for(unsigned char b:s) byte(b);}
};
uint64_t parameterFingerprint(std::span<const EffectParameter> parameters) {
    Fingerprint f;f.word(uint32_t(parameters.size()));
    for(const auto& p:parameters) {
        f.text(p.name);f.word(p.handle);f.word(p.descriptorWords[0]);f.word(p.descriptorWords[1]);
    }
    return f.value;
}
uint64_t techniqueFingerprint(std::span<const EffectTechnique> techniques) {
    Fingerprint f;f.word(uint32_t(techniques.size()));
    for(const auto& t:techniques) {
        f.text(t.name);f.word(t.handle);f.word(t.passHandle);f.word(t.contextOffset);
        f.word(t.vertexShaderAddress);f.word(t.pixelShaderAddress);
        f.word(uint32_t(t.scalars.size()));
        for(const auto& s:t.scalars) {f.word(s.sdkId);f.word(s.value);}
        f.word(uint32_t(t.samplers.size()));
        for(const auto& s:t.samplers) {f.word(s.stage);f.word(s.sdkId);f.word(s.value);}
    }
    return f.value;
}
uint64_t shaderFingerprint(std::span<const EffectShader> shaders) {
    Fingerprint f;f.word(uint32_t(shaders.size()));
    for(const auto& s:shaders) {
        f.word(s.originalAddress);f.word(s.bodyOffset);f.word(s.recordBytes);
        f.word(s.stage==MaterialStage::Vertex?0u:1u);
    }
    return f.value;
}

struct Golden {
    uint32_t address,bytes;
    std::string_view name,sha256;
    uint32_t techniques,shaders,privateNames,sharedNames,privateOffset,privateBytes,sharedOffset,sharedBytes;
    uint32_t cacheBytes,scalars,samplers;
    uint64_t techniquesFingerprint,privateFingerprint,sharedFingerprint,shadersFingerprint;
};
// analysis/native-effect-catalog.json SHA256:
// 031241d1bff10c771433ce0c5154c05a93628a4b83b114c0d0bfcfdadea2a43b
constexpr Golden golden[]={
    {0x820B8AA0,0xCAC,"fourtapblend","18f58d1e2c0b74fa86a73abe2dce583baf131583f328a044fe6cd2095378cee9",
     1,2,2,0,976,80,0,0,200,8,5,
     0x1F68ACAD6EF6D25Eull,0x213D5FC1BC14F081ull,0x4D25767F9DCE13F5ull,0x6A67587D69F6692Eull}, // row 0
    {0x820D5730,0x47B0,"littextured","38972d41a498c52988afc803454fb6830086e3ec8a3cac966ad2a3705ee63fd9",
     2,4,10,11,1232,656,17760,320,312,6,12,
     0xDAE29957848639C4ull,0x7F817FF5CBD4927Bull,0xD4454B29D2020CE5ull,0x1122CE771EDC3551ull}, // row 1
    {0x820B9750,0x1220,"particles","313533556039f794c9e705a53e95005e5d05cfff40e62d091c8a56384c4798a3",
     1,2,7,4,1008,256,4432,112,120,0,6,
     0x7CE08FB3DAD5C77Cull,0xD925AC3924B5405Cull,0x17BFDC5C885A3B4Eull,0x3E6380D0883C192Full}, // row 2
    {0x820BA970,0x5BDC,"quad","b34fd55e3d5abef878c775718455c3147387ce1bd4c1b6e32cdc0e8942355a30",
     17,34,8,0,992,176,0,0,3644,99,128,
     0x1A55B26AEA89B8C3ull,0x3A89388DF917A908ull,0x4D25767F9DCE13F5ull,0x40910FB4B7B7A3BEull}, // row 3
    {0x820C0550,0xFA60,"shadows","71fbbfdbc2027a8407f1cb12d73612d4dd6bda87253f381f73b200ea02d4fde5",
     8,13,15,11,1584,4464,63504,320,1224,22,48,
     0xF961D8B45511A481ull,0x3CB72B7F1737F9ABull,0xD4454B29D2020CE5ull,0xFA27EF87F5F14AE2ull}, // row 4
    {0x820FE8F0,0x8510,"skinned_lit","4a06ddf6be80e5a8eeb0d9b397357f4a42369d4d5eecdfa385e2bdf364a5cf29",
     2,4,11,11,1744,4704,33472,320,492,5,24,
     0x4501FE39A85367F2ull,0x01367D654520167Bull,0xD4454B29D2020CE5ull,0xC217926D0482F3B9ull}, // row 5
    {0x82140AB0,0x8630,"skinned_lit_tint_color","4686034fee5f5e963f43c0892e35565bd574cdba93fc7b9ea78f5fef684411cc",
     2,4,11,11,1744,4704,33760,320,492,5,24,
     0xD0AF730CDD4FAD5Bull,0x01367D654520167Bull,0xD4454B29D2020CE5ull,0xD911E635B05DF65Full}, // row 6
    {0x82110C70,0xA300,"skinned_bruise","60210d069f2cf5a863b77fbea2af8c8b6f3caf6ba05decb8faf96c5c015bcddc",
     2,4,18,11,1808,4816,41136,320,1068,5,60,
     0x793BC745D4D7AA33ull,0x2F821AB927FE70BEull,0xD4454B29D2020CE5ull,0x52659C92FDBDF1FDull}, // row 7
    {0x82106E00,0x9E70,"skinned_spec","d2f49dcb59ae37c37f0bdfe763ccfffacc67e0af1d8809b2d847560d28ea6a1f",
     2,4,15,11,1776,4768,39968,320,876,5,48,
     0xCE65B2EF6E00512Eull,0x4E1DD7815CAF0250ull,0xD4454B29D2020CE5ull,0xDFD291F850644B1Eull}, // row 8
    {0x820F9350,0x55A0,"skinned_unlit","aee4d143a0be467b081e5871b02706790d00b77d789c24cf1ea25a0a4eb36c8c",
     2,4,9,4,1536,4368,21712,112,324,7,12,
     0xA19190D8F613C98Aull,0x609CCDCBB2A33136ull,0x17BFDC5C885A3B4Eull,0xAB592BD220E6ADA1ull}, // row 9
    {0x820CFFB0,0x5780,"unlittextured","49af618ef58d554f57015a7ce9d3325f086b0774b9042664ae8c63bb09c5c1cf",
     2,4,10,4,1552,4384,22192,112,336,8,12,
     0xC544A21B4F76A8A2ull,0x5D988634B1A34BF4ull,0x17BFDC5C885A3B4Eull,0xCE706AA6A38A4992ull}, // row 10
    {0x820D9EE0,0x4BB0,"terrain","f1866e81bb1592bc1deb1318c881f6bd544339a99ae2347d52869fc048d81b93",
     2,4,18,11,1296,928,18784,320,1548,5,90,
     0x555852AF3D7B5B2Cull,0xC1055C9FE2F30343ull,0xD4454B29D2020CE5ull,0xAAE10257EB66DF03ull}, // row 11
    {0x820DEA90,0x4080,"nrmmapnoskin","609ed1decb49f89e5ac275a879e4feed46f9e9b35a240f3d7f17467b790e8ca1",
     2,4,13,11,1248,800,15920,320,1504,4,88,
     0x9B2B4178CFC9031Cull,0x25646EDA71C7AA64ull,0xD4454B29D2020CE5ull,0xEA9E7451764CBAADull}, // row 12
    {0x820E2B10,0x47F0,"road","8b4ae25cd2fd5d78f66168e11b61bd5b505f889a03db3e4efc1f2f8c979822e8",
     2,4,17,11,1280,960,17824,320,1888,4,112,
     0x37619D5E88B2D202ull,0xBC15EF548CD83FF6ull,0xD4454B29D2020CE5ull,0xE75CB1AE3FE6E27Bull}, // row 13
    {0x82124790,0x4760,"base_detail","1a50a5b15f2d8b8ed02843ccb75ab5e00dcf753f757eb7e2d3a5d04e4eef172a",
     2,4,16,11,1280,896,17680,320,1696,4,100,
     0x7B405162AA103DDFull,0xF706727C45F33B89ull,0xD4454B29D2020CE5ull,0x4D26E9A334077603ull}, // row 14
    {0x820E7300,0x28D0,"sky","0c5c4c6bd8b2ac3a98f29b548be9fe1d962010fd2acb04cd69a8e61b824934c8",
     1,2,21,11,1312,784,9856,320,344,0,20,
     0x8876B80A2D6527E7ull,0x133CFFE32AFEB3A9ull,0xD4454B29D2020CE5ull,0xDC300AAE4EB0558Aull}, // row 15
    {0x820E9BD0,0x66A0,"simple","d905239e5f8ca1de6b0747d559e37313a3948b3e87fd3b8db9b8b754b89d1a61",
     2,4,14,11,1776,4752,25680,320,324,7,12,
     0xE9FDB611CE1F6E9Aull,0x1003D9922389C411ull,0xD4454B29D2020CE5ull,0x7EE5658B53D2004Cull}, // row 16
    {0x820F0270,0x90E0,"simplelit","e53cd0785f14f3099e29812caca0edb05e45efe01deaa414997d78a0f76e6431",
     2,4,15,11,1776,4768,36496,320,772,7,40,
     0x2B70E1BEAD94EF03ull,0xB9D8A1CF17195289ull,0xD4454B29D2020CE5ull,0xD5A0CB35349628CDull}, // row 17
    {0x8211AF70,0x4510,"water","4417b04e6fa8e1711565a5595eb51ebe3a30f80bb270b8748aa60dc47a4e3f56",
     2,4,18,11,1296,928,17088,320,1120,4,64,
     0xCF49D976B11DC540ull,0x8688BF8643E3A8D4ull,0xD4454B29D2020CE5ull,0x4F8EFB4916AC3174ull}, // row 18
    {0x8211F480,0x5310,"mono","24c7dc906da91b4b89cf0f59dd034c9584005dda68205d13753455cb9c3c68ca",
     2,4,9,4,1536,4368,21056,112,72,2,0,
     0xB9D67C8C6AA7A304ull,0xC273B2770379B1B7ull,0x17BFDC5C885A3B4Eull,0x5C5B142375170B57ull}, // row 19
    {0x82128EF0,0x89E0,"skinned_bruisenospec","37694f3b725f208d1a50480ce8d8a9f0cd1cdb91a6acae981b6dce457bfd4ce7",
     2,4,14,11,1776,4752,34704,320,684,5,36,
     0x27277475C17A46D7ull,0xB9EA5FD793AC5A7Dull,0xD4454B29D2020CE5ull,0x27E97D6AF77118EFull}, // row 20
    {0x82136260,0x46C0,"standardworld","68cf412656afb1b97ab2f0643c3b0b0f3eccf5e369d2bfccdbf1322beaf581cc",
     2,4,15,11,1264,832,17520,320,1260,5,72,
     0xD1C20804C4CB7145ull,0xB63FF634F830B501ull,0xD4454B29D2020CE5ull,0xCF7439E9439166E1ull}, // row 21
    {0x821318D0,0x4990,"dualtexture","31308f08fb73a7a8d13e5981b147fed6c49fe04494bfa418ee707d856d246b4a",
     2,4,11,11,1232,720,18240,320,504,6,24,
     0x49B28E38D1B8B521ull,0xFAB0B2F7EE2AE308ull,0xD4454B29D2020CE5ull,0x3BD29CFDBC24184Eull}, // row 22
    {0x8213A920,0x6190,"carnrmmap","004406a1cf7af65c515886e18346169122349b5fc6a586c0a7757f2b54f486d5",
     2,4,13,11,1248,800,24384,320,1120,4,64,
     0x602483836D4697D3ull,0x25646EDA71C7AA64ull,0xD4454B29D2020CE5ull,0x8D95E035999A511Dull}, // row 23
    {0x821490E0,0x5330,"zprepass","5468a2f63b578bf497431aeede9522730b6a46b10aa65f1e77970289ce264c6b",
     2,2,10,4,1568,4400,21088,112,72,2,0,
     0x393FB72860327A80ull,0x6116068270AABD0Bull,0x17BFDC5C885A3B4Eull,0x80C3C60A9C692159ull}, // row 24
    // Second table82CD1448: local rows0..23 become combined ordinals25..48.
    // analysis/native-post-effect-catalog.json SHA256: 325d8f64aff8e6d260cabecffc06f923cd2c8b426b1bba917bd30e16a1e4338e
    {0x82006348,0x6970,"simpsons_skin","7bdd4625fb9e0cb3348d90935e98fc8576e2395eb49e65f050f7fb764bb9d9a9",
     2,4,16,11,1648,4592,26400,320,192,4,6,
     0x1874B30D69AD3A38ull,0xED05EF0317320C81ull,0xD4454B29D2020CE5ull,0x164B600F91140462ull}, // combined row 25
    {0x8200CCB8,0x2EE0,"simpsons_rigid","99fb9340ac71f857aaa0b84578d01f51dbfd96981c87cb95d9850aa1a97bf812",
     2,4,15,11,1120,544,11408,320,384,4,18,
     0x8ABD9511336849A2ull,0x3C620D9043201297ull,0xD4454B29D2020CE5ull,0x760C1757AC17275Cull}, // combined row 26
    {0x8202DF98,0x1AE0,"simpsons_edge","8dd35cfbee97f5c72d22544d9d575415a97a50a101b47a2a7648ad98317e55c9",
     1,2,16,4,1152,656,6672,112,180,5,6,
     0xED9801406584171Dull,0x330D4D779704F449ull,0x17BFDC5C885A3B4Eull,0xF40DF2DD91FCC904ull}, // combined row 27
    {0x8202FA78,0x1870,"simpsons_aa","1e43e441085ff58eb8f74f2224350e538f082184fb5b9f57250f325b4db7831b",
     1,2,13,4,1152,608,6048,112,168,4,6,
     0xEB209219B618E05Bull,0x23807F8974A0BF8Aull,0x17BFDC5C885A3B4Eull,0x7FF06C95BE4A431Aull}, // combined row 28
    {0x82034008,0x2440,"simpsons_edgeAA","a4ade450a1c7af0292a44548f7181c5a2a5037fdf32a26f266367e1e5878e993",
     1,2,23,4,1136,752,9072,112,552,4,30,
     0x8227AC19AA6C7352ull,0xB6626C820A6DC5BBull,0x17BFDC5C885A3B4Eull,0x7C1812716A9BEE96ull}, // combined row 29
    {0x820312E8,0x1690,"simpsons_aa_row","723d06b47f2884088d352fa8b4e9f85c70666b2edd27c29cbebc7bd99b0b982d",
     1,2,11,4,1104,512,5568,112,168,4,6,
     0x68C7B9E0B89964A0ull,0xE2F82656B43192B4ull,0x17BFDC5C885A3B4Eull,0x9EEBF9BA16B8CBA4ull}, // combined row 30
    {0x82032978,0x1690,"simpsons_aa_col","124606b18dda5c43659d387b4bb0f12f68eb3fc67b44155ed8cc76358c8f4324",
     1,2,11,4,1104,512,5568,112,168,4,6,
     0x51D5ADADCB22C352ull,0xE2F82656B43192B4ull,0x17BFDC5C885A3B4Eull,0x71A6EAC7D3C58562ull}, // combined row 31
    {0x8200FB98,0x6D60,"simpsons_skin_textured","62f63d3eba363db31493a6898639e6de3465d2782582d186fb90d0d9b2e12a1f",
     2,4,16,11,1648,4592,27408,320,384,4,18,
     0x66AEF536A4714FF5ull,0x705E210E945243D7ull,0xD4454B29D2020CE5ull,0x8E61FBD1B91D28CDull}, // combined row 32
    {0x820168F8,0x3090,"simpsons_rigid_textured","20213b2d1c5c1c8e7dfce827e513cf236f928d7fd1808f91afec4cacaf21ca01",
     2,4,16,11,1136,560,11840,320,480,4,24,
     0x2A60C18A0870AB52ull,0x405C4E46CD225993ull,0xD4454B29D2020CE5ull,0x24D7C9088193FDB6ull}, // combined row 33
    {0x8201CD48,0x7110,"simpsons_skin_dualtextured","9fbe1c024716b82ecdf94cb79121f468663cd2887cc0c945896c075d5ccd9eb7",
     2,4,20,11,1680,4752,28352,320,384,4,18,
     0x778048205C7C71BCull,0x4D8485A5A870F9ACull,0xD4454B29D2020CE5ull,0x05CD292025AFCC75ull}, // combined row 34
    {0x8202AD78,0x3220,"simpsons_rigid_dualtextured","1c71a2f2f31878a775f79c47d3b161a8a799711b86594bc1a4515c15d2702649",
     2,4,17,11,1136,624,12240,320,480,4,24,
     0x42F326416FBA4039ull,0xC6DA725AAB9E467Cull,0xD4454B29D2020CE5ull,0xA29D7DC662E8DDE4ull}, // combined row 35
    {0x82019988,0x33C0,"simpsons_rigid_gloss","7115380680315fe44162a744a5121a9c5b5534e0ed3f0e7d04b6ebce5132e983",
     2,4,15,11,1120,544,12656,320,480,4,24,
     0xC98C942D1940908Cull,0x02455CF3CD0AB355ull,0xD4454B29D2020CE5ull,0x4329F597FE07AF7Dull}, // combined row 36
    {0x82023E58,0x6F20,"simpsons_skin_gloss","40b11722f77074cf6dd5b9cfebeea66d5ca2bdf9f79a16a3c72fb03e6d89df44",
     2,4,19,11,1664,4688,27856,320,288,4,12,
     0x55EF864DB79F3BCBull,0xC835CEF781FCAB9Cull,0xD4454B29D2020CE5ull,0x1E05E49797950C49ull}, // combined row 37
    {0x82036448,0x2DC0,"simpsons_sky","b13b8d005119d97a16bac18a10824813997e02eb99ee2e12e2de55b37e41932b",
     2,4,19,11,1152,752,11120,320,864,4,48,
     0x8A2DA859191E7E25ull,0xEC162B54C89B4D26ull,0xD4454B29D2020CE5ull,0xDB7AE07665903E2Bull}, // combined row 38
    {0x82039208,0x2E00,"simpsons_flipbook","23ae68cc3dc8f0234c1ceb6d6cff0616c457760648792cc3f41837d42a7b1041",
     2,4,14,11,1120,480,11184,320,288,4,12,
     0xCD73B0387670B754ull,0x344AC4FE587096FBull,0xD4454B29D2020CE5ull,0x6F660A272579CAA8ull}, // combined row 39
    {0x8203C008,0x6F50,"simpsons_skin_flipbook","253b0b9239bb8716e9647d652058d53188d67726439c066098a78e2de81ea268",
     2,4,18,11,1664,4672,27904,320,288,4,12,
     0xA71A2DF939E398EAull,0x015E65CCBF3EA7BEull,0xD4454B29D2020CE5ull,0xAD3FCAC7CE71F879ull}, // combined row 40
    {0x82042F58,0x3690,"simpsons_uv","cbe6bb0f5ddb8f231998a74c18d7eebbe67e929c4836fca2f3cf093b307cdd86",
     2,4,17,11,1136,528,13376,320,480,4,24,
     0x41936D037E90CBB4ull,0x2A7658C8CFA155BCull,0xD4454B29D2020CE5ull,0x421D2E95A933D00Eull}, // combined row 41
    {0x8204A058,0x77B0,"simpsons_skin_dualtextured_uv","c8c955cbb051da8d849795fa3925b520d2173bc28668c21d3dbe3dbae68ef9ab",
     2,4,24,11,1712,4816,30048,320,480,4,24,
     0x3021F74D59FBDD5Full,0xF1D2095EBD4CF5F8ull,0xD4454B29D2020CE5ull,0x4E5A8D2AC70311CBull}, // combined row 42
    {0x820465E8,0x3A70,"simpsons_rigid_dualtextured_uv","aca225b7e054662efed2e63c5052e2aad8bc4199dd327e2a7c13db8d7a30abd4",
     2,4,21,11,1168,688,14368,320,576,4,30,
     0x203AA136BFDC7588ull,0x20FE5C0F04D00C27ull,0xD4454B29D2020CE5ull,0x086011A0A3A960CCull}, // combined row 43
    {0x82051808,0x2FE0,"simpsons_projtex","0a9fe2adc551273b009f6816734bc0504b946c1a7ceb46adae9b8d06c27f7838",
     2,4,14,11,1120,528,11664,320,576,4,30,
     0x20C2F1E96816E318ull,0x70739A154217018Full,0xD4454B29D2020CE5ull,0xF8DB8269CC6A8399ull}, // combined row 44
    {0x820547E8,0x3620,"simpsons_rigid_multitone","20736b7201ff5a656a6409c36df50a4ca102d4b67db6c3a326f3f9000816b8a3",
     2,4,16,11,1136,608,13264,320,576,4,30,
     0xB7313650F395932Cull,0x9D90C24FBA1513C6ull,0xD4454B29D2020CE5ull,0x603970670B3B0F41ull}, // combined row 45
    {0x82057E08,0x3A40,"simpsons_rigid_normalmap","7ceab412bbcb59ecf575a288618b80d01d854c77ed411fbaa0143c40c99b3f34",
     2,4,17,11,1136,624,14320,320,576,4,30,
     0x47ED0B42149F90DCull,0x7327AF01C46F8261ull,0xD4454B29D2020CE5ull,0x9A4A2D03618C2108ull}, // combined row 46
    {0x8205B848,0x1A90,"simpsons_vfx_rigid_textured","968b970886a3c0989b7fd0afd4b5afd1fe23a04c92423c79d04af58b9d078261",
     1,2,10,11,1088,416,6208,320,120,0,6,
     0x7E4691CEF9271D9Dull,0x2F2DDC7ABFB45BEEull,0xD4454B29D2020CE5ull,0x212679ABA16E1672ull}, // combined row 47
    {0x8205D2D8,0x4100,"simpsons_chocolate","d86c905c272eefdf122024967581fcfa16b7bdafcee51a1027377a37d923a7ca",
     2,4,21,11,1168,736,16048,320,768,4,42,
     0x4552939C9D8F4A35ull,0xE7AF7FBB918D1007ull,0xD4454B29D2020CE5ull,0xB0EB2979CDF1EEF1ull}, // combined row 48
};
static_assert(std::size(golden)==49);

std::span<const uint8_t> source(std::span<const uint8_t> image,const Golden& g) {
    require(g.address>=imageBase,"Golden address below original image");
    const size_t offset=g.address-imageBase;
    require(offset<=image.size() && g.bytes<=image.size()-offset,"Golden envelope outside image");
    return image.subspan(offset,g.bytes);
}
const EffectParameter& parameter(const EffectRecord& record,bool shared,std::string_view name) {
    for(const auto& p:record.parameters(shared)) if(p.name==name) return p;
    throw std::runtime_error(std::string(currentCase)+": missing parameter "+std::string(name));
}
void sameMetadata(const EffectRecord& record,const Golden& g) {
    const auto& identity=record.identity();
    require(identity.originalAddress==g.address && identity.recordBytes==g.bytes &&
            identity.name==g.name && identity.sha256==g.sha256,"Pinned original identity differs");
    require(record.techniques().size()==g.techniques && record.shaders().size()==g.shaders,
            "Technique or framed shader count differs");
    require(record.parameters(false).size()==g.privateNames && record.parameters(true).size()==g.sharedNames,
            "Top-level parameter namespace count differs");
    require(record.cacheBytes()==g.cacheBytes,"CPU reflection cache byte count differs");
    require(record.privateDefaults().size()==g.privateBytes && record.sharedDefaults().size()==g.sharedBytes,
            "Default storage was sized from leaves or logical components");
    require(techniqueFingerprint(record.techniques())==g.techniquesFingerprint,
            "Technique/pass handles, context/shader association, or exact ordered state values differ");
    require(parameterFingerprint(record.parameters(false))==g.privateFingerprint,
            "Private parameter names, handles or descriptor words differ");
    require(parameterFingerprint(record.parameters(true))==g.sharedFingerprint,
            "Shared parameter names, handles or descriptor words differ");
    require(shaderFingerprint(record.shaders())==g.shadersFingerprint,
            "Original shader span/stage inventory differs");
}

void allOriginalEffects(std::span<const uint8_t> image) {
    const auto identities=originalEffectIdentities();
    require(identities.size()==49,"Effect allowlist must contain exactly 49 combined registration rows");
    size_t techniques=0,privateNames=0,sharedNames=0,scalars=0,samplers=0,vertices=0,pixels=0,nullPixels=0;
    std::set<uint32_t> uniqueShaders;
    MaterialRegistry materials;
    for(size_t i=0;i<std::size(golden);++i) {
        const auto& g=golden[i];currentCase=g.name;
        const auto& id=identities[i];
        require(id.row==i && id.originalAddress==g.address && id.recordBytes==g.bytes &&
                id.name==g.name && id.sha256==g.sha256,"Effect identities are not in registration order");
        const auto bytes=source(image,g);
        EffectRecord record(g.address,bytes);
        sameMetadata(record,g);
        require(record.identity().row==i,"Owned effect identity row differs");
        require(equal(record.bytes(),bytes) && record.bytes().data()!=bytes.data(),"Envelope is changed or borrowed");
        require(equal(record.body(),bytes.subspan(12)) && record.body().data()==record.bytes().data()+12,
                "Body does not view the owned envelope at +12");
        require(equal(record.privateDefaults(),bytes.subspan(12+g.privateOffset,g.privateBytes)),
                "Private defaults differ from the complete original storage span");
        if(g.sharedBytes) {
            require(equal(record.sharedDefaults(),bytes.subspan(12+g.sharedOffset,g.sharedBytes)),
                    "Shared defaults do not follow the serialized pointer cell");
            require(record.sharedDefaults().data()==record.body().data()+g.sharedOffset,
                    "Shared defaults do not view owned body storage");
        }
        require(record.privateDefaults().data()==record.body().data()+g.privateOffset,
                "Private defaults do not view owned body storage");
        size_t rowScalars=0,rowSamplers=0;
        for(const auto& t:record.techniques()) {
            rowScalars+=t.scalars.size();rowSamplers+=t.samplers.size();
            for(const auto address:{t.vertexShaderAddress,t.pixelShaderAddress}) {
                if(!address) continue;
                require(std::any_of(record.shaders().begin(),record.shaders().end(),
                    [&](const auto& s){return s.originalAddress==address;}),"Pass refers outside owned shader spans");
            }
            require(t.vertexShaderAddress!=0,"Unexpected null vertex association in original all49");
            nullPixels+=t.pixelShaderAddress==0;
        }
        require(rowScalars==g.scalars && rowSamplers==g.samplers,"Per-effect state inventory differs");
        for(const auto& s:record.shaders()) {
            require(s.bodyOffset<=record.body().size() && s.recordBytes<=record.body().size()-s.bodyOffset,
                    "Shader span escapes owned body");
            require(s.originalAddress==g.address+12+s.bodyOffset,"Shader VA/body offset relation differs");
            require(uniqueShaders.insert(s.originalAddress).second,"Original all49 shader inventory has an unexpected duplicate");
            const auto material=materials.create(s.originalAddress,record.body().subspan(s.bodyOffset,s.recordBytes));
            require(materials.record(material).identity().stage==s.stage &&
                    materials.capability(material)==MaterialCapability::Uncompiled,
                    "CPU shader record stage differs or falsely claims compilation");
            materials.release(material);
            if(s.stage==MaterialStage::Vertex) ++vertices;else ++pixels;
        }
        techniques+=g.techniques;privateNames+=g.privateNames;sharedNames+=g.sharedNames;
        scalars+=rowScalars;samplers+=rowSamplers;
        if(i==24) {
            require(techniques==68 && privateNames==320 && sharedNames==218,"First25 parameter/technique totals differ");
            require(scalars==229 && samplers==1101,"First25 literal-state totals differ");
            require(vertices==68 && pixels==63 && uniqueShaders.size()==131 && nullPixels==5,
                    "First25 framed shader inventory or null associations differ");
        }
    }
    currentCase="all49 totals";
    require(techniques==110 && privateNames==719 && sharedNames==447,"All49 parameter/technique totals differ");
    require(scalars==322 && samplers==1587,"All49 literal-state totals differ");
    require(vertices==110 && pixels==105 && uniqueShaders.size()==215 && nullPixels==5,
            "Framed shader inventory or explicit null association count differs");
    require(materials.liveCount()==0,"CPU shader metadata owners leaked");
    require(identities[1].originalAddress==0x820D5730 && identities[2].originalAddress==0x820B9750,
            "Registration order was replaced with physical blob order");
    require(identities[25].originalAddress==0x82006348 && identities[25].name=="simpsons_skin" &&
            identities[48].originalAddress==0x8205D2D8 && identities[48].name=="simpsons_chocolate",
            "Second table combined ordinals or registration endpoints differ");
}

void exactStatesAndStorage(std::span<const uint8_t> image) {
    currentCase="fourtapblend exact literals and padded defaults";
    EffectRecord first(golden[0].address,source(image,golden[0]));
    require(first.techniques().size()==1,"First effect technique count differs");
    const auto& t=first.techniques()[0];
    require(t.name=="Technique0" && t.handle==0x0003FFFC && t.passHandle==0x0003FFFE &&
            t.contextOffset==0xBB0 && t.vertexShaderAddress==0x820B8F08 && t.pixelShaderAddress==0x820B90D4,
            "First technique/pass/shader association differs");
    constexpr EffectScalar scalar[]={{0x2C,7},{0x30,0},{0x38,0},{0x3C,1},{0x48,1},{0x4C,1},{0x6C,0},{0x130,0}};
    constexpr EffectSampler sampler[]={{0,0,2},{0,4,2},{0,0x10,1},{0,0x14,1},{0,0x18,2}};
    require(t.scalars.size()==std::size(scalar) && t.samplers.size()==std::size(sampler),
            "First literal-state counts differ");
    for(size_t i=0;i<std::size(scalar);++i)
        require(t.scalars[i].sdkId==scalar[i].sdkId && t.scalars[i].value==scalar[i].value,"First scalar literal changed");
    for(size_t i=0;i<std::size(sampler);++i)
        require(t.samplers[i].stage==sampler[i].stage && t.samplers[i].sdkId==sampler[i].sdkId &&
                t.samplers[i].value==sampler[i].value,"First sampler literal changed");
    require(parameter(first,false,"g_Weights").handle==0x00040000 &&
            parameter(first,false,"g_Sampler").handle==0x00180008,"Array descendants did not contribute to later handle");
    constexpr uint32_t x[]={0x3E555555,0x3E2AAAAB,0x3DAAAAAB,0x3D2AAAAB,0};
    for(size_t i=0;i<std::size(x);++i) {
        require(be32(first.privateDefaults(),16*i)==x[i],"First default X lane changed");
        require(be32(first.privateDefaults(),16*i+4)==0 && be32(first.privateDefaults(),16*i+8)==0 &&
                be32(first.privateDefaults(),16*i+12)==0x3F800000,"Default padding/YZW lanes were discarded");
    }
    require(first.sharedDefaults().empty() && first.parameters(true).empty(),"Invented first-effect shared namespace");

    currentCase="matrix leaves and recursive light arrays";
    EffectRecord lit(golden[1].address,source(image,golden[1]));
    // Consumer layout: all headers, then scalar/sampler blocks per technique.
    // These offsets deliberately distinguish interleaving from grouping all scalars first.
    require(lit.techniques().size()==2,"Littextured technique count differs");
    size_t cacheAt=24*lit.techniques().size();
    require(cacheAt==48,"Littextured cache header extent differs");
    constexpr size_t scalarAt[]={48,156},samplerAt[]={60,216};
    for(size_t i=0;i<lit.techniques().size();++i) {
        require(cacheAt==scalarAt[i],"Littextured scalar cache block offset differs");
        cacheAt+=12*lit.techniques()[i].scalars.size();
        require(cacheAt==samplerAt[i],"Littextured sampler cache block offset differs");
        cacheAt+=16*lit.techniques()[i].samplers.size();
    }
    require(cacheAt==312 && cacheAt==lit.cacheBytes(),"Littextured interleaved cache end differs");
    const auto& worldI=parameter(lit,false,"g_WorldI");
    const auto& world=parameter(lit,false,"g_World");
    require(worldI.handle==0x00080002 && world.handle==0x000C0004,
            "Matrix rows were counted as separate leaves when forming handles");
    require(worldI.descriptorWords[1]==0x00100001 && world.descriptorWords[1]==0x00100005,
            "Matrix storage indices lost four-slot stride");
    require(lit.privateDefaults().size()==656 && lit.sharedDefaults().size()==320,
            "Littextured defaults incorrectly use 16 times leaf count");
    const auto& lights=parameter(lit,false,"g_WorldLightParameters");
    require(lights.handle==0x00280010 && lights.descriptorWords==std::array<uint32_t,2>{0x00400012,0x00440019},
            "Four structures were flattened into four leaf descriptors");
    require(parameter(lit,false,"g_SpecularExponent").handle==0x008C0038,
            "Handle after nested array did not skip 25 descriptors and count 20 descendant leaves");
    const uint32_t desc=be32(lit.body(),0x108);
    constexpr uint32_t children[]={11,17,23,29};
    for(size_t i=0;i<std::size(children);++i) {
        require(be32(lit.body(),desc+8*children[i])==0x15 &&
                be32(lit.body(),desc+8*children[i]+4)==0x00110006,
                "Nested structure child no longer spans six descriptors");
        require((be32(lit.body(),desc+8*(children[i]+1)+4)&0xFFFF)==14+5*i,
                "Nested child storage is not the explicit first-member slot");
    }
    require(parameter(lit,true,"g_ViewProjection").handle==0x00040001 &&
            parameter(lit,true,"g_WorldEyePosition").handle==0x00080003,
            "Shared namespace bit/one-leaf matrix handle differs");

    currentCase="64-matrix array padding and null pixel sentinels";
    EffectRecord shadows(golden[4].address,source(image,golden[4]));
    require(shadows.techniques().size()==8,"Shadows technique count differs");
    const auto& bones=parameter(shadows,false,"kBoneMatrices");
    require(bones.handle==0x0040001C && bones.descriptorWords==std::array<uint32_t,2>{0x00200102,0x03000041},
            "64-matrix array container/count/handle differs");
    require(parameter(shadows,false,"kAlphaTextureSampler").handle==0x0144009C,
            "64 padded matrices were counted as 256 leaves");
    const uint32_t sd=be32(shadows.body(),0x108);
    for(uint32_t i=0;i<64;++i) {
        require(be32(shadows.body(),sd+8*(17+i))==0x5B0 &&
                be32(shadows.body(),sd+8*(17+i)+4)==(0x000C0000u | (20+4*i)),
                "Matrix leaf logical size/storage slot differs");
    }
    for(size_t i:{size_t(1),size_t(3),size_t(5)})
        require(shadows.techniques()[i].pixelShaderAddress==0,"Null pixel sentinel became a real shader address");
    EffectRecord z(golden[24].address,source(image,golden[24]));
    require(z.techniques().size()==2,"Zprepass technique count differs");
    require(parameter(z,false,"kBlendWeights").handle==0x00340016 &&
            parameter(z,false,"kBlendWeights").descriptorWords==std::array<uint32_t,2>{0x0000000A,0x00080003},
            "Two-element zprepass array differs");
    require(z.techniques()[0].pixelShaderAddress==0 && z.techniques()[1].pixelShaderAddress==0,
            "Zprepass null pixel associations were rejected or invented");
}

void immutableOwnership(std::span<const uint8_t> image) {
    for(const auto& g:golden) {
        currentCase=g.name;
        const auto original=source(image,g);
        std::vector<uint8_t> scratch(original.begin(),original.end());
        auto first=std::make_unique<EffectRecord>(g.address,scratch);
        EffectRecord second(g.address,scratch);
        require(first->bytes().data()!=second.bytes().data(),"Distinct effect constructors share mutable backing");
        std::fill(scratch.begin(),scratch.end(),0xA5);scratch.clear();scratch.shrink_to_fit();
        require(equal(first->bytes(),original) && equal(second.bytes(),original),"Caller mutation/free altered owned original bytes");
        sameMetadata(*first,g);sameMetadata(second,g);
        first.reset();
        require(equal(second.bytes(),original),"Destroying one owner invalidated a second owner");
        sameMetadata(second,g);
    }
}

void secondTableContextTail(std::span<const uint8_t> image) {
    currentCase="simpsons_edgeAA bounded opaque context tail";
    // Frozen second catalog row4, combined ordinal29. The tail is preserved,
    // not interpreted as states, constants, shaders or an implicit no-op.
    // Original82C16E50 adds context+54; +50 is the base end, not total end.
    const auto& g=golden[29];
    require(g.address==0x82034008 && g.name=="simpsons_edgeAA" && g.bytes==0x2440,
            "Opaque-tail fixture selected the wrong combined row");
    const auto original=source(image,g),body=original.subspan(12);
    EffectRecord survivor(g.address,original);
    sameMetadata(survivor,g);
    constexpr uint32_t first=0x1DA0,second=0x2060,baseBytes=0x2C0,tailAt=0x2320,tailBytes=16,end=0x2330;
    constexpr std::array<uint8_t,tailBytes> tail={0,0,0,10,0,0,0,0,0,0,0,1,0,0,0,0};
    require(body.size()==0x2434 && be32(body,0x224)==first && be32(body,0x228)==2 &&
            be32(body,0x22C)==0x590,"Original context array framing differs");
    require(be32(body,first+0x50)==second && be32(body,first+0x54)==0 && first+baseBytes==second,
            "Initial context must have no extension");
    require(be32(body,second+0x50)==tailAt && be32(body,second+0x54)==tailBytes &&
            second+baseBytes==tailAt && tailAt+tailBytes==end && first+be32(body,0x22C)==end,
            "Base end/tail start was conflated with the next-context address");
    require(equal(survivor.body().subspan(tailAt,tailBytes),tail) &&
            survivor.body().subspan(tailAt,tailBytes).data()!=body.subspan(tailAt,tailBytes).data(),
            "Exact opaque tail bytes were changed or borrowed");
    require(survivor.techniques().size()==1 && survivor.techniques()[0].name=="aa" &&
            survivor.techniques()[0].contextOffset==second && survivor.shaders().size()==2,
            "Tail was counted as another context/technique/shader");
    auto unchanged=[&] {
        require(equal(survivor.bytes(),original) && equal(survivor.body().subspan(tailAt,tailBytes),tail),
                "Tail rejection damaged the surviving immutable owner");
        sameMetadata(survivor,g);
    };
    auto badWord=[&](size_t offset,uint32_t value,const char* reason,std::string_view category) {
        auto changed=std::vector<uint8_t>(original.begin(),original.end());
        put32(changed,12+offset,value);
        rejects([&]{EffectRecord bad(g.address,changed);},reason,{category});
        unchanged();
    };
    struct Fault {size_t offset;uint32_t value;const char* reason;std::string_view category;};
    const Fault faults[]={
        {second+0x50,end,"+50 points after the opaque tail","malformed:"},
        {second+0x50,tailAt-16,"+50 truncates base geometry","malformed:"},
        {second+0x54,0,"tail erased without reducing array extent","malformed:"},
        {second+0x54,4,"unqualified short context extension","unrecognized framing:"},
        {second+0x54,15,"unaligned context extension","unrecognized framing:"},
        {second+0x54,32,"extension exceeds remaining context array","bounds:"},
        {second+0x54,0xFFFFFFFF,"extension addition must not wrap","bounds:"},
        {0x22C,0x580,"context array cuts off the tail","bounds:"},
        {0x22C,0x591,"context array has an unconsumed trailing byte","malformed:"},
        {0x228,3,"tail cannot supply a third context header","bounds:"},
        {second+0x40,tailAt,"private vector cache redirected into opaque tail","malformed:"},
        {second+0x44,tailAt,"shared vector cache redirected into opaque tail","malformed:"},
        {second+0x3C,tailAt,"bookkeeping slice redirected into opaque tail","malformed:"},
    };
    for(const auto& f:faults) badWord(f.offset,f.value,f.reason,f.category);
    const auto pass=be32(body,0x210);
    badWord(pass+8,tailAt,"pass references tail as a context","bounds:");
    badWord(pass+8,end,"pass references context-array end as a context","bounds:");
    // Every tail byte is opaque but identity-protected. These mutations leave
    // valid framing and must reach the envelope SHA check, not a guessed parser.
    for(size_t i=0;i<tailBytes;++i) {
        auto changed=std::vector<uint8_t>(original.begin(),original.end());
        changed[12+tailAt+i]^=uint8_t(1);
        rejects([&]{EffectRecord bad(g.address,changed);},"changed opaque context-tail byte",{"identity:"});
        unchanged();
    }
}

void identityAndEnvelopeRejection(std::span<const uint8_t> image) {
    for(const auto& g:golden) {
        currentCase=g.name;
        const auto original=source(image,g);
        EffectRecord survivor(g.address,original);
        for(size_t length:{size_t(0),size_t(11),size_t(12),size_t(12+0x30F),original.size()-1})
            rejects([&]{EffectRecord bad(g.address,original.first(length));},"truncated envelope");
        auto changed=std::vector<uint8_t>(original.begin(),original.end());
        changed.push_back(0);
        rejects([&]{EffectRecord bad(g.address,changed);},"extra envelope byte");
        changed.assign(original.begin(),original.end());changed.at(12+g.privateOffset+3)^=1;
        rejects([&]{EffectRecord bad(g.address,changed);},"changed structurally valid private default",{"identity:"});
        if(g.sharedBytes) {
            changed.assign(original.begin(),original.end());changed.at(12+g.sharedOffset+3)^=1;
            rejects([&]{EffectRecord bad(g.address,changed);},"changed structurally valid shared default",{"identity:"});
        }
        // Catalog marks the bytes at body+310 opaque in every admitted envelope.
        changed.assign(original.begin(),original.end());changed.at(12+0x310)^=1;
        rejects([&]{EffectRecord bad(g.address,changed);},"changed opaque original byte",{"identity:"});
        rejects([&]{EffectRecord bad(g.address+4,original);},"interior source address",{"identity:"});
        require(equal(survivor.bytes(),original),"Failed construction altered an existing owner");
        sameMetadata(survivor,g);
    }
    currentCase="unknown/mismatched original identity";
    rejects([&]{EffectRecord bad(0,source(image,golden[0]));},"unknown identity",{"identity:"});
    rejects([&]{EffectRecord bad(golden[0].address,source(image,golden[1]));},"another effect's envelope");
    rejects([&]{EffectRecord bad(golden[0].address,image);},"whole image instead of exact envelope");
}

void structuralRejection(std::span<const uint8_t> image) {
    for(const auto& g:golden) {
        currentCase=g.name;
        const auto original=source(image,g);
        const auto body=original.subspan(12);
        auto badEnvelope=[&](size_t offset,uint32_t value,const char* reason,std::string_view category) {
            auto changed=std::vector<uint8_t>(original.begin(),original.end());
            put32(changed,offset,value);
            rejects([&]{EffectRecord bad(g.address,changed);},reason,{category});
        };
        badEnvelope(0,0,"wrong envelope magic/version","unrecognized framing:");
        badEnvelope(4,0,"zero declared body length","malformed:");
        badEnvelope(4,0xFFFFFFFF,"overflowing declared body length","malformed:");
        badEnvelope(8,0,"copy prefix below root header","malformed:");
        badEnvelope(8,uint32_t(body.size()+16),"copy prefix beyond body","malformed:");
        badEnvelope(8,be32(original,8)+16,"copy prefix disagrees with context start","malformed:");
        auto badWord=[&](size_t offset,uint32_t value,const char* reason) {
            auto changed=std::vector<uint8_t>(original.begin(),original.end());
            put32(changed,12+offset,value);
            rejects([&]{EffectRecord bad(g.address,changed);},reason,
                    {"bounds:","malformed:","unrecognized framing:"});
        };
        for(uint32_t field:{0x110u,0x118u,0x130u,0x20Cu,0x220u,0x228u,0x234u,0x24Cu,0x258u})
            badWord(field,0xFFFFFFFF,"overflowing count");
        for(uint32_t field:{0x108u,0x128u,0x288u,0x298u})
            badWord(field,uint32_t(body.size()),"end-of-body used as readable parameter object");
        badWord(0x138,0xFFFFFFF0,"out-of-body private defaults extent");
        badWord(0x28C,1,"truncated packed names");
        const auto firstVertex=be32(body,0x230)+8; // Skip the zero-payload sentinel entry.
        const auto shader=firstVertex+8;
        badWord(firstVertex+4,uint32_t(body.size()),"shader envelope exceeds resource array");
        badWord(shader,0x102A1100,"pixel stage tag in a vertex resource array");
        badWord(shader+4,32,"shader header shorter than fixed metadata");
        badWord(shader+8,0xFFFFFFFF,"shader payload/header partition overrun");
        badWord(shader+24,0,"missing shader code metadata link");
        const auto d=be32(body,0x108);
        const auto w1=be32(body,d+12);
        badWord(d+12,(w1&0xFFFF0000u)|0xFFFF,"descriptor subtree/storage index overrun");
        const auto passes=be32(body,0x210);
        badWord(passes+12,uint32_t(body.size()),"pass state link outside any state block");
        badWord(passes+8,be32(body,0x224)+4,"pass context link into a context interior");
        const auto tech=be32(body,be32(body,0x200));
        badWord(tech+16,passes+4,"technique link into a pass interior");
        const auto ctx=be32(body,0x224);
        badWord(ctx+0x50,be32(body,ctx+0x50)+4,"context end link inconsistent with stride");
        if(g.sharedNames) {
            badWord(0x10C,uint32_t(body.size()-2),"truncated shared descriptor pointer cell");
            badWord(be32(body,0x12C),uint32_t(body.size()),"shared default target outside body");
            badWord(be32(body,0x290),uint32_t(body.size()),"shared name target outside body");
        }
    }
    currentCase="recursive descriptor/category negative fixtures";
    auto mutate=[&](size_t row,size_t at,uint32_t value,const char* reason,
                    std::initializer_list<std::string_view> categories) {
        const auto& g=golden[row];const auto original=source(image,g);
        auto changed=std::vector<uint8_t>(original.begin(),original.end());put32(changed,12+at,value);
        rejects([&]{EffectRecord bad(g.address,changed);},reason,categories);
    };
    mutate(0,0x39C,0x00040000,"zero array descriptor stride",{"bounds:","malformed:"});
    mutate(0,0x398,0x00200016,"array child count exceeds declared subtree",{"bounds:","malformed:"});
    mutate(1,0x390+8*11+4,0x00110002,"nested struct span too short for five members",{"bounds:","malformed:"});
    mutate(4,0x390+8*16+4,0x03000040,"64-element array declared as 64 total descriptor slots",{"bounds:","malformed:"});
    mutate(0,0x398,0x00200013,"unknown descriptor kind",{"unrecognized framing:"});
    for(bool sampler:{false,true}) for(uint32_t category:{0u,1u}) {
        const auto& g=golden[0];const auto original=source(image,g);
        auto changed=std::vector<uint8_t>(original.begin(),original.end());
        const size_t counts=sampler?0x9D0:0x870;
        put32(changed,12+counts+4*category,1);
        put32(changed,12+counts+8,sampler?4:7); // Keep the total/stride unchanged.
        rejects([&]{EffectRecord bad(g.address,changed);},"nonliteral state category",
                {"unrecognized framing:"});
    }
}
}

int main(int argc,char** argv) {
    static_assert(!std::is_copy_constructible_v<EffectRecord> && !std::is_move_constructible_v<EffectRecord>);
    static_assert(std::is_same_v<decltype(std::declval<const EffectRecord&>().bytes()),std::span<const uint8_t>>);
    static_assert(std::is_same_v<decltype(std::declval<const EffectRecord&>().privateDefaults()),std::span<const uint8_t>>);
    static_assert(std::is_same_v<decltype(std::declval<const EffectRecord&>().parameters(false)),std::span<const EffectParameter>>);
    try {
        if(argc!=2) throw std::runtime_error("Supply the original flat analysis/simpsons.pe path");
        std::ifstream input(argv[1],std::ios::binary|std::ios::ate);
        if(!input || input.tellg()!=std::streampos(imageBytes)) throw std::runtime_error("Wrong/missing original image extent");
        std::vector<uint8_t> image(imageBytes);input.seekg(0);
        if(!input.read(reinterpret_cast<char*>(image.data()),static_cast<std::streamsize>(image.size())))
            throw std::runtime_error("Failed to read original image");
        const std::pair<const char*,void(*)(std::span<const uint8_t>)> cases[]={
            {"all49 independent catalog goldens and 215 CPU shader spans",allOriginalEffects},
            {"exact literals, recursive arrays, padded matrices and null associations",exactStatesAndStorage},
            {"16-byte context tail ownership, framing and identity mutations",secondTableContextTail},
            {"immutable copy and independent owner lifetime",immutableOwnership},
            {"exact identity, defaults, opaque bytes and envelope rejection",identityAndEnvelopeRejection},
            {"count/link/recursive-descriptor rejection before identity check",structuralRejection}};
        for(const auto& [name,test]:cases) {currentCase=name;test(image);std::printf("PASS: %s\n",name);}
        std::printf("6 CPU effect resource test groups passed (%zu checks); no SDK execution, shader compilation or rendering.\n",checks);
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"Effect resources: %s\n",error.what());return 1;}
}
