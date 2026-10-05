// Standalone CPU state tests; argv[1] is the read-only original flat image.
// No D3D device, guest writes, generated runtime, or GPU command interpreter.
#include "renderer/engine_state.h"
#include <bit>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string_view>
#include <vector>

using namespace Simpsons::Graphics;
namespace {
size_t checks=0;
void require(bool value,const char* reason) {
    ++checks;if(!value) throw std::runtime_error(reason);
}
template<class F> void rejects(F&& action,std::string_view part={}) {
    ++checks;
    try {action();}
    catch(const EngineStateError& error) {
        if(std::string_view(error.what()).find(part)==std::string_view::npos)
            throw std::runtime_error(std::string("Wrong rejection reason: expected '")+std::string(part)+"' in '"+error.what()+"'");
        ++checks;return;
    }
    throw std::runtime_error("Expected explicit state rejection");
}
uint32_t word(std::span<const uint8_t> image,uint32_t va) {
    const auto offset=size_t(va-0x82000000);
    require(va>=0x82000000 && offset<=image.size() && image.size()-offset>=4,"Evidence word outside image");
    return uint32_t(image[offset])<<24 | uint32_t(image[offset+1])<<16 |
           uint32_t(image[offset+2])<<8 | uint32_t(image[offset+3]);
}
void verifyOriginal(std::span<const uint8_t> image) {
    require(image.size()==15466496,"Unexpected original image extent");
    for(const auto& f:scalarStateEvidence()) {
        const uint32_t row=0x82CD28B8+12*(f.id/4);
        require(word(image,row+4)==f.setterAddress,"Original scalar setter identity changed");
        require(word(image,row+8)==f.sdkDefault,"Original scalar default changed");
    }
    for(const auto& f:samplerStateEvidence()) {
        const uint32_t row=0x82CD2D78+12*(f.id/4);
        require(word(image,row+4)==f.setterAddress,"Original sampler setter identity changed");
        require(word(image,row+8)==f.sdkDefault,"Original sampler default changed");
    }
    // Independent original instruction anchors for engine overrides, inline mip
    // initialization, direct quad calls/words, and arithmetic. These are byte
    // assertions, not an instruction interpreter or an invented data schema.
    constexpr uint32_t anchors[][2]={
        {0x824009EC,0x38800006},{0x824009F4,0x3860002C},{0x824009FC,0x4BFFF775},
        {0x82400A00,0x38800001},{0x82400A04,0x38600030},{0x82400A08,0x4BFFF769},
        {0x82400A0C,0x38600028},{0x82400A10,0x4BFFF761},
        {0x82400A18,0x38800000},{0x82400A1C,0x3860006C},{0x82400A28,0x4BFFF749},
        {0x82400A2C,0x38600074},{0x82400A34,0x38600078},{0x82400A3C,0x3860007C},
        {0x82400A44,0x38800007},{0x82400A48,0x38600080},{0x82400A50,0x38800000},
        {0x82400A54,0x38600084},{0x82400A60,0x38600088},{0x82400A68,0x3860008C},
        {0x82400C44,0x38800006},{0x82400C48,0x38600048},{0x82400C4C,0x4BFFF525},
        {0x82400C50,0x38800007},{0x82400C54,0x3860004C},{0x82400C58,0x4BFFF519},
        {0x82400C60,0x38800004},{0x82400C64,0x38600068},{0x82400C74,0x4BFFF4FD},
        {0x82400C7C,0x38800000},{0x82400C80,0x38600064},{0x82400C90,0x4BFFF4E1},
        {0x82400C94,0x3860003C},{0x82400C98,0x4BFFF4D9},{0x82400C9C,0x38600060},
        {0x82400CA0,0x4BFFF4D1},{0x82400CB8,0x38800002},{0x82400CBC,0x38600038},
        {0x82400CC0,0x4BFFF4B1},
        {0x8206AA70,0x07030700},{0x82400AEC,0x556905AD},
        {0x82400B0C,0x38A00001},{0x82400B1C,0x4803AF25},
        {0x82400B2C,0x38A00001},{0x82400B3C,0x4803B095},
        {0x82400B8C,0x2B0B0002},{0x82400BA4,0x52E9C1D0},
        {0x82400BB8,0x38A00000},{0x82400BBC,0x38800000},{0x82400BC4,0x4BFFF6B5},
        {0x82400BC8,0x38A00000},{0x82400BCC,0x38800004},{0x82400BD4,0x4BFFF6A5},
        {0x82400BD8,0x38A00000},{0x82400BDC,0x3880000C},{0x82400BEC,0x4BFFF68D},
        {0x82400BF0,0x38A00001},{0x82400BF4,0x38800024},{0x82400C04,0x4BFFF675},
        {0x8243A02C,0x55692036},{0x8243A030,0x716A1010},{0x8243A038,0x554A601E},
        {0x8243A03C,0x554A0314},{0x8243A040,0x554A0104},{0x8243A044,0x7D4B5B78},
        {0x8243A058,0x91632938},{0x8243A05C,0x91632958},
        {0x8243A060,0x9163295C},{0x8243A064,0x91632960},{0x8243CBA8,0x7CAA192E},
        {0x8243A6CC,0x90832E5C},{0x8243A6D8,0x38800000},{0x8243A6E0,0x508B0FBC},
        {0x8243A774,0x90832E60},{0x8243A780,0x38800000},{0x8243A788,0x508B07FE},
        {0x821DD204,0x3B808081},{0x8243A44C,0xFDA00018},{0x8243A454,0xEC0D0032},
        {0x8243C1A0,0x50A954EA},{0x8243C1F0,0x50A96C24},{0x8243C240,0x50A9835E},
        {0x82756554,0x4BCE4175},{0x82756560,0x4BCE39A1},
        {0x82756564,0x38800001},{0x8275656C,0x4BCE39F5},{0x82756570,0x38800004},
        {0x82756578,0x4BCE3F29},{0x8275657C,0x38800001},{0x82756584,0x4BCE3EB5},
        {0x827565A8,0x60A50001},{0x827565AC,0x4BCE65D5},{0x827565C0,0x4BCE4DF1},
        {0x8275662C,0x4BCE5415},{0x8275663C,0x4BCE5595},
        {0x82756658,0x554A05A4},{0x82756674,0x554A04DE},
        {0x82756728,0x60A50186},{0x8275672C,0x4BCE6455},
        {0x82756734,0x60A50706},{0x82756738,0x4BCE6449},
        {0x82756740,0x60A50106},{0x82756744,0x4BCE643D},
        {0x8275681C,0x4BCE37F5},{0x82756828,0x4BCE3739},{0x82756834,0x4BCE3E95}
    };
    for(const auto& entry:anchors) require(word(image,entry[0])==entry[1],"Original state instruction/constant changed");
}
std::vector<uint32_t> values(const EngineState& state) {
    std::vector<uint32_t> result;
    for(const auto& f:scalarStateEvidence()) result.push_back(state.scalar(f.id));
    for(uint32_t stage=0;stage<16;++stage)
        for(const auto& f:samplerStateEvidence()) result.push_back(state.sampler(stage,f.id));
    for(uint32_t target=0;target<4;++target) result.push_back(state.effectiveBlend(target));
    return result;
}

void originalEvidence(std::span<const uint8_t> image) {
    require(scalarStateEvidence().size()==82 && samplerStateEvidence().size()==20,"Unexpected evidence extent");
    verifyOriginal(image);
    auto altered=std::vector<uint8_t>(image.begin(),image.end());
    altered.at(0x82CD28B8-0x82000000+12*(0x2C/4)+11)^=1;
    bool failed=false;
    try {verifyOriginal(altered);} catch(const std::runtime_error&) {failed=true;}
    require(failed,"Altered original defaults were accepted");
}
void initialization() {
    EngineState empty;
    require(!empty.initialized(),"Implicit baseline was invented");
    rejects([&]{empty.scalar(ScalarState::DepthEnable);});
    rejects([&]{empty.setScalar(ScalarState::DepthEnable,0);});
    rejects([&]{empty.sampler(0,SamplerState::AddressU);});
    rejects([&]{empty.setSampler(0,SamplerState::AddressU,0);});
    rejects([&]{empty.setPackedBlend(0,0x10001);});
    rejects([&]{empty.effectiveBlend(0);});
    rejects([&]{empty.requestedBlendWord();});
    rejects([&]{empty.effectiveDepthTest(false);});
    rejects([&]{empty.effectiveStencilTest(false);});
    rejects([&]{empty.requireScreenState(false);});
    rejects([&]{empty.applyScreenQuadState(3,false);});
    rejects([&]{empty.finishScreenQuadState(3);});
    const auto state=EngineState::fromOriginalStartup();
    require(state.initialized(),"Explicit initialization failed");
    for(const auto& f:scalarStateEvidence()) require(state.scalar(f.id)==f.startupValue,"Startup scalar mismatch");
    constexpr uint32_t expectedSampler[]={0,0,0,0,1,1,2,0,0,1,0,0,0,13,0,0,0,0,0,1};
    for(uint32_t stage=0;stage<8;++stage)
        for(uint32_t i=0;i<20;++i) require(state.sampler(stage,i*4)==expectedSampler[i],"Startup sampler mismatch");
    for(uint32_t stage=8;stage<16;++stage)
        for(const auto& f:samplerStateEvidence()) require(state.sampler(stage,f.id)==f.sdkDefault,"Non-RenderWare stage did not inherit the original SDK default");
    require(state.scalar(0x2C)==6 && state.scalar(0x38)==2,"Engine depth/cull overrides missing");
    require(state.requestedBlendWord()==0x00010706,"Retained source-alpha blend factors missing");
    for(uint32_t i=0;i<4;++i) require(state.effectiveBlend(i)==0x00010001,"Startup blending was enabled incorrectly");
    rejects([&]{state.requireScreenState(false);},"depth request off");
}
void depthAndStencil() {
    auto state=EngineState::fromOriginalStartup();
    require(state.effectiveDepthTest(true) && !state.effectiveDepthTest(false),"Depth surface dependence lost");
    require(!state.effectiveStencilTest(true),"Startup stencil active");
    state.setScalar(ScalarState::StencilEnable,1);
    require(state.effectiveStencilTest(true) && !state.effectiveStencilTest(false),"Stencil surface dependence lost");
    state.applyScreenQuadState(3,false);
    require(!state.effectiveDepthTest(true) && state.scalar(ScalarState::DepthWrite)==1 && state.scalar(ScalarState::DepthCompare)==6,
            "Quad discarded retained depth policy");
    rejects([&]{state.requireScreenState(false);},"stencil request off");
    state.setScalar(ScalarState::StencilEnable,0);
    const auto snapshot=state.requireScreenState(false);
    require(!snapshot.depthTest && !snapshot.stencilTest && snapshot.retainedDepthWrite && snapshot.retainedDepthCompare==6,
            "Gate incorrectly requires raw depth writes disabled");
    state.finishScreenQuadState(3);
    require(state.effectiveDepthTest(true) && !state.effectiveDepthTest(false),"Cleanup depth request was not retained");
}
void blendShadowAndPublication() {
    auto state=EngineState::fromOriginalStartup();
    const auto requested=state.requestedBlendWord();
    state.setPackedBlend(0,0x10106);
    state.setPackedBlend(2,0xDEADBEEF);
    require(state.requestedBlendWord()==requested && state.scalar(ScalarState::BlendEnable)==0,"Packed blend altered scalar shadows");
    require(state.effectiveBlend(0)==0x10106 && state.effectiveBlend(1)==0x10001 && state.effectiveBlend(2)==0xDEADBEEF,
            "Packed blend was gated on enable or broadcast to other targets");
    state.setScalar(ScalarState::SourceBlend,1);
    state.setScalar(ScalarState::DestinationBlend,0);
    state.setScalar(ScalarState::BlendOperation,4);
    state.setScalar(ScalarState::AlphaSourceBlend,7);
    require(state.effectiveBlend(0)==0x10106 && state.effectiveBlend(2)==0xDEADBEEF,"Disabled shadow update published unexpectedly");
    state.setScalar(ScalarState::BlendEnable,0); // Equal value MUST still execute.
    for(uint32_t i=0;i<4;++i) require(state.effectiveBlend(i)==0x10001,"Equal disabled call did not broadcast replacement");
    state.setScalar(ScalarState::SeparateAlpha,1);
    state.setScalar(ScalarState::BlendEnable,1);
    require(state.effectiveBlend(0)==state.requestedBlendWord(),"Separate-alpha publication did not use requested fields");
    state.setScalar(ScalarState::AlphaDestinationBlend,6);
    require(state.effectiveBlend(3)==state.requestedBlendWord(),"Enabled separate alpha failed to broadcast");
    state.setScalar(ScalarState::SeparateAlpha,0);
    const auto inherited=state.effectiveBlend(0);
    state.setScalar(ScalarState::AlphaBlendOperation,3);
    require(state.effectiveBlend(0)==inherited,"Inactive separate-alpha operation published");
    state.setPackedBlend(0,0x10706);
    state.setScalar(ScalarState::SeparateAlpha,0); // Also equal, also rebuilds.
    require(state.effectiveBlend(0)==inherited,"Equal separate-alpha setter failed to rebuild");
}
void blendFactorTransform() {
    auto state=EngineState::fromOriginalStartup();
    state.setScalar(ScalarState::BlendEnable,1);
    for(uint32_t source=0;source<32;++source) for(uint32_t dest=0;dest<32;++dest) {
        state.setScalar(ScalarState::SourceBlend,source);
        state.setScalar(ScalarState::DestinationBlend,dest);
        for(uint32_t op=0;op<8;++op) {
            state.setScalar(ScalarState::BlendOperation,op);
            // Independently expressed factor mapping from the original bit moves.
            const auto alphaSource=(source&15)|(source>>4);
            const auto alphaDest=(dest&15)|(dest>>4);
            const auto expected=source|(op<<5)|(dest<<8)|(alphaSource<<16)|(op<<21)|(alphaDest<<24);
            require(state.effectiveBlend(0)==expected && state.effectiveBlend(3)==expected,"Nonseparate alpha factor transform mismatch");
        }
    }
}
void quadAndCleanup() {
    auto state=EngineState::fromOriginalStartup();
    state.setScalar(ScalarState::DepthCompare,1);
    state.setScalar(ScalarState::DepthWrite,0);
    state.setScalar(ScalarState::Cull,7);
    state.setScalar(ScalarState::AlphaReference,123);
    state.setSampler(0,SamplerState::Minification,0);
    state.setSampler(0,SamplerState::AddressU,3);
    state.setSampler(7,SamplerState::AddressV,6);
    state.applyScreenQuadState(3,false);
    const auto flat=state.requireScreenState(false);
    require(flat.blendSelector==3 && flat.effectiveBlendWord==0x10001 && !flat.retainedBlendEnable,"Flat quad effective blend incorrect");
    require(flat.alphaTest && flat.alphaCompare==4 && flat.alphaReferenceInteger==1 && !flat.sampler,"Flat quad alpha/sampler behavior incorrect");
    require(std::bit_cast<uint32_t>(flat.alphaReference)==0x3B808081,"Original alpha integer 1 became wrong float");
    require(!flat.retainedDepthWrite && flat.retainedDepthCompare==1,"Quad overwrote inherited depth policy");
    require(state.sampler(0,SamplerState::Minification)==0 && state.sampler(0,SamplerState::AddressU)==3,"Flat quad unexpectedly changed sampler");
    rejects([&]{state.requireScreenState(true);},"repeat U/V/W");
    state.applyScreenQuadState(3,true);
    const auto textured=state.requireScreenState(true);
    require(textured.sampler && textured.sampler->minification==1 && textured.sampler->magnification==1 &&
            textured.sampler->mipFilter==2 && textured.sampler->minimumMip==0 && textured.sampler->maximumMip==13 &&
            textured.sampler->maximumAnisotropy==1 && textured.sampler->lodBias==0,"Effective sampler baseline mismatch");
    require(state.sampler(7,SamplerState::AddressV)==6,"Quad changed another sampler stage");
    state.finishScreenQuadState(3);
    require(state.scalar(ScalarState::Cull)==0 && state.scalar(ScalarState::AlphaReference)==1 && state.scalar(ScalarState::AlphaCompare)==4,
            "Cleanup falsely restored previous state");
    require(state.scalar(ScalarState::DepthEnable)==1 && state.scalar(ScalarState::AlphaTest)==0,"Cleanup missed direct scalar writes");
    require(state.sampler(0,SamplerState::Minification)==1 && state.sampler(0,SamplerState::AddressU)==0,"Cleanup falsely restored sampler");
}
void inheritedGates() {
    auto base=EngineState::fromOriginalStartup();base.applyScreenQuadState(3,true);
    auto state=base;
    state.setScalar(ScalarState::AlphaToMask,1);
    rejects([&]{state.requireScreenState(false);},"Alpha-to-mask");
    state.setScalar(ScalarState::AlphaToMask,0);
    state.setScalar(ScalarState::AlphaToMaskOffsets,0x5A);
    state.setScalar(ScalarState::BlendConstant,0x12345678);
    auto retained=state.requireScreenState(false);
    require(retained.retainedAlphaToMaskOffsets==0x5A && retained.retainedBlendConstant==state.blendConstant(),
            "Screen snapshot lost inactive coverage offsets or blend constant");
    state=base;
    state.setScalar(ScalarState::ColorMask0,7);state.applyScreenQuadState(3,true);
    rejects([&]{state.requireScreenState(true);},"write mask F");
    state=base;state.setScalar(ScalarState::StencilEnable,1);state.applyScreenQuadState(3,true);
    rejects([&]{state.requireScreenState(true);},"stencil request off");
    state=base;state.setSampler(0,SamplerState::AddressW,1);state.applyScreenQuadState(3,true);
    rejects([&]{state.requireScreenState(true);},"repeat U/V/W");
    state.requireScreenState(false); // Inactive texture sampler does not affect flat shader.
    state=base;state.setSampler(0,SamplerState::MipFilter,1);state.applyScreenQuadState(3,true);
    rejects([&]{state.requireScreenState(true);},"base-map-only");
    state=base;state.setSampler(0,SamplerState::Magnification,0);
    rejects([&]{state.requireScreenState(true);},"linear minification");
    state=base;state.setScalar(ScalarState::AlphaCompare,7);
    rejects([&]{state.requireScreenState(false);},"GREATER");
    state.setScalar(ScalarState::AlphaTest,0);state.requireScreenState(false);
    state=base;state.setPackedBlend(0,0xDEADBEEF);
    rejects([&]{state.requireScreenState(false);},"blend equation");
    state=base;state.setScalar(ScalarState::Cull,2);
    rejects([&]{state.requireScreenState(false);},"no culling");
    constexpr uint32_t words[]={0x10106,0x10706,0x10186};
    for(uint32_t selector=0;selector<3;++selector) {
        state=base;state.applyScreenQuadState(selector,true);
        require(state.effectiveBlend(0)==words[selector] && state.scalar(ScalarState::ExpandedBlend0)==1,"Quad blend selection/precision request lost");
        rejects([&]{state.requireScreenState(true);},"Expanded blending precision");
        state.applyScreenQuadState(3,false); // Selector 3 does not reset prior request!
        rejects([&]{state.requireScreenState(false);},"Expanded blending precision");
        state.finishScreenQuadState(selector);
        require(state.scalar(ScalarState::ExpandedBlend0)==0 && state.effectiveBlend(0)==0x10001,"Expanded cleanup incorrect");
    }
    // All four effective equations can be represented; this does not bypass
    // the original helper's independent expanded-precision requirement.
    for(uint32_t selector=0;selector<3;++selector) {
        state=base;state.setPackedBlend(0,words[selector]);
        require(state.requireScreenState(false).blendSelector==selector,"Packed equation cannot be represented independently");
    }
}
void rejectedTransactions() {
    auto state=EngineState::fromOriginalStartup();state.applyScreenQuadState(3,true);
    for(const auto& request:std::array<std::array<uint32_t,2>,5>{{{0xC8,1},{0xCC,0xBA83126F},{0xCC,0x3B03126F},{0xD0,0xB9D1B717},{0xD0,0xBB449BA6}}}) {
        auto pending=state;pending.setScalar(request[0],request[1]);
        require(pending.scalar(request[0])==request[1],"Original shadow state request was converted or discarded");
        rejects([&]{pending.requireOriginalScreenState(true);}); // State ownership does not authorize a screen draw.
    }
    const auto before=values(state);
    for(uint32_t id:{0U,1U,0x29U,0x160U,0x164U,0x174U,0x17CU,0x196U,0x1A1U,0xFFFFFFFFU}) {
        rejects([&]{state.setScalar(id,0);});rejects([&]{state.scalar(id);});
    }
    constexpr uint32_t invalidScalar[][2]={
        {0x28,2},{0x3C,2},{0x40,2},{0x60,2},{0x6C,2},{0x134,2},{0x150,2},{0x154,256},{0x70,2},
        {0x2C,8},{0x38,8},{0x68,8},{0x48,32},{0x4C,32},{0x50,8},
        {0x54,32},{0x58,32},{0x5C,8},{0x64,256},{0x64,0x3F800000},
        {0xD4,16},{0xCC,0x7F800000},{0xCC,0xFF800000},{0xCC,0x7FC00000},{0xD0,0x7FC00000},{0x34,1},
        {0x74,1},{0x84,256},{0x88,256},{0x8C,256},{0xA0,256},{0xA4,256},{0xA8,256},
        {0x168,1},{0x16C,1},{0x170,1},{0x130,0},{0xC8,2},{0x144,2},{0x158,0},{0x15C,0},
        {0xAC,1},{0xC0,0},{0xC4,0},{0xB0,0},{0xB4,0},{0xB8,1},{0xBC,0},
        {0xE4,2},{0xE8,0},{0xEC,0},{0x178,2},{0xF0,1},{0x12C,1},{0x148,2},{0x14C,0}
    };
    for(const auto& entry:invalidScalar) rejects([&]{state.setScalar(entry[0],entry[1]);});
    for(uint32_t id:{1U,0x50U,0xFFFFFFFFU})
        rejects([&]{state.setSampler(0,id,0);});
    constexpr uint32_t invalidSampler[][2]={
        {0,8},{4,8},{8,8},{0xC,1},{0x10,2},{0x14,4},{0x18,3},
        {0x1C,0x3F800000},{0x1C,0x7F800000},{0x1C,0x7FC00000},
        {0x20,1},{0x24,0},{0x24,16},{0x34,0},
        {0x28,2},{0x2C,2},{0x30,2},{0x38,1},{0x3C,0x3F800000},{0x40,1},{0x44,1},{0x48,1},{0x4C,0}
    };
    for(const auto& entry:invalidSampler) rejects([&]{state.setSampler(0,entry[0],entry[1]);});
    for(uint32_t stage:{16U,0xFFFFFFFFU}) {
        rejects([&]{state.setSampler(stage,0,0);});rejects([&]{state.sampler(stage,0);});
    }
    for(uint32_t target:{4U,0xFFFFFFFFU}) {
        rejects([&]{state.setPackedBlend(target,0x10001);});rejects([&]{state.effectiveBlend(target);});
    }
    for(uint32_t selector:{4U,0xFFFFFFFFU}) {
        rejects([&]{state.applyScreenQuadState(selector,true);});rejects([&]{state.finishScreenQuadState(selector);});
    }
    require(values(state)==before,"Rejected change partially mutated host effective state");
}
void alphaAndSnapshots() {
    auto state=EngineState::fromOriginalStartup();state.applyScreenQuadState(3,true);
    const auto snapshot=state.requireScreenState(true);
    const auto original=state;
    for(uint32_t value=0;value<=255;++value) {
        state.setScalar(ScalarState::AlphaReference,value);
        // Each byte converts exactly to float. Double evaluates the product
        // exactly before one float32 rounding, independent of division by 255.
        const float expected=static_cast<float>(double(value)*double(std::bit_cast<float>(uint32_t{0x3B808081})));
        require(std::bit_cast<uint32_t>(state.requireScreenState(true).alphaReference)==std::bit_cast<uint32_t>(expected),
                "Alpha reference does not match original float32 multiply");
    }
    require(snapshot.alphaReferenceInteger==1 && std::bit_cast<uint32_t>(snapshot.alphaReference)==0x3B808081,
            "Value snapshot aliased later state");
    require(original.scalar(ScalarState::AlphaReference)==1,"Independent copied owner changed");
    state.setSampler(0,SamplerState::MipFilter,0);
    require(snapshot.sampler->mipFilter==2 && original.sampler(0,SamplerState::MipFilter)==2,"Sampler snapshot/copy aliasing");
    state=EngineState::fromOriginalStartup();
    require(state.scalar(ScalarState::AlphaReference)==0 && state.sampler(0,SamplerState::MipFilter)==2,"Explicit new startup state was not independent");
    state.applyScreenQuadState(3,false);
    state.setScalar(ScalarState::HalfPixelOffset,1);
    rejects([&]{state.requireScreenState(false);},"half-pixel raster policy");
    state.setScalar(ScalarState::HalfPixelOffset,0);
    state.setScalar(ScalarState::GuardBandX,0x3F800000);
    rejects([&]{state.requireScreenState(false);},"guardband raster policy");
    state=EngineState::fromOriginalStartup();state.applyScreenQuadState(3,true);
    state.setSampler(0,SamplerState::SeparateZFilter,1);
    rejects([&]{state.requireScreenState(true);},"Separate volume filtering");
}
void im2dAddressing() {
    auto base=EngineState::fromOriginalStartup();base.applyScreenQuadState(3,true);
    base.setScalar(ScalarState::HalfPixelOffset,1);
    base.setScalar(ScalarState::GuardBandX,0x3F800000);base.setScalar(ScalarState::GuardBandY,0x3F800000);
    for(uint32_t u:{0u,2u})for(uint32_t v:{0u,2u}) {
        auto state=base;state.setSampler(0,SamplerState::AddressU,u);state.setSampler(0,SamplerState::AddressV,v);
        state.setSampler(0,SamplerState::AddressW,2);state.setSampler(0,SamplerState::MipFilter,0);
        const auto before=values(state);const auto checked=state.requireOriginalIm2DScreenState(true);
        require(checked.sampler && checked.sampler->addressU==u && checked.sampler->addressV==v &&
            checked.sampler->addressW==2 && checked.sampler->mipFilter==0 &&
            checked.sampler->minification==1 && checked.sampler->magnification==1,
            "Im2D addressing qualification changed retained sampler requests");
        require(values(state)==before,"Im2D sampler validation mutated effective state");
        if(u||v)rejects([&]{state.requireOriginalScreenState(true);},"repeat U/V/W");
    }
    for(auto field:{SamplerState::AddressU,SamplerState::AddressV})for(uint32_t value:{1u,3u,4u,5u,6u,7u}) {
        auto state=base;state.setSampler(0,field,value);const auto before=values(state);
        rejects([&]{state.requireOriginalIm2DScreenState(true);},"repeat/clamp U/V only");
        require(values(state)==before,"Rejected Im2D address mode mutated effective state");
        require(!state.requireOriginalIm2DScreenState(false).sampler,"Flat Im2D unexpectedly consumed an inactive sampler");
    }
    auto state=base;state.setSampler(0,SamplerState::AddressU,2);state.setSampler(0,SamplerState::Magnification,0);
    rejects([&]{state.requireOriginalIm2DScreenState(true);},"linear minification");
    state=base;state.setScalar(ScalarState::StencilEnable,1);
    rejects([&]{state.requireOriginalIm2DScreenState(true);},"stencil request off");
}
std::vector<uint32_t> snapshotValues(const ScreenStateSnapshot& s) {
    std::vector<uint32_t> v={s.depthTest,s.stencilTest,s.retainedDepthWrite,s.retainedDepthCompare,
        s.cull,s.fill,s.colorMask,s.retainedBlendEnable,s.retainedSeparateAlpha,s.effectiveBlendWord,
        s.blendSelector,s.expandedBlendRequested,s.alphaTest,s.alphaCompare,s.alphaReferenceInteger,
        std::bit_cast<uint32_t>(s.alphaReference),s.retainedAlphaToMaskOffsets,uint32_t(bool(s.sampler))};
    for(float f:s.retainedBlendConstant)v.push_back(std::bit_cast<uint32_t>(f));
    if(s.sampler) {
        const auto& p=*s.sampler;
        v.insert(v.end(),{p.minification,p.magnification,p.mipFilter,p.addressU,p.addressV,p.addressW,
            p.maximumAnisotropy,p.minimumMip,p.maximumMip,std::bit_cast<uint32_t>(p.lodBias)});
    }
    return v;
}
// Preserve the former copy-and-override algorithm as an independent regression
// oracle. It delegates only the unchanged ordinary screen policy, never the
// optimized original/Im2D qualification under test.
ScreenStateSnapshot legacyOriginalGate(EngineState state,bool textured,unsigned policy) {
    using S=ScalarState;using P=SamplerState;
    auto need=[](bool yes){if(!yes)throw EngineStateError("Legacy gate rejection");};
    if(policy==2) {
        const auto cull=state.scalar(S::Cull),blend=state.effectiveBlend(0);
        need(cull==0 || cull==2 || cull==6);
        need(blend==0x07060706 || blend==0x00010001 || blend==0x00010706 ||
             blend==0x00010106 || blend==0x00010186 || blend==0x01000100);
        state.setScalar(S::Cull,0);state.setScalar(S::DepthEnable,0);state.setPackedBlend(0,0x00010001);
    }
    const auto u=state.sampler(0,P::AddressU),v=state.sampler(0,P::AddressV);
    if(policy && textured) {
        need((u==0 || u==2) && (v==0 || v==2));
        state.setSampler(0,P::AddressU,0);state.setSampler(0,P::AddressV,0);
    }
    need(state.scalar(S::HalfPixelOffset)==1);
    need(state.scalar(S::GuardBandX)==0x3F800000 && state.scalar(S::GuardBandY)==0x3F800000);
    need(!state.scalar(S::ClipPlaneEnable) && !state.scalar(S::ScissorEnable) && state.scalar(S::ViewportEnable)==1);
    need(!state.scalar(S::Wrap0));
    for(auto id:{S::ExpandedBlend1,S::ExpandedBlend2,S::ExpandedBlend3})need(!state.scalar(id));
    const auto expanded=state.scalar(S::ExpandedBlend0),w=state.sampler(0,P::AddressW),mip=state.sampler(0,P::MipFilter);
    state.setScalar(S::HalfPixelOffset,0);state.setScalar(S::GuardBandX,0x40000000);
    state.setScalar(S::GuardBandY,0x40000000);state.setScalar(S::ExpandedBlend0,0);
    if(textured) {
        state.setSampler(0,P::AddressW,0);state.setSampler(0,P::MipFilter,2);state.setSampler(0,P::SeparateZFilter,0);
    }
    auto result=state.requireScreenState(textured);result.expandedBlendRequested=expanded!=0;
    if(textured) {
        result.sampler->addressW=w;result.sampler->mipFilter=mip;
        if(policy){result.sampler->addressU=u;result.sampler->addressV=v;}
    }
    return result;
}
void directGateEquivalence() {
    auto base=EngineState::fromOriginalStartup();base.applyScreenQuadState(3,true);
    // Check all aligned IDs, holes and unaligned offsets against the recovered
    // registration list; boundary/unknown IDs must still fail before indexing.
    for(uint32_t id=0;id<0x200;++id) {
        bool registered=false;for(const auto& field:scalarStateEvidence())registered|=field.id==id;
        if(registered)(void)base.scalar(id);
        else {rejects([&]{base.scalar(id);});rejects([&]{base.setScalar(id,0);});}
    }
    for(uint32_t id:{0xFFFFFFFCu,0xFFFFFFFFu}) {
        rejects([&]{base.scalar(id);});rejects([&]{base.setScalar(id,0);});
    }
    base.setScalar(ScalarState::HalfPixelOffset,1);base.setScalar(ScalarState::GuardBandX,0x3F800000);
    base.setScalar(ScalarState::GuardBandY,0x3F800000);
    size_t cases=0;
    auto compare=[&](const EngineState& state) {
        const auto before=values(state);
        for(bool textured:{false,true})for(unsigned policy=0;policy<3;++policy) {
            std::optional<ScreenStateSnapshot> expected,actual;
            try {expected=legacyOriginalGate(state,textured,policy);}catch(const EngineStateError&){}
            try {actual=policy==0?state.requireOriginalScreenState(textured):policy==1?
                state.requireOriginalIm2DScreenState(textured):state.requireOriginalIm2DDrawState(textured);}
            catch(const EngineStateError&){}
            require(bool(expected)==bool(actual),"Direct state gate changed acceptance/rejection");
            if(expected)require(snapshotValues(*expected)==snapshotValues(*actual),"Direct state gate changed a retained snapshot field");
            ++cases;
        }
        require(values(state)==before,"Direct state gate changed its owner");
    };
    compare(base);
    for(const auto& f:scalarStateEvidence())for(uint32_t value:{0u,1u,2u,3u,4u,6u,7u,8u,15u,255u,
        0xFFFFu,0xFFFFFFFFu,0x3F800000u,0x40000000u,f.sdkDefault,f.startupValue}) {
        auto state=base;try {state.setScalar(f.id,value);}catch(const EngineStateError&){continue;}compare(state);
    }
    for(uint32_t stage:{0u,1u,15u})for(const auto& f:samplerStateEvidence())
        for(uint32_t value:{0u,1u,2u,3u,4u,5u,6u,7u,13u,f.sdkDefault,f.startupValue}) {
            auto state=base;try {state.setSampler(stage,f.id,value);}catch(const EngineStateError&){continue;}compare(state);
        }
    for(uint32_t depth:{0u,1u})for(uint32_t cull:{0u,2u,6u})for(uint32_t expanded:{0u,1u})
        for(uint32_t blend:{0x07060706u,0x00010001u,0x00010706u,0x00010106u,0x00010186u,0x01000100u,0u,0xFFFFFFFFu})
        for(uint32_t uv:{0u,2u})for(uint32_t mip:{0u,1u,2u}) {
            auto state=base;state.setScalar(ScalarState::DepthEnable,depth);state.setScalar(ScalarState::Cull,cull);
            state.setScalar(ScalarState::ExpandedBlend0,expanded);state.setPackedBlend(0,blend);
            state.setSampler(0,SamplerState::AddressU,uv);state.setSampler(0,SamplerState::AddressV,2-uv);
            state.setSampler(0,SamplerState::AddressW,2);state.setSampler(0,SamplerState::MipFilter,mip);
            state.setSampler(0,SamplerState::SeparateZFilter,1);compare(state);
        }
    rejects([&]{EngineState{}.requireOriginalIm2DDrawState(true);});
    std::printf("Direct screen qualification: %zu differential cases\n",cases);
}
} // namespace

