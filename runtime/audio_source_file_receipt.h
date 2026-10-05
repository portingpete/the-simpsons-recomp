#pragma once
#include "file_read_source.h"
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace Simpsons {
// Whether a catalog source path and an opened file's relative path name the same file: equal after folding
// case, separators and any drive/leading separators, or one is a whole-component suffix of the other.
inline bool auditedPathsAgree(const std::string& catalogPath,const std::string& openedRelative) {
    const auto normalize=[](std::string text){
        for(char& ch:text){if(ch=='\\')ch='/';else if(ch>='A'&&ch<='Z')ch=char(ch-'A'+'a');}
        if(text.size()>=2&&text[1]==':')text.erase(0,2);
        while(!text.empty()&&text.front()=='/')text.erase(0,1);
        return text;
    };
    const auto wholeComponentSuffix=[](const std::string& whole,const std::string& tail){
        return whole.size()>tail.size()&&whole.compare(whole.size()-tail.size(),tail.size(),tail)==0&&whole[whole.size()-tail.size()-1]=='/';
    };
    const auto a=normalize(catalogPath),b=normalize(openedRelative);
    return !a.empty()&&!b.empty()&&(a==b||wholeComponentSuffix(a,b)||wholeComponentSuffix(b,a));
}

// One catalog candidate of a matched block (a copied certificate row: a candidate, not proof of which file was opened).
struct AudioSourceCandidate {std::string path;uint64_t audioOffset{},audioBytes{};uint32_t streamIndex{};};
struct AudioSourceFileClaim {uint32_t address{},token{},owner{},length{};};
struct AudioSourceFileReceipt {std::string asset,parameters,ownership,instance;};

// The `audio-source-file` audit row: the claim, the recorded read relation and up to eight catalog candidates.
// `candidateCount` is the number of candidates the catalog matched (some may have no identity); `candidates`
// holds the identities that exist. The relation is by address recency, never by content; the claim bytes
// themselves are catalog-verified before this is called.
inline AudioSourceFileReceipt formatAudioSourceFileReceipt(const AudioSourceFileClaim& claim,uint32_t block,uint32_t blockBytes,
        const FileReadSource& read,size_t candidateCount,const std::vector<AudioSourceCandidate>& candidates) {
    const bool known=read.status!=FileReadSource::Status::None;
    const char* relation=read.status==FileReadSource::Status::Covered?"covered":read.status==FileReadSource::Status::Spanning?"spanning":known?"ambiguous":"none";
    size_t agreeing=0,shown=0;std::ostringstream listed;
    for(const auto& candidate:candidates) {
        if(known&&auditedPathsAgree(candidate.path,read.relative))++agreeing;
        if(shown<8){listed<<" candidate"<<shown<<"="<<candidate.path<<"@"<<candidate.audioOffset<<":"<<candidate.audioBytes<<"#stream"<<candidate.streamIndex;++shown;}
    }
    std::ostringstream pieces;
    for(size_t i=0;i<read.pieces.size()&&i<read.readCount;++i) {
        const auto& piece=read.pieces[i];
        pieces<<std::hex<<" piece"<<i<<"="<<piece.begin<<"-"<<piece.end<<std::dec<<"@"<<piece.fileOffset<<"#"<<piece.readOrdinal<<"/open"<<piece.openId<<"/seq"<<piece.sequence;
    }
    std::ostringstream asset,parameters,ownership,instance;
    asset<<"file:"<<(known?read.relative:std::string("unknown"));
    parameters<<"relation="<<relation<<" candidates="<<candidateCount<<" catalog_path_match="<<(!known?"unknown":agreeing?"yes":"no");
    ownership<<"admission=catalog-matched-claim claim_content=catalog-block-sha256 relation_basis=address-recency relation_content_compared=0";
    instance<<std::hex<<"claim_address="<<claim.address<<" claim_token="<<claim.token<<" claim_owner="<<claim.owner<<std::dec
            <<" claim_length="<<claim.length<<" block="<<block<<" block_bytes="<<blockBytes
            <<" file_offset="<<read.fileOffset<<" file_extent="<<read.fileExtent<<" read_open_id="<<read.openId
            <<" read_count="<<read.readCount<<" read_ordinal="<<read.readOrdinal<<" read_sequence="<<read.sequence<<std::hex<<" read_destination="<<read.destination
            <<std::dec<<" read_completed="<<read.completed<<" declared="<<read.declared<<pieces.str()<<listed.str();
    return {asset.str(),parameters.str(),ownership.str(),instance.str()};
}
}
