#include "runtime/rigid_material_constants.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
size_t checks{};
void check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
uint32_t bits(float f){return std::bit_cast<uint32_t>(f);}
struct AlphaMaterialRowEvidence {
    Simpsons::RigidMaterialRow row;
    uint32_t handle;
};
struct AlphaMaterialEvidence {
    uint32_t source,body,context,privateMap,sharedMap,words,leaves,vertex,pixel;
    std::array<unsigned,24> usage;
    std::array<AlphaMaterialRowEvidence,3> rows;
    size_t rowCount;
};
// Independent original context tables, rather than opaque profile-derived
// expectations: the selected alpha pass omits its shadow/noise/normal rows.
constexpr std::array<AlphaMaterialEvidence,3> alphaMaterials{{
    {0x82019988,0x19994,0x2DF0,0x2ED0,0x3030,136,22,0x8201A430,0x8201B0BC,
        {0,0,1,0,0,0,0,0,0,0,0,0,0,2,2,2,0,128,0,2,0,0},
        {{{{14,80,49},0x0048001C},{{13,76,50},0x0044001A},{{19,124,47},0x005C0026}}},3},
    {0x820547E8,0x547F4,0x3040,0x3120,0x3290,152,23,0x82055440,0x820560B8,
        {0,0,1,0,0,0,0,0,0,0,0,0,0,0,2,2,0,128,0,0,0,0,0},
        {{{{14,80,49},0x0048001C},{},{}}},1},
    {0x82057E08,0x57E14,0x3450,0x3530,0x36B0,156,24,0x820589EC,0x82059880,
        {0,0,1,0,0,0,0,0,0,0,0,0,0,2,2,2,0,128,0,0,2,0,0,0},
        {{{{14,80,49},0x0048001C},{{13,76,50},0x0044001A},{{20,140,47},0x00600028}}},3}
}};
}
int main(int argc,char** argv)try {
    using namespace Simpsons;
    std::array<uint32_t,136> values{};
    for(uint32_t i=0;i<values.size();++i)values[i]=0x3F000000+8192*i;
    values[81]=0x80000000;values[122]=0x00000001;
    Graphics::RigidPixelConstants material{};
    for(uint32_t r=0;r<50;++r)for(uint32_t j=0;j<4;++j)material[r][j]=float(1000+4*r+j);
    const auto before=material;
    std::array<uint8_t,128> eligibility;eligibility.fill(0xFF);eligibility[0]=0xDF;eligibility[1]=0x06;
    const auto originalEligibility=eligibility;
    std::array<uint8_t,128> dirty{};std::fill_n(dirty.begin(),16,uint8_t(0xFF));
    filterRigidDirty(dirty,eligibility);projectRigidMaterial(values,dirty,material);
    for(uint32_t r=0;r<50;++r)for(uint32_t j=0;j<4;++j) {
        const auto expected=r==49?values[80+j]:r==47?values[120+j]:r==46?values[124+j]:bits(before[r][j]);
        check(bits(material[r][j])==expected,"First material commit changed the wrong register/lane");
    }
    check(eligibility==originalEligibility,"Dirty filtering changed eligibility");
    const auto first=material;
    // A later material changes every CPU value, but only g_ShadowEnable was
    // dirtied. Re-reading all defaults/slots here would corrupt the retained
    // custom line and rim-shadow values of the preceding SDK commit.
    for(auto& v:values)v^=0x00400000;
    dirty.fill(0);dirty[2]=0x20; // leaf18
    projectRigidMaterial(values,dirty,material);
    for(uint32_t r=0;r<50;++r)for(uint32_t j=0;j<4;++j)
        check(bits(material[r][j])==(r==47?values[120+j]:bits(first[r][j])),"Partial material commit lost accumulated values");
    const auto second=material;dirty.fill(0);projectRigidMaterial(values,dirty,material);
    check(material==second,"Empty dirty mask recommitted changed CPU values");
    // Inherited world/light/object parameters never overwrite a material bank.
    dirty.fill(0);dirty[0]=0x20;dirty[1]=0xF9;filterRigidDirty(dirty,eligibility);
    projectRigidMaterial(values,dirty,material);check(material==second,"Inherited parameter dirtiness entered material constants");
    // Original extent1 rounds to one pair of vectors, not 1, 16 or 128 bytes.
    dirty.fill(0xFF);eligibility.fill(0);filterRigidDirty(dirty,eligibility);
    for(uint32_t i=0;i<128;++i)check(dirty[i]==(i<32?0:0xFF),"Original vector dirty-filter extent differs");
    bool rejected=false;try{projectRigidMaterial(std::span<const uint32_t>(values).first(135),dirty,material);}
    catch(const std::invalid_argument&){rejected=true;}
    check(rejected&&material==second,"Truncated private storage mutated a material snapshot");
    // Union-map selection is immaterial only after the actual shared filter
    // removes every bit examined by all eight SDK categories. Model arbitrary
    // unqualified union maps: any surviving dirty bit could select real work.
    std::array<uint8_t,128> sharedDirty{},sharedMask;sharedMask.fill(0xFF);
    sharedDirty[0]=0xFF;sharedDirty[1]=0xE0; // all eleven genuine shared leaves
    sharedMask[0]=0;sharedMask[1]=0x1F; // their accumulated reflection exclusions
    const auto rawShared=sharedDirty,maskBefore=sharedMask;
    auto filteredShared=sharedDirty;filterRigidDirty(filteredShared,sharedMask);
    check(rigidSharedCommitHasNoWork(filteredShared,1),"Filtered eleven-leaf union should have no shared work");
    check(sharedDirty==rawShared&&sharedMask==maskBefore,"Union preflight mutated source dirty/eligibility");
    for(uint32_t bit=0;bit<64;++bit) {
        auto candidate=filteredShared;candidate[bit/8]|=uint8_t(0x80>>(bit%8));const auto beforeCheck=candidate;
        check(!rigidSharedCommitHasNoWork(candidate,1)&&candidate==beforeCheck,
              "Union qualification accepted an active bit or mutated rejection input");
    }
    // Bytes8..31 are still filtered, but no category examines them at extent1.
    // Bytes32..127 are untouched by filtering. SDK completion clears ALL128.
    for(uint32_t i=8;i<128;++i)filteredShared[i]=0xFF;
    check(rigidSharedCommitHasNoWork(filteredShared,1),"Union qualification confused filter/commit/clear extents");
    check(!rigidSharedCommitHasNoWork(filteredShared,0)&&!rigidSharedCommitHasNoWork(filteredShared,2),
          "Union qualification accepted an unproved SDK word extent");
    if(argc==2) {
        std::ifstream input(argv[1],std::ios::binary);check(bool(input),"Original image missing");
        const std::vector<uint8_t> image{std::istreambuf_iterator<char>(input),{}};
        const auto word=[&](size_t at){check(at+4<=image.size(),"Truncated original evidence");
            return uint32_t(image[at])<<24|uint32_t(image[at+1])<<16|uint32_t(image[at+2])<<8|image[at+3];};
        // Pin all seven chocolate material mappings against the original
        // alpha pass, including the formerly missing final VS register c47.
        constexpr size_t chocolateBody=0x5D2E4,chocolateContext=0x3AD0;
        const auto chocolateMap=word(chocolateBody+chocolateContext+0x40);
        const auto chocolateDescriptors=word(chocolateBody+0x108);
        const auto chocolateRow=[&](const RigidMaterialRow& row,unsigned stage) {
            if(row.leaf==0xFFFFFFFFu)return;
            const auto at=chocolateBody+chocolateMap+16*row.leaf;
            const auto handle=word(at);
            check(((handle>>1)&0x1FFFF)==row.leaf,"Chocolate material leaf changed");
            check(word(at+4+4*stage)==row.reg,"Chocolate material shader register changed");
            check(word(at+4+4*(1-stage))==0,"Chocolate material unexpectedly maps both stages");
            check((word(chocolateBody+chocolateDescriptors+8*(handle>>18)+4)&0xFFFF)*4==row.word,
                  "Chocolate material storage changed");
            for(unsigned category=0;category<8;++category) {
                const auto maskAt=chocolateBody+word(chocolateBody+chocolateContext+4*category);
                const auto mask=(uint64_t(word(maskAt))<<32)|word(maskAt+4);
                check(bool(mask&(uint64_t(1)<<(63-row.leaf)))==(category==stage),"Chocolate material category changed");
            }
        };
        for(const auto& row:rigidPixelMaterialRows(0x8205D2D8,true))chocolateRow(row,1);
        for(const auto& row:rigidVertexMaterialRows(0x8205D2D8))chocolateRow(row,0);
        // UV motion has two genuine sampled base textures and five VS
        // material vectors. Its opaque pass uses only character shadow t0;
        // alpha keeps the UV motion and both bases, with no shadow/light map.
        constexpr size_t uvBody=0x465F4;
        check(word(uvBody+0x138)==172*4&&word(uvBody+0x130)==28&&
              word(uvBody+0x120)==1&&word(uvBody+0x124)==1,"UV private bank or dirty extent changed");
        const auto uvDescriptors=word(uvBody+0x108);
        const auto uvUsage=[&](unsigned context,unsigned ns,unsigned leaf) {
            unsigned result=0;
            for(unsigned category=0;category<8;++category) {
                const auto at=uvBody+word(uvBody+context+32*ns+4*category);
                const uint64_t mask=(uint64_t(word(at))<<32)|word(at+4);
                result|=unsigned((mask>>(63-leaf))&1)<<category;
            }
            return result;
        };
        constexpr unsigned uvOpaquePrivate[]={0,0,1,0,0,0,0,0,2,2,2,2,0,0,2,2,0,1,1,1,1,1,2,1,0,128,128,2};
        constexpr unsigned uvAlphaPrivate[]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,2,1,0,128,128,0};
        constexpr unsigned uvOpaqueShared[]={1,0,0,0,0,1,0,128,0,2,2};
        constexpr unsigned uvAlphaShared[]={1,0,0,0,0,0,0,0,0,0,0};
        for(unsigned leaf=0;leaf<28;++leaf) {
            check(uvUsage(0x30F0,0,leaf)==uvOpaquePrivate[leaf],"UV opaque private usage changed");
            check(uvUsage(0x3440,0,leaf)==uvAlphaPrivate[leaf],"UV alpha private usage changed");
        }
        for(unsigned leaf=0;leaf<11;++leaf) {
            check(uvUsage(0x30F0,1,leaf)==uvOpaqueShared[leaf],"UV opaque shadow/camera ownership changed");
            check(uvUsage(0x3440,1,leaf)==uvAlphaShared[leaf],"UV alpha inherited ownership changed");
        }
        const auto uvMapRow=[&](unsigned context,unsigned ns,unsigned leaf,unsigned handle,
                               unsigned stage,unsigned reg,unsigned usage,unsigned storageWord) {
            const auto map=word(uvBody+context+0x40+4*ns);
            const auto at=uvBody+map+16*leaf;
            check(word(at)==handle&&uvUsage(context,ns,leaf)==usage,"UV mapped leaf identity changed");
            check(word(at+4+4*stage)==reg&&word(at+4+4*(1-stage))==0,"UV mapped shader register changed");
            if(!ns)check((word(uvBody+uvDescriptors+8*(handle>>18)+4)&0xFFFF)*4==storageWord,
                         "UV mapped private storage changed");
        };
        constexpr RigidMaterialRow uvVSRows[]={{17,92,47},{18,96,46},{19,100,45},{20,104,44},{21,108,43}};
        constexpr unsigned uvVSHandles[]={0x00540022,0x00580024,0x005C0026,0x00600028,0x0064002A};
        for(unsigned context:{0x30F0u,0x3440u}) {
            for(unsigned i=0;i<5;++i)uvMapRow(context,0,uvVSRows[i].leaf,uvVSHandles[i],0,uvVSRows[i].reg,1,uvVSRows[i].word);
            uvMapRow(context,0,22,0x0068002C,1,42,2,112);
            uvMapRow(context,0,23,0x006C002E,0,22,1,116); // Original DELTA_TIME callback.
            const auto map=word(uvBody+context+0x40);
            for(unsigned leaf:{25u,26u}) {
                const unsigned stage=leaf-25+(context==0x30F0?1:0);
                const unsigned handle=leaf==25?0x00740032:0x00780034;
                const auto at=uvBody+map+16*leaf;
                check(word(at)==handle&&word(at+8)==stage<<22&&word(at+4)==0,
                      "UV base sampler did not retain its original selected-pass stage");
                check((word(uvBody+uvDescriptors+8*(handle>>18)+4)&0xFFFF)*4==(leaf==25?136:152),
                      "UV base sampler storage changed");
            }
        }
        uvMapRow(0x30F0,0,14,0x0048001C,1,49,2,80);
        uvMapRow(0x30F0,0,27,0x007C0036,1,48,2,168);
        const auto uvSharedMap=word(uvBody+0x30F0+0x44);
        check(word(uvBody+uvSharedMap+16*5+4)==0xC1A&&
              word(uvBody+uvSharedMap+16*7+8)==0,
              "UV opaque must use the character shadow matrix and texture t0");
        const auto uvRows=rigidVertexMaterialRows(0x820465E8);
        for(const auto& expected:uvVSRows)check(std::count_if(uvRows.begin(),uvRows.end(),[&](const auto& row){
            return row.leaf==expected.leaf&&row.word==expected.word&&row.reg==expected.reg;})==1,
            "UV profile omitted a mapped VS material vector");
        constexpr uint32_t fallbackPrefix[]={0x7D8802A6,0x482FC2CD,0x9421FF80,0x7C7F1B78,
            0x7C9E2378,0x7CDC3378,0x7FC7F378,0x807F0018,0x809F0000,0x80DF0008,
            0x80BF0004,0x81630000,0x816B0014,0x7D6903A6,0x4E800421};
        for(unsigned i=0;i<std::size(fallbackPrefix);++i)check(word(0x7400F8+4*i)==fallbackPrefix[i],
            "Original fallback no longer overwrites unused entry r5 before the first object callback");
        constexpr uint32_t fallbackArguments[]={0x7EC6B378,0x7EA5AB78,0x7EE4BB78,0x7FE3FB78,0x4BFFF5D5};
        for(unsigned i=0;i<std::size(fallbackArguments);++i)check(word(0x740B14+4*i)==fallbackArguments[i],
            "Original fallback no longer receives the dispatcher Boolean flags and packet");
        constexpr size_t glossBody=0x19994,context=0x2B00;
        check(word(glossBody+0x120)==1&&word(glossBody+0x124)==1,"Gloss dirty extent changed");
        const auto glossProfile=rigidProfile(0x82019988);
        check(glossProfile.context==context&&glossProfile.words==136,"Gloss profile extent changed");
        const auto usage=[&](unsigned ns,unsigned leaf) {
            unsigned result=0;
            for(unsigned k=0;k<8;++k) {
                const auto at=glossBody+word(glossBody+context+32*ns+4*k);
                const uint64_t mask=(uint64_t(word(at))<<32)|word(at+4);
                result|=unsigned((mask>>(63-leaf))&1)<<k;
            }
            return result;
        };
        constexpr unsigned privateUsage[]={0,0,1,0,0,0,0,0,2,2,2,2,0,2,2,2,0,128,0,2,2,2};
        constexpr unsigned sharedUsage[]={1,2,0,0,1,1,128,128,0,2,2};
        for(unsigned i=0;i<22;++i)check(usage(0,i)==privateUsage[i],"Gloss private register ownership changed");
        for(unsigned i=0;i<11;++i)check(usage(1,i)==sharedUsage[i],"Gloss shared register ownership changed");
        const auto glossMap=word(glossBody+context+0x40),glossDescriptors=word(glossBody+0x108);
        constexpr unsigned glossLeaves[]={13,14,19,20,21},glossRegs[]={50,49,47,46,45};
        constexpr unsigned glossHandles[]={0x44001A,0x48001C,0x5C0026,0x600028,0x64002A};
        constexpr unsigned glossOffsets[]={304,320,496,512,528};
        for(unsigned i=0;i<5;++i) {
            const auto at=glossBody+glossMap+16*glossLeaves[i];
            check(word(at)==glossHandles[i]&&word(at+8)==glossRegs[i],"Gloss selected material map changed");
            check((word(glossBody+glossDescriptors+8*(glossHandles[i]>>18)+4)&0xFFFF)*16==glossOffsets[i],
                "Gloss selected material storage changed");
        }
        const auto sharedMap=word(glossBody+context+0x44);
        constexpr unsigned sharedLeaves[]={0,1,4,5,6,7,9,10};
        constexpr unsigned sharedWords[]={0xC00,4,0xC16,0xC1A,0,0x400000,30,31};
        for(unsigned i=0;i<8;++i) {
            const auto leaf=sharedLeaves[i],lane=(sharedUsage[leaf]&1)?4u:8u;
            check(word(glossBody+sharedMap+16*leaf+lane)==sharedWords[i],"Gloss inherited shared register or sampler map changed");
        }
        constexpr unsigned familyAlphaSharedUsage[]={1,2,0,0,0,0,0,0,0,0,0};
        for(const auto& evidence:alphaMaterials) {
            const auto alpha=rigidAlphaPass(evidence.source);
            check(alpha.context==evidence.context&&alpha.vertex==evidence.vertex&&
                  alpha.pixel==evidence.pixel&&alpha.samplers==6,"Family alpha selected pass differs from original evidence");
            check(word(evidence.body+0x120)==1&&word(evidence.body+0x124)==1&&
                  word(evidence.body+0x130)==evidence.leaves&&word(evidence.body+0x138)==4*evidence.words,
                  "Family alpha private bank or dirty extent changed");
            check(word(evidence.body+evidence.context+0x40)==evidence.privateMap&&
                  word(evidence.body+evidence.context+0x44)==evidence.sharedMap,
                  "Family alpha original selected mapping pointers changed");
            check(0x82000000u+evidence.body+word(evidence.body+evidence.context+0x48)+8==evidence.vertex&&
                  0x82000000u+evidence.body+word(evidence.body+evidence.context+0x4C)+8==evidence.pixel,
                  "Family alpha original selected shader records changed");
            const auto familyUsage=[&](unsigned ns,unsigned leaf) {
                unsigned result=0;
                for(unsigned category=0;category<8;++category) {
                    const auto at=evidence.body+word(evidence.body+evidence.context+32*ns+4*category);
                    const uint64_t mask=(uint64_t(word(at))<<32)|word(at+4);
                    result|=unsigned((mask>>(63-leaf))&1)<<category;
                }
                return result;
            };
            for(unsigned leaf=0;leaf<evidence.leaves;++leaf)
                check(familyUsage(0,leaf)==evidence.usage[leaf],"Family alpha private selected-pass ownership changed");
            for(unsigned leaf=0;leaf<11;++leaf)
                check(familyUsage(1,leaf)==familyAlphaSharedUsage[leaf],"Family alpha retained opaque shared shadow/light ownership");
            const auto descriptors=word(evidence.body+0x108);
            for(size_t i=0;i<evidence.rowCount;++i) {
                const auto& expected=evidence.rows[i];
                const auto at=evidence.body+evidence.privateMap+16*expected.row.leaf;
                check(word(at)==expected.handle&&word(at+4)==0&&word(at+8)==expected.row.reg&&
                      familyUsage(0,expected.row.leaf)==2,"Family alpha material leaf or selected PS register changed");
                check((word(evidence.body+descriptors+8*(expected.handle>>18)+4)&0xFFFF)*4==expected.row.word,
                      "Family alpha material storage word changed");
            }
            const auto privateAt=evidence.body+evidence.privateMap;
            const auto sharedAt=evidence.body+evidence.sharedMap;
            check(word(privateAt+16*2)==0x000C0004&&word(privateAt+16*2+4)==0xC0C&&
                  word(privateAt+16*15)==0x004C001E&&word(privateAt+16*15+8)==40,
                  "Family alpha per-object world or object ID mapping changed");
            check(word(privateAt+16*17)==0x00540022&&word(privateAt+16*17+4)==0&&
                  word(privateAt+16*17+8)==0&&familyUsage(0,17)==128&&
                  (word(evidence.body+descriptors+8*(0x00540022u>>18)+4)&0xFFFF)*4==92,
                  "Family alpha base texture is not its original stage-zero private sampler");
            check(word(sharedAt+4)==0xC00&&word(sharedAt+16+8)==4,
                  "Family alpha camera or world-eye mapping changed");
            // Multitone's opaque NoiseParams2 leaf21 feeds VS c46. Its alpha
            // context has no material VS maps; only per-object world remains.
            for(unsigned leaf=0;leaf<evidence.leaves;++leaf)if(leaf!=2)
                check(word(privateAt+16*leaf+4)==0,"Family alpha unexpectedly retains an opaque vertex material map");
        }
        constexpr size_t body=0xCCC4;
        check(word(body+0x120)==1&&word(body+0x124)==1,"Original rigid dirty extents differ");
        const auto map=word(body+0x2620+0x40),descriptors=word(body+0x108);
        constexpr uint32_t leaves[]={14,18,19},registers[]={49,47,46},handles[]={0x0048001C,0x00580024,0x005C0026};
        constexpr uint32_t offsets[]={320,480,496};
        for(uint32_t i=0;i<3;++i) {
            check(word(body+map+16*leaves[i])==handles[i]&&word(body+map+16*leaves[i]+8)==registers[i],"Original pass material map differs");
            const auto descriptor=(handles[i]>>18)*8;
            check((word(body+descriptors+descriptor+4)&0xFFFF)*16==offsets[i],"Original material slot offset differs");
        }
        check(word(0x6B2F44)==0x39080004&&word(0x6B2F68)==0x396B0020,"Original dirty loop increment differs");
        check(word(0xC1ED00)==0x7C20AFEC&&word(0xC1ED0C)==0x7C205FEC,"Original SDK full dirty clears differ");
        constexpr uint32_t selector[]={0x3D6082D0,0x815502B8,0x38600000,0x7D4A0034,0x816B0F80,0x7D6B0034,
            0x7D4B5B78,0x3D4082E3,0x556BDFFE,0x396BFFFF,0x814AD96C,0x7D6A5038,0x7EEB5878,0x7F0B5214};
        for(uint32_t i=0;i<std::size(selector);++i)check(word(0xC1E59C+4*i)==selector[i],"Original union selection predicate/pointer changed");
        constexpr uint32_t categoryLoad[]={0xC1E5E8,0xC1E6FC,0xC1E810,0xC1E900,0xC1E9F0,0xC1EAC8,0xC1EB98,0xC1EC58};
        constexpr uint32_t counts[]={0xC1E5D4,0xC1E6E4,0xC1E7F8,0xC1E8E8,0xC1E9D8,0xC1EAB0,0xC1EB80,0xC1EC40};
        constexpr uint32_t dirtyLoads[]={0xC1E5EC,0xC1E700,0xC1E818,0xC1E908,0xC1E9F8,0xC1EAD0,0xC1EBA0,0xC1EC60};
        constexpr uint32_t ands[]={0xC1E5FC,0xC1E710,0xC1E824,0xC1E914,0xC1EA04,0xC1EADC,0xC1EBAC,0xC1EC6C};
        constexpr uint32_t zeroBranches[]={0xC1E6C8,0xC1E7DC,0xC1E8CC,0xC1E9BC,0xC1EA94,0xC1EB64,0xC1EC24,0xC1ECE4};
        constexpr uint32_t branchWords[]={0x409AFF3C,0x409AFF3C,0x409AFF60,0x409AFF60,0x409AFF78,0x409AFF80,0x409AFF90,0x409AFF90};
        constexpr uint32_t increments[]={0xC1E6D8,0xC1E7EC,0xC1E8DC,0xC1E9CC,0xC1EAA4,0xC1EB74,0xC1EC34,0xC1ECF4};
        for(uint32_t i=0;i<8;++i) {
            check(word(counts[i])==0x81750124&&word(categoryLoad[i])==0x81580020+4*i,"Shared category extent or map-pointer field changed");
            check(word(dirtyLoads[i])==(i<2?0x7D25882A:0x7D3C882A),"Shared category no longer reads the same full dirty qword");
            check(word(ands[i])==(i<2?0x7D275038:0x7D3E5838),"Shared category no longer intersects original dirty word");
            check(word(zeroBranches[i]-4)==(i<2?0x2B270000:0x2B3E0000)&&word(zeroBranches[i])==branchWords[i],
                  "Shared category has no exact zero-work branch");
            check(word(increments[i])==(i<2?0x38A50008:0x3B9C0008),"Shared category no longer advances by one qword");
        }
    } else check(argc==1,"Expected optional original image path only");
    std::array<uint32_t,140> texturedValues{};
    for(uint32_t index=0;index<texturedValues.size();++index)texturedValues[index]=bits(float(index));
    auto texturedMaterial=before;dirty.fill(0);dirty[1]=2;dirty[2]=0x18;
    projectRigidMaterial(texturedValues,dirty,texturedMaterial,0x820168F8);
    for(uint32_t reg=0;reg<50;++reg)for(uint32_t lane=0;lane<4;++lane) {
        const auto expected=reg==49?80+lane:reg==47?124+lane:reg==46?128+lane:0;
        check(bits(texturedMaterial[reg][lane])==(expected?texturedValues[expected]:bits(before[reg][lane])),
              "Textured material map confused the added sampler with shadow/rim constants");
    }
    const auto firstTextured=texturedMaterial;dirty.fill(0);dirty[2]=0x10;
    texturedValues[124]=bits(-19.f);texturedValues[128]=bits(-23.f);
    projectRigidMaterial(texturedValues,dirty,texturedMaterial,0x820168F8);
    check(texturedMaterial[47][0]==-19.f&&texturedMaterial[46]==firstTextured[46],"Textured material lost independent dirty accumulation");
    std::array<uint32_t,156> dualValues{};
    for(uint32_t index=0;index<dualValues.size();++index)dualValues[index]=bits(float(index));
    auto dualMaterial=before;dirty.fill(0);dirty[1]=2;dirty[2]=0x0C;
    projectRigidMaterial(dualValues,dirty,dualMaterial,0x8202AD78);
    for(uint32_t reg=0;reg<50;++reg)for(uint32_t lane=0;lane<4;++lane) {
        const auto expected=reg==49?80+lane:reg==47?140+lane:reg==46?144+lane:0;
        check(bits(dualMaterial[reg][lane])==(expected?dualValues[expected]:bits(before[reg][lane])),
              "Dual material shadow/rim map changed unrelated registers");
    }
    const auto firstDual=dualMaterial;dirty.fill(0);dirty[2]=0x08;
    dualValues[140]=bits(-31.f);dualValues[144]=bits(-37.f);
    projectRigidMaterial(dualValues,dirty,dualMaterial,0x8202AD78);
    check(dualMaterial[47][0]==-31.f&&dualMaterial[46]==firstDual[46],"Dual material lost independent dirty accumulation");
    rejected=false;try{projectRigidMaterial(texturedValues,dirty,dualMaterial,0x8202AD78);}
    catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Dual material accepted textured private-bank extent");
    std::array<uint32_t,152> noiseValues{};
    for(size_t i=0;i<noiseValues.size();++i)noiseValues[i]=bits(float(i)+.25f);
    Graphics::RigidVertexConstants noiseVS{};auto noisePS=before;
    dirty.fill(0);dirty[1]=2;dirty[2]=0x0E;
    projectRigidMaterial(noiseValues,dirty,noisePS,0x820547E8);
    projectRigidVertexMaterial(noiseValues,dirty,noiseVS,0x820547E8);
    for(size_t row=0;row<noisePS.size();++row)for(size_t lane=0;lane<4;++lane) {
        const auto offset=row==49?80:row==47?140:row==46?144:row==45?148:0;
        check(bits(noisePS[row][lane])==(offset?noiseValues[offset+lane]:bits(before[row][lane])),"Multitone PS material projection changed unrelated lanes");
    }
    for(size_t row=0;row<noiseVS.size();++row)for(size_t lane=0;lane<4;++lane)
        check(bits(noiseVS[row][lane])==(row==46?noiseValues[144+lane]:0),"Multitone VS projection is not limited to c46");
    const auto savedPS=noisePS;dirty.fill(0);dirty[2]=4;noiseValues[144]=bits(-17.f);
    projectRigidMaterial(noiseValues,dirty,noisePS,0x820547E8);
    projectRigidVertexMaterial(noiseValues,dirty,noiseVS,0x820547E8);
    check(noisePS[46][0]==-17&&noiseVS[46][0]==-17&&noisePS[45]==savedPS[45]&&noisePS[47]==savedPS[47],"Multitone dual-stage dirty update lost ownership");
    dirty.fill(0);noiseValues[144]=bits(99.f);
    projectRigidVertexMaterial(noiseValues,dirty,noiseVS,0x820547E8);
    check(noiseVS[46][0]==-17,"Multitone empty dirty mask changed VS material");
    std::array<uint32_t,136> glossValues{};
    for(uint32_t i=0;i<glossValues.size();++i)glossValues[i]=bits(float(i));
    auto gloss=before;dirty.fill(0);dirty[1]=6;dirty[2]=0x1C;
    projectRigidMaterial(glossValues,dirty,gloss,0x82019988);
    for(size_t reg=0;reg<gloss.size();++reg)for(size_t lane=0;lane<4;++lane) {
        const auto offset=reg==50?76:reg==49?80:reg==47?124:reg==46?128:reg==45?132:0;
        check(bits(gloss[reg][lane])==(offset?glossValues[offset+lane]:bits(before[reg][lane])),
            "Gloss projection changed the wrong register or lost c50");
    }
    for(const auto leaf:{13u,14u,19u,20u,21u}) {
        auto retained=gloss;dirty.fill(0);dirty[leaf/8]=uint8_t(0x80>>(leaf&7));
        const auto reg=leaf==13?50:leaf==14?49:leaf==19?47:leaf==20?46:45;
        const auto offset=leaf==13?76:leaf==14?80:leaf==19?124:leaf==20?128:132;
        glossValues[offset]=bits(-float(leaf));
        projectRigidMaterial(glossValues,dirty,gloss,0x82019988);
        retained[reg][0]=-float(leaf);check(gloss==retained,"Gloss dirty leaf overwrote another material constant");
    }
    std::array<uint32_t,156> normalValues{};
    for(uint32_t i=0;i<normalValues.size();++i)normalValues[i]=bits(float(i)+.5f);
    auto normalmat=before;dirty.fill(0);dirty[1]=6;dirty[2]=0x0F;
    // Leaves 13,14,20,21,22,23 -> regs 50,49,47,46,45,44 (words 76,80,140,144,148,152).
    projectRigidMaterial(normalValues,dirty,normalmat,0x82057E08);
    for(size_t reg=0;reg<normalmat.size();++reg)for(size_t lane=0;lane<4;++lane) {
        const auto offset=reg==50?76:reg==49?80:reg==47?140:reg==46?144:reg==45?148:reg==44?152:0;
        check(bits(normalmat[reg][lane])==(offset?normalValues[offset+lane]:bits(before[reg][lane])),
            "Normalmap projection changed the wrong register or lost c44/c50");
    }
    for(const auto leaf:{13u,14u,20u,21u,22u,23u}) {
        auto retained=normalmat;dirty.fill(0);dirty[leaf/8]=uint8_t(0x80>>(leaf&7));
        const auto reg=leaf==13?50:leaf==14?49:leaf==20?47:leaf==21?46:leaf==22?45:44;
        const auto offset=leaf==13?76:leaf==14?80:leaf==20?140:leaf==21?144:leaf==22?148:152;
        normalValues[offset]=bits(-float(leaf)-.5f);
        projectRigidMaterial(normalValues,dirty,normalmat,0x82057E08);
        retained[reg][0]=-float(leaf)-.5f;check(normalmat==retained,"Normalmap dirty leaf overwrote another material constant");
    }
    check(isRigidSource(0x82057E08),"Normalmap source not admitted");
    const auto normalProfile=rigidProfile(0x82057E08);
    check(normalProfile.vertex==0x8205855C&&normalProfile.pixel==0x82058CBC&&normalProfile.context==0x3140&&
        normalProfile.samplers==24&&normalProfile.words==156&&normalProfile.textured&&normalProfile.normalmap&&
        normalProfile.extraLeaf==23&&normalProfile.extraWord==152&&normalProfile.extraReg==44,
        "Normalmap profile extent changed");
    std::array<uint32_t,184> chocolateValues{};
    for(size_t i=0;i<chocolateValues.size();++i)chocolateValues[i]=bits(float(i)+.125f);
    Graphics::RigidVertexConstants chocolateVS{};auto chocolatePS=before;
    for(auto& row:chocolateVS)row.fill(-7);
    const auto chocolateBeforeVS=chocolateVS;
    // Only the seven mapped material leaves can affect either GPU bank.
    for(unsigned leaf=0;leaf<28;++leaf) {
        chocolateVS=chocolateBeforeVS;chocolatePS=before;
        dirty.fill(0);dirty[leaf/8]=uint8_t(0x80>>(leaf&7));
        projectRigidMaterial(chocolateValues,dirty,chocolatePS,0x8205D2D8,true);
        projectRigidVertexMaterial(chocolateValues,dirty,chocolateVS,0x8205D2D8,true);
        for(size_t reg=0;reg<chocolatePS.size();++reg)for(size_t lane=0;lane<4;++lane) {
            const auto offset=(leaf==13&&reg==50)?76:(leaf==20&&reg==44)?104:
                (leaf==21&&reg==43)?108:(leaf==22&&reg==42)?112:0;
            check(bits(chocolatePS[reg][lane])==(offset?chocolateValues[offset+lane]:bits(before[reg][lane])),
                  "Chocolate pixel projection lost a material or overwrote inherited state");
        }
        for(size_t reg=0;reg<chocolateVS.size();++reg)for(size_t lane=0;lane<4;++lane) {
            const auto offset=(leaf==17&&reg==47)?92:(leaf==18&&reg==46)?96:(leaf==19&&reg==45)?100:0;
            check(bits(chocolateVS[reg][lane])==(offset?chocolateValues[offset+lane]:bits(chocolateBeforeVS[reg][lane])),
                  "Chocolate vertex projection lost UV animation or overwrote inherited state");
        }
        const auto retainedVS=chocolateVS;const auto retainedPS=chocolatePS;
        dirty.fill(0);
        projectRigidMaterial(chocolateValues,dirty,chocolatePS,0x8205D2D8,true);
        projectRigidVertexMaterial(chocolateValues,dirty,chocolateVS,0x8205D2D8,true);
        check(chocolateVS==retainedVS&&chocolatePS==retainedPS,"Chocolate clean commit lost accumulated constants");
    }
    // New selected passes: each leaf independently proves its exact material
    // projection, including rows unused by the other pass of the same source.
    for(uint32_t source:{0x8205D2D8u,0x82051808u})for(bool alpha:{false,true}) {
        const auto profile=rigidProfile(source);std::vector<uint32_t> words(profile.words);
        for(size_t i=0;i<words.size();++i)words[i]=bits(float(i)+.375f);
        const auto leaves=source==0x8205D2D8u?28u:21u;
        for(uint32_t leaf=0;leaf<leaves;++leaf) {
            auto pixel=before;auto vertex=chocolateBeforeVS;dirty.fill(0);dirty[leaf/8]=uint8_t(0x80>>(leaf&7));
            projectRigidMaterial(words,dirty,pixel,source,alpha);projectRigidVertexMaterial(words,dirty,vertex,source,alpha);
            for(size_t reg=0;reg<pixel.size();++reg)for(size_t lane=0;lane<4;++lane) {
                uint32_t offset=0;
                if(source==0x8205D2D8u)offset=alpha?((leaf==13&&reg==50)?76u:(leaf==20&&reg==44)?104u:(leaf==21&&reg==43)?108u:(leaf==22&&reg==42)?112u:0u):
                    ((leaf==14&&reg==49)?80u:(leaf==20&&reg==44)?104u:0u);
                else offset=(leaf==14&&reg==49)?80u:(!alpha&&leaf==20&&reg==47)?128u:0u;
                check(bits(pixel[reg][lane])==(offset?words[offset+lane]:bits(before[reg][lane])),"Selected remaining pixel material projection differs");
            }
            for(size_t reg=0;reg<vertex.size();++reg)for(size_t lane=0;lane<4;++lane) {
                const uint32_t offset=source==0x8205D2D8u?((leaf==17&&reg==47)?92u:(leaf==18&&reg==46)?96u:(leaf==19&&reg==45)?100u:0u):0u;
                check(bits(vertex[reg][lane])==(offset?words[offset+lane]:bits(chocolateBeforeVS[reg][lane])),"Selected remaining vertex material projection differs");
            }
        }
    }
    const auto uvProfile=rigidProfile(0x820465E8);
    const auto uvAlpha=rigidAlphaPass(0x820465E8);
    check(isRigidSource(0x820465E8)&&isAlphaBeginSource(0x820465E8)&&
          !isAlphaFamilySource(0x820465E8),"UV source must admit opaque and explicitly selected alpha passes");
    check(uvProfile.vertex==0x82046D9C&&uvProfile.pixel==0x820477A8&&uvProfile.context==0x30F0&&
          uvProfile.samplers==18&&uvProfile.words==172&&uvProfile.uv&&!uvProfile.textured,
          "UV opaque profile confused motion storage with a single base texture");
    check(uvAlpha.vertex==0x82047318&&uvAlpha.pixel==0x82047F44&&uvAlpha.context==0x3440&&
          uvAlpha.samplers==12,"UV alpha profile changed");
    std::array<uint32_t,172> uvValues{};
    for(size_t i=0;i<uvValues.size();++i)uvValues[i]=bits(float(i)+.0625f);
    Graphics::RigidVertexConstants uvVS{};for(auto& row:uvVS)row.fill(-11);
    const auto uvBeforeVS=uvVS;auto uvPS=before;
    for(unsigned leaf=0;leaf<28;++leaf) {
        uvVS=uvBeforeVS;uvPS=before;dirty.fill(0);dirty[leaf/8]=uint8_t(0x80>>(leaf&7));
        projectRigidMaterial(uvValues,dirty,uvPS,0x820465E8);
        projectRigidVertexMaterial(uvValues,dirty,uvVS,0x820465E8);
        for(size_t reg=0;reg<uvPS.size();++reg)for(size_t lane=0;lane<4;++lane) {
            const auto offset=(leaf==14&&reg==49)?80:(leaf==22&&reg==42)?112:(leaf==27&&reg==48)?168:0;
            check(bits(uvPS[reg][lane])==(offset?uvValues[offset+lane]:bits(before[reg][lane])),
                  "UV pixel projection overwrote inherited or clean material constants");
        }
        for(size_t reg=0;reg<uvVS.size();++reg)for(size_t lane=0;lane<4;++lane) {
            const auto offset=leaf>=17&&leaf<=21&&reg==64-leaf?92+4*(leaf-17):0;
            check(bits(uvVS[reg][lane])==(offset?uvValues[offset+lane]:bits(uvBeforeVS[reg][lane])),
                  "UV vertex motion lost a mapped vector or overwrote TimeTicker/matrix state");
        }
        const auto retainedVS=uvVS;const auto retainedPS=uvPS;dirty.fill(0);
        projectRigidMaterial(uvValues,dirty,uvPS,0x820465E8);
        projectRigidVertexMaterial(uvValues,dirty,uvVS,0x820465E8);
        check(uvVS==retainedVS&&uvPS==retainedPS,"UV clean commit lost accumulated constants");
    }
    // A new velocity value must preserve the previously committed scale and
    // all three wave/motion vectors, even when their CPU words also change.
    dirty.fill(0xFF);projectRigidVertexMaterial(uvValues,dirty,uvVS,0x820465E8);
    const auto accumulatedUV=uvVS;for(auto& value:uvValues)value^=0x00400000;
    dirty.fill(0);dirty[2]=0x40;projectRigidVertexMaterial(uvValues,dirty,uvVS,0x820465E8);
    for(size_t reg=0;reg<uvVS.size();++reg)for(size_t lane=0;lane<4;++lane)
        check(bits(uvVS[reg][lane])==(reg==47?uvValues[92+lane]:bits(accumulatedUV[reg][lane])),
              "UV partial dirty commit replaced clean animation constants");
    const auto committedUV=uvVS;rejected=false;
    try{projectRigidVertexMaterial(std::span<const uint32_t>(uvValues).first(171),dirty,uvVS,0x820465E8);}
    catch(const std::invalid_argument&){rejected=true;}
    check(rejected&&uvVS==committedUV,"Truncated UV bank modified a material snapshot");
    for(const auto& evidence:alphaMaterials) {
        const auto pixelRows=rigidPixelMaterialRows(evidence.source,true);
        size_t activeRows=0;
        for(const auto& row:pixelRows) {
            if(row.leaf==0xFFFFFFFFu) {
                check(row.word==0xFFFFFFFFu&&row.reg==0xFFFFFFFFu,"Family alpha omitted material row has partial sentinel fields");
                continue;
            }
            ++activeRows;
            check(std::count_if(evidence.rows.begin(),evidence.rows.begin()+evidence.rowCount,[&](const auto& expected){
                return row.leaf==expected.row.leaf&&row.word==expected.row.word&&row.reg==expected.row.reg;})==1,
                "Family alpha projection retained an opaque material row or changed a selected map");
        }
        check(activeRows==evidence.rowCount,"Family alpha projection omitted an original selected material vector");
        for(const auto& row:rigidVertexMaterialRows(evidence.source,true))
            check(row.leaf==0xFFFFFFFFu&&row.word==0xFFFFFFFFu&&row.reg==0xFFFFFFFFu,
                  "Family alpha projection retained an opaque VS material row");
        std::vector<uint32_t> alphaValues(evidence.words);
        for(size_t wordIndex=0;wordIndex<alphaValues.size();++wordIndex)
            alphaValues[wordIndex]=0x3F000000u+uint32_t(8192*wordIndex);
        for(size_t i=0;i<evidence.rowCount;++i) {
            alphaValues[evidence.rows[i].row.word+1]=0x80000000; // Preserve signed zero bitwise.
            alphaValues[evidence.rows[i].row.word+2]=0x00000001; // Preserve subnormal bitwise.
        }
        Graphics::RigidPixelConstants initialPS{};
        Graphics::RigidVertexConstants initialVS{};
        for(size_t reg=0;reg<initialPS.size();++reg)for(size_t lane=0;lane<4;++lane)
            initialPS[reg][lane]=std::bit_cast<float>(0x43000000u+uint32_t(4*reg+lane));
        for(size_t reg=0;reg<initialVS.size();++reg)for(size_t lane=0;lane<4;++lane)
            initialVS[reg][lane]=std::bit_cast<float>(0x44000000u+uint32_t(4*reg+lane));
        // Test every original private leaf, including inherited and opaque-only
        // leaves. Unmapped dirty bits must leave every sentinel lane untouched.
        for(unsigned leaf=0;leaf<evidence.leaves;++leaf) {
            auto alphaPS=initialPS;
            auto alphaVS=initialVS;
            dirty.fill(0);dirty[leaf/8]=uint8_t(0x80>>(leaf&7));
            projectRigidMaterial(alphaValues,dirty,alphaPS,evidence.source,true);
            projectRigidVertexMaterial(alphaValues,dirty,alphaVS,evidence.source,true);
            for(size_t reg=0;reg<alphaPS.size();++reg)for(size_t lane=0;lane<4;++lane) {
                auto expected=bits(initialPS[reg][lane]);
                for(size_t i=0;i<evidence.rowCount;++i) {
                    const auto& row=evidence.rows[i].row;
                    if(row.leaf==leaf&&row.reg==reg)expected=alphaValues[row.word+lane];
                }
                check(bits(alphaPS[reg][lane])==expected,"Family alpha dirty leaf changed another selected or unmapped pixel lane");
            }
            check(alphaVS==initialVS,"Family alpha dirty leaf changed an unmapped vertex material lane");
        }
        auto accumulatedPS=initialPS;
        auto accumulatedVS=initialVS;
        dirty.fill(0xFF);
        projectRigidMaterial(alphaValues,dirty,accumulatedPS,evidence.source,true);
        projectRigidVertexMaterial(alphaValues,dirty,accumulatedVS,evidence.source,true);
        for(size_t reg=0;reg<accumulatedPS.size();++reg)for(size_t lane=0;lane<4;++lane) {
            auto expected=bits(initialPS[reg][lane]);
            for(size_t i=0;i<evidence.rowCount;++i)if(evidence.rows[i].row.reg==reg)
                expected=alphaValues[evidence.rows[i].row.word+lane];
            check(bits(accumulatedPS[reg][lane])==expected,"Family alpha full dirty commit overwrote an opaque-only or inherited pixel lane");
        }
        check(accumulatedVS==initialVS,"Family alpha full dirty commit retained an opaque vertex material map");
        for(auto& value:alphaValues)value^=0x00400000;
        for(size_t i=0;i<evidence.rowCount;++i) {
            const auto& row=evidence.rows[i].row;
            auto partialPS=accumulatedPS;
            auto partialVS=accumulatedVS;
            dirty.fill(0);dirty[row.leaf/8]=uint8_t(0x80>>(row.leaf&7));
            projectRigidMaterial(alphaValues,dirty,partialPS,evidence.source,true);
            projectRigidVertexMaterial(alphaValues,dirty,partialVS,evidence.source,true);
            for(size_t reg=0;reg<partialPS.size();++reg)for(size_t lane=0;lane<4;++lane)
                check(bits(partialPS[reg][lane])==(reg==row.reg?alphaValues[row.word+lane]:bits(accumulatedPS[reg][lane])),
                      "Family alpha partial commit lost a clean material vector or unmapped sentinel lane");
            check(partialVS==accumulatedVS,"Family alpha partial commit changed unmapped vertex constants");
        }
        const auto committedPS=accumulatedPS;
        const auto committedVS=accumulatedVS;
        dirty.fill(0);
        projectRigidMaterial(alphaValues,dirty,accumulatedPS,evidence.source,true);
        projectRigidVertexMaterial(alphaValues,dirty,accumulatedVS,evidence.source,true);
        check(accumulatedPS==committedPS&&accumulatedVS==committedVS,"Family alpha clean commit reread changed CPU material words");
        rejected=false;
        try{projectRigidMaterial(std::span<const uint32_t>(alphaValues).first(evidence.words-1),dirty,accumulatedPS,evidence.source,true);}
        catch(const std::invalid_argument&){rejected=true;}
        check(rejected&&accumulatedPS==committedPS,"Family alpha truncated private bank modified the pixel snapshot");
        rejected=false;
        try{projectRigidVertexMaterial(std::span<const uint32_t>(alphaValues).first(evidence.words-1),dirty,accumulatedVS,evidence.source,true);}
        catch(const std::invalid_argument&){rejected=true;}
        check(rejected&&accumulatedVS==committedVS,"Family alpha truncated private bank modified the vertex snapshot");
    }
    // Independently pinned single-UV selected maps. Dirty changes must leave
    // the inherited c22 overlap (TimeTicker/world-shadow matrix) untouched.
    const auto singleProfile=rigidProfile(0x82042F58);const auto singleAlpha=rigidAlphaPass(0x82042F58);
    check(singleProfile.vertex==0x8204364C&&singleProfile.pixel==0x82044068&&singleProfile.context==0x2D90&&
          singleProfile.samplers==18&&singleProfile.words==132&&singleProfile.textured&&singleProfile.singleUv&&!singleProfile.uv,
          "Single UV profile borrowed the dual UV texture/storage contract");
    check(singleAlpha.vertex==0x82043BC0&&singleAlpha.pixel==0x82044850&&singleAlpha.context==0x30A0&&singleAlpha.samplers==6,
          "Single UV alpha profile lost its original records");
    check(isRigidSource(0x82042F58)&&isAlphaBeginSource(0x82042F58)&&!isAlphaFamilySource(0x82042F58),
          "Single UV source cannot select both original passes");
    std::array<uint32_t,132> singleValues{};
    for(size_t i=0;i<singleValues.size();++i)singleValues[i]=bits(float(i)+.0625f);
    for(bool alpha:{false,true})for(unsigned leaf=0;leaf<24;++leaf) {
        auto ps=before;auto vs=uvBeforeVS;dirty.fill(0);dirty[leaf/8]=uint8_t(0x80>>(leaf&7));
        projectRigidMaterial(singleValues,dirty,ps,0x82042F58,alpha);
        projectRigidVertexMaterial(singleValues,dirty,vs,0x82042F58,alpha);
        for(size_t reg=0;reg<ps.size();++reg)for(size_t lane=0;lane<4;++lane) {
            const unsigned offset=(leaf==14&&reg==49)?80:(alpha&&leaf==16&&reg==48)?88:
                (!alpha&&leaf==20&&reg==44)?104:(!alpha&&leaf==21&&reg==43)?108:0;
            check(bits(ps[reg][lane])==(offset?singleValues[offset+lane]:bits(before[reg][lane])),
                  "Single UV pixel commit changed an inherited or inactive selected-pass row");
        }
        for(size_t reg=0;reg<vs.size();++reg)for(size_t lane=0;lane<4;++lane) {
            const unsigned offset=(leaf>=17&&leaf<=19&&reg==64-leaf)?92+4*(leaf-17):0;
            check(bits(vs[reg][lane])==(offset?singleValues[offset+lane]:bits(uvBeforeVS[reg][lane])),
                  "Single UV animation commit changed TimeTicker/world-shadow overlap or a clean row");
        }
        const auto committedPS=ps;const auto committedVS=vs;dirty.fill(0);
        projectRigidMaterial(singleValues,dirty,ps,0x82042F58,alpha);
        projectRigidVertexMaterial(singleValues,dirty,vs,0x82042F58,alpha);
        check(ps==committedPS&&vs==committedVS,"Single UV clean commit lost selected-pass accumulation");
        rejected=false;
        try{projectRigidMaterial(std::span<const uint32_t>(singleValues).first(131),dirty,ps,0x82042F58,alpha);}
        catch(const std::invalid_argument&){rejected=true;}
        check(rejected&&ps==committedPS,"Truncated single UV bank changed the committed pixel snapshot");
    }
    // Flipbook maps differ from single/dual UV. The real reflected ticker18
    // stays inherited; alpha-test16 and the opaque-only rim have no alpha map.
    const auto flipbookProfile=rigidProfile(0x82039208);const auto flipbookAlpha=rigidAlphaPass(0x82039208);
    check(flipbookProfile.vertex==0x820398BC&&flipbookProfile.pixel==0x8203A24C&&flipbookProfile.context==0x2560&&
          flipbookProfile.samplers==6&&flipbookProfile.words==120&&flipbookProfile.textured&&flipbookProfile.flipbook&&
          !flipbookProfile.singleUv&&!flipbookProfile.uv,"Flipbook profile borrowed another UV storage/resource contract");
    check(flipbookAlpha.vertex==0x82039D70&&flipbookAlpha.pixel==0x8203A644&&flipbookAlpha.context==0x2840&&flipbookAlpha.samplers==6,
          "Flipbook alpha profile lost its original records");
    std::array<uint32_t,120> flipbookValues{};
    for(size_t i=0;i<flipbookValues.size();++i)flipbookValues[i]=bits(float(i)+.0625f);
    for(bool alpha:{false,true})for(unsigned leaf=0;leaf<21;++leaf) {
        auto ps=before;auto vs=uvBeforeVS;dirty.fill(0);dirty[leaf/8]=uint8_t(0x80>>(leaf&7));
        projectRigidMaterial(flipbookValues,dirty,ps,0x82039208,alpha);
        projectRigidVertexMaterial(flipbookValues,dirty,vs,0x82039208,alpha);
        for(size_t reg=0;reg<ps.size();++reg)for(size_t lane=0;lane<4;++lane) {
            const unsigned offset=(leaf==14&&reg==49)?80:(!alpha&&leaf==20&&reg==46)?116:0;
            check(bits(ps[reg][lane])==(offset?flipbookValues[offset+lane]:bits(before[reg][lane])),
                  "Flipbook pixel commit changed unused alpha-test, inherited or inactive rim lanes");
        }
        for(size_t reg=0;reg<vs.size();++reg)for(size_t lane=0;lane<4;++lane)
            check(bits(vs[reg][lane])==((leaf==17&&reg==47)?flipbookValues[92+lane]:bits(uvBeforeVS[reg][lane])),
                  "Flipbook animation commit changed reflected ticker, world or clean lanes");
        const auto committedPS=ps;const auto committedVS=vs;dirty.fill(0);
        projectRigidMaterial(flipbookValues,dirty,ps,0x82039208,alpha);
        projectRigidVertexMaterial(flipbookValues,dirty,vs,0x82039208,alpha);
        check(ps==committedPS&&vs==committedVS,"Flipbook clean commit lost selected-pass accumulation");
        for(bool vertex:{false,true}) {
            rejected=false;
            try {if(vertex)projectRigidVertexMaterial(std::span<const uint32_t>(flipbookValues).first(119),dirty,vs,0x82039208,alpha);
                 else projectRigidMaterial(std::span<const uint32_t>(flipbookValues).first(119),dirty,ps,0x82039208,alpha);}
            catch(const std::invalid_argument&){rejected=true;}
            check(rejected&&ps==committedPS&&vs==committedVS,"Truncated flipbook bank changed a committed snapshot");
        }
    }
    std::printf("Rigid material accumulation: %zu checks, 0 failures\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Rigid material accumulation failed after %zu checks: %s\n",checks,e.what());return 1;}