void edgeDrawGate() {
    // The original edge rectangle is an unblended replacement. A retained expanded-precision request (retail's unready
    // sprite branch skips its reset) is accepted and reported; every other deviation still rejects.
    auto base=EngineState::fromOriginalStartup();base.applyScreenQuadState(3,false);
    base.setScalar(ScalarState::HalfPixelOffset,1);base.setScalar(ScalarState::GuardBandX,0x3F800000);base.setScalar(ScalarState::GuardBandY,0x3F800000);
    base.setScalar(ScalarState::BlendEnable,0);base.setScalar(ScalarState::AlphaTest,0);
    base.setScalar(ScalarState::DepthWrite,0);base.setScalar(ScalarState::SeparateAlpha,0);
    base.setScalar(ScalarState::MultisampleMask,UINT32_MAX);base.setScalar(ScalarState::TessellationMode,0);
    auto snapshot=base.requireOriginalEdgeState();
    require(!snapshot.expandedBlendRequested&&snapshot.blendSelector==3&&!snapshot.retainedBlendEnable,"The unblended edge baseline was not accepted");
    auto state=base;state.setScalar(ScalarState::ExpandedBlend0,1);
    snapshot=state.requireOriginalEdgeState();
    require(snapshot.expandedBlendRequested&&snapshot.blendSelector==3&&!snapshot.retainedBlendEnable,"A retained expanded request was rejected or lost for the edge draw");
    rejects([&]{state.requireScreenState(false);},"Expanded blending precision"); // The native policy keeps rejecting it.
    state.setScalar(ScalarState::ExpandedBlend1,1);
    rejects([&]{state.requireOriginalEdgeState();},"additional expanded attachments");
    for(const bool expanded:{false,true}) {
        const auto start=[&]{auto s=base;s.setScalar(ScalarState::ExpandedBlend0,expanded?1u:0u);return s;};
        state=start();state.setScalar(ScalarState::BlendEnable,1);state.setPackedBlend(0,0x00010001);
        rejects([&]{state.requireOriginalEdgeState();},"edge effective draw state");
        state=start();state.setScalar(ScalarState::AlphaTest,1);state.setScalar(ScalarState::AlphaCompare,4);
        rejects([&]{state.requireOriginalEdgeState();},"edge effective draw state");
        state=start();state.setScalar(ScalarState::DepthWrite,1);
        rejects([&]{state.requireOriginalEdgeState();},"edge effective draw state");
        state=start();state.setScalar(ScalarState::SeparateAlpha,1);
        rejects([&]{state.requireOriginalEdgeState();},"edge effective draw state");
        state=start();state.setPackedBlend(0,0x10106);
        rejects([&]{state.requireOriginalEdgeState();},"edge effective draw state");
        state=start();state.setScalar(ScalarState::MultisampleMask,0xFFFF);
        rejects([&]{state.requireOriginalEdgeState();},"edge effective draw state");
        state=start();state.setScalar(ScalarState::TessellationMode,1);
        rejects([&]{state.requireOriginalEdgeState();},"edge effective draw state");
    }
}
int main(int argc,char** argv) {
    try {
        if(argc!=2) throw std::runtime_error("Usage: test_engine_state <original-flat-image>");
        std::ifstream file(argv[1],std::ios::binary|std::ios::ate);
        if(!file || file.tellg()!=15466496) throw std::runtime_error("Cannot read expected original flat image");
        std::vector<uint8_t> image(15466496);file.seekg(0);
        if(!file.read(reinterpret_cast<char*>(image.data()),std::streamsize(image.size())))
            throw std::runtime_error("Incomplete original image read");
        originalEvidence(image);initialization();depthAndStencil();blendShadowAndPublication();
        blendFactorTransform();quadAndCleanup();inheritedGates();rejectedTransactions();alphaAndSnapshots();im2dAddressing();directGateEquivalence();edgeDrawGate();
        std::printf("PASS: 12 engine state groups / %zu checks\n",checks);
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL after %zu checks: %s\n",checks,error.what());return 1;
    }
}
