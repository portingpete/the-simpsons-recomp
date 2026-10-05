#include "driver_resources.h"

namespace Simpsons::Graphics {
namespace {
class BackendFactory final : public StartupResourceFactory {
    NativeBackend& backend;
public:
    explicit BackendFactory(NativeBackend& source):backend(source) {}
    std::shared_ptr<RenderTarget> createTarget(uint32_t width,uint32_t height,TargetFormat format) override {
        return backend.createTarget(width,height,format,TargetScale::Scene);
    }
    std::shared_ptr<DepthTarget> createDepthTarget(uint32_t width,uint32_t height) override {
        return backend.createDepthTarget(width,height,TargetScale::Scene);
    }
    std::shared_ptr<Buffer> createBuffer(uint32_t size,BufferKind kind) override {
        return backend.createBuffer(size,kind);
    }
};

template<class T> std::shared_ptr<T> owned(std::shared_ptr<T> resource) {
    if(!resource) throw Error("Startup allocation returned no native resource");
    return resource;
}

void validate(const StartupResources::Resources& value) {
    const std::array colors={value.defaultColor,value.frontColor0,value.frontColor1,value.colorCopy};
    for(const auto& color:colors) {
        if(!color || color->width!=value.width || color->height!=value.height || color->format!=TargetFormat::RGB10A2)
            throw Error("Startup color allocation returned an invalid resource");
    }
    const std::array depths={value.defaultDepth,value.depthCopy};
    for(const auto& depth:depths) {
        if(!depth || depth->pixelWidth()!=value.width || depth->pixelHeight()!=value.height)
            throw Error("Startup depth allocation returned an invalid resource");
    }
    if(!value.scratchIndex || value.scratchIndex->byteSize()!=StartupResources::ScratchIndexBytes ||
       value.scratchIndex->type()!=BufferKind::Index16)
        throw Error("Startup scratch index allocation returned an invalid resource");
    for(const auto& vertex:value.vertices) {
        if(!vertex || vertex->byteSize()!=StartupResources::VertexBytes || vertex->type()!=BufferKind::Vertex)
            throw Error("Startup vertex allocation returned an invalid resource");
    }
    // Roles own distinct resources even when dimensions/format match. Reject
    // accidental factory reuse; the backend always creates fresh native objects.
    const std::array<const void*,11> identities={
        value.defaultColor.get(),value.defaultDepth.get(),value.depthCopy.get(),
        value.frontColor0.get(),value.frontColor1.get(),value.colorCopy.get(),
        value.scratchIndex.get(),value.vertices[0].get(),value.vertices[1].get(),
        value.vertices[2].get(),value.vertices[3].get()};
    for(size_t i=0;i<identities.size();++i)
        for(size_t j=0;j<i;++j)
            if(identities[i]==identities[j]) throw Error("Startup resource roles alias one allocation");
}
}

void StartupResources::initialize(NativeBackend& backend,uint32_t width,uint32_t height) {
    BackendFactory factory(backend);
    initialize(factory,width,height);
}

void StartupResources::initialize(StartupResourceFactory& factory,uint32_t width,uint32_t height) {
    if(!width || !height || width>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || height>D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        throw Error("Startup resource dimensions exceed positive D3D11 bounds");

    auto pending=std::make_unique<Resources>();
    pending->width=width;pending->height=height;
    pending->defaultColor=owned(factory.createTarget(width,height,TargetFormat::RGB10A2));
    pending->defaultDepth=owned(factory.createDepthTarget(width,height));
    pending->depthCopy=owned(factory.createDepthTarget(width,height));
    pending->frontColor0=owned(factory.createTarget(width,height,TargetFormat::RGB10A2));
    pending->frontColor1=owned(factory.createTarget(width,height,TargetFormat::RGB10A2));
    pending->colorCopy=owned(factory.createTarget(width,height,TargetFormat::RGB10A2));
    pending->scratchIndex=owned(factory.createBuffer(ScratchIndexBytes,BufferKind::Index16));
    for(auto& vertex:pending->vertices) vertex=owned(factory.createBuffer(VertexBytes,BufferKind::Vertex));
    validate(*pending);
    resources_.swap(pending);
    // The old owner (if any) releases here, after all new allocations validate.
}

const StartupResources::Resources& StartupResources::resources() const {
    if(!resources_) throw Error("Startup resources have not been allocated");
    return *resources_;
}
}
