#include "native_xma_codec.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <xmmintrin.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
}

namespace Simpsons::Audio {
namespace {
// Decoding runs with private host FP controls. Hardware audio would not alter
// the guest CPU's floating-point environment; restore its host state afterward.
struct CodecFloatingPoint {
    const uint32_t previous=_mm_getcsr();
    CodecFloatingPoint(){_mm_setcsr((previous|0x1f80u)&~0xe040u);}
    ~CodecFloatingPoint(){_mm_setcsr(previous);}
};
std::string diagnostic(const char* operation,int status) {
    char reason[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(status,reason,sizeof(reason));
    return std::string(operation)+": "+reason+" ("+std::to_string(status)+")";
}
struct Packet {
    AVPacket* value=av_packet_alloc();
    Packet(){if(!value) throw CodecError("Native XMA packet allocation failed");}
    ~Packet(){av_packet_free(&value);}
};
}
struct NativeXmaCodec::State {
    const XmaFormat format;
    std::mutex mutex;
    AVCodecContext* codec{};
    AVFrame* frame{};
    uint32_t offset{};
    bool failed=false;
    explicit State(XmaFormat value):format(value) {}
    ~State(){CodecFloatingPoint floatingPoint;av_frame_free(&frame);avcodec_free_context(&codec);}
    [[noreturn]] void fail(const char* operation,int status) {
        failed=true;throw CodecError(diagnostic(operation,status));
    }
    void ready() const {if(failed) throw CodecError("Native XMA decoder failed; an explicit reset is required");}
};
struct NativeXmaFactory::State {
    AVCodecContext* capabilities[2]{};
    ~State(){CodecFloatingPoint floatingPoint;for(auto& value:capabilities) avcodec_free_context(&value);}
};
NativeXmaFactory::NativeXmaFactory():state(std::make_unique<State>()) {
    CodecFloatingPoint floatingPoint;
    if(avcodec_version()!=LIBAVCODEC_VERSION_INT || avutil_version()!=LIBAVUTIL_VERSION_INT)
        throw CodecError("Native XMA factory runtime/header version mismatch");
    unsigned index=0;
    for(auto id:{AV_CODEC_ID_XMA1,AV_CODEC_ID_XMA2}) {
        const auto* codec=avcodec_find_decoder(id);
        if(!codec) throw CodecError("Native XMA factory decoder unavailable");
        auto*& capability=state->capabilities[index++];capability=avcodec_alloc_context3(codec);
        if(!capability) throw CodecError("Native XMA factory context allocation failed");
        const int status=av_opt_set_int(capability->priv_data,"simpsons_raw_frames",1,0);
        if(status<0) throw CodecError(diagnostic("Native XMA factory raw-frame option unavailable",status));
    }
}
NativeXmaFactory::~NativeXmaFactory()=default;
std::unique_ptr<NativeXmaCodec> NativeXmaFactory::create(XmaFormat format) const {
    return std::make_unique<NativeXmaCodec>(format);
}
NativeXmaCodec::NativeXmaCodec(XmaFormat format):state(std::make_unique<State>(format)) {
    if((format.channels!=1 && format.channels!=2) ||
       (format.sampleRate!=24000 && format.sampleRate!=32000 && format.sampleRate!=44100 && format.sampleRate!=48000) ||
       (format.variant!=XmaVariant::Xma1 && format.variant!=XmaVariant::Xma2))
        throw CodecError("Unsupported native XMA layer format");
    auto& s=*state;CodecFloatingPoint floatingPoint;
    // Header/runtime mismatch must not be hidden by an apparently available
    // decoder. CMake also verifies the owned dependency's provenance/hashes.
    if(avcodec_version()!=LIBAVCODEC_VERSION_INT || avutil_version()!=LIBAVUTIL_VERSION_INT)
        throw CodecError("Native XMA codec runtime/header version mismatch");
    const auto* decoder=avcodec_find_decoder(format.variant==XmaVariant::Xma1?AV_CODEC_ID_XMA1:AV_CODEC_ID_XMA2);
    if(!decoder) throw CodecError("Required native XMA decoder is unavailable");
    s.codec=avcodec_alloc_context3(decoder);s.frame=av_frame_alloc();
    if(!s.codec || !s.frame) throw CodecError("Native XMA decoder/frame allocation failed");
    s.codec->sample_rate=int(format.sampleRate);s.codec->block_align=packetBytes;s.codec->thread_count=1;
    av_channel_layout_default(&s.codec->ch_layout,int(format.channels));
    const int bytes=format.variant==XmaVariant::Xma1?28:34;
    s.codec->extradata=static_cast<uint8_t*>(av_mallocz(bytes+AV_INPUT_BUFFER_PADDING_SIZE));
    if(!s.codec->extradata) throw CodecError("Native XMA format allocation failed");
    s.codec->extradata_size=bytes;
    if(format.variant==XmaVariant::Xma1) {
        s.codec->extradata[4]=1; // One independent mono/stereo stream.
        s.codec->extradata[25]=uint8_t(format.channels);
    } else {
        s.codec->extradata[0]=1; // XMA2 NumStreams, little endian.
        s.codec->extradata[2]=format.channels==1?4:3; // Codec layer layout, not an engine speaker assignment.
        // No encoded/play/loop extent is supplied: the engine owns those trims.
    }
    int result=av_opt_set_int(s.codec->priv_data,"simpsons_raw_frames",1,0);
    if(result<0) s.fail("Native XMA raw-frame option is unavailable",result);
    result=avcodec_open2(s.codec,decoder,nullptr);
    if(result<0) s.fail("Native XMA decoder open failed",result);
    if(s.codec->sample_fmt!=AV_SAMPLE_FMT_FLTP || s.codec->sample_rate!=int(format.sampleRate) ||
       s.codec->ch_layout.nb_channels!=int(format.channels))
        throw CodecError("Native XMA decoder changed its declared layer format");
}
NativeXmaCodec::~NativeXmaCodec()=default;
XmaFormat NativeXmaCodec::format() const {return state->format;}
PacketResult NativeXmaCodec::send(std::span<const uint8_t> input) {
    if(input.size()!=packetBytes) throw CodecError("Native XMA submission requires one complete 2048-byte packet");
    auto& s=*state;std::lock_guard lock(s.mutex);s.ready();CodecFloatingPoint floatingPoint;
    Packet packet;int result=av_new_packet(packet.value,packetBytes);
    if(result<0) throw CodecError(diagnostic("Native XMA owned packet allocation failed",result));
    std::memcpy(packet.value->data,input.data(),packetBytes);
    result=avcodec_send_packet(s.codec,packet.value);
    if(result==AVERROR(EAGAIN)) return PacketResult::NeedDrain;
    if(result<0) s.fail("Native XMA packet decode failed",result);
    return PacketResult::Accepted;
}
uint32_t NativeXmaCodec::read(std::span<float> destination) {
    auto& s=*state;std::lock_guard lock(s.mutex);s.ready();
    if(destination.size()%s.format.channels) throw CodecError("Native XMA destination must contain complete channel frames");
    if(destination.empty()) return 0;
    CodecFloatingPoint floatingPoint;
    if(s.offset==uint32_t(s.frame->nb_samples)) {
        av_frame_unref(s.frame);s.offset=0;
        const int result=avcodec_receive_frame(s.codec,s.frame);
        if(result==AVERROR(EAGAIN)) return 0;
        if(result<0) s.fail("Native XMA frame decode failed",result);
        if(s.frame->nb_samples!=frameSamples || s.frame->format!=AV_SAMPLE_FMT_FLTP ||
           s.frame->ch_layout.nb_channels!=int(s.format.channels) || s.frame->sample_rate!=int(s.format.sampleRate))
            s.fail("Native XMA output frame violates the configured raw contract",AVERROR_INVALIDDATA);
        for(uint32_t channel=0;channel<s.format.channels;++channel) {
            if(!s.frame->extended_data[channel]) s.fail("Native XMA output plane is absent",AVERROR_INVALIDDATA);
            const auto* plane=reinterpret_cast<const float*>(s.frame->extended_data[channel]);
            for(uint32_t index=0;index<frameSamples;++index)
                if(!std::isfinite(plane[index])) s.fail("Native XMA output contains a non-finite sample",AVERROR_INVALIDDATA);
        }
    }
    const uint32_t take=uint32_t(std::min<size_t>(destination.size()/s.format.channels,frameSamples-s.offset));
    for(uint32_t index=0;index<take;++index)
        for(uint32_t channel=0;channel<s.format.channels;++channel)
            destination[index*s.format.channels+channel]=reinterpret_cast<const float*>(s.frame->extended_data[channel])[s.offset+index];
    s.offset+=take;return take;
}
void NativeXmaCodec::reset() {
    auto& s=*state;std::lock_guard lock(s.mutex);CodecFloatingPoint floatingPoint;
    avcodec_flush_buffers(s.codec);av_frame_unref(s.frame);s.offset=0;s.failed=false;
}
}
