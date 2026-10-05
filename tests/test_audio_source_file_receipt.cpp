// The `audio-source-file` audit row is built by one pure formatter. These checks pin the exact words the live
// verifier (build/restrictive-audit/verify-audio-relation-20261003.py) parses: relation names, candidate listing
// and its eight-entry limit, whole-component path agreement over ALL candidates, piece listing limits, number bases.
#include "runtime/audio_source_file_receipt.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {
using Simpsons::AudioSourceCandidate;
using Simpsons::AudioSourceFileClaim;
using Simpsons::FileReadSource;
using Simpsons::auditedPathsAgree;
using Simpsons::formatAudioSourceFileReceipt;

[[noreturn]] void fail(const char* message) {
    std::printf("FAIL audio source file receipt: %s\n",message);
    std::exit(1);
}
void require(bool condition,const char* message) {if(!condition)fail(message);}

std::map<std::string,std::string> words(const std::string& text) {
    std::map<std::string,std::string> result;std::istringstream stream(text);std::string word;
    while(stream>>word) {
        const auto at=word.find('=');
        require(at!=std::string::npos&&at>0,"a receipt word has no key=value shape");
        require(result.emplace(word.substr(0,at),word.substr(at+1)).second,"a receipt key repeats");
    }
    return result;
}

FileReadSource coveredRead() {
    FileReadSource read;
    read.status=FileReadSource::Status::Covered;read.openId=31;read.readOrdinal=1;read.sequence=244;read.fileOffset=4358;read.fileExtent=4382720;
    read.destination=0xE42C0170u;read.completed=59392;read.readCount=1;
    read.relative="audiostreams/spr_mus.mus";read.declared="d:\\AudioStreams\\SPR_MUS.mus";read.root="game";
    read.pieces[0]={0xE42C0176u,0xE42C0A38u,4358,1,31,244};
    return read;
}
std::vector<AudioSourceCandidate> candidates(size_t count,size_t matching) {
    std::vector<AudioSourceCandidate> result;
    for(size_t i=0;i<count;++i)
        result.push_back({i==matching?"audiostreams/spr_mus.mus":"audiostreams/other_"+std::to_string(i)+".mus",4352,4382720,uint32_t(700+i)});
    return result;
}
}

