#include "xma_source.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>

namespace Simpsons::Audio {
namespace {
constexpr auto maximum = std::numeric_limits<uint64_t>::max();
std::atomic<uint64_t> nextOwner{1};
uint64_t identity() {
    auto value = nextOwner.load();
    while (value != maximum)
        if (nextOwner.compare_exchange_weak(value, value + 1)) return value;
    throw XmaSourceError("XMA source owner identities exhausted");
}
void require(bool condition, const char* message) {
    if (!condition) throw XmaSourceError(message);
}
void increment(uint64_t& value, uint64_t amount) {
    if (amount > maximum - value) throw XmaSourceError("XMA source counter exhausted");
    value += amount;
}
}

struct XmaSource::State {
    struct Layer {
        std::unique_ptr<NativeXmaCodec> codec;
        LayerView view;
        std::vector<float> raw; // Bounded interleaved ring, no sample arithmetic.
        uint32_t head = 0;
    };
    struct Source {
        SourceView view;
        std::vector<LayerInput> input;
        std::vector<size_t> fed; // Packets per layer already sent to the codec.
        bool complete() const noexcept {
            for (size_t i = 0; i < input.size(); ++i) if (fed[i] < input[i].packets.size()) return false;
            return true;
        }
    };
    mutable std::mutex mutex;
    std::shared_ptr<NativeXmaCodecPool> pool; // Decoders and rings return here on close.
    Limits limits;
    uint64_t owner = 0, generation = 0, epoch = 0, lastSequence = 0, nextTicket = 1;
    uint64_t copiedBytes = 0, committedFrames = 0;
    uint32_t channels = 0;
    bool closed = false, configured = false;
    Failure failure;
    std::vector<Layer> layers;
    std::vector<std::unique_ptr<Source>> sources;
    std::vector<std::weak_ptr<const Quota>> tickets;
    Ticket pending;
    std::shared_ptr<const FreshImage> freshImage;
    Receipt sharedFreshDecode;

