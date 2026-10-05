#include "audio/audio_catalog.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <vector>

using namespace Simpsons::Audio;
namespace {
void need(bool ok,const char* why){if(!ok)throw XmaSourceError(why);}
std::vector<uint8_t> read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    need(bool(file),"Fixture catalog open failed");
    const auto size=file.tellg();need(size>=76 && size<1024*1024,"Fixture catalog extent changed");
    std::vector<uint8_t> bytes(size_t(size),uint8_t{});
    file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),size);
    need(bool(file),"Fixture catalog read failed");return bytes;
}
uint32_t u32(std::span<const uint8_t> bytes,uint64_t at) {
    need(at<=bytes.size() && 4<=bytes.size()-at,"Fixture table word truncated");
    return uint32_t(bytes[size_t(at)])|uint32_t(bytes[size_t(at+1)])<<8|
           uint32_t(bytes[size_t(at+2)])<<16|uint32_t(bytes[size_t(at+3)])<<24;
}
uint64_t u64(std::span<const uint8_t> bytes,uint64_t at) {
    return uint64_t(u32(bytes,at))|uint64_t(u32(bytes,at+4))<<32;
}
std::string hex(std::span<const uint8_t> bytes) {
    std::string result;for(auto b:bytes){result+="0123456789abcdef"[b>>4];result+="0123456789abcdef"[b&15];}return result;
}
struct Sha {
    BCRYPT_HASH_HANDLE value{};
    Sha(){need(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,&value,nullptr,0,nullptr,0,0)>=0,"Hash create failed");}
    ~Sha(){if(value)BCryptDestroyHash(value);}
    void add(std::span<const uint8_t> bytes){need(bytes.size()<=ULONG_MAX && BCryptHashData(value,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),0)>=0,"Hash update failed");}
    std::array<uint8_t,32> finish(){std::array<uint8_t,32> output{};need(BCryptFinishHash(value,output.data(),ULONG(output.size()),0)>=0,"Hash finish failed");return output;}
};
std::array<uint8_t,32> digest(std::span<const uint8_t> bytes){Sha hash;hash.add(bytes);return hash.finish();}
std::string clean(std::string text) {
    std::string output;
    for(unsigned char ch:text) {
        if(ch=='"')output+="\\\"";
        else if(ch=='\\')output+="\\\\";
        else if(ch<32){output+="\\u00";output+="0123456789abcdef"[ch>>4];output+="0123456789abcdef"[ch&15];}
        else output+=char(ch);
    }
    return output;
}
struct CaseLog {
    std::ofstream file;
    uint32_t passed{},failed{};
    explicit CaseLog(const std::filesystem::path& base) {
        const auto name="native-cases-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+".jsonl";
        const auto path=base/name;need(!std::filesystem::exists(path),"Preserve prior native extent receipt");
        file.open(path,std::ios::binary);need(bool(file),"Native case log open failed");
        std::printf("EXTENT_CASE_LOG %s\n",path.string().c_str());
    }
    template<class Function> void run(const char* name,const std::filesystem::path& catalog,
                                      const char* classification,Function function) {
        const auto raw=read(catalog);const auto hash=hex(digest(raw));
        const uint64_t sourceAt=u64(raw,28),streamAt=u64(raw,36);
        file<<"{\"phase\":\"before_validation\",\"case\":\""<<name<<"\",\"asset\":\""<<clean(catalog.generic_string())
            <<"\",\"catalog_sha256\":\""<<hash<<"\",\"catalog_bytes\":"<<raw.size()
            <<",\"declared_source_kind\":"<<u32(raw,sourceAt+20)<<",\"declared_source_count\":"<<u32(raw,12)
            <<",\"declared_stream_count\":"<<u32(raw,16)<<",\"declared_block_count\":"<<u32(raw,20)<<",\"declared_layer_count\":"<<u32(raw,24)
            <<",\"declared_channels\":"<<u32(raw,streamAt+44)<<",\"declared_sample_rate\":"<<u32(raw,streamAt+40)
            <<",\"declared_frames\":"<<u32(raw,streamAt+48)<<",\"declared_flags\":"<<u32(raw,streamAt+60)
            <<",\"declared_source_bytes\":"<<u64(raw,sourceAt+24)<<",\"declared_header_offset\":"<<u64(raw,streamAt+8)
            <<",\"declared_audio_offset\":"<<u64(raw,streamAt+16)<<",\"declared_audio_bytes\":"<<u64(raw,streamAt+24)
            <<",\"caller\":\"AudioCatalogExtentTests\",\"mission\":\"offline_dialogue_fixture\",\"last_action\":\"fresh_catalog_load\",\"ownership\":\"unloaded\",\"classification\":\""
            <<classification<<"\",\"scope\":\"native_catalog_decoder_only\"}\n";file.flush();need(bool(file),"Prevalidation audit write failed before admission");
        try {function();++passed;file<<"{\"phase\":\"result\",\"case\":\""<<name<<"\",\"status\":\"passed\"}\n";}
        catch(const std::exception& error){++failed;file<<"{\"phase\":\"result\",\"case\":\""<<name<<"\",\"status\":\"failed\",\"reason\":\""<<clean(error.what())<<"\"}\n";}
        file.flush();need(bool(file),"Native case log write failed");
    }
};
struct Result {std::vector<float> pcm;uint32_t blocks{},retired{};uint64_t frames{};};
Result use(const std::filesystem::path& base,const char* label,CaseLog& log) {
    const auto directory=base/label;
    const auto raw=read(directory/"catalog.bin");
    const uint64_t sourceAt=u64(raw,28),streamAt=u64(raw,36),blockAt=u64(raw,44);
    need(u32(raw,12)==1 && u32(raw,16)==1 && u32(raw,20)==14 && u32(raw,24)==14,
         "Generated fixture inventory changed");
    const uint64_t fileBytes=u64(raw,sourceAt+24),audioOffset=u64(raw,streamAt+16),audioBytes=u64(raw,streamAt+24);
    need(fileBytes==audioOffset+21056 && audioBytes==21056 && u64(raw,streamAt+8)==16,
         "Generated original source range changed");
    std::array<uint8_t,32> sourceHash{};std::copy_n(raw.begin()+size_t(sourceAt+32),32,sourceHash.begin());
    // Capture producer parameters and the actual opened file identity before
    // the catalog admission that formerly rejected the relocated source.
    const auto sourcePath=directory/"source/audiostreams/dialogue.exa.snu";
    std::ifstream source(sourcePath,std::ios::binary);
    need(bool(source),"Generated SNU open failed");
    Sha sourceDigest;std::array<uint8_t,65536> buffer{};uint64_t actualSize=0;
    while(source.read(reinterpret_cast<char*>(buffer.data()),buffer.size()) || source.gcount()) {
        const auto count=size_t(source.gcount());sourceDigest.add(std::span(buffer).first(count));actualSize+=count;
    }
    need(source.eof() && actualSize==fileBytes && sourceDigest.finish()==sourceHash,
         "Actual SNU extent/full-file digest differs from generator certificate");
    log.file<<"{\"phase\":\"source_before_admission\",\"asset\":\""<<clean(sourcePath.generic_string())
        <<"\",\"label\":\""<<label<<"\",\"source_sha256\":\""<<hex(sourceHash)
        <<"\",\"source_bytes\":"<<fileBytes<<",\"audio_offset\":"<<audioOffset
        <<",\"audio_bytes\":"<<audioBytes<<",\"header_offset\":16,\"blocks\":14,\"frames\":67328,\"channels\":1,\"sample_rate\":48000,\"last_action\":\"actual_source_hash_complete\",\"ownership\":\"source_open_no_catalog_or_decoder\"}\n";log.file.flush();need(bool(log.file),"Source audit write failed before continuing");
    source.clear();source.seekg(16);std::array<uint8_t,8> header{};
    source.read(reinterpret_cast<char*>(header.data()),header.size());need(bool(source),"SNU header read failed");
    need(std::equal(header.begin(),header.end(),raw.begin()+size_t(streamAt+64)),"Actual SNU header differs from generated certificate");
    const auto catalog=AudioCatalog::load(directory/"catalog.bin");
    const XmaFormat format{1,48000,XmaVariant::Xma2};NativeXmaFactory factory;
    XmaSource owner(factory,1,std::span(&format,1),{});
    std::vector<uint32_t> candidates;Result result;XmaSource::Ticket retained;
    std::vector<float> retainedPcm;XmaSource::Segment closedProbe;
    uint32_t staleQueryRejects=0,staleStageRejects=0;
    for(uint32_t i=0;i<14;++i) {
        const uint64_t row=blockAt+uint64_t(i)*100,offset=u64(raw,row+8);
        const uint32_t bytes=u32(raw,row+16);
        need(offset<=fileBytes && bytes<=fileBytes-offset,"Generated block escapes verified source");
        source.clear();source.seekg(std::streamoff(offset));need(bool(source),"Original/relocated block seek failed");
        std::vector<uint8_t> owned(bytes);source.read(reinterpret_cast<char*>(owned.data()),bytes);
        need(bool(source),"Original/relocated block read failed");owned[0]&=0x7f;
        auto matched=catalog.match(header,candidates,uint64_t(i)+1,owned);
        need(matched.candidates==std::vector<uint32_t>{0},"Isolated generated source candidate changed");
        auto parsed=parseCatalogEaXmaBlock(owned,1,matched.block);
        XmaSource::Segment segment;segment.sequence=uint64_t(i)+1;segment.slot=0;
        segment.declaredFrames=matched.block.frames;
        segment.continuity=i?XmaSource::Continuity::ContinueContext:XmaSource::Continuity::FreshContext;
        segment.layers=std::move(parsed.layers);if(!i)segment.layers[0].skipFrames=384;
        if(!i){closedProbe=segment;closedProbe.sequence=15;}
        const auto accepted=owner.prepare(segment);need(accepted.status==XmaSource::PrepareStatus::Accepted,"Original source block prepare failed");
        for(auto& layer:segment.layers)for(auto& packet:layer.packets)packet.fill(0xa5);
        segment.layers.clear();
        uint32_t delivered=0;
        while(delivered<matched.block.frames) {
            const auto quota=std::min(1024u,matched.block.frames-delivered);
            const auto staged=owner.stage(accepted.receipt,quota);
            need(staged.status==XmaSource::StageStatus::Complete && staged.ticket && staged.ticket->frames()==quota,
                 "Original source block did not produce its complete declared quota");
            const auto plane=staged.ticket->plane(0);result.pcm.insert(result.pcm.end(),plane.begin(),plane.end());
            if(!retained){retained=staged.ticket;retainedPcm.assign(plane.begin(),plane.end());}
            owner.validateCommit(staged.ticket);owner.commit(staged.ticket);delivered+=quota;
        }
        need(owner.query(accepted.receipt).status==XmaSource::SourceStatus::Delivered,"Source receipt not delivered before retirement");
        owner.retire(accepted.receipt);++result.retired;
        need(owner.snapshot().copiedBytes==0 && owner.snapshot().sources.empty(),"Retired source receipt retains input ownership");
        try{(void)owner.query(accepted.receipt);}catch(const XmaSourceError& error){need(std::string(error.what())=="Stale XMA source receipt","Retired query hit a different guard");++staleQueryRejects;}
        try{(void)owner.stage(accepted.receipt,1);}catch(const XmaSourceError& error){need(std::string(error.what())=="Stale XMA source receipt","Retired stage hit a different guard");++staleStageRejects;}
        need(staleQueryRejects==i+1 && staleStageRejects==i+1,"Retired source receipt remains usable");
        candidates=std::move(matched.candidates);result.frames+=delivered;++result.blocks;
    }
    need(catalog.complete(candidates,14) && result.frames==67328 && result.retired==14,
         "Generated full dialogue source did not complete every block/receipt");
    owner.close();owner.close();const auto closed=owner.snapshot();
    need(closed.closed && closed.sources.empty() && closed.copiedBytes==0 && closed.layers[0].bufferedFrames==0 &&
         closed.committedFrames==67328 && !closed.pendingTicket,
         "Decoder close did not release full original source lifetime");
    const auto retainedPlane=retained->plane(0);
    need(retainedPlane.size()==retainedPcm.size() && std::memcmp(retainedPlane.data(),retainedPcm.data(),retainedPcm.size()*sizeof(float))==0,
         "Owned immutable quota changed after decoder close");
    bool closedReject=false;try{owner.prepare(closedProbe);}catch(const XmaSourceError& error){need(std::string(error.what())=="XMA source is closed","Closed-owner rejection hit an independently invalid-input guard");closedReject=true;}
    const auto afterClosedReject=owner.snapshot();
    need(closedReject && afterClosedReject.owner==closed.owner && afterClosedReject.instanceGeneration==closed.instanceGeneration &&
         afterClosedReject.epoch==closed.epoch && afterClosedReject.closed && afterClosedReject.sources.empty() &&
         afterClosedReject.copiedBytes==0 && afterClosedReject.committedFrames==closed.committedFrames &&
         afterClosedReject.pendingTicket==closed.pendingTicket && afterClosedReject.ticketsAlive==closed.ticketsAlive &&
         afterClosedReject.layers[0].bufferedFrames==0 && afterClosedReject.layers[0].acceptedPackets==closed.layers[0].acceptedPackets &&
         afterClosedReject.layers[0].decodedFrames==closed.layers[0].decodedFrames,
         "Closed-owner rejection changed completed source state");
    source.close();need(!source.is_open(),"Source file remained open after complete use");
    const auto pcmHash=hex(digest(std::span(reinterpret_cast<const uint8_t*>(result.pcm.data()),result.pcm.size()*sizeof(float))));
    log.file<<"{\"phase\":\"lifetime_complete\",\"label\":\""<<label
        <<"\",\"blocks\":"<<result.blocks<<",\"retired\":"<<result.retired<<",\"frames\":"<<result.frames
        <<",\"stale_query_rejections\":"<<staleQueryRejects<<",\"stale_stage_rejections\":"<<staleStageRejects
        <<",\"closed_valid_input_rejected\":"<<(closedReject?"true":"false")
        <<",\"pcm_sha256\":\""<<pcmHash<<"\",\"source_file_closed\":true,\"decoder_closed\":true,\"source_copies\":0,\"buffered_frames\":0,\"scope\":\"native_catalog_decoder_not_guest_reader_or_device\"}\n";log.file.flush();need(bool(log.file),"Source audit write failed before continuing");
    return result;
}
}
int main(int argc,char** argv) {
    try {
        need(argc==2,"Expected isolated extent fixture directory");const auto base=std::filesystem::path(argv[1]);CaseLog log(base);
        Result low,high;bool lowReady=false,highReady=false;
        log.run("original_complete",base/"original/catalog.bin","actual_generator_original_payload",[&]{low=use(base,"original",log);lowReady=true;});
        log.run("relocated_complete",base/"relocated/catalog.bin","actual_generator_relocated_original_payload",[&]{
            high=use(base,"relocated",log);need(lowReady && high.pcm.size()==low.pcm.size() &&
                std::memcmp(high.pcm.data(),low.pcm.data(),low.pcm.size()*sizeof(float))==0,"Relocated full PCM differs from original");highReady=true;
        });
        log.run("relocated_reopen",base/"relocated/catalog.bin","actual_generator_relocated_original_payload",[&]{
            const auto replay=use(base,"relocated",log);need(highReady && replay.pcm.size()==high.pcm.size() &&
                std::memcmp(replay.pcm.data(),high.pcm.data(),high.pcm.size()*sizeof(float))==0,"Reopened relocated full PCM differs");
        });
        struct Negative{const char* name;const char* diagnostic;};
        for(const auto& item:std::array<Negative,5>{{
            {"zero_source_extent","Audio catalog source span/identity is invalid"},
            {"source_shorter_than_audio","Audio catalog stream header/span is invalid"},
            {"audio_offset_wrap","Audio catalog stream header/span is invalid"},
            {"header_outside_source","Audio catalog stream header/span is invalid"},
            {"source_kind_invalid","Audio catalog source span/identity is invalid"}}}) {
            const auto path=base/"negative"/(std::string(item.name)+".bin");
            log.run(item.name,path,"structural_corruption",[&]{
                bool rejected=false;try{const auto bad=AudioCatalog::load(path);(void)bad;}
                catch(const XmaSourceError& error){need(std::string(error.what())==item.diagnostic,"Structural corruption rejected by a different earlier guard");rejected=true;}
                need(rejected,"Structurally corrupt catalog admitted");
            });
        }
        std::printf("AUDIT_AUDIO_CATALOG_EXTENT cases_passed=%u cases_failed=%u scope=native_catalog_decoder_lifetime_only\n",log.passed,log.failed);
        need(log.failed==0 && log.passed==8,"Independent source extent regressions failed after all cases ran");return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL audio catalog extent: %s\n",error.what());return 1;}
}