int main() {
    // Path agreement: folding of case, separators and drive; whole-component suffixes only.
    require(auditedPathsAgree("D:\\Data\\Audio.SNU","data/audio.snu")&&auditedPathsAgree("data/audio.snu","D:\\Data\\Audio.SNU"),"case, separator or drive folding differs");
    require(auditedPathsAgree("K:\\game\\data\\audio.snu","data/audio.snu")&&auditedPathsAgree("data/audio.snu","game/data/audio.snu"),"a whole-component suffix was not accepted");
    require(!auditedPathsAgree("data/xaudio.snu","audio.snu")&&!auditedPathsAgree("audio.snu","data/xaudio.snu"),"a partial file name was accepted as a suffix");
    require(!auditedPathsAgree("data/audio.snu","data/audio2.snu")&&!auditedPathsAgree("data/a.snu","data/b.snu"),"different files agreed");
    require(!auditedPathsAgree("","data/audio.snu")&&!auditedPathsAgree("data/audio.snu","")&&!auditedPathsAgree("/","\\"),"an empty path agreed");

    // A covered claim whose catalog candidates include the opened file (third of three, one without identity).
    {
        const auto receipt=formatAudioSourceFileReceipt({0xE42C0176u,0x2Au,0x1234u,10821},40,10821,coveredRead(),4,candidates(3,2));
        require(receipt.asset=="file:audiostreams/spr_mus.mus","the asset is not the opened file");
        const auto parameters=words(receipt.parameters),ownership=words(receipt.ownership),instance=words(receipt.instance);
        require(parameters.size()==3&&parameters.at("relation")=="covered"&&parameters.at("candidates")=="4"&&parameters.at("catalog_path_match")=="yes",
                "relation, candidate count (including a candidate without identity) or path match differs");
        require(ownership.size()==4&&ownership.at("admission")=="catalog-matched-claim"&&ownership.at("claim_content")=="catalog-block-sha256"&&
                ownership.at("relation_basis")=="address-recency"&&ownership.at("relation_content_compared")=="0","ownership claims more than address recency");
        require(instance.at("claim_address")=="e42c0176"&&instance.at("claim_token")=="2a"&&instance.at("claim_owner")=="1234"&&instance.at("claim_length")=="10821"&&
                instance.at("block")=="40"&&instance.at("block_bytes")=="10821","claim identity is not hex address/token/owner and decimal length/block");
        require(instance.at("file_offset")=="4358"&&instance.at("file_extent")=="4382720"&&instance.at("read_open_id")=="31"&&instance.at("read_count")=="1"&&
                instance.at("read_ordinal")=="1"&&instance.at("read_sequence")=="244"&&instance.at("read_destination")=="e42c0170"&&instance.at("read_completed")=="59392"&&
                instance.at("declared")=="d:\\AudioStreams\\SPR_MUS.mus","read numbers differ");
        require(instance.at("piece0")=="e42c0176-e42c0a38@4358#1/open31/seq244"&&!instance.contains("piece1"),"piece list differs");
        require(instance.at("candidate0")=="audiostreams/other_0.mus@4352:4382720#stream700"&&instance.at("candidate2")=="audiostreams/spr_mus.mus@4352:4382720#stream702"&&
                !instance.contains("candidate3"),"candidate list differs");
    }
    // No recorded read: unknown file, relation none, no pieces, path match unknown even with candidates.
    {
        const auto receipt=formatAudioSourceFileReceipt({0xE40A7150u,1,2,64},1,64,FileReadSource{},21,candidates(2,0));
        const auto parameters=words(receipt.parameters),instance=words(receipt.instance);
        require(receipt.asset=="file:unknown"&&parameters.at("relation")=="none"&&parameters.at("catalog_path_match")=="unknown"&&parameters.at("candidates")=="21","an unrelated claim was given a file or a path match");
        require(!instance.contains("piece0")&&instance.at("read_count")=="0"&&instance.at("read_open_id")=="0","an unrelated claim listed pieces or a read");
    }
    // Ambiguous with a disagreeing path: reported ambiguous, path match no.
    {
        auto read=coveredRead();read.status=FileReadSource::Status::Ambiguous;read.relative="audiostreams/bsh_mus.mus";
        const auto receipt=formatAudioSourceFileReceipt({0xE42C0176u,1,2,100},9,100,read,2,candidates(2,5));
        const auto parameters=words(receipt.parameters);
        require(parameters.at("relation")=="ambiguous"&&parameters.at("catalog_path_match")=="no","a disagreeing path matched or the relation name differs");
    }
    // Spanning keeps its name.
    {
        auto read=coveredRead();read.status=FileReadSource::Status::Spanning;
        require(words(formatAudioSourceFileReceipt({1,1,1,8},1,8,read,1,candidates(1,0)).parameters).at("relation")=="spanning","the spanning relation name differs");
    }
    // Twelve candidates: eight are listed, but agreement considers all of them (the match is the eleventh).
    {
        const auto receipt=formatAudioSourceFileReceipt({0xE42C0176u,1,2,100},1,100,coveredRead(),12,candidates(12,10));
        const auto parameters=words(receipt.parameters),instance=words(receipt.instance);
        require(parameters.at("catalog_path_match")=="yes"&&parameters.at("candidates")=="12","agreement ignored candidates beyond the listing limit");
        require(instance.contains("candidate7")&&!instance.contains("candidate8")&&!instance.contains("candidate10"),"the candidate listing limit differs from eight");
    }
    // Ten reads: the true count is kept, only the eight retained pieces are listed; one read lists one piece even if more are filled.
    {
        auto read=coveredRead();read.status=FileReadSource::Status::Spanning;read.readCount=10;
        for(uint32_t i=0;i<read.pieces.size();++i)read.pieces[i]={0x1000u+i,0x1001u+i,100u+i,1u+i,31,244u+i};
        auto instance=words(formatAudioSourceFileReceipt({0x1000u,1,2,10},1,10,read,1,candidates(1,0)).instance);
        require(instance.at("read_count")=="10"&&instance.contains("piece7")&&!instance.contains("piece8")&&instance.at("piece7")=="1007-1008@107#8/open31/seq251","a ten-read span lost its count or retained pieces");
        read.readCount=1;
        instance=words(formatAudioSourceFileReceipt({0x1000u,1,2,10},1,10,read,1,candidates(1,0)).instance);
        require(instance.contains("piece0")&&!instance.contains("piece1"),"a one-read claim listed unused pieces");
    }
    std::printf("PASS audio source file receipt: path agreement, relation names, candidate and piece limits, number bases, unknown file\n");
    return 0;
}
