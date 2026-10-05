// Standalone CPU owner tests. Supply analysis/simpsons.pe as argv[1].
// The compiler fixture below tests transactions/lifetime only: it does not
// represent a native compiled shader, renderer test, or original frame.
#include "renderer/material_resources.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <limits>
#include <new>
#include <type_traits>

using namespace Simpsons::Graphics;
namespace {
size_t checks=0;
void require(bool value,const char* message) {
    ++checks;
    if(!value) throw std::runtime_error(message);
}
template<class Exception=MaterialError,class F> void rejects(F&& action) {
    ++checks;
    try {action();} catch(const Exception&) {return;}
    throw std::runtime_error("Expected operation to reject without success");
}
uint32_t be32(std::span<const uint8_t> data,size_t offset) {
    require(offset<=data.size() && data.size()-offset>=4,"Test word range");
    return uint32_t(data[offset])<<24 | uint32_t(data[offset+1])<<16 |
           uint32_t(data[offset+2])<<8 | uint32_t(data[offset+3]);
}
void put32(std::vector<uint8_t>& data,size_t offset,uint32_t value) {
    for(size_t i=0;i<4;++i) data.at(offset+i)=uint8_t(value>>(24-8*i));
}
bool equal(std::span<const uint8_t> a,std::span<const uint8_t> b) {
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin());
}
std::span<const uint8_t> source(std::span<const uint8_t> image,uint32_t va) {
    for(const auto& identity:originalMaterialIdentities())
        if(identity.originalAddress==va) return image.subspan(va-0x82000000,identity.recordBytes);
    throw std::runtime_error("Test requested an unknown identity");
}

