#include "renderer/driver_resources.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace Simpsons::Graphics;
namespace {
void require(bool value,const char* reason) {if(!value) throw Error(reason);}
template<class F> void rejects(F action) {
    try {action();} catch(const Error&) {return;}
    throw Error("Startup resource failure was accepted");
}

enum class Fault {None,Before,After,Null,AliasColor,AliasDepth,AliasVertex,WrongColor,WrongDepth,WrongBuffer};

// Every successful allocation is a real backend object. Only weak references
// are retained here so this observer cannot mask a bundle ownership leak.
class ObservedFactory final : public StartupResourceFactory {
    NativeBackend& backend;
    std::weak_ptr<RenderTarget> firstColor;
    std::weak_ptr<DepthTarget> firstDepth;
    std::weak_ptr<Buffer> firstVertex;
    Fault fault;
    unsigned failAt;
    bool begin() {
        ++calls;
        if(calls==failAt && fault==Fault::Before) throw Error("Injected failure before native allocation");
        return calls==failAt && fault==Fault::Null;
    }
    template<class T> std::shared_ptr<T> record(std::shared_ptr<T> result) {
        observed.emplace_back(result);
        if(calls==failAt && fault==Fault::After) throw Error("Injected failure after native allocation");
        return result;
    }
public:
    unsigned calls{};
    std::vector<std::weak_ptr<void>> observed;
    explicit ObservedFactory(NativeBackend& source,Fault kind=Fault::None,unsigned at=0):
        backend(source),fault(kind),failAt(at) {}
    std::shared_ptr<RenderTarget> createTarget(uint32_t width,uint32_t height,TargetFormat format) override {
        if(begin()) return {};
        if(fault==Fault::AliasColor)
            if(auto retained=firstColor.lock()) return record(std::move(retained));
        auto result=backend.createTarget(width,height,fault==Fault::WrongColor?TargetFormat::RGBA8:format);
        firstColor=result;
        return record(std::move(result));
    }
    std::shared_ptr<DepthTarget> createDepthTarget(uint32_t width,uint32_t height) override {
        if(begin()) return {};
        if(fault==Fault::AliasDepth)
            if(auto retained=firstDepth.lock()) return record(std::move(retained));
        auto result=backend.createDepthTarget(width+(fault==Fault::WrongDepth?1:0),height);
        firstDepth=result;
        return record(std::move(result));
    }
    std::shared_ptr<Buffer> createBuffer(uint32_t size,BufferKind kind) override {
        if(begin()) return {};
        if(kind==BufferKind::Vertex && fault==Fault::AliasVertex)
            if(auto retained=firstVertex.lock()) return record(std::move(retained));
        auto result=backend.createBuffer(size,fault==Fault::WrongBuffer?BufferKind::Vertex:kind);
        if(kind==BufferKind::Vertex) firstVertex=result;
        return record(std::move(result));
    }
    void allReleased() const {
        for(const auto& resource:observed) require(resource.expired(),"Partial native resource escaped rollback");
    }
    void allOwnedOnce() const {
        require(observed.size()==11,"Wrong startup allocation count");
        for(const auto& resource:observed) require(resource.use_count()==1,"Bundle does not exclusively own an unretained resource");
    }
};

std::vector<uint8_t> bufferBytes(size_t size,unsigned salt) {
    std::vector<uint8_t> result(size);
    for(size_t i=0;i<size;++i) result[i]=uint8_t(i*17+i/13+salt*43);
    return result;
}

void verifyNativeBacking(NativeBackend& backend,const StartupResources::Resources& resources) {
    require(resources.width==19 && resources.height==7,"Bundle lost input dimensions");
    const std::array colors={resources.defaultColor,resources.frontColor0,resources.frontColor1,resources.colorCopy};
    const std::array<std::array<float,4>,4> clears={{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
    const std::array<uint32_t,4> packed={0x3FF,0xFFC00,0x3FF00000,0xC0000000};
    for(size_t i=0;i<colors.size();++i) {
        require(colors[i]->width==19 && colors[i]->height==7 && colors[i]->format==TargetFormat::RGB10A2,
                "Startup target dimensions or format changed");
        backend.clearTarget(colors[i],clears[i]);
    }
    // Read after clearing ALL roles: distinct wrapper pointers alone would not
    // establish distinct native backing. No startup content was presumed zero.
    for(size_t i=0;i<colors.size();++i) {
        auto bytes=backend.readbackTarget(colors[i]);
        require(bytes.size()==19*7*4,"Unexpected native color storage");
        for(size_t offset=0;offset<bytes.size();offset+=4) {
            uint32_t word;std::memcpy(&word,bytes.data()+offset,4);
            require(word==packed[i],"Startup colors alias or are not actual RGB10A2 targets");
        }
    }
    const std::array depths={resources.defaultDepth,resources.depthCopy};
    for(size_t i=0;i<depths.size();++i) {
        require(depths[i]->pixelWidth()==19 && depths[i]->pixelHeight()==7,"Wrong depth dimensions");
        // Both values are exact in original 20e4; no depth rasterization here.
        backend.clearDepthTarget(depths[i],i?0.5f:1.0f,uint8_t(0x31+i));
    }
    for(size_t i=0;i<depths.size();++i) {
        auto bytes=backend.readbackDepthTarget(depths[i]);
        require(bytes.size()==19*7*8,"Unexpected native depth/stencil storage");
        for(size_t offset=0;offset<bytes.size();offset+=8) {
            float depth;std::memcpy(&depth,bytes.data()+offset,4);
            require(depth==(i?0.5f:1.0f) && bytes[offset+4]==0x31+i,
                    "Startup depth roles alias or stencil was lost");
            // The remaining three bytes are padding, not prescribed data.
        }
    }
    require(resources.scratchIndex->byteSize()==0x4E20 && resources.scratchIndex->type()==BufferKind::Index16,
            "Wrong scratch index allocation contract");
    auto scratch=bufferBytes(0x4E20,17);
    backend.writeBuffer(resources.scratchIndex,0,scratch);
    for(size_t i=0;i<resources.vertices.size();++i) {
        const auto& vertex=resources.vertices[i];
        require(vertex->byteSize()==0x40000 && vertex->type()==BufferKind::Vertex,"Wrong vertex allocation contract");
        backend.writeBuffer(vertex,0,bufferBytes(0x40000,unsigned(i)));
    }
    require(backend.readbackBuffer(resources.scratchIndex)==scratch,"Scratch index data did not survive vertex uploads");
    for(size_t i=0;i<resources.vertices.size();++i)
        require(backend.readbackBuffer(resources.vertices[i])==bufferBytes(0x40000,unsigned(i)),
                "Startup vertex roles alias or lost native storage");
}

void run(bool hardware) {
    NativeBackend backend(!hardware);
    StartupResources resources;
    require(!resources.hasResources(),"Empty bundle claims allocations");
    rejects([&]{resources.resources();});
    resources.reset();resources.reset();
    for(auto [width,height]:{std::pair{0u,7u},{19u,0u},{0u,0u},{16385u,7u},{19u,16385u},{0xFFFFFFFFu,7u}}) {
        ObservedFactory factory(backend);
        rejects([&]{resources.initialize(factory,width,height);});
        require(factory.calls==0 && !resources.hasResources(),"Invalid dimensions reached native allocation");
    }

    ObservedFactory initial(backend);
    resources.initialize(initial,19,7);
    initial.allOwnedOnce();verifyNativeBacking(backend,resources.resources());initial.allOwnedOnce();
    const auto* original=&resources.resources();
    for(Fault fault:{Fault::Before,Fault::After,Fault::Null}) {
        for(unsigned at=1;at<=11;++at) {
            ObservedFactory replacement(backend,fault,at);
            rejects([&]{resources.initialize(replacement,31,13);});
            require(replacement.calls==at,"Fault did not stop at the selected allocation");
            require(&resources.resources()==original && resources.resources().width==19,
                    "Failed replacement modified the previous bundle");
            replacement.allReleased();initial.allOwnedOnce();

            StartupResources empty;
            ObservedFactory firstAttempt(backend,fault,at);
            rejects([&]{empty.initialize(firstAttempt,31,13);});
            require(!empty.hasResources(),"Failed initial allocation published a partial bundle");
            firstAttempt.allReleased();
        }
    }
    for(Fault fault:{Fault::AliasColor,Fault::AliasDepth,Fault::AliasVertex,
                    Fault::WrongColor,Fault::WrongDepth,Fault::WrongBuffer}) {
        ObservedFactory invalid(backend,fault);
        rejects([&]{resources.initialize(invalid,31,13);});
        require(&resources.resources()==original,"Invalid factory result replaced the bundle");
        invalid.allReleased();initial.allOwnedOnce();
    }

    std::atomic<bool> rejected=false;
    std::thread other([&]{try {resources.initialize(backend,31,13);} catch(const Error&) {rejected=true;}});
    other.join();
    require(rejected && &resources.resources()==original,"Owner-thread rejection lost previous allocations");
    verifyNativeBacking(backend,resources.resources());

    auto retainedColor=resources.resources().defaultColor;
    auto retainedDepth=resources.resources().defaultDepth;
    auto retainedVertex=resources.resources().vertices[2];
    ObservedFactory replacement(backend);
    resources.initialize(replacement,31,13);
    replacement.allOwnedOnce();
    require(resources.resources().width==31 && resources.resources().height==13,"Replacement dimensions not published");
    for(size_t i=0;i<initial.observed.size();++i)
        require(initial.observed[i].use_count()==(i==0 || i==1 || i==9?1:0),
                "Successful replacement leaked old ownership or destroyed retained references");
    require(backend.readbackTarget(retainedColor).size()==19*7*4,"Retained old color no longer usable");
    require(backend.readbackDepthTarget(retainedDepth).size()==19*7*8,"Retained old depth no longer usable");
    require(backend.readbackBuffer(retainedVertex)==bufferBytes(0x40000,2),"Retained old vertex changed");
    retainedColor.reset();retainedDepth.reset();retainedVertex.reset();initial.allReleased();
    resources.reset();resources.reset();replacement.allReleased();
    require(!resources.hasResources(),"Reset left bundle allocated");

    // Production convenience constructor, movement, destruction and repeated
    // allocation cycles use the same native path, without the observing seam.
    std::vector<std::weak_ptr<void>> destroyed;
    for(unsigned cycle=0;cycle<4;++cycle) {
        StartupResources created(backend,19,7);
        destroyed.emplace_back(created.resources().defaultColor);
        destroyed.emplace_back(created.resources().depthCopy);
        destroyed.emplace_back(created.resources().scratchIndex);
        StartupResources moved(std::move(created));
        require(!created.hasResources() && moved.hasResources(),"Move did not transfer bundle ownership");
    }
    for(const auto& object:destroyed) require(object.expired(),"Bundle destructor retained native ownership");
    require(backend.presentationCount()==0 && backend.screenDrawCount()==0,"Resource test submitted a frame");
}
}

int main(int argc,char** argv) {
    try {
        if(argc>2 || (argc==2 && std::string(argv[1])!="--hardware"))
            throw Error("Usage: DriverResourcesTests [--hardware]");
        run(argc==2);
        puts("Startup resource ownership passed: six distinct targets, five buffers, 66 injected rollback cases; no engine start or draws.");
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"Driver resource contract failure: %s\n",error.what());
        return 1;
    }
}