    State(NativeXmaFactory* factory, uint64_t instance, std::span<const XmaFormat> formats, Limits bounds)
        : limits(bounds), generation(instance) {
        require(instance && instance != maximum, "Invalid XMA instance generation");
        require(limits.maxSources && limits.maxSources <= 64 && limits.maxTickets && limits.maxTickets <= 8 &&
                limits.maxBufferedFramesPerLayer && limits.maxBufferedFramesPerLayer <= 4194304 &&
                limits.maxQuotaFrames && limits.maxQuotaFrames <= limits.maxBufferedFramesPerLayer &&
                limits.maxDeclaredFrames && limits.maxDeclaredFrames <= 0x7FFFFFFF &&
                limits.maxCompressedBytes && limits.maxCompressedBytes <= 64ull * 1024 * 1024,
                "Invalid XMA source storage limits");
        require(!formats.empty() && formats.size() <= 3, "XMA source requires one to three layers");
        for (const auto& format : formats) {
            require((format.channels == 1 || format.channels == 2) && format.sampleRate == formats[0].sampleRate &&
                    format.variant == formats[0].variant, "XMA layers require mono/stereo and a common rate/variant");
            channels += format.channels;
        }
        layers.reserve(formats.size());
        if (factory) pool = factory->pool();
        for (const auto& format : formats) {
            Layer layer;
            layer.view.channels = format.channels;
            if (pool) {
                layer.codec = pool->take(format);
                layer.raw = pool->takeRing(size_t(limits.maxBufferedFramesPerLayer) * format.channels);
            }
            layers.push_back(std::move(layer));
        }
        sources.resize(limits.maxSources);
        tickets.resize(limits.maxTickets);
        owner = identity();
    }
    void ready() const {
        require(!closed, "XMA source is closed");
        require(failure.stage == FailureStage::None, "XMA source has a terminal failure; inspect snapshot");
    }
    Receipt receipt(uint64_t sequence) const { return {owner, generation, sequence}; }
    Source& find(Receipt receiptValue) const {
        require(receiptValue.owner == owner && receiptValue.instanceGeneration == generation && receiptValue.sequence,
                "Foreign XMA source receipt");
        for (const auto& source : sources)
            if (source && source->view.receipt == receiptValue) return *source;
        throw XmaSourceError("Stale XMA source receipt");
    }
    Source* oldest() const {
        Source* result = nullptr;
        for (const auto& source : sources)
            if (source && source->view.status == SourceStatus::Prepared &&
                (!result || source->view.receipt.sequence < result->view.receipt.sequence)) result = source.get();
        return result;
    }
    void fail(FailureStage stage, Receipt source, uint32_t layer) noexcept {
        if (failure.stage == FailureStage::None) failure = {stage, source, layer, std::current_exception()};
        for (auto& value : sources)
            if (value && value->view.status == SourceStatus::Prepared) value->view.status = SourceStatus::Failed;
        pending.reset();
    }
    uint32_t read(Layer& layer, FailureStage& operation) {
        std::array<float, 1024> buffer{};
        operation = FailureStage::Decode;
        const auto count = layer.codec->read(std::span(buffer).first(size_t(512) * layer.view.channels));
        operation = FailureStage::Counter;
        increment(layer.view.decodedFrames, count);
        operation = FailureStage::Storage;
        require(count <= limits.maxBufferedFramesPerLayer - layer.view.bufferedFrames,
                "XMA decoded raw storage limit exceeded; source is terminal");
        // The codec already returns interleaved finite samples. Append the
        // same bytes in at most two contiguous spans, including ring wrap,
        // without a division and channel loop for every decoded frame.
        const auto at = (layer.head + layer.view.bufferedFrames) % limits.maxBufferedFramesPerLayer;
        const auto first = std::min(count, limits.maxBufferedFramesPerLayer - at);
        const auto frameBytes = size_t(layer.view.channels) * sizeof(float);
        std::memcpy(layer.raw.data() + size_t(at) * layer.view.channels, buffer.data(), size_t(first) * frameBytes);
        if (first < count)
            std::memcpy(layer.raw.data(), buffer.data() + size_t(first) * layer.view.channels,
                        size_t(count - first) * frameBytes);
        layer.view.bufferedFrames += count;
        return count;
    }
    void send(Layer& layer, const Packet& packet, FailureStage& operation) {
        for (;;) {
            operation = FailureStage::Decode;
            const auto result = layer.codec->send(packet);
            operation = FailureStage::Counter;
            if (result == PacketResult::Accepted) {
                increment(layer.view.acceptedPackets, 1);
                return; // The packet is never resubmitted after acceptance.
            }
            increment(layer.view.sendBackpressure, 1);
            const auto count = read(layer, operation);
            operation = FailureStage::Decode;
            require(count != 0, "XMA send/read made no progress after NeedDrain");
        }
    }
    void feed(Source& source, uint32_t index, FailureStage& operation) {
        auto& layer = layers[index];
        auto& fed = source.fed[index];
        const auto& packets = source.input[index].packets;
        for (; fed < packets.size(); ++fed) send(layer, packets[fed], operation);
        // Greedy sends above exercise actual codec backpressure. A zero read is
        // a pending bitstream, never EOF/reset; surplus remains in this ring.
        while (read(layer, operation)) {}
    }
    // Incremental decoding (Limits::prepareFrames): send this source's next packets
    // until the layer buffers `target` frames or its packets run out. Each packet is
    // drained as it is sent; the codec yields the same samples in the same order.
    void feedUntil(Source& source, uint32_t index, uint64_t target, FailureStage& operation) {
        auto& layer = layers[index];
        auto& fed = source.fed[index];
        const auto& packets = source.input[index].packets;
        while (fed < packets.size() && layer.view.bufferedFrames < target) {
            send(layer, packets[fed], operation);
            ++fed;
            while (read(layer, operation)) {}
        }
    }
    void validate(const Ticket& ticket) const {
        ready();
        require(ticket && pending && ticket.get() == pending.get(), "Stale/foreign XMA quota ticket");
        const auto& source = find(ticket->source_);
        require(oldest() == &source && source.view.status == SourceStatus::Prepared &&
                ticket->epoch_ == epoch && ticket->before_ == source.view.consumedFrames &&
                ticket->frames_ && ticket->frames_ <= source.view.declaredFrames - source.view.consumedFrames,
                "XMA quota progress changed before commit");
        require(ticket->frames_ <= maximum - committedFrames, "XMA commit frame counter exhausted");
        for (size_t index = 0; index < layers.size(); ++index) {
            const auto& layer = layers[index];
            const auto take = uint64_t(source.input[index].skipFrames) + ticket->frames_;
            require(take <= layer.view.bufferedFrames && take <= maximum - layer.view.rawHeadFrame &&
                    ticket->skips_[index] == source.input[index].skipFrames &&
                    ticket->rawStarts_[index] == layer.view.rawHeadFrame + source.input[index].skipFrames,
                    "XMA quota raw/skip cursor changed before commit");
        }
    }
};

XmaSource::XmaSource(NativeXmaFactory& factory, uint64_t generation,
                     std::span<const XmaFormat> formats, Limits limits)
    : state(std::make_unique<State>(&factory, generation, formats, limits)) {}
XmaSource::~XmaSource() { close(); }

XmaSource::XmaSource(uint64_t generation,std::shared_ptr<const FreshImage> image,std::span<const XmaFormat> formats,
                     const Segment& segment,Limits limits)
    : state(std::make_unique<State>(nullptr,generation,formats,limits)) {
    auto& s=*state;
    require(segment.continuity==Continuity::FreshContext && segment.sequence && segment.sequence!=maximum &&
            segment.declaredFrames==image->declaredFrames && segment.initialConsumedFrames==image->initialConsumedFrames &&
            segment.layers.size()==image->input.size() && formats.size()==image->formats.size() &&
            limits.maxSources==image->limits.maxSources && limits.maxTickets==image->limits.maxTickets &&
            limits.maxQuotaFrames==image->limits.maxQuotaFrames &&
            limits.maxBufferedFramesPerLayer==image->limits.maxBufferedFramesPerLayer &&
            limits.maxDeclaredFrames==image->limits.maxDeclaredFrames && limits.maxCompressedBytes==image->limits.maxCompressedBytes,
            "Shared fresh XMA admission changed its decoded input");
    for(size_t i=0;i<image->input.size();++i) {
        require(segment.layers[i].skipFrames==image->input[i].skipFrames &&
                segment.layers[i].packets==image->input[i].packets &&
                formats[i].channels==image->formats[i].channels && formats[i].sampleRate==image->formats[i].sampleRate &&
                formats[i].variant==image->formats[i].variant &&
                image->views[i].bufferedFrames<=limits.maxBufferedFramesPerLayer,
                "Shared fresh XMA packets/skip/storage differ from actual decoded input");
        s.layers[i].view=image->views[i];
    }
    auto source=std::make_unique<State::Source>();
    uint64_t bytes=0;for(const auto& layer:image->input)bytes+=layer.packets.size()*NativeXmaCodec::packetBytes;
    require(bytes<=limits.maxCompressedBytes && segment.declaredFrames<=limits.maxDeclaredFrames &&
            segment.initialConsumedFrames<segment.declaredFrames,"Shared fresh XMA source exceeds configured admission bounds");
    source->view={s.receipt(segment.sequence),segment.slot,segment.declaredFrames,
                  segment.initialConsumedFrames,SourceStatus::Prepared,bytes};
    source->input=image->input;
    source->fed.resize(source->input.size());
    for(size_t i=0;i<source->input.size();++i)source->fed[i]=source->input[i].packets.size(); // Decoded in the image.
    s.sources[0]=std::move(source);s.copiedBytes=bytes;s.lastSequence=segment.sequence;
    s.epoch=1;s.configured=true;s.sharedFreshDecode=image->origin;s.freshImage=std::move(image);
}

bool XmaSource::decodeAhead(uint32_t packets) {
    auto& s=*state;std::lock_guard lock(s.mutex);
    if(s.closed || s.failure.stage!=FailureStage::None || s.freshImage)return false;
    State::Source* next=nullptr;
    for(auto& source:s.sources)
        if(source && !source->complete() && (!next || source->view.receipt.sequence<next->view.receipt.sequence))next=source.get();
    if(!next)return false;
    FailureStage operation=FailureStage::Decode;uint32_t index=0;
    try {
        for(;index<s.layers.size();++index) {
            auto& layer=s.layers[index];auto& fed=next->fed[index];
            const auto& input=next->input[index].packets;
            for(uint32_t sent=0;sent<packets && fed<input.size();++sent,++fed) {
                s.send(layer,input[fed],operation);
                while(s.read(layer,operation)){}
            }
        }
    } catch(...) {s.fail(operation,next->view.receipt,index);throw;}
    return !next->complete();
}
bool XmaSource::decodedAllInput() const {
    const auto& s=*state;std::lock_guard lock(s.mutex);
    for(const auto& source:s.sources)if(source && !source->complete())return false;
    return true;
}
std::shared_ptr<const XmaSource::FreshImage> XmaSource::completedFreshImage(const std::vector<LayerInput>& input,Receipt origin,
                                                                          uint32_t declaredFrames,uint32_t initialConsumedFrames) const {
    const auto& s=*state;std::lock_guard lock(s.mutex);
    if(s.closed || s.failure.stage!=FailureStage::None || s.epoch!=1 || s.freshImage || input.size()!=s.layers.size())return {};
    for(size_t i=0;i<s.layers.size();++i) {
        const auto& layer=s.layers[i];
        // The ring began at frame 0 and never wrapped, so it still holds every frame.
        if(!layer.codec || layer.view.acceptedPackets!=input[i].packets.size() ||
           layer.view.decodedFrames>s.limits.maxBufferedFramesPerLayer || layer.raw.empty())return {};
    }
    auto image=std::make_shared<FreshImage>();
    image->input=input;image->origin=origin;image->limits=s.limits;image->limits.prepareFrames=0;
    image->declaredFrames=declaredFrames;image->initialConsumedFrames=initialConsumedFrames;
    uint64_t bytes=sizeof(FreshImage)+s.layers.size()*(sizeof(XmaFormat)+sizeof(LayerView)+sizeof(std::vector<float>)+sizeof(LayerInput));
    for(const auto& layer:s.layers) {
        image->formats.push_back(layer.codec->format());
        LayerView view=layer.view;view.rawHeadFrame=0;view.bufferedFrames=uint32_t(layer.view.decodedFrames);
        image->views.push_back(view);image->raw.push_back(layer.raw);
        bytes+=layer.raw.capacity()*sizeof(float);
    }
    for(const auto& layer:input)bytes+=layer.packets.size()*sizeof(Packet);
    image->memoryBytes=bytes;
    return image;
}
uint64_t XmaSource::freshImageBytes() const {
    const auto& s=*state;std::lock_guard lock(s.mutex);s.ready();
    require(s.epoch==1 && !s.committedFrames && !s.pending && !s.freshImage,
            "Fresh XMA image requires an unconsumed actual decoder result");
    const State::Source* source=nullptr;
    for(const auto& candidate:s.sources)if(candidate) {require(!source,"Fresh XMA image has multiple receipts");source=candidate.get();}
    require(source && source->view.status==SourceStatus::Prepared,"Fresh XMA image lacks a prepared source");
    uint64_t bytes=sizeof(FreshImage)+s.layers.size()*(sizeof(XmaFormat)+sizeof(LayerView)+sizeof(std::vector<float>)+sizeof(LayerInput));
    for(const auto& layer:s.layers) {
        require(layer.codec && !layer.head && !layer.view.rawHeadFrame,"Fresh XMA image has mutable continuation/output progress");
        bytes+=layer.raw.capacity()*sizeof(float);
    }
    for(const auto& layer:source->input)bytes+=layer.packets.size()*sizeof(Packet);
    return bytes;
}

std::shared_ptr<const XmaSource::FreshImage> XmaSource::shareFreshImage() {
    const auto bytes=freshImageBytes();
    auto& s=*state;std::lock_guard lock(s.mutex);s.ready();
    // Cache publication is private to an unexposed prepared owner. Allocate
    // all image metadata/packet copies before moving any real decoded backing.
    require(s.epoch==1 && !s.committedFrames && !s.pending && !s.freshImage,
            "Fresh XMA image output changed before publication");
    const State::Source* source=nullptr;
    for(const auto& candidate:s.sources)if(candidate) {require(!source,"Fresh XMA image has multiple receipts");source=candidate.get();}
    require(source && source->view.status==SourceStatus::Prepared,"Fresh XMA image lost its prepared source");
    auto image=std::make_shared<FreshImage>();
    image->raw.resize(s.layers.size());image->formats.reserve(s.layers.size());image->views.reserve(s.layers.size());
    image->input=source->input;image->origin=source->view.receipt;image->limits=s.limits;
    image->declaredFrames=source->view.declaredFrames;image->initialConsumedFrames=source->view.consumedFrames;
    image->memoryBytes=bytes;
    for(const auto& layer:s.layers) {
        require(layer.codec && !layer.head && !layer.view.rawHeadFrame,"Fresh XMA image cursor changed before publication");
        image->formats.push_back(layer.codec->format());image->views.push_back(layer.view);
    }
    for(size_t i=0;i<s.layers.size();++i)image->raw[i]=std::move(s.layers[i].raw);
    s.sharedFreshDecode=image->origin;s.freshImage=image;
    for(auto& layer:s.layers) {
        if(s.pool)s.pool->give(std::move(layer.codec)); // Fully decoded; the image no longer needs it.
        layer.codec.reset();
    }
    return image;
}

XmaSource::Preparation XmaSource::prepare(const Segment& segment) {
    auto& s = *state;
    std::lock_guard lock(s.mutex);
    s.ready();
    require(!s.freshImage,"An immutable fresh XMA owner cannot accept compressed continuation");
    require(segment.sequence && segment.sequence != maximum && segment.sequence > s.lastSequence,
            "XMA source sequence must increase without wrapping");
    require(segment.continuity == Continuity::FreshContext || segment.continuity == Continuity::ContinueContext,
            "Invalid XMA source continuity");
    require(segment.layers.size() == s.layers.size() && segment.declaredFrames &&
            segment.declaredFrames <= s.limits.maxDeclaredFrames && segment.initialConsumedFrames < segment.declaredFrames,
            "Invalid XMA source layers/declared progress");
    require(s.configured || segment.continuity == Continuity::FreshContext, "First XMA source must explicitly be fresh");
    uint64_t bytes = 0;
    for (const auto& layer : segment.layers) {
        require(layer.skipFrames < s.limits.maxBufferedFramesPerLayer, "XMA explicit skip leaves no quota storage");
        require(layer.packets.size() <= (s.limits.maxCompressedBytes - bytes) / NativeXmaCodec::packetBytes,
                "XMA compressed source exceeds configured limit");
        bytes += layer.packets.size() * NativeXmaCodec::packetBytes;
    }
    auto empty = s.sources.end();
    for (auto it = s.sources.begin(); it != s.sources.end(); ++it) {
        if (!*it) { if (empty == s.sources.end()) empty = it; continue; }
        require((*it)->view.slot != segment.slot, "XMA caller slot is still owned");
        require(segment.continuity != Continuity::FreshContext, "Fresh XMA context requires all prior receipts retired");
    }
    if (empty == s.sources.end() || bytes > s.limits.maxCompressedBytes - s.copiedBytes)
        return {PrepareStatus::Backpressure, {}};
    require(segment.continuity != Continuity::FreshContext || s.epoch != maximum, "XMA context epochs exhausted");

    // All source storage allocations/copies precede codec mutation/admission.
    auto source = std::make_unique<State::Source>();
    source->view = {s.receipt(segment.sequence), segment.slot, segment.declaredFrames,
                    segment.initialConsumedFrames, SourceStatus::Prepared, bytes};
    source->input = segment.layers;
    source->fed.assign(segment.layers.size(), 0);
    auto* admitted = source.get();
    *empty = std::move(source);
    s.copiedBytes += bytes;
    s.lastSequence = segment.sequence;
    FailureStage operation = FailureStage::Reset;
    uint32_t index = 0;
    try {
        if (segment.continuity == Continuity::FreshContext) {
            for (auto& layer : s.layers) {
                operation = FailureStage::Counter;
                increment(layer.view.discardedOnFresh, layer.view.bufferedFrames);
                operation = FailureStage::Reset;
                if (s.configured) layer.codec->reset();
                layer.view.bufferedFrames = 0;
                layer.view.rawHeadFrame = 0;
                layer.head = 0;
                ++index;
            }
            ++s.epoch;
            s.configured = true;
        }
        // Codec order follows source order: an earlier source decoding incrementally
        // finishes all of its packets before this segment's packets are sent.
        for (auto& earlier : s.sources)
            if (earlier && earlier.get() != admitted && !earlier->complete())
                for (index = 0; index < s.layers.size(); ++index) s.feed(*earlier, index, operation);
        for (index = 0; index < s.layers.size(); ++index) {
            if (s.limits.prepareFrames)
                s.feedUntil(*admitted, index, uint64_t(admitted->input[index].skipFrames) + s.limits.prepareFrames, operation);
            else s.feed(*admitted, index, operation);
        }
    } catch (...) {
        s.fail(operation, admitted->view.receipt, index);
        throw;
    }
    return {PrepareStatus::Accepted, admitted->view.receipt};
}

XmaSource::Staging XmaSource::stage(Receipt receipt, uint32_t frames) {
    auto& s = *state;
    std::lock_guard lock(s.mutex);
    s.ready();
    auto& source = s.find(receipt);
    require(s.oldest() == &source && source.view.status == SourceStatus::Prepared,
            "XMA quotas must follow source order");
    require(frames && frames <= s.limits.maxQuotaFrames && frames <= source.view.declaredFrames - source.view.consumedFrames,
            "Invalid XMA quota extent");
    if (s.pending) {
        require(s.pending->source_ == receipt && s.pending->frames_ == frames, "Another XMA quota is pending");
        return {StageStatus::Complete, s.pending};
    }
    bool needsInput = false;
    for (size_t index = 0; index < s.layers.size(); ++index) {
        const auto take = uint64_t(source.input[index].skipFrames) + frames;
        require(take <= s.limits.maxBufferedFramesPerLayer, "XMA skip plus quota exceeds configured storage");
        require(take <= maximum - s.layers[index].view.rawHeadFrame, "XMA raw cursor exhausted");
        if (take > s.layers[index].view.bufferedFrames && !s.freshImage && !source.complete()) {
            FailureStage operation = FailureStage::Decode;
            try { s.feedUntil(source, uint32_t(index), take, operation); }
            catch (...) { s.fail(operation, receipt, uint32_t(index)); throw; }
        }
        if (take > s.layers[index].view.bufferedFrames) needsInput = true;
    }
    if (needsInput) return {StageStatus::NeedInput, {}};
    auto slot = std::find_if(s.tickets.begin(), s.tickets.end(), [](const auto& value) { return value.expired(); });
    if (slot == s.tickets.end()) return {StageStatus::Backpressure, {}};
    require(s.nextTicket != maximum, "XMA quota ticket identities exhausted");
    auto ticket = std::shared_ptr<Quota>(new Quota);
    ticket->source_ = receipt;
    ticket->identity_ = s.nextTicket;
    ticket->epoch_ = s.epoch;
    ticket->before_ = source.view.consumedFrames;
    ticket->frames_ = frames;
    ticket->channels_ = s.channels;
    ticket->layers_ = uint32_t(s.layers.size());
    ticket->samples_.resize(size_t(frames) * s.channels);
    uint32_t component = 0;
    for (size_t index = 0; index < s.layers.size(); ++index) {
        const auto& layer = s.layers[index];
        const auto skip = source.input[index].skipFrames;
        ticket->skips_[index] = skip;
        ticket->rawStarts_[index] = layer.view.rawHeadFrame + skip;
        for (uint32_t channel = 0; channel < layer.view.channels; ++channel, ++component)
            for (uint32_t frame = 0; frame < frames; ++frame) {
                const auto at = (layer.head + skip + frame) % s.limits.maxBufferedFramesPerLayer;
                const auto& raw=s.freshImage?s.freshImage->raw[index]:layer.raw;
                ticket->samples_[size_t(component) * frames + frame] = raw[size_t(at) * layer.view.channels + channel];
            }
    }
    *slot = ticket;
    s.pending = ticket;
    ++s.nextTicket;
    return {StageStatus::Complete, std::move(ticket)};
}

void XmaSource::validateCommit(const Ticket& ticket) const {
    const auto& s = *state;
    std::lock_guard lock(s.mutex);
    s.validate(ticket);
}
void XmaSource::commit(const Ticket& ticket) {
    auto& s = *state;
    std::lock_guard lock(s.mutex);
    s.validate(ticket);
    auto& source = s.find(ticket->source_);
    for (size_t index = 0; index < s.layers.size(); ++index) {
        auto& layer = s.layers[index];
        const auto count = source.input[index].skipFrames + ticket->frames_;
        layer.head = (layer.head + count) % s.limits.maxBufferedFramesPerLayer;
        layer.view.bufferedFrames -= count;
        layer.view.rawHeadFrame += count;
        source.input[index].skipFrames = 0;
    }
    source.view.consumedFrames += ticket->frames_;
    s.committedFrames += ticket->frames_;
    if (source.view.consumedFrames == source.view.declaredFrames) source.view.status = SourceStatus::Delivered;
    s.pending.reset();
}
XmaSource::SourceView XmaSource::query(Receipt receipt) const {
    const auto& s = *state;
    std::lock_guard lock(s.mutex);
    return s.find(receipt).view;
}
void XmaSource::retire(Receipt receipt) {
    auto& s = *state;
    std::lock_guard lock(s.mutex);
    auto& source = s.find(receipt);
    require(source.view.status != SourceStatus::Prepared, "Cannot retire unconsumed XMA source");
    if (source.view.status == SourceStatus::Delivered && !source.complete() && !s.freshImage && !s.closed &&
        s.failure.stage == FailureStage::None) {
        // A continuation segment needs the codec to have seen every earlier packet.
        FailureStage operation = FailureStage::Decode;
        uint32_t index = 0;
        try { for (; index < s.layers.size(); ++index) s.feed(source, index, operation); }
        catch (...) { s.fail(operation, receipt, index); throw; }
    }
    s.copiedBytes -= source.view.copiedBytes;
    for (auto& value : s.sources)
        if (value.get() == &source) { value.reset(); break; }
}
XmaSource::Snapshot XmaSource::snapshot() const {
    const auto& s = *state;
    std::lock_guard lock(s.mutex);
    Snapshot result;
    result.owner = s.owner;
    result.instanceGeneration = s.generation;
    result.epoch = s.epoch;
    result.copiedBytes = s.copiedBytes;
    result.committedFrames = s.committedFrames;
    result.pendingTicket = s.pending ? s.pending->identity_ : 0;
    result.closed = s.closed;
    result.configured = s.configured;
    result.failure = s.failure;
    for (const auto& ticket : s.tickets) if (!ticket.expired()) ++result.ticketsAlive;
    for (const auto& source : s.sources) if (source) result.sources.push_back(source->view);
    std::sort(result.sources.begin(), result.sources.end(), [](const auto& a, const auto& b) {
        return a.receipt.sequence < b.receipt.sequence;
    });
    for (const auto& layer : s.layers) result.layers.push_back(layer.view);
    result.sharedFreshDecode=s.sharedFreshDecode;
    return result;
}
void XmaSource::close() {
    auto& s = *state;
    std::lock_guard lock(s.mutex);
    if (s.closed) return;
    s.closed = true;
    s.pending.reset();
    for (auto& source : s.sources) if (source) {
        if (source->view.status == SourceStatus::Prepared) source->view.status = SourceStatus::Cancelled;
        source->view.copiedBytes = 0;
        std::vector<LayerInput>().swap(source->input);
    }
    s.copiedBytes = 0;
    for (auto& layer : s.layers) {
        if (s.pool) {
            s.pool->give(std::move(layer.codec));
            s.pool->giveRing(std::move(layer.raw));
        }
        layer.codec.reset();
        std::vector<float>().swap(layer.raw);
        layer.view.bufferedFrames = 0;
        layer.head = 0;
    }
    s.freshImage.reset();
}
std::span<const float> XmaSource::Quota::plane(uint32_t component) const {
    require(component < channels_, "XMA quota component outside extent");
    return {samples_.data() + size_t(component) * frames_, frames_};
}
uint64_t XmaSource::Quota::rawStartFrame(uint32_t layer) const {
    require(layer < layers_, "XMA quota layer outside extent");
    return rawStarts_[layer];
}
uint32_t XmaSource::Quota::skippedFrames(uint32_t layer) const {
    require(layer < layers_, "XMA quota layer outside extent");
    return skips_[layer];
}
}