struct FailingMemory final : std::pmr::memory_resource {
    size_t budget=std::numeric_limits<size_t>::max(),outstanding=0,allocations=0;
    void* do_allocate(size_t bytes,size_t alignment) override {
        if(!budget) throw std::bad_alloc();
        auto* result=std::pmr::new_delete_resource()->allocate(bytes,alignment);
        if(budget!=std::numeric_limits<size_t>::max()) --budget;
        outstanding+=bytes;++allocations;
        return result;
    }
    void do_deallocate(void* p,size_t bytes,size_t alignment) override {
        outstanding-=bytes;
        std::pmr::new_delete_resource()->deallocate(p,bytes,alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {return this==&other;}
};

struct FixtureArtifact final : CompiledMaterial {
    int& destroyed;
    std::function<void()> onDestroy;
    FixtureArtifact(uint32_t va,MaterialStage stage,int& counter,std::function<void()> callback={})
        :CompiledMaterial(va,stage),destroyed(counter),onDestroy(std::move(callback)) {}
    ~FixtureArtifact() override {++destroyed;if(onDestroy) onDestroy();}
};
struct FixtureCompiler final : MaterialCompiler {
    size_t calls=0;
    std::function<std::unique_ptr<CompiledMaterial>(const MaterialRecord&)> action;
    std::unique_ptr<CompiledMaterial> compile(const MaterialRecord& record) override {
        ++calls;return action(record);
    }
};

void originalRecords(std::span<const uint8_t> image) {
    // Independently recorded header/prefix/code sizes from original metadata.
    // Fifth column: 1=vertex, 0=pixel; order preserves the startup 20 then catalog rows.
    constexpr uint32_t expected[][5]={
        {0x821524C8,0xFC,0x0,0x24,0},
        {0x821525E8,0xD4,0x0,0x48,1},
        {0x82152708,0x138,0x0,0x3C,0},
        {0x82152880,0xE4,0x0,0x60,1},
        {0x82199150,0x78,0x40,0x90,0},
        {0x82199298,0x50,0x0,0x78,0},
        {0x82199360,0x4C,0x0,0x6C,0},
        {0x82199418,0x7C,0x40,0xC0,0},
        {0x82199598,0x7C,0x40,0xE4,0},
        {0x82199738,0x7C,0x40,0xD8,0},
        {0x821B7050,0x7C,0x40,0xF0,0},
        {0x821B7200,0x7C,0x40,0xFC,0},
        {0x821B7DD8,0x88,0x80,0x174,0},
        {0x821B8058,0x8C,0x80,0x180,0},
        {0x821B73B8,0x78,0x40,0xB4,0},
        {0x821B7528,0x78,0x40,0xC0,0},
        {0x821B82E8,0x84,0x80,0x144,0},
        {0x821B8530,0x88,0x80,0x150,0},
        {0x821B8B70,0x88,0x80,0x15C,0},
        {0x821B8DD8,0x8C,0x80,0x168,0},
        {0x820B8F08,0x114,0x0,0xA8,1},
        {0x820B90D4,0x188,0x0,0x84,0},
        {0x820D5F04,0x290,0x40,0x120,1},
        {0x820D62FC,0x290,0x40,0x120,1},
        {0x820D66FC,0x448,0x40,0xC78,0},
        {0x820D7804,0x448,0x40,0xC78,0},
        {0x820B9C88,0x170,0x0,0x9C,1},
        {0x820B9EA4,0x160,0x0,0x54,0},
        {0x820BB108,0x10C,0x40,0x54,1},
        {0x820BB2B0,0x10C,0x40,0x54,1},
        {0x820BB458,0x10C,0x40,0x54,1},
        {0x820BB600,0x10C,0x40,0x54,1},
        {0x820BB7A8,0x10C,0x40,0x54,1},
        {0x820BB950,0x10C,0x40,0x54,1},
        {0x820BBAF8,0x10C,0x40,0x54,1},
        {0x820BBCA0,0x10C,0x40,0x54,1},
        {0x820BBE48,0x10C,0x40,0x54,1},
        {0x820BBFF0,0x10C,0x40,0x54,1},
        {0x820BC198,0x10C,0x40,0x54,1},
        {0x820BC340,0xE8,0x0,0x60,1},
        {0x820BC490,0xE8,0x0,0x60,1},
        {0x820BC5E0,0xE8,0x0,0x60,1},
        {0x820BC730,0xE8,0x0,0x60,1},
        {0x820BC880,0xE8,0x0,0x60,1},
        {0x820BC9D0,0x10C,0x40,0x54,1},
        {0x820BCB80,0x170,0x40,0xC0,0},
        {0x820BCDF8,0x170,0x40,0xC0,0},
        {0x820BD070,0x140,0x0,0x54,0},
        {0x820BD20C,0x110,0x0,0x24,0},
        {0x820BD348,0x108,0x0,0x3C,0},
        {0x820BD494,0x148,0x0,0x48,0},
        {0x820BD62C,0x108,0x0,0x3C,0},
        {0x820BD778,0x108,0x0,0x3C,0},
        {0x820BD8C4,0x108,0x0,0x48,0},
        {0x820BDA1C,0x128,0x0,0x54,0},
        {0x820BDBA0,0x108,0x0,0x54,0},
        {0x820BDD04,0x108,0x0,0x3C,0},
        {0x820BDE50,0x108,0x0,0x3C,0},
        {0x820BDF9C,0x108,0x0,0x3C,0},
        {0x820BE0E8,0x108,0x0,0x48,0},
        {0x820BE240,0x128,0x0,0x54,0},
        {0x820BE3C4,0x1C4,0x40,0x1EC,0},
        {0x820C1E6C,0xE28,0x40,0x2C4,1},
        {0x820C2FA0,0xE28,0x40,0x2C4,1},
        {0x820C40D4,0xE28,0x40,0x2C4,1},
        {0x820C5208,0xE28,0x40,0x210,1},
        {0x820C6288,0xE28,0x40,0x210,1},
        {0x820C7308,0xE28,0x40,0x210,1},
        {0x820C8388,0xE60,0x40,0x228,1},
        {0x820C9458,0xE60,0x40,0x228,1},
        {0x820CA530,0x180,0x40,0x60,0},
        {0x820CA758,0x1E0,0x40,0xCC,0},
        {0x820CAA4C,0x180,0x40,0x60,0},
        {0x820CAC74,0x1F0,0x40,0x1A4,0},
        {0x820CB050,0x1F0,0x40,0x1A4,0},
        {0x82100294,0xE38,0x40,0x2AC,1},
        {0x821013C0,0xE38,0x40,0x2AC,1},
        {0x821024F4,0x464,0x40,0xDA4,0},
        {0x82103744,0x464,0x40,0xDA4,0},
        {0x82142454,0xE50,0x40,0x2C4,1},
        {0x821435B0,0xE50,0x40,0x2C4,1},
        {0x82144714,0x474,0x40,0xDC8,0},
        {0x82145998,0x474,0x40,0xDC8,0},
        {0x821126C4,0xEAC,0x40,0x354,1},
        {0x8211390C,0xEAC,0x40,0x354,1},
        {0x82114B5C,0x5B4,0x40,0x1674,0},
        {0x821167CC,0x5B4,0x40,0x1674,0},
        {0x82108804,0xEA8,0x40,0x384,1},
        {0x82109A78,0xEA8,0x40,0x384,1},
        {0x8210ACF4,0x544,0x40,0x1638,0},
        {0x8210C8B8,0x544,0x40,0x1638,0},
        {0x820FAAD4,0xDCC,0x40,0x18C,1},
        {0x820FBA74,0xDCC,0x40,0x18C,1},
        {0x820FCA1C,0x1C4,0x0,0x48,0},
        {0x820FCC30,0x1C4,0x0,0x48,0},
        {0x820D1754,0xE64,0x40,0x1C8,1},
        {0x820D27C8,0xE64,0x40,0x1C8,1},
        {0x820D3844,0x1C4,0x0,0x48,0},
        {0x820D3A58,0x1C4,0x0,0x48,0},
        {0x820DA804,0x2FC,0x40,0x2B8,1},
        {0x820DAE00,0x2FC,0x40,0x2B8,1},
        {0x820DB404,0x5C0,0x40,0x750,0},
        {0x820DC15C,0x5C0,0x40,0x750,0},
        {0x820DF304,0x2F4,0x40,0x258,1},
        {0x820DF898,0x2F4,0x40,0x258,1},
        {0x820DFE34,0x54C,0x40,0x42C,0},
        {0x820E07F4,0x54C,0x40,0x42C,0},
        {0x820E3444,0x2A8,0x40,0x264,1},
        {0x820E3998,0x2A8,0x40,0x264,1},
        {0x820E3EF4,0x560,0x40,0x5A0,0},
        {0x820E4A3C,0x560,0x40,0x5A0,0},
        {0x82125084,0x2AC,0x40,0x264,1},
        {0x821255DC,0x2AC,0x40,0x264,1},
        {0x82125B3C,0x570,0x40,0x5D0,0},
        {0x821266C4,0x570,0x40,0x5D0,0},
        {0x820E7B78,0x270,0x40,0x114,1},
        {0x820E7F4C,0x32C,0x40,0x1D4,0},
        {0x820EB5C4,0xED4,0x40,0x210,1},
        {0x820EC6F0,0xED4,0x40,0x210,1},
        {0x820ED824,0x20C,0x0,0x84,0},
        {0x820EDABC,0x20C,0x0,0x84,0},
        {0x820F1C74,0xED8,0x40,0x2B8,1},
        {0x820F2E4C,0xED8,0x40,0x2B8,1},
        {0x820F402C,0x530,0x40,0x108C,0},
        {0x820F5630,0x530,0x40,0x108C,0},
        {0x8211B894,0x238,0x0,0x1BC,1},
        {0x8211BC90,0x238,0x0,0x1BC,1},
        {0x8211C094,0x400,0x40,0x63C,0},
        {0x8211CB18,0x400,0x40,0x63C,0},
        {0x82120C04,0xDF8,0x40,0x1A4,1},
        {0x82121BE8,0xDF8,0x40,0x1A4,1},
        {0x82122BD4,0xF8,0x40,0x24,0},
        {0x82122D38,0xF8,0x40,0x24,0},
        {0x8212A8E4,0xE50,0x40,0x2C4,1},
        {0x8212BA40,0xE50,0x40,0x2C4,1},
        {0x8212CBA4,0x4E8,0x40,0xDEC,0},
        {0x8212DEC0,0x4E8,0x40,0xDEC,0},
        {0x82136B04,0x304,0x40,0x270,1},
        {0x821370C0,0x304,0x40,0x270,1},
        {0x82137684,0x570,0x40,0x6D8,0},
        {0x82138314,0x570,0x40,0x6D8,0},
        {0x821320E4,0x290,0x40,0x120,1},
        {0x821324DC,0x290,0x40,0x120,1},
        {0x821328DC,0x468,0x40,0xC9C,0},
        {0x82133A28,0x468,0x40,0xC9C,0},
        {0x8213B194,0x2AC,0x40,0x228,1},
        {0x8213B6B0,0x2AC,0x40,0x228,1},
        {0x8213BBD4,0x584,0x40,0x1524,0},
        {0x8213D6C4,0x584,0x40,0x1524,0},
        {0x8214A8A4,0xE68,0x40,0x264,1},
        {0x8214B9B8,0xE68,0x40,0x264,1},
        // Second table: independent full framing from the frozen offline catalog.
        // analysis/native-post-effect-catalog.json SHA256: 325d8f64aff8e6d260cabecffc06f923cd2c8b426b1bba917bd30e16a1e4338e
        // simpsons_skin
        {0x82007C1C,0xE98,0x40,0x324,1},
        {0x82008E20,0xE98,0x40,0x324,1},
        {0x8200A02C,0x25C,0x40,0x1D4,0},
        {0x8200A4A4,0x1D0,0x40,0xC0,0},
        // simpsons_rigid
        {0x8200D3AC,0x218,0x0,0x168,1},
        {0x8200D734,0x1A8,0x0,0xFC,1},
        {0x8200D9E8,0x390,0x40,0x3FC,0},
        {0x8200E1BC,0x1D4,0x40,0xC0,0},
        // simpsons_edge
        {0x8202E6F0,0xE0,0x0,0x60,1},
        {0x8202E840,0x284,0x40,0x198,0},
        // simpsons_aa
        {0x820301A0,0xDC,0x0,0x60,1},
        {0x820302EC,0x250,0x40,0x90,0},
        // simpsons_edgeAA
        {0x820347B0,0xE0,0x0,0x60,1},
        {0x82034900,0x448,0x80,0x5B8,0},
        // simpsons_aa_row
        {0x82031980,0xE0,0x0,0x60,1},
        {0x82031AD0,0x250,0x40,0x78,0},
        // simpsons_aa_col
        {0x82033010,0xE0,0x0,0x60,1},
        {0x82033160,0x250,0x40,0x78,0},
        // simpsons_skin_textured
        {0x8201146C,0xED8,0x40,0x360,1},
        {0x820126EC,0xEA0,0x40,0x324,1},
        {0x82013900,0x314,0x40,0x330,0},
        {0x82013F8C,0x210,0x40,0xFC,0},
        // simpsons_rigid_textured
        {0x8201700C,0x220,0x0,0x168,1},
        {0x8201739C,0x1B0,0x0,0xFC,1},
        {0x82017658,0x3BC,0x40,0x3F0,0},
        {0x82017E4C,0x214,0x40,0xFC,0},
        // simpsons_skin_dualtextured
        {0x8201E6DC,0xEE8,0x40,0x378,1},
        {0x8201F984,0xEA4,0x40,0x324,1},
        {0x82020B9C,0x3C0,0x80,0x360,0},
        {0x82021344,0x1DC,0x40,0xC0,0},
        // simpsons_rigid_dualtextured
        {0x8202B4CC,0x230,0x0,0x180,1},
        {0x8202B884,0x1B4,0x0,0xFC,1},
        {0x8202BB44,0x3C4,0x80,0x42C,0},
        {0x8202C3BC,0x218,0x40,0xFC,0},
        // simpsons_rigid_gloss
        {0x8201A07C,0x22C,0x0,0x180,1},
        {0x8201A430,0x1B0,0x0,0xFC,1},
        {0x8201A6EC,0x444,0x80,0x504,0},
        {0x8201B0BC,0x258,0x40,0x1C8,0},
        // simpsons_skin_gloss
        {0x8202579C,0xED4,0x40,0x330,1},
        {0x820269E8,0xEC8,0x40,0x318,1},
        {0x82027C18,0x2EC,0x40,0x30C,0},
        {0x82028258,0x1D4,0x40,0xC0,0},
        // simpsons_sky
        {0x82036C2C,0x220,0x0,0xB4,1},
        {0x82036F08,0x220,0x0,0xB4,1},
        {0x820371EC,0x1DC,0x40,0xD8,0},
        {0x820374E8,0x1DC,0x40,0xD8,0},
        // simpsons_flipbook
        {0x820398BC,0x28C,0x40,0x1E0,1},
        {0x82039D70,0x270,0x40,0x21C,1},
        {0x8203A24C,0x2C0,0x40,0xF0,0},
        {0x8203A644,0x1D8,0x40,0xC0,0},
        // simpsons_skin_flipbook
        {0x8203D93C,0xF58,0x40,0x45C,1},
        {0x8203ED38,0xF4C,0x40,0x444,1},
        {0x82040118,0x224,0x40,0x18C,0},
        {0x82040510,0x1D8,0x40,0xC0,0},
        // simpsons_uv
        {0x8204364C,0x328,0x40,0x204,1},
        {0x82043BC0,0x2B4,0x40,0x1A4,1},
        {0x82044068,0x3B0,0x40,0x3F0,0},
        {0x82044850,0x208,0x40,0xFC,0},
        // simpsons_skin_dualtextured_uv
        {0x8204BA4C,0xFC4,0x40,0x438,1},
        {0x8204CE90,0xFC4,0x40,0x438,1},
        {0x8204E2DC,0x27C,0x40,0x1BC,0},
        {0x8204E75C,0x27C,0x40,0x1BC,0},
        // simpsons_rigid_dualtextured_uv
        {0x82046D9C,0x318,0x40,0x21C,1},
        {0x82047318,0x29C,0x40,0x1A4,1},
        {0x820477A8,0x3C0,0x80,0x354,0},
        {0x82047F44,0x1A4,0x40,0x180,0},
        // simpsons_projtex
        {0x82051EEC,0x28C,0x40,0x198,1},
        {0x82052358,0x1AC,0x0,0xFC,1},
        {0x82052610,0x3B8,0x40,0x3E4,0},
        {0x82052DF4,0x1B8,0x40,0xD8,0},
        // simpsons_rigid_multitone
        {0x82054F2C,0x280,0x40,0x24C,1},
        {0x82055440,0x1B8,0x0,0x108,1},
        {0x82055710,0x434,0x80,0x4EC,0},
        {0x820560B8,0x1E0,0x40,0xC0,0},
        // simpsons_rigid_normalmap
        {0x8205855C,0x23C,0x0,0x24C,1},
        {0x820589EC,0x1B8,0x0,0x108,1},
        {0x82058CBC,0x4B8,0x80,0x684,0},
        {0x82059880,0x260,0x40,0x18C,0},
        // simpsons_vfx_rigid_textured
        {0x8205BE70,0x1D8,0x0,0xA8,1},
        {0x8205C100,0x150,0x0,0x48,0},
        // simpsons_chocolate
        {0x8205DABC,0x308,0x40,0x2AC,1},
        {0x8205E0B8,0x298,0x40,0x240,1},
        {0x8205E5E0,0x3BC,0x80,0x4B0,0},
        {0x8205EED4,0x378,0x40,0x2D0,0},
        {0x82152B68,0x164,0x40,0x78,0},
        {0x82153278,0x120,0x40,0x84,1},
        {0x82153460,0x144,0x40,0x108,0},
        {0x821536F0,0x194,0,0x60,0},
        {0x821511D8,0x14C,0,0x90,1},
        {0x821509D8,0x104,0,0x3C,0},
        {0x82150B18,0x12C,0,0x54,0},
        {0x821538E8,0x190,0x40,0xCC,1},
        {0x82153B88,0xD0,0,0x24,0},
        {0x82153C80,0x118,0,0x3C,0},
        {0x821529C8,0x10C,0x40,0x54,1},
        {0x821583B8,0x104,0,0x3C,0},
        {0x82155F28,0x158,0x40,0x90,0},
        {0x82158118,0x170,0x40,0xF0,0},
        {0x82155D60,0x130,0x40,0x54,0},
        {0x82156548,0x16C,0x0,0xB4,1},
        {0x82156150,0x12C,0x40,0x84,0},
        {0x82156340,0x158,0x40,0x6C,0},
        {0x821513B8,0x1CC,0x40,0x12C,1},
        {0x82150C98,0x158,0x40,0xE4,0},
        {0x82150F18,0x180,0x40,0xFC,0},
    };
    static_assert(std::size(expected)==256);
    require(originalMaterialIdentities().size()==256,"Expected exactly 256 pinned identities");
    require(originalMaterialIdentities()[20].originalAddress==0x820B8F08 &&
            originalMaterialIdentities()[21].originalAddress==0x820B90D4,"FourTapBlend inventory indices changed");
    require(originalMaterialIdentities()[151].originalAddress==0x82007C1C,
            "Second-table shaders must follow the unchanged startup20 and first131 FX records");
    MaterialRegistry registry;
    size_t vertices=0,recordIndex=0;
    for(const auto& row:expected) {
        const auto bytes=source(image,row[0]);
        const auto id=registry.create(row[0],bytes);
        const auto& record=registry.record(id);
        const auto& meta=record.metadata();
        const bool vertex=row[4]!=0;
        require(originalMaterialIdentities()[recordIndex].originalAddress==row[0],"Material inventory order differs");
        vertices+=vertex;
        require(record.identity().stage==(vertex?MaterialStage::Vertex:MaterialStage::Pixel),"Wrong stage");
        require(meta.headerBytes==row[1] && meta.prefixBytes==row[2] && meta.codeBytes==row[3],"Original framing changed");
        require(meta.payloadBytes==row[2]+row[3],"Incorrect prefix/code partition");
        require(equal(record.bytes(),bytes),"Owned record differs from original");
        require(record.bytes().data()!=bytes.data(),"Original record was borrowed, not copied");
        require(equal(record.header(),bytes.first(row[1])),"Header view mismatch");
        require(equal(record.prefix(),bytes.subspan(row[1],row[2])),"Prefix view mismatch");
        require(equal(record.code(),bytes.subspan(row[1]+row[2],row[3])),"Code view mismatch");
        require(be32(record.codeMetadata(),0)==row[2] && be32(record.codeMetadata(),4)==row[3],"Code metadata mismatch");
        require(registry.referenceCount(id)==1 && registry.capability(id)==MaterialCapability::Uncompiled,
                "Creation falsely published compilation or wrong refcount");
        require(registry.unsupportedReason(id).empty(),"New resource has an unsupported diagnostic");
        rejects([&]{registry.requireCompiled(id);});
        require(registry.capability(id)==MaterialCapability::Uncompiled,"Bind guard changed capability");
        const bool strippedStartup=recordIndex>=4 && recordIndex<20;
        ++recordIndex;
        if(recordIndex==151)
            require(vertices==70 && registry.liveCount()==151,"Original151 inventory prefix changed");
        if(strippedStartup) {
            require(record.identity().originalName.empty(),"Invented startup shader name");
            require(!meta.debugOffset && !meta.reflectionOffset,"Invented startup reflection");
        } else require(!record.identity().originalName.empty() && meta.reflectionOffset,"Missing named shader identity/reflection offset");
    }
    require(vertices==118 && std::size(expected)-vertices==138 && registry.liveCount()==256,"Original inventory counts differ");
}

void malformed(std::span<const uint8_t> image) {
    MaterialRegistry registry;
    for(const auto& identity:originalMaterialIdentities()) {
        const auto bytes=source(image,identity.originalAddress);
        rejects([&]{registry.create(identity.originalAddress,bytes.first(bytes.size()-1));});
        auto changed=std::vector<uint8_t>(bytes.begin(),bytes.end());
        changed.push_back(0);
        rejects([&]{registry.create(identity.originalAddress,changed);});
        changed.pop_back();changed.back()^=1;
        rejects([&]{registry.create(identity.originalAddress,changed);}); // Real digest validation.
    }
    const auto bytes=source(image,0x82199150);
    const auto good=registry.create(0x82199150,bytes);
    const size_t codeMetadata=registry.record(good).metadata().codeMetadataOffset;
    const std::pair<size_t,uint32_t> corruptions[]={
        {0,0x102A1101},{4,0xFFFFFFFC},{4,32},{4,0x79},{8,0xFFFFFFFF},
        {12,0xFFFFFFFC},{16,0x78},{20,35},{24,0},{24,0xFFFFFFFC},{24,0x74},
        {codeMetadata,0xFFFFFFFC},{codeMetadata,3},
        {codeMetadata+4,0xFFFFFFFF},{codeMetadata+4,0},{codeMetadata+4,25},
        {codeMetadata+4,0x84}};
    for(const auto& [offset,value]:corruptions) {
        auto changed=std::vector<uint8_t>(bytes.begin(),bytes.end());put32(changed,offset,value);
        rejects([&]{registry.create(0x82199150,changed);});
        require(registry.liveCount()==1 && registry.referenceCount(good)==1,"Validation failure altered existing owner");
    }
    rejects([&]{registry.create(0,bytes);});
    rejects([&]{registry.create(0x82199154,bytes);});
    rejects([&]{registry.create(0x82199150,image);});
    rejects([&]{registry.create(0x82199150,{});});
    require(equal(registry.record(good).bytes(),bytes),"Failure changed surviving bytes");
}

void sourceOwnership(std::span<const uint8_t> image) {
    MaterialRegistry registry;
    const auto original=source(image,0x821B8058);
    std::vector<uint8_t> scratch(original.begin(),original.end());
    auto a=registry.create(0x821B8058,scratch);
    auto b=registry.create(0x821B8058,scratch);
    require(a!=b,"Two original create calls were silently deduplicated");
    std::fill(scratch.begin(),scratch.end(),0);scratch.clear();scratch.shrink_to_fit();
    require(equal(registry.record(a).bytes(),original),"Caller source mutation changed owned bytes");
    require(equal(registry.record(b).bytes(),original),"Second copied record changed");
    registry.release(a);
    require(!registry.contains(a) && equal(registry.record(b).bytes(),original),"Independent resource lifetime failed");
}

void idsAndReferences(std::span<const uint8_t> image) {
    const auto bytes=source(image,0x821524C8);
    MaterialRegistry registry({1,2}),other;
    auto a=registry.create(0x821524C8,bytes);
    registry.retain(a);require(registry.referenceCount(a)==2,"Retain failed");
    rejects([&]{registry.retain(a);});
    require(registry.referenceCount(a)==2,"Reference overflow changed count");
    rejects([&]{registry.create(0x821524C8,bytes);});
    registry.release(a);require(registry.referenceCount(a)==1,"Nonfinal release failed");
    registry.release(a);require(!registry.contains(a) && registry.liveCount()==0,"Final release did not invalidate ID");
    auto b=registry.create(0x821524C8,bytes);
    require(b.slot==a.slot && b.generation!=a.generation,"Slot reuse lacks generation protection");
    rejects([&]{registry.release(a);});rejects([&]{registry.retain(a);});rejects([&]{registry.record(a);});
    const auto foreign=other.create(0x821524C8,bytes);
    require(foreign.slot==b.slot && !registry.contains(foreign) && !other.contains(b),"Cross-owner ID alias");
    rejects([&]{registry.record(foreign);});
    rejects([&]{registry.record({});});
    rejects([&]{registry.record({b.generation,UINT32_MAX});});
    registry.reset();require(!registry.contains(b) && !registry.liveCount(),"Reset left stale IDs valid");
    const auto c=registry.create(0x821524C8,bytes);
    require(c.slot==b.slot && c.generation!=b.generation,"Reset recycled a generation");
    require(registry.referenceCount(c)==1,"Failed stale operations damaged new resource");
    rejects([]{MaterialRegistry bad({0,1});});rejects([]{MaterialRegistry bad({1,0});});
    rejects([]{MaterialRegistry bad({},nullptr);});
}

void allocationRollback(std::span<const uint8_t> image) {
    auto bytes=source(image,0x82199150);
    for(size_t budget:{size_t(0),size_t(1)}) {
        FailingMemory memory;memory.budget=budget;
        {
            MaterialRegistry registry({},&memory);
            rejects<std::bad_alloc>([&]{registry.create(0x82199150,bytes);});
            require(registry.liveCount()==0 && memory.outstanding==0,"Partial allocation leaked/published a resource");
            memory.budget=std::numeric_limits<size_t>::max();
            auto id=registry.create(0x82199150,bytes);
            require(registry.contains(id),"Allocation failure left registry unusable");
        }
        require(memory.outstanding==0,"Registry destruction leaked PMR memory");
    }
    FailingMemory memory;
    {
        MaterialRegistry registry({},&memory);
        auto id=registry.create(0x82199150,bytes);const auto before=memory.outstanding;
        memory.budget=0;
        rejects<std::bad_alloc>([&]{registry.create(0x82199150,bytes);});
        require(memory.outstanding==before && registry.liveCount()==1 && registry.referenceCount(id)==1,
                "Failed replacement allocation altered prior owner");
        require(equal(registry.record(id).bytes(),bytes),"Allocation failure corrupted record");
        // Existing capacity is one: permit the pending byte copy, then fail
        // growth of the slot vector. The previous ID and its bytes must survive.
        memory.budget=1;
        rejects<std::bad_alloc>([&]{registry.create(0x82199150,bytes);});
        require(memory.outstanding==before && registry.contains(id) && registry.liveCount()==1 &&
                equal(registry.record(id).bytes(),bytes),"Slot growth failure corrupted/leaked a prior resource");
    }
    require(memory.outstanding==0,"Prior owner leaked after allocation failure");
}

void compileTransactions(std::span<const uint8_t> image) {
    MaterialRegistry registry;const auto bytes=source(image,0x821524C8);
    const auto id=registry.create(0x821524C8,bytes);
    FixtureCompiler compiler;int destroyed=0;
    compiler.action=[&](const auto& record)->std::unique_ptr<CompiledMaterial> {
        auto pending=std::make_unique<FixtureArtifact>(record.identity().originalAddress,record.identity().stage,destroyed);
        throw std::bad_alloc();
    };
    rejects<std::bad_alloc>([&]{registry.prepareForBind(id,compiler);});
    require(destroyed==1 && registry.capability(id)==MaterialCapability::Uncompiled,"Transient compile failure did not unwind");
    compiler.action=[](const auto&)->std::unique_ptr<CompiledMaterial> {return {};};
    rejects([&]{registry.prepareForBind(id,compiler);});
    for(bool wrongStage:{false,true}) {
        compiler.action=[&](const auto& record)->std::unique_ptr<CompiledMaterial> {
            return std::make_unique<FixtureArtifact>(wrongStage?record.identity().originalAddress:0x821525E8,
                wrongStage?MaterialStage::Vertex:record.identity().stage,destroyed);
        };
        rejects([&]{registry.prepareForBind(id,compiler);});
        require(registry.capability(id)==MaterialCapability::Uncompiled,"Invalid artifact published compilation");
    }
    require(destroyed==3 && registry.referenceCount(id)==1,"Wrong artifact lifetime/refcount");
    compiler.action=[&](const auto& record)->std::unique_ptr<CompiledMaterial> {
        return std::make_unique<FixtureArtifact>(record.identity().originalAddress,record.identity().stage,destroyed);
    };
    auto* artifact=&registry.prepareForBind(id,compiler);const auto calls=compiler.calls;
    require(registry.capability(id)==MaterialCapability::Compiled,"Accepted fixture artifact not owned");
    require(&registry.prepareForBind(id,compiler)==artifact && &registry.requireCompiled(id)==artifact && compiler.calls==calls,
            "Compiled artifact recreated or lost");
    registry.retain(id);registry.release(id);require(destroyed==3,"Retained artifact released early");
    registry.release(id);require(destroyed==4 && !registry.contains(id),"Compiled artifact not released exactly once");
    rejects([&]{registry.requireCompiled(id);});
    const auto next=registry.create(0x821524C8,bytes);
    registry.prepareForBind(next,compiler);registry.reset();
    require(destroyed==5 && !registry.contains(next),"Reset did not release compiled artifact");
}

void unsupportedBinding(std::span<const uint8_t> image) {
    MaterialRegistry registry;FixtureCompiler compiler;
    auto id=registry.create(0x821B7DD8,source(image,0x821B7DD8));
    compiler.action=[](const auto&)->std::unique_ptr<CompiledMaterial> {
        throw UnsupportedMaterial("Original startup shader has no verified native translation");
    };
    rejects<UnsupportedMaterial>([&]{registry.prepareForBind(id,compiler);});
    require(registry.capability(id)==MaterialCapability::Unsupported && compiler.calls==1,"Unsupported bind did not fail/cache state");
    require(registry.unsupportedReason(id)=="Original startup shader has no verified native translation","Missing unsupported reason");
    rejects<UnsupportedMaterial>([&]{registry.prepareForBind(id,compiler);});
    rejects<UnsupportedMaterial>([&]{registry.requireCompiled(id);});
    require(compiler.calls==1 && registry.referenceCount(id)==1,"Unsupported bind retried or damaged references");
    registry.retain(id);registry.release(id);registry.release(id);
    require(!registry.contains(id),"Unsupported CPU resource did not release");
}

void compilerReentry(std::span<const uint8_t> image) {
    MaterialRegistry registry;FixtureCompiler compiler;
    const auto bytes=source(image,0x821524C8);auto id=registry.create(0x821524C8,bytes);
    const std::function<void()> attempts[]={
        [&]{registry.release(id);},[&]{registry.retain(id);},[&]{registry.reset();},
        [&]{registry.create(0x821524C8,bytes);},[&]{registry.prepareForBind(id,compiler);}};
    for(const auto& attempt:attempts) {
        compiler.action=[&](const auto&)->std::unique_ptr<CompiledMaterial> {attempt();return {};};
        rejects([&]{registry.prepareForBind(id,compiler);});
        require(registry.contains(id) && registry.referenceCount(id)==1 &&
                registry.capability(id)==MaterialCapability::Uncompiled,"Compiler reentry changed/destroyed its input");
    }
    registry.release(id);require(registry.liveCount()==0,"Compiler guard stayed stuck after exception");
}

void teardownReentry(std::span<const uint8_t> image) {
    const auto bytes=source(image,0x821524C8);
    int destroyed=0,rejected=0;bool consistent=true;
    MaterialRegistry registry;FixtureCompiler compiler;
    auto a=registry.create(0x821524C8,bytes),b=registry.create(0x821524C8,bytes);
    compiler.action=[&](const auto& record)->std::unique_ptr<CompiledMaterial> {
        return std::make_unique<FixtureArtifact>(record.identity().originalAddress,record.identity().stage,destroyed,[&] {
            consistent=consistent && !registry.contains(a) && registry.contains(b) && registry.liveCount()==1;
            try {registry.create(0x821524C8,bytes);} catch(const MaterialError&) {++rejected;}
        });
    };
    registry.prepareForBind(a,compiler);registry.release(a);
    require(consistent && rejected==1 && destroyed==1,"Final release permitted callback mutation or inconsistent state");
    a=registry.create(0x821524C8,bytes);
    compiler.action=[&](const auto& record)->std::unique_ptr<CompiledMaterial> {
        return std::make_unique<FixtureArtifact>(record.identity().originalAddress,record.identity().stage,destroyed,[&] {
            consistent=consistent && !registry.contains(a) && !registry.contains(b) && registry.liveCount()==0;
            try {registry.create(0x821524C8,bytes);} catch(const MaterialError&) {++rejected;}
        });
    };
    registry.prepareForBind(a,compiler);registry.prepareForBind(b,compiler);registry.reset();
    require(consistent && rejected==3 && destroyed==3 && registry.liveCount()==0,
            "Reset permitted destructor replacement or exposed old IDs");
    const auto afterReset=registry.create(0x821524C8,bytes);
    registry.release(afterReset);require(registry.liveCount()==0,"Teardown guard/count remained corrupt");

    auto owner=std::make_unique<MaterialRegistry>();auto* raw=owner.get();
    const auto doomed=raw->create(0x821524C8,bytes);
    compiler.action=[&](const auto& record)->std::unique_ptr<CompiledMaterial> {
        return std::make_unique<FixtureArtifact>(record.identity().originalAddress,record.identity().stage,destroyed,[&] {
            consistent=consistent && !raw->contains(doomed) && raw->liveCount()==0;
            try {raw->create(0x821524C8,bytes);} catch(const MaterialError&) {++rejected;}
        });
    };
    raw->prepareForBind(doomed,compiler);owner.reset();
    require(consistent && rejected==4 && destroyed==4,"Registry destruction exposed live IDs or allowed mutation");
}

struct DiagnosticCallback final : UnsupportedMaterial {
    std::function<void()> callback;
    explicit DiagnosticCallback(std::function<void()> value)
        :UnsupportedMaterial("Untranslated material with callback diagnostic"),callback(std::move(value)) {}
    const char* what() const noexcept override {callback();return UnsupportedMaterial::what();}
};
void diagnosticReentry(std::span<const uint8_t> image) {
    MaterialRegistry registry;FixtureCompiler compiler;
    const auto id=registry.create(0x821524C8,source(image,0x821524C8));
    int rejected=0;
    compiler.action=[&](const auto&)->std::unique_ptr<CompiledMaterial> {
        throw DiagnosticCallback([&] {
            try {registry.release(id);} catch(const MaterialError&) {++rejected;}
            try {registry.reset();} catch(const MaterialError&) {++rejected;}
        });
    };
    rejects<UnsupportedMaterial>([&]{registry.prepareForBind(id,compiler);});
    require(rejected==2 && registry.contains(id) && registry.referenceCount(id)==1 &&
            registry.capability(id)==MaterialCapability::Unsupported,
            "Exception diagnostic destroyed the resource before state publication");
    require(registry.unsupportedReason(id)=="Untranslated material with callback diagnostic","Unsupported diagnostic lost");
    registry.release(id);require(!registry.contains(id),"Diagnostic guard remained active");
}
}

int main(int argc,char** argv) {
    static_assert(std::is_same_v<decltype(std::declval<const MaterialRecord&>().bytes()),std::span<const uint8_t>>);
    static_assert(!std::is_copy_constructible_v<MaterialRegistry>);
    try {
        if(argc!=2) throw std::runtime_error("Supply the original flat analysis/simpsons.pe path");
        std::ifstream input(argv[1],std::ios::binary|std::ios::ate);
        if(!input || input.tellg()!=std::streampos(15466496)) throw std::runtime_error("Wrong/missing original image extent");
        std::vector<uint8_t> image(15466496);input.seekg(0);
        if(!input.read(reinterpret_cast<char*>(image.data()),static_cast<std::streamsize>(image.size())))
            throw std::runtime_error("Failed to read original image");
        const std::pair<const char*,void(*)(std::span<const uint8_t>)> cases[]={
            {"256 pinned original records and immutable metadata",originalRecords},
            {"malformed framing and identity rejection",malformed},
            {"source copy and independent ownership",sourceOwnership},
            {"reference limits, stale generations and foreign IDs",idsAndReferences},
            {"allocation failure rollback",allocationRollback},
            {"compile transaction and artifact lifetime",compileTransactions},
            {"explicit unsupported first bind",unsupportedBinding},
            {"compiler reentry rejection",compilerReentry},
            {"backend teardown reentry and prior ID invalidation",teardownReentry},
            {"unsupported diagnostic callback reentry",diagnosticReentry}};
        for(const auto& [name,test]:cases) {test(image);std::printf("PASS: %s\n",name);}
        std::printf("10 material owner test groups passed (%zu checks); no native shader compilation or rendering claimed.\n",checks);
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"Material resources: %s\n",error.what());return 1;}
}
