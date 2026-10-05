#include "material_resources.h"
#include "build/generated/effect_catalog.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <limits>
#include <utility>

namespace Simpsons::Graphics {
namespace {
constexpr MaterialIdentity startupIdentities[]={
    {0x821524C8,MaterialStage::Pixel,288,"Screen_Xenon_PSFlat","bd254ae4adf48d461ab2ddc1958156ca0dafa3d2a1bc4ed67d1a9a3238ae1709"},
    {0x821525E8,MaterialStage::Vertex,284,"Screen_Xenon_VSFlat","58a5e69479abbfb250270590a3c2254720aa1dc04b5a60858c6fc3f05376cccc"},
    {0x82152708,MaterialStage::Pixel,372,"Screen_Xenon_PSTextured","3257ec7a18a3daa21fe5c570da1d9cdac2941f33c1e7f7cf4df72248b4f83780"},
    {0x82152880,MaterialStage::Vertex,324,"Screen_Xenon_VSTextured","475f31982d13ae15928e0498686bcd9bdb6da902e5c5479228f4a390efe678a9"},
    {0x82199150,MaterialStage::Pixel,328,"","51500945fc0256a35320c24dfaa461eb4ecf90e7abe40e5ad1b22e3cc47de2ea"},
    {0x82199298,MaterialStage::Pixel,200,"","23e34823b102c00af7fd6a5a47e780699d74a4e147033272531f8249cb0ce7b8"},
    {0x82199360,MaterialStage::Pixel,184,"","0b448c12b5e3740b58aabddaa6fe3bae4548263ed9321278fa293e553e786737"},
    {0x82199418,MaterialStage::Pixel,380,"","a10f87ebbe5a227902156f8a5f4f3fa8efc0b0b390ec39438d652c2eca208239"},
    {0x82199598,MaterialStage::Pixel,416,"","6afaf3929d722ecbec6f907f75454b1c281deee15174e36c338894a2eabba5d7"},
    {0x82199738,MaterialStage::Pixel,404,"","761d88c371244b22e95160b84fd09a6b5ccf86db137f274bc73c888572fc53ef"},
    {0x821B7050,MaterialStage::Pixel,428,"","cc06d8d1bdf50c22201e2d8f059905ade1eb6a0abc98af1124101e648f15143f"},
    {0x821B7200,MaterialStage::Pixel,440,"","462b4a059eeedd41211690bbf7fc25e1778c8a112ac18cc502bcf1554087e60a"},
    {0x821B7DD8,MaterialStage::Pixel,636,"","883e732fe1e1fe1fe8215357d712f92379f8f5b5e24c7052a3ad8ec9711652dd"},
    {0x821B8058,MaterialStage::Pixel,652,"","923c6388a441dae8f5c74c3d7a767c0d07af4be3bca40d871ed7faa2e3d5514c"},
    {0x821B73B8,MaterialStage::Pixel,364,"","30c732a3a7f442f2301bbb2f099885acb5a183644081998ec9fc9112ab505cb6"},
    {0x821B7528,MaterialStage::Pixel,376,"","d8970bae27097b022777c1fa072880c064386d80e8f3cda6f5a5b2b55628ed04"},
    {0x821B82E8,MaterialStage::Pixel,584,"","c29d3682679eed5a8d57536097725a866c98b3b339bb969be180829ce7f6a12d"},
    {0x821B8530,MaterialStage::Pixel,600,"","faae2a35c8eab657af2e3e4fe5f557e7e9df580e9c87fd955e02ab463a2dffbd"},
    {0x821B8B70,MaterialStage::Pixel,612,"","b3472e1289112d330eb75e511a10c043a12ac05b6379019fbe176b0fc1ecd89b"},
    {0x821B8DD8,MaterialStage::Pixel,628,"","3fd82e1ab39351a2d1cb780ed64f835d7373ed719d0218a5257fb74f0b570795"},
};
// Preserve the four screen and sixteen stripped startup records in their exact
// historical order. Generated FX entries include FourTap at indices20/21 once.
constexpr auto identities=[] {
    std::array<MaterialIdentity,std::size(startupIdentities)+Generated::effectMaterialIdentities.size()+21> result{};
    size_t at=0;
    for(const auto& identity:startupIdentities) result[at++]=identity;
    for(const auto& identity:Generated::effectMaterialIdentities) result[at++]=identity;
    // Append the independently qualified movie PS without reindexing existing
    // screen/startup/FX records. Literal constants are part of its pinned bytes.
    result[at++]={0x82152B68,MaterialStage::Pixel,0x21C,"vp6_y_cr_cb_Xenon_PS",
        "48d052096755186f10dc040a2e0718544f2dea5692f59b6e2ca5a9435351deee"};
    result[at++]={0x82153278,MaterialStage::Vertex,484,"CoronaQuery_VS","248aa2ab41c6d88687f7cf937dd7f318974c70a4d31320ae06a66c1ebf790672"};
    result[at++]={0x82153460,MaterialStage::Pixel,652,"CoronaQuery_PS","5b8b4c65b746d1a3e00d89074bb1b805f7f698ebf6df5464deb81fd53d80c05c"};
    result[at++]={0x821536F0,MaterialStage::Pixel,500,"CoronaSprite_PS","7f58913b69b8a16abf6ef477ef374170c900b430c0b4aea82cb3472abd77e2e8"};
    result[at++]={0x821511D8,MaterialStage::Vertex,476,"Immediate_VS","1c1dba8b408e655abf59080e7269f82475ac017a03c47e1822f862f3d25a95e6"};
    result[at++]={0x821509D8,MaterialStage::Pixel,320,"Immediate_PS","72598ead0b2bfdd687aefe4b133485032bbf30422859ac2e72da53dba18b7ef5"};
    result[at++]={0x82150B18,MaterialStage::Pixel,0x180,"ImmediateDual_PS","1e2b6d7a9c6f1ffd9e4662d1cb326c6baa43d6222ff26c80d0d62dd69c4b55a3"};
    result[at++]={0x821538E8,MaterialStage::Vertex,668,"Radial_VS","e3fbc38bef4533c19bb237326733af0095dd8b4eb8455c3b182e6157da10db4b"};
    result[at++]={0x82153B88,MaterialStage::Pixel,244,"Radial_PS","cebf30e9fb47af4d4ade02ef67e22a431a4da488c89d2e158670706eeb91adf9"};
    result[at++]={0x82153C80,MaterialStage::Pixel,340,"RadialQuery_PS","fed4ef38b9119751a349dae0ff3e24e2241c9f90ed1a7f1c85a8c6b7e8ccb05d"};
    // Standalone82773D40 originals: owned record validation only; the post
    // backend selects its separately qualified ahead-of-time artifacts.
    result[at++]={0x821529C8,MaterialStage::Vertex,0x1A0,"Screen_Xenon_VSAutoTextured","614b202400472622f158bacbbdf85f7ad1e858e07f97c3e2894ac72e4add3d98"};
    result[at++]={0x821583B8,MaterialStage::Pixel,0x140,"Glow_Xenon_PSAlphaToRGBA","68e04d6e31cf1589505521c90776d4fb25d00098d2a0d925fac9b1fbd85d176f"};
    result[at++]={0x82155F28,MaterialStage::Pixel,0x228,"Distort_Xenon_PSFiltered","79a5ed98ef041d70881017e11afc24fe145bf87b4880c3e62c84fcd2d79ebb18"};
    result[at++]={0x82158118,MaterialStage::Pixel,0x2A0,"Glow_Xenon_PS","e31b962ae3b94d98b18501ab3609c439b8221b3b6082d8ebf697beacf30344cb"};
    result[at++]={0x82155D60,MaterialStage::Pixel,0x1C4,"Distort_Xenon_PSSprite","ae476ee112be6bf2ae46c842758d3a27cce2a9d15ac218234d36acd43f54dec2"};
    result[at++]={0x82156548,MaterialStage::Vertex,0x220,"Distort_Xenon_VSSprite","2e3e12ad3ec9bf12b9834fef3217d5a2ea84ae99efe24ad054e1b71ffb84dcdd"};
    result[at++]={0x82156150,MaterialStage::Pixel,0x1F0,"Distort_Xenon_PSEmboss","a5c1986f6ac7013e9d917b60571eb9b4acff9284defa187e11c703c98f9deda4"};
    result[at++]={0x82156340,MaterialStage::Pixel,0x204,"Distort_Xenon_PSDisplace","e596b80d58c7f9064a24bb5890109509fb22ecb681237b06691394762a95124b"};
    result[at++]={0x821513B8,MaterialStage::Vertex,824,"ImmediateProjected_VS","54d2ce2a05bed7e87a68b951c67901715a5daaa12830a58e2af120a4ca0ad3f3"};
    result[at++]={0x82150C98,MaterialStage::Pixel,636,"ImmediateProjected_PS","38073f6a85020ac7e1364df305042cf3484c857d45ebdde3db6bff7cc82ca428"};
    result[at++]={0x82150F18,MaterialStage::Pixel,700,"ImmediateProjectedDual_PS","90fe8241c19a06883059c8c9396d94e8e5d9bfa0ce0f4ba98960c01228f9c0d8"};
    return result;
}();
static_assert(identities.size()==256);

uint32_t be32(std::span<const uint8_t> bytes,size_t offset) {
    if(offset>bytes.size() || bytes.size()-offset<4) throw MaterialError("Truncated material word");
    return uint32_t(bytes[offset])<<24 | uint32_t(bytes[offset+1])<<16 |
           uint32_t(bytes[offset+2])<<8 | uint32_t(bytes[offset+3]);
}

// Small SHA-256 for record identity only; no platform/crypto-library dependency.
// Input is already restricted to one of the pinned bounded record sizes.
std::array<uint8_t,32> sha256(std::span<const uint8_t> bytes) {
    constexpr uint32_t k[]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::array<uint32_t,8> h={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                             0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    const size_t padded=((bytes.size()+9+63)/64)*64;
    for(size_t block=0;block<padded;block+=64) {
        std::array<uint8_t,64> chunk{};
        for(size_t i=0;i<64;++i) {
            const size_t pos=block+i;
            if(pos<bytes.size()) chunk[i]=bytes[pos];
            else if(pos==bytes.size()) chunk[i]=0x80;
            else if(pos>=padded-8) chunk[i]=uint8_t((uint64_t(bytes.size())*8)>>((padded-1-pos)*8));
        }
        std::array<uint32_t,64> w{};
        for(size_t i=0;i<16;++i) w[i]=be32(chunk,i*4);
        for(size_t i=16;i<64;++i) {
            const auto x=w[i-15],y=w[i-2];
            const auto s0=std::rotr(x,7)^std::rotr(x,18)^(x>>3);
            const auto s1=std::rotr(y,17)^std::rotr(y,19)^(y>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],v=h[7];
        for(size_t i=0;i<64;++i) {
            const auto s1=std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25);
            const auto t1=v+s1+((e&f)^(~e&g))+k[i]+w[i];
            const auto s0=std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22);
            const auto t2=s0+((a&b)^(a&c)^(b&c));
            v=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=v;
    }
    std::array<uint8_t,32> result{};
    for(size_t i=0;i<32;++i) result[i]=uint8_t(h[i/4]>>(24-8*(i%4)));
    return result;
}

bool matchesDigest(std::span<const uint8_t> bytes,std::string_view expected) {
    const auto digest=sha256(bytes);
    constexpr std::string_view hex="0123456789abcdef";
    if(expected.size()!=64) return false;
    for(size_t i=0;i<digest.size();++i)
        if(expected[2*i]!=hex[digest[i]>>4] || expected[2*i+1]!=hex[digest[i]&15]) return false;
    return true;
}

std::pmr::memory_resource* checkedMemory(std::pmr::memory_resource* memory) {
    if(!memory) throw MaterialError("Null material allocation resource");
    return memory;
}

struct MutationGuard {
    bool& active;
    explicit MutationGuard(bool& value) noexcept:active(value) {active=true;}
    ~MutationGuard() {active=false;}
};

// Shared across registry instances, so IDs from a destroyed or different owner
// cannot accidentally resolve. Exhaustion is an error; no wrapping or recycling.
std::atomic<uint64_t> nextGeneration{1};
uint64_t allocateGeneration() {
    uint64_t current=nextGeneration.load(std::memory_order_relaxed);
    while(current!=std::numeric_limits<uint64_t>::max()) {
        if(nextGeneration.compare_exchange_weak(current,current+1,std::memory_order_relaxed)) return current;
    }
    throw MaterialError("Material generation space exhausted");
}
}

namespace detail {
// Shared CPU-only identity primitive used by the independently decoded FX
// owner. Keep the existing SHA implementation and material validation unchanged.
bool matchesOriginalRecordDigest(std::span<const uint8_t> bytes,std::string_view expected) {
    return matchesDigest(bytes,expected);
}
}

std::span<const MaterialIdentity> originalMaterialIdentities() noexcept {return identities;}

MaterialRecord::MaterialRecord(const MaterialIdentity& identity,std::span<const uint8_t> source,
                               std::pmr::memory_resource* memory)
    :identity_(&identity),bytes_(source.begin(),source.end(),memory) {
    for(size_t i=0;i<metadata_.headerWords.size();++i) metadata_.headerWords[i]=be32(bytes_,i*4);
    const auto& h=metadata_.headerWords;
    const uint32_t tag=identity.stage==MaterialStage::Vertex?0x102A1101u:0x102A1100u;
    if(h[0]!=tag) throw MaterialError("Material stage/tag does not match pinned identity");
    metadata_.headerBytes=h[1];metadata_.payloadBytes=h[2];
    if(h[1]<36 || h[1]%4 || h[1]>bytes_.size() || h[2]!=bytes_.size()-h[1])
        throw MaterialError("Invalid material header/payload framing");
    // Original sections use header-relative offsets. Validate pointers only;
    // do not invent layouts for debug/reflection/constants not decoded here.
    for(size_t field=3;field<=6;++field)
        if(h[field] && (h[field]%4 || h[field]<36 || h[field]>=h[1]))
            throw MaterialError("Invalid material metadata offset");
    if(!h[6] || h[1]-h[6]<8) throw MaterialError("Truncated material code metadata");
    metadata_.debugOffset=h[3];metadata_.reflectionOffset=h[4];
    metadata_.constantMetadataOffset=h[5];metadata_.codeMetadataOffset=h[6];
    metadata_.prefixBytes=be32(bytes_,h[6]);metadata_.codeBytes=be32(bytes_,h[6]+4);
    if(metadata_.prefixBytes%4 || metadata_.prefixBytes>h[2] ||
       metadata_.codeBytes!=h[2]-metadata_.prefixBytes ||
       metadata_.codeBytes<24 || metadata_.codeBytes%12)
        throw MaterialError("Invalid material payload prefix/code framing");
    if(!matchesDigest(bytes_,identity.sha256)) throw MaterialError("Original material record hash mismatch");
}

std::span<const uint8_t> MaterialRecord::header() const noexcept {return bytes().first(metadata_.headerBytes);}
std::span<const uint8_t> MaterialRecord::payload() const noexcept {return bytes().subspan(metadata_.headerBytes);}
std::span<const uint8_t> MaterialRecord::prefix() const noexcept {return payload().first(metadata_.prefixBytes);}
std::span<const uint8_t> MaterialRecord::code() const noexcept {return payload().subspan(metadata_.prefixBytes);}
std::span<const uint8_t> MaterialRecord::codeMetadata() const noexcept {return header().subspan(metadata_.codeMetadataOffset);}

CompiledMaterial::~CompiledMaterial()=default;

struct MaterialRegistry::Entry {
    std::unique_ptr<const MaterialRecord> record;
    uint32_t references=1;
    MaterialCapability capability=MaterialCapability::Uncompiled;
    std::unique_ptr<CompiledMaterial> compiled;
    std::string unsupported;
};
MaterialRegistry::Slot::Slot()=default;
MaterialRegistry::Slot::~Slot()=default;
MaterialRegistry::Slot::Slot(Slot&&) noexcept=default;
MaterialRegistry::Slot& MaterialRegistry::Slot::operator=(Slot&&) noexcept=default;

MaterialRegistry::MaterialRegistry(MaterialLimits limits,std::pmr::memory_resource* memory)
    :limits_(limits),memory_(checkedMemory(memory)),slots_(memory_) {
    if(!limits_.maxLive || !limits_.maxReferences) throw MaterialError("Material limits must be positive");
}
MaterialRegistry::~MaterialRegistry() {
    callbacksActive_=true;
    clearEntries(); // Invalidate all IDs before backend/resource destructors run.
}

void MaterialRegistry::requireMutable() const {
    if(callbacksActive_) throw MaterialError("Material callback cannot reenter registry mutation");
}

MaterialId MaterialRegistry::create(uint32_t originalAddress,std::span<const uint8_t> source) {
    requireMutable();
    MutationGuard guard(callbacksActive_);
    const auto found=std::find_if(std::begin(identities),std::end(identities),
        [originalAddress](const auto& i){return i.originalAddress==originalAddress;});
    if(found==std::end(identities)) throw MaterialError("Unknown original material address");
    if(source.size()!=found->recordBytes) throw MaterialError("Original material record must have exact pinned size");
    if(live_>=limits_.maxLive) throw MaterialError("Material resource limit reached");
    auto pending=std::make_unique<Entry>();
    pending->record=std::unique_ptr<const MaterialRecord>(new MaterialRecord(*found,source,memory_));
    size_t index=0;
    while(index<slots_.size() && slots_[index].entry) ++index;
    const bool append=index==slots_.size();
    if(append) slots_.emplace_back(); // Pending owned bytes unwind if growth fails.
    uint64_t generation;
    try {generation=allocateGeneration();}
    catch(...) {if(append) slots_.pop_back();throw;}
    auto& slot=slots_[index];
    slot.generation=generation;slot.entry=std::move(pending);++live_;
    return {generation,static_cast<uint32_t>(index)};
}

bool MaterialRegistry::contains(MaterialId id) const noexcept {
    return id.generation && id.slot<slots_.size() &&
           slots_[id.slot].generation==id.generation && bool(slots_[id.slot].entry);
}
const MaterialRegistry::Entry& MaterialRegistry::get(MaterialId id) const {
    if(!contains(id)) throw MaterialError("Invalid, foreign or stale material ID");
    return *slots_[id.slot].entry;
}
MaterialRegistry::Entry& MaterialRegistry::get(MaterialId id) {
    return const_cast<Entry&>(std::as_const(*this).get(id));
}
const MaterialRecord& MaterialRegistry::record(MaterialId id) const {return *get(id).record;}
uint32_t MaterialRegistry::referenceCount(MaterialId id) const {return get(id).references;}
MaterialCapability MaterialRegistry::capability(MaterialId id) const {return get(id).capability;}
std::string_view MaterialRegistry::unsupportedReason(MaterialId id) const {return get(id).unsupported;}

void MaterialRegistry::retain(MaterialId id) {
    requireMutable();auto& entry=get(id);
    if(entry.references>=limits_.maxReferences) throw MaterialError("Material reference limit reached");
    ++entry.references;
}
void MaterialRegistry::release(MaterialId id) {
    requireMutable();auto& entry=get(id);
    if(entry.references>1) {--entry.references;return;}
    MutationGuard guard(callbacksActive_);
    auto released=std::move(slots_[id.slot].entry);
    --live_; // ID is already invalid before owned artifacts are destroyed.
}
void MaterialRegistry::reset() {
    requireMutable();
    MutationGuard guard(callbacksActive_);
    clearEntries();
}
void MaterialRegistry::clearEntries() noexcept {
    // Two passes: even the first backend destructor observes all old IDs as
    // invalid and the count as zero. No allocation is required for teardown.
    for(auto& slot:slots_) slot.generation=0;
    live_=0;
    for(auto& slot:slots_) slot.entry.reset();
}

const CompiledMaterial& MaterialRegistry::requireCompiled(MaterialId id) const {
    const auto& entry=get(id);
    if(entry.capability==MaterialCapability::Unsupported) throw UnsupportedMaterial(entry.unsupported);
    if(entry.capability!=MaterialCapability::Compiled || !entry.compiled)
        throw MaterialError("Material is uncompiled; native bind rejected");
    return *entry.compiled;
}

const CompiledMaterial& MaterialRegistry::prepareForBind(MaterialId id,MaterialCompiler& compiler) {
    requireMutable();auto& entry=get(id);
    if(entry.capability!=MaterialCapability::Uncompiled) return requireCompiled(id);
    // Covers the compiler, virtual exception diagnostics, state publication,
    // and destruction of an invalid/failed returned artifact.
    MutationGuard guard(callbacksActive_);
    std::unique_ptr<CompiledMaterial> pending;
    try {
        pending=compiler.compile(*entry.record);
    } catch(const UnsupportedMaterial& error) {
        // If storing the diagnostic itself fails, keep the prior Uncompiled state.
        std::string reason=error.what();
        entry.unsupported=std::move(reason);
        entry.capability=MaterialCapability::Unsupported;
        throw;
    }
    const auto& identity=entry.record->identity();
    if(!pending || pending->originalAddress()!=identity.originalAddress || pending->stage()!=identity.stage)
        throw MaterialError("Compiler returned null or a mismatched material artifact");
    entry.compiled=std::move(pending);
    entry.capability=MaterialCapability::Compiled;
    return *entry.compiled;
}
}
