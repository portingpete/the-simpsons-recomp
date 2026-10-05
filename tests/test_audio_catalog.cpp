#include "audio/audio_catalog.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <limits>
#include <utility>
#include <windows.h>
#include <bcrypt.h>

using namespace Simpsons::Audio;
namespace {
void need(bool value,const char* why){if(!value)throw XmaSourceError(why);}
uint32_t be32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
struct OriginalBlock {std::array<uint8_t,8> header{};std::vector<uint8_t> bytes;};
OriginalBlock block(const std::filesystem::path& path,uint32_t ordinal) {
    std::ifstream input(path,std::ios::binary);need(bool(input),"Original SNU fixture missing");
    std::array<uint8_t,24> prefix{};input.read(reinterpret_cast<char*>(prefix.data()),prefix.size());
    need(bool(input),"Original SNU fixture header truncated");
    OriginalBlock result;std::copy_n(prefix.begin()+16,8,result.header.begin());
    uint64_t offset=be32(prefix.data()+8);
    for(uint32_t i=0;i<=ordinal;++i) {
        input.seekg(offset);std::array<uint8_t,4> word{};input.read(reinterpret_cast<char*>(word.data()),4);
        need(bool(input),"Original SNU fixture block header truncated");
        const uint32_t size=be32(word.data())&0xFFFFFFu;
        need(size>=8 && size<=1024*1024,"Original SNU fixture block extent changed");
        if(i==ordinal) {
            result.bytes.resize(size);input.seekg(offset);
            input.read(reinterpret_cast<char*>(result.bytes.data()),size);
            need(bool(input),"Original SNU fixture block truncated");
            result.bytes[0]&=0x7F;
        }
        offset+=size;
    }
    return result;
}
// Hand-authored cases never open their asserted source paths. Existing original matching controls below read
// fixed fixture paths independently of the certificate accessor; copied rows
// neither open files nor establish the original runtime opened/read lineage.
using CatalogBytes=std::vector<uint8_t>;
uint32_t little32(const CatalogBytes& bytes,size_t at) {
    need(at<=bytes.size() && bytes.size()-at>=4,"Fixture word outside raw catalog");
    return uint32_t(bytes[at])|uint32_t(bytes[at+1])<<8|uint32_t(bytes[at+2])<<16|uint32_t(bytes[at+3])<<24;
}
uint64_t little64(const CatalogBytes& bytes,size_t at) {
    return uint64_t(little32(bytes,at))|uint64_t(little32(bytes,at+4))<<32;
}
void put32(CatalogBytes& bytes,size_t at,uint32_t value) {
    need(at<=bytes.size() && bytes.size()-at>=4,"Fixture write word outside catalog");
    for(unsigned i=0;i<4;++i)bytes[at+i]=uint8_t(value>>(i*8));
}
void put64(CatalogBytes& bytes,size_t at,uint64_t value) {
    put32(bytes,at,uint32_t(value));put32(bytes,at+4,uint32_t(value>>32));
}
void putBig32(CatalogBytes& bytes,size_t at,uint32_t value) {
    need(at<=bytes.size() && bytes.size()-at>=4,"Fixture write header outside catalog");
    for(unsigned i=0;i<4;++i)bytes[at+i]=uint8_t(value>>((3-i)*8));
}
std::array<uint8_t,32> catalogDigest(const CatalogBytes& bytes) {
    need(bytes.size()<=(std::numeric_limits<ULONG>::max)(),"Fixture hash extent exceeds ULONG");
    std::array<uint8_t,32> digest{};
    need(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(bytes.data()),
                   ULONG(bytes.size()),digest.data(),ULONG(digest.size()))>=0,
         "Fixture SHA256 failed");return digest;
}
template<size_t N> std::string digestHex(const std::array<uint8_t,N>& digest) {
    std::string text;text.reserve(N*2);
    for(uint8_t byte:digest){text.push_back("0123456789abcdef"[byte>>4]);text.push_back("0123456789abcdef"[byte&15]);}
    return text;
}
CatalogBytes catalogFile(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    need(bool(input),"Fixture catalog read failed");const auto length=input.tellg();
    need(length>=0 && length<=std::streamoff(512*1024*1024),"Fixture catalog read extent invalid");
    CatalogBytes bytes(size_t(length),uint8_t{});input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()),length);need(bool(input),"Fixture catalog read truncated");
    return bytes;
}
std::string jsonText(const std::string& text) {
    std::string result="\"";
    for(unsigned char c:text) {
        if(c=='"' || c=='\\'){result.push_back('\\');result.push_back(char(c));}
        else if(c<32){char encoded[7]{};std::snprintf(encoded,sizeof(encoded),"\\u%04x",unsigned(c));result+=encoded;}
        else result.push_back(char(c));
    }
    result.push_back('"');return result;
}
struct CatalogCases {
    std::filesystem::path directory;
    std::ofstream records;
    size_t passed{},failed{};
    std::vector<std::string> failures;
    std::vector<std::filesystem::path> files;
    std::vector<std::string> observations;
    std::string activeName,activeClassification;
    CatalogCases() {
        directory=std::filesystem::current_path()/
            ("audio-catalog-identity-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        need(std::filesystem::create_directory(directory),"Fixture output directory already exists");
        records.open(directory/"cases.jsonl",std::ios::binary|std::ios::out);
        need(bool(records),"Fixture outcome file could not be opened");
        std::printf("[AUDIO CATALOG IDENTITY PROBE] directory=%s\n",directory.string().c_str());
    }
    std::filesystem::path save(const std::string& name,const CatalogBytes& bytes) {
        const auto path=directory/(name+".bin");need(!std::filesystem::exists(path),"Fixture would overwrite a binary");
        std::ofstream out(path,std::ios::binary|std::ios::out);
        out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));out.flush();
        need(bool(out),"Fixture binary flush failed");out.close();need(bool(out),"Fixture binary close failed");
        files.push_back(path);
        // Log the saved raw catalog before returning its path to the loader.
        // Do not parse malformed fields here: the real loader remains the authority.
        records<<"{\"schema\":1,\"kind\":\"catalog-certificate-binary\",\"phase\":\"before_load\",\"name\":"<<jsonText(activeName)
               <<",\"classification\":"<<jsonText(activeClassification)<<",\"binary_name\":"<<jsonText(name)
               <<",\"caller\":\"CatalogCases::save\",\"helper\":\"AudioCatalog::load\""
               <<",\"mission\":\"offline_catalog_certificate\",\"last_action\":\"fixture_binary_saved_before_load\""
               <<",\"ownership\":\"fixture_file_closed_target_catalog_load_pending\""
               <<",\"scope\":\"host_catalog_fixture\",\"source_identity\":\"serialized_certificate_only\""
               <<",\"path\":"<<jsonText(path.generic_string())<<",\"sha256\":"<<jsonText(digestHex(catalogDigest(bytes)))
               <<",\"bytes\":"<<bytes.size()<<"}\n";
        records.flush();need(bool(records),"Fixture before-load binary receipt write failed");return path;
    }
    template<class Function> void run(const char* name,const char* classification,Function&& function) {
        files.clear();observations.clear();std::string error;activeName=name;activeClassification=classification;
        // This flushed receipt precedes every case, including rejected inputs.
        records<<"{\"schema\":1,\"kind\":\"catalog-certificate-attempt\",\"phase\":\"before_validation\",\"name\":"<<jsonText(name)
               <<",\"classification\":"<<jsonText(classification)
               <<",\"caller\":\"catalogMetadataRegression\",\"helper\":\"CatalogCases::run\""
               <<",\"mission\":\"offline_catalog_certificate\",\"last_action\":\"case_attempt_before_validation\""
               <<",\"ownership\":\"case_not_entered\",\"scope\":\"host_catalog_fixture\""
               <<",\"source_identity\":\"serialized_certificate_only\"}\n";
        records.flush();need(bool(records),"Fixture before-validation attempt receipt write failed");
        try{function();++passed;}catch(const std::exception& problem){error=problem.what();++failed;failures.push_back(std::string(name)+": "+error);}
        records<<"{\"schema\":1,\"kind\":\"catalog-certificate-case\",\"name\":"<<jsonText(name)
               <<",\"classification\":"<<jsonText(classification)<<",\"scope\":\"host_catalog_fixture\""
               <<",\"source_identity\":\"serialized_certificate_only\""
               <<",\"outcome\":"<<jsonText(error.empty()?"passed":"failed")<<",\"error\":"<<jsonText(error)<<",\"binaries\":[";
        for(size_t i=0;i<files.size();++i) {
            const auto bytes=catalogFile(files[i]);if(i)records<<',';
            records<<"{\"path\":"<<jsonText(files[i].string())<<",\"sha256\":"<<jsonText(digestHex(catalogDigest(bytes)))
                   <<",\"bytes\":"<<bytes.size()<<'}';
        }
        records<<"],\"observations\":[";
        for(size_t i=0;i<observations.size();++i){if(i)records<<',';records<<jsonText(observations[i]);}
        records<<"]}\n";records.flush();need(bool(records),"Fixture outcome write failed");
        std::printf("[CATALOG CERTIFICATE CASE] %s %s%s%s\n",name,error.empty()?"PASS":"FAIL",error.empty()?"":": ",error.c_str());
    }
    void finish(bool accessorPresent) {
        records.close();need(bool(records),"Fixture outcome close failed");
        const auto raw=catalogFile(directory/"cases.jsonl");
        std::printf("AUDIT_AUDIO_CATALOG_CERTIFICATES accessor=%u cases_passed=%zu cases_failed=%zu raw=%s sha256=%s bytes=%zu scope=serialized_certificate_only\n",
                    unsigned(accessorPresent),passed,failed,(directory/"cases.jsonl").string().c_str(),digestHex(catalogDigest(raw)).c_str(),raw.size());
        need(passed+failed==(accessorPresent?55u:48u),"Catalog certificate planned case census differs after independent cases");
        if(!accessorPresent) {
            if(failed!=1 || failures.size()!=1 || failures.front()!=
                    "metadata_accessor_presence: Audio catalog metadata accessor missing")
                throw XmaSourceError("Audio catalog independent cases failed before missing accessor frontier; see preserved cases.jsonl");
            throw XmaSourceError("Audio catalog metadata accessor missing after original checks and independent loader cases");
        }
        if(failed)throw XmaSourceError("Audio catalog certificate regression failed after independent cases; see preserved cases.jsonl");
    }
};
struct SmallCatalog {
    CatalogBytes bytes;
    std::array<std::vector<CatalogBytes>,5> owned;
};
CatalogBytes literalBlock(uint8_t payload,uint32_t frames,bool last) {
    CatalogBytes bytes(17,uint8_t{});putBig32(bytes,0,(last?0x80000000u:0u)|17u);
    putBig32(bytes,4,frames);putBig32(bytes,8,39);bytes[12]=8;bytes[16]=payload;return bytes;
}
SmallCatalog makeSmallCatalog() {
    // Four independent source certificates, five streams and eight blocks.
    // Alpha/gamma alias in full; beta shares only their first block. MUS
    // ordinals0/1 share source3, with distinct controls and asserted serialized offsets.
    constexpr uint64_t sourceAt=76,streamAt=332,blockAt=692,layerAt=1492,stringAt=1780;
    const std::array<std::string,4> names{{"certificate/alpha.snu","certificate/beta.snu","certificate/gamma.snu","certificate/menu.mus"}};
    size_t stringBytes=0;for(const auto& name:names)stringBytes+=name.size();
    SmallCatalog result;result.bytes.resize(size_t(stringAt)+stringBytes);
    auto& bytes=result.bytes;std::memcpy(bytes.data(),"SIMAUD01",8);
    put32(bytes,8,1);put32(bytes,12,4);put32(bytes,16,5);put32(bytes,20,8);put32(bytes,24,8);
    put64(bytes,28,sourceAt);put64(bytes,36,streamAt);put64(bytes,44,blockAt);put64(bytes,52,layerAt);put64(bytes,60,stringAt);put64(bytes,68,stringBytes);
    const std::array<uint8_t,32> firstDigest{{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,255}};
    size_t pathAt=0;
    for(uint32_t i=0;i<4;++i) {
        const auto at=size_t(sourceAt)+size_t(i)*64;put64(bytes,at,pathAt);put32(bytes,at+8,uint32_t(names[i].size()));
        put32(bytes,at+12,i);put32(bytes,at+16,i==3?2u:1u);put32(bytes,at+20,i==3?2u:1u);put64(bytes,at+24,i==3?2432u:66u);
        for(size_t j=0;j<32;++j)bytes[at+32+j]=uint8_t(firstDigest[j]^uint8_t(i));
        std::copy(names[i].begin(),names[i].end(),bytes.begin()+std::ptrdiff_t(stringAt+pathAt));pathAt+=names[i].size();
    }
    result.owned[0]={literalBlock(0x51,1,false),literalBlock(0xA1,1,true)};
    result.owned[1]={literalBlock(0x51,1,false),literalBlock(0xB2,1,true)};
    result.owned[2]=result.owned[0];result.owned[3]={literalBlock(0xC3,3,true)};result.owned[4]={literalBlock(0xD4,4,true)};
    uint32_t firstBlock=0;
    for(uint32_t i=0;i<5;++i) {
        const auto at=size_t(streamAt)+size_t(i)*72;const uint32_t count=uint32_t(result.owned[i].size());
        const uint64_t audio=i<3?32u:(i==3?0x300u:0x900u);const uint64_t header=i<3?16u:(i==3?0x60u:0x80u);
        const uint32_t frames=i<3?2u:(i==3?3u:4u);
        put32(bytes,at,i<3?i:3u);put32(bytes,at+4,i<4?0u:1u);put64(bytes,at+8,header);put64(bytes,at+16,audio);put64(bytes,at+24,uint64_t(count)*17);
        put32(bytes,at+32,firstBlock);put32(bytes,at+36,count);put32(bytes,at+40,48000);put32(bytes,at+44,1);put32(bytes,at+48,frames);
        putBig32(bytes,at+64,0x0300BB80);putBig32(bytes,at+68,0x40000000u|frames);
        for(uint32_t j=0;j<count;++j) {
            const uint32_t index=firstBlock+j;const auto b=size_t(blockAt)+size_t(index)*100;const auto l=size_t(layerAt)+size_t(index)*36;
            const auto& raw=result.owned[i][j];auto normalized=raw;normalized[0]&=0x7F;
            put32(bytes,b,i);put32(bytes,b+4,j);put64(bytes,b+8,audio+uint64_t(j)*17);put32(bytes,b+16,17);put32(bytes,b+20,i<3?1u:frames);
            put32(bytes,b+24,index);bytes[b+28]=1;bytes[b+30]=uint8_t(j+1==count?0x80:0);
            const auto rawDigest=catalogDigest(raw),normalizedDigest=catalogDigest(normalized);
            std::copy(rawDigest.begin(),rawDigest.end(),bytes.begin()+std::ptrdiff_t(b+36));std::copy(normalizedDigest.begin(),normalizedDigest.end(),bytes.begin()+std::ptrdiff_t(b+68));
            put32(bytes,l,index);put32(bytes,l+4,0);put64(bytes,l+8,audio+uint64_t(j)*17+8);put32(bytes,l+16,9);put64(bytes,l+20,audio+uint64_t(j)*17+12);put32(bytes,l+28,5);put32(bytes,l+32,2043);
        }
        firstBlock+=count;
    }
    need(firstBlock==8,"Literal fixture block count changed");return result;
}
std::array<uint8_t,8> rawHeader(const CatalogBytes& bytes,uint32_t index) {
    const auto at=size_t(little64(bytes,36))+size_t(index)*72+64;
    need(at<=bytes.size() && bytes.size()-at>=8,"Fixture stream header outside catalog");
    std::array<uint8_t,8> header{};std::copy_n(bytes.begin()+std::ptrdiff_t(at),8,header.begin());return header;
}
CatalogBytes normalizedBlock(const SmallCatalog& fixture,uint32_t index,uint32_t ordinal) {
    auto bytes=fixture.owned.at(index).at(ordinal);bytes[0]&=0x7F;return bytes;
}
void replacePath(CatalogBytes& bytes,uint32_t index,const std::string& replacement) {
    const auto sources=little32(bytes,12);const auto sourceAt=size_t(little64(bytes,28)),stringAt=size_t(little64(bytes,60));
    std::vector<std::string> paths;
    for(uint32_t i=0;i<sources;++i) {
        const auto at=sourceAt+size_t(i)*64;const auto offset=size_t(little64(bytes,at));const auto length=size_t(little32(bytes,at+8));
        need(stringAt+offset<=bytes.size() && length<=bytes.size()-stringAt-offset,"Fixture old path outside strings");
        paths.emplace_back(reinterpret_cast<const char*>(bytes.data()+stringAt+offset),length);
    }
    paths.at(index)=replacement;bytes.resize(stringAt);size_t pathAt=0;
    for(uint32_t i=0;i<sources;++i) {
        const auto at=sourceAt+size_t(i)*64;put64(bytes,at,pathAt);put32(bytes,at+8,uint32_t(paths[i].size()));
        bytes.insert(bytes.end(),paths[i].begin(),paths[i].end());pathAt+=paths[i].size();
    }
    put64(bytes,68,pathAt);
}
void relocateAudio(CatalogBytes& bytes,uint32_t index,uint64_t audio,uint64_t fileBytes) {
    const auto s=size_t(little64(bytes,36))+size_t(index)*72;const auto source=little32(bytes,s);
    put64(bytes,size_t(little64(bytes,28))+size_t(source)*64+24,fileBytes);put64(bytes,s+16,audio);
    const auto first=little32(bytes,s+32),count=little32(bytes,s+36);
    const auto blocks=size_t(little64(bytes,44)),layers=size_t(little64(bytes,52));
    uint64_t offset=audio;
    for(uint32_t i=0;i<count;++i) {
        const auto b=blocks+size_t(first+i)*100;put64(bytes,b+8,offset);
        const auto l=layers+size_t(little32(bytes,b+24))*36;put64(bytes,l+8,offset+8);put64(bytes,l+20,offset+12);offset+=little32(bytes,b+16);
    }
}
void exerciseSmall(const AudioCatalog& catalog,const SmallCatalog& fixture) {
    need(catalog.sourceCount()==4 && catalog.streamCount()==5 && catalog.blockCount()==8,"Literal catalog census changed");
    const auto header=rawHeader(fixture.bytes,0);const auto first=catalog.match(header,{},1,normalizedBlock(fixture,0,0));
    need(first.candidates==std::vector<uint32_t>({0,1,2}),"Literal first collision changed");
    const auto last=catalog.match(header,first.candidates,2,normalizedBlock(fixture,0,1));
    need(last.candidates==std::vector<uint32_t>({0,2}) && catalog.complete(last.candidates,2),"Literal full aliases changed");
    const std::array<uint32_t,3> reverse{{2,0,2}};const auto repeated=catalog.match(header,reverse,2,normalizedBlock(fixture,0,1));
    need(repeated.candidates==std::vector<uint32_t>({2,0,2}) && catalog.complete(repeated.candidates,2),"Literal alias order/duplicate index lost");
    for(uint32_t i=3;i<5;++i) {
        const auto match=catalog.match(rawHeader(fixture.bytes,i),{},1,normalizedBlock(fixture,i,0));
        need(match.candidates==std::vector<uint32_t>({i}) && catalog.complete(match.candidates,1),"Literal MUS stream association changed");
    }
}
template<class Identity> void compareCertificate(const Identity& id,const CatalogBytes& bytes,uint32_t index) {
    const auto s=size_t(little64(bytes,36))+size_t(index)*72;const auto source=little32(bytes,s);const auto a=size_t(little64(bytes,28))+size_t(source)*64;
    const auto pathAt=size_t(little64(bytes,60))+size_t(little64(bytes,a));const auto pathLength=size_t(little32(bytes,a+8));
    need(pathAt<=bytes.size() && pathLength<=bytes.size()-pathAt,"Certificate oracle path outside bytes");
    const std::string path(reinterpret_cast<const char*>(bytes.data()+pathAt),pathLength);
    need(id.streamIndex==index && id.sourceIndex==source && id.sourceKind==little32(bytes,a+20) && id.ordinal==little32(bytes,s+4) &&
         id.sourcePath==path && id.sourceFileBytes==little64(bytes,a+24) && id.headerOffset==little64(bytes,s+8) &&
         id.audioOffset==little64(bytes,s+16) && id.audioBytes==little64(bytes,s+24) && id.header==rawHeader(bytes,index),
         "Copied candidate certificate scalars/path/header differ from independent raw rows");
    for(size_t i=0;i<32;++i)need(id.sourceSha256[i]==bytes[a+32+i],"Copied candidate source digest differs from raw Source64");
}
template<class Identity> bool equalCertificate(const Identity& a,const Identity& b) {
    return a.streamIndex==b.streamIndex && a.sourceIndex==b.sourceIndex && a.sourceKind==b.sourceKind && a.ordinal==b.ordinal &&
           a.sourcePath==b.sourcePath && a.sourceSha256==b.sourceSha256 && a.sourceFileBytes==b.sourceFileBytes &&
           a.headerOffset==b.headerOffset && a.audioOffset==b.audioOffset && a.audioBytes==b.audioBytes && a.header==b.header;
}
void rejectedCatalog(CatalogCases& cases,const char* name,const CatalogBytes& bytes,const char* exactError,const char* classification) {
    const auto path=cases.save(name,bytes);std::string observed;
    try{const auto admitted=AudioCatalog::load(path);(void)admitted.sourceCount();}
    catch(const XmaSourceError& error){observed=error.what();}
    cases.observations.push_back(std::string("rejection_classification=")+classification);
    cases.observations.push_back(std::string("expected_current_loader_rejection=")+exactError);
    cases.observations.push_back("observed_rejection="+(observed.empty()?std::string("ADMITTED"):observed));
    if(observed!=exactError)throw XmaSourceError(std::string("Catalog loader rejection mismatch; expected=")+exactError+"; observed="+(observed.empty()?std::string("ADMITTED"):observed));
}
template<class Catalog> void catalogMetadataRegression(const Catalog& shipped,const std::filesystem::path& catalogPath,
                                                      const std::filesystem::path& root,const std::vector<uint32_t>& aliases,uint64_t sequence) {
    CatalogCases cases;const auto literal=makeSmallCatalog();const auto original=catalogFile(catalogPath);
    cases.run("catalog_authority","serialized_certificate_boundary",[&] {
        need(digestHex(catalogDigest(original))=="7e629459bc742f7a0e389f87311c81db94e08645f5d6c34fc9a60f6714f9d0f5","Shipped catalog byte authority changed");
        need(digestHex(catalogDigest(literal.bytes))=="83115b9d2ae1a23c43c0d447c5a378c27fc2ff6e85c27c07853469bd424cc801","Hand-authored certificate bytes differ from preserved independent literal");
        need(little32(original,12)==7430 && little32(original,16)==9470 && little32(original,20)==377643 && little32(original,24)==677048,"Shipped raw table census changed");
    });
    cases.run("literal_load_match_destroy","serialized_certificate_boundary",[&] {const auto loaded=Catalog::load(cases.save("literal_load_match_destroy",literal.bytes));exerciseSmall(loaded,literal);});
    const auto accepted=[&](const char* name,CatalogBytes bytes) {
        cases.run(name,"serialized_certificate_boundary",[&] {const auto loaded=Catalog::load(cases.save(name,bytes));auto changed=literal;changed.bytes=bytes;exerciseSmall(loaded,changed);
            if constexpr(requires(const Catalog& owner){owner.candidateIdentity(uint32_t{});}) {
                for(uint32_t i=0;i<5;++i){const auto id=loaded.candidateIdentity(i);need(bool(id),"Accepted boundary certificate missing");compareCertificate(*id,bytes,i);}
            }
        });
    };
    {auto bytes=literal.bytes;replacePath(bytes,0,"a");accepted("path_length1",std::move(bytes));}
    {auto bytes=literal.bytes;replacePath(bytes,0,std::string(4096,'a'));accepted("path_length4096",std::move(bytes));}
    {auto bytes=literal.bytes;put64(bytes,76+24,512ull*1024*1024);accepted("serialized_source_extent512MiB",std::move(bytes));}
    {auto bytes=literal.bytes;put64(bytes,332+8,58);accepted("header_exact_declared_end",std::move(bytes));}
    {auto bytes=literal.bytes;relocateAudio(bytes,0,1024,1058);accepted("audio_exact_declared_end",std::move(bytes));}
    {auto bytes=literal.bytes;replacePath(bytes,2,"certificate/alpha.snu");std::copy_n(bytes.begin()+76+32,32,bytes.begin()+76+2*64+32);accepted("duplicate_path_digest_indices",std::move(bytes));}
    const auto structural=[&](const char* name,CatalogBytes bytes,const char* exactError) {
        cases.run(name,"structural_corruption",[&]{rejectedCatalog(cases,name,bytes,exactError,"structural_corruption");});
    };
    const auto policy=[&](const char* name,CatalogBytes bytes,const char* exactError) {
        cases.run(name,"current_native_policy",[&] {
            cases.observations.push_back("original_producer_invariant=unqualified");
            rejectedCatalog(cases,name,bytes,exactError,"current_native_policy");
        });
    };
    // Source extent cap+1 and path4097 have no authenticated producer input.
    // They are plan-only notes, not categorical malformed/must-reject cases.
    std::printf("AUDIT_AUDIO_CATALOG_POLICY_PLAN source_extent_over512MiB=unqualified_plan_only path_length4097=unqualified_plan_only native_execution=none\n");
    constexpr const char* sourceError="Audio catalog source span/identity is invalid";
    constexpr const char* pathError="Audio catalog source path is invalid";
    constexpr const char* streamError="Audio catalog stream header/span is invalid";
    constexpr const char* framingError="Audio catalog table offsets are inconsistent";
    for(const auto& [name,path]:std::array<std::pair<const char*,std::string>,6>{{
        {"path_empty",""},{"path_nul",std::string("a\0b",3)},
        {"path_backslash","a\\b"},{"path_absolute","/a"},{"path_parent","a/../b"},{"path_dotdot",".."}}}) {
        auto bytes=literal.bytes;replacePath(bytes,0,path);policy(name,std::move(bytes),path.empty()?sourceError:pathError);
    }
    {auto bytes=literal.bytes;put64(bytes,76,(std::numeric_limits<uint64_t>::max)());structural("path_span_overflow",std::move(bytes),sourceError);}
    for(const auto& [name,extent]:std::array<std::pair<const char*,uint64_t>,1>{{{"source_extent0",0}}}) {
        auto bytes=literal.bytes;put64(bytes,76+24,extent);structural(name,std::move(bytes),sourceError);
    }
    {auto bytes=literal.bytes;put32(bytes,76+20,3);structural("source_kind3",std::move(bytes),sourceError);}
    {auto bytes=literal.bytes;put32(bytes,76+12,1);structural("source_first_interval",std::move(bytes),sourceError);}
    {auto bytes=literal.bytes;put32(bytes,76+16,2);structural("SNU_multiple_streams",std::move(bytes),sourceError);}
    {auto bytes=literal.bytes;put32(bytes,332,4);structural("stream_source_equal_count",std::move(bytes),streamError);}
    {auto bytes=literal.bytes;put32(bytes,332,1);structural("stream_wrong_source_interval",std::move(bytes),streamError);}
    {auto bytes=literal.bytes;put32(bytes,332+4,1);structural("SNU_wrong_ordinal",std::move(bytes),streamError);}
    {auto bytes=literal.bytes;put32(bytes,332+4*72+4,0);structural("MUS_wrong_ordinal",std::move(bytes),streamError);}
    for(const auto& [name,offset]:std::array<std::pair<const char*,uint64_t>,3>{{{"header_one_byte_short",59},{"header_beyond_owner",67},{"header_wrap",(std::numeric_limits<uint64_t>::max)()}}}) {
        auto bytes=literal.bytes;put64(bytes,332+8,offset);structural(name,std::move(bytes),streamError);
    }
    {auto bytes=literal.bytes;put64(bytes,332+16,67);structural("audio_beyond_owner",std::move(bytes),streamError);}
    {auto bytes=literal.bytes;put64(bytes,332+16,(std::numeric_limits<uint64_t>::max)());structural("audio_wrap",std::move(bytes),streamError);}
    {auto bytes=literal.bytes;put64(bytes,332+24,0);structural("audio_extent0",std::move(bytes),streamError);}
    {auto bytes=literal.bytes;put64(bytes,332+24,35);structural("audio_one_byte_overflow",std::move(bytes),streamError);}
    {auto bytes=literal.bytes;put32(bytes,0,0);structural("magic",std::move(bytes),"Audio catalog magic/version mismatch");}
    {auto bytes=literal.bytes;put32(bytes,8,2);policy("version2",std::move(bytes),"Audio catalog magic/version mismatch");}
    for(const auto& [name,offset]:std::array<std::pair<const char*,size_t>,5>{{{"source_table_offset",28},{"stream_table_offset",36},{"block_table_offset",44},{"layer_table_offset",52},{"string_table_offset",60}}}) {
        auto bytes=literal.bytes;put64(bytes,offset,little64(bytes,offset)+1);structural(name,std::move(bytes),framingError);
    }
    for(const auto& [name,length]:std::array<std::pair<const char*,size_t>,4>{{{"truncated_header",75},{"truncated_source_digest",76+63},{"truncated_stream_header",332+71},{"truncated_string",literal.bytes.size()-1}}}) {
        auto bytes=literal.bytes;bytes.resize(length);structural(name,std::move(bytes),length<76?"Audio catalog file extent is invalid":framingError);
    }
    {auto bytes=literal.bytes;put64(bytes,692+8,33);structural("block_order",std::move(bytes),"Audio catalog block ordering/extent is invalid");}
    {auto bytes=literal.bytes;put32(bytes,692+16,7);structural("block_too_small",std::move(bytes),"Audio catalog block span/framing is invalid");}
    {auto bytes=literal.bytes;put64(bytes,1492+20,45);structural("layer_payload_offset",std::move(bytes),"Audio catalog layer framing is invalid");}
    {auto bytes=literal.bytes;bytes[692+100+30]=0;structural("segment_end_missing",std::move(bytes),"Audio catalog stream samples/segment ends are inconsistent");}
    {auto bytes=literal.bytes;put32(bytes,692+20,2);structural("block_frame_sum",std::move(bytes),"Audio catalog stream samples/segment ends are inconsistent");}
    // Current native alias-control equality policy runs before block use.
    // This composite mutation also lacks a valid loop introduction layout;
    // it does not qualify the complete original same-header loop-control range.
    {auto bytes=literal.bytes;const size_t s=332+72;put32(bytes,s+52,1);put32(bytes,s+56,17);put32(bytes,s+60,1);putBig32(bytes,s+68,0x60000002);
     // A genuinely equal header encodes loop1 in both, but distinct loop offsets.
     put32(bytes,332+52,1);put32(bytes,332+56,16);put32(bytes,332+60,1);putBig32(bytes,332+68,0x60000002);
     policy("same_header_control_disagreement",std::move(bytes),"Audio catalog same-header aliases disagree on stream/loop control");}
    constexpr bool present=requires(const Catalog& catalog){catalog.candidateIdentity(uint32_t{});};
    if constexpr(present) {
        cases.run("accessor_empty_and_loaded_bounds","serialized_certificate_boundary",[&] {
            const Catalog empty;need(!empty.candidateIdentity(0) && !empty.candidateIdentity((std::numeric_limits<uint32_t>::max)()),"Empty catalog accessor admitted an index");
            const auto loaded=Catalog::load(cases.save("accessor_empty_and_loaded_bounds",literal.bytes));
            for(uint32_t index:{0u,4u}) {const auto id=loaded.candidateIdentity(index);need(bool(id),"Valid boundary identity absent");compareCertificate(*id,literal.bytes,index);}
            for(uint32_t index:{5u,(std::numeric_limits<uint32_t>::max)()})need(!loaded.candidateIdentity(index),"Out-of-range identity admitted");
            const auto again=loaded.candidateIdentity(0);need(bool(again),"Invalid accessor mutated catalog");compareCertificate(*again,literal.bytes,0);exerciseSmall(loaded,literal);
        });
        cases.run("all9470_shipped_certificates","serialized_certificate_boundary",[&] {
            for(uint32_t i=0;i<9470;++i){const auto id=shipped.candidateIdentity(i);need(bool(id),"Shipped identity missing");compareCertificate(*id,original,i);}
            need(!shipped.candidateIdentity(9470) && !shipped.candidateIdentity((std::numeric_limits<uint32_t>::max)()),"Shipped accessor out-of-range admitted");
            const auto a=shipped.candidateIdentity(3810),b=shipped.candidateIdentity(3811);
            need(a && b && a->sourceIndex==3150 && b->sourceIndex==3150 && a->sourceKind==2 && b->sourceKind==2 && a->ordinal==0 && b->ordinal==1 &&
                 a->sourcePath=="audiostreams/menu_mus.mus" && b->sourcePath==a->sourcePath && a->sourceSha256==b->sourceSha256 &&
                 a->headerOffset==1280 && b->headerOffset==1296 && a->audioOffset==1920 && b->audioOffset==1630976 &&
                 a->audioBytes==1628992 && b->audioBytes==4736,"Authored menu ordinal/offset association lost");
        });
        cases.run("owning_copy_destroy_replace_move","serialized_certificate_boundary",[&] {
            const auto path=cases.save("owning_copy_destroy_replace_move",literal.bytes);
            auto retained=[&]{const auto owner=Catalog::load(path);return owner.candidateIdentity(3);}();
            need(bool(retained),"Destroyed catalog had no copied identity");compareCertificate(*retained,literal.bytes,3);
            auto owner=Catalog::load(path);const auto before=owner.candidateIdentity(3);need(bool(before),"Live catalog had no identity");
            auto altered=*before;altered.sourcePath.assign(4096,'x');altered.sourceSha256.fill(0);altered.header.fill(0);altered.streamIndex=99;altered.sourceIndex=99;altered.sourceKind=99;altered.ordinal=99;
            altered.sourceFileBytes=0;altered.headerOffset=0;altered.audioOffset=0;altered.audioBytes=0;
            const auto after=owner.candidateIdentity(3);need(after && equalCertificate(*before,*after),"Returned copy mutation affected catalog");exerciseSmall(owner,literal);
            auto copied=owner;auto moved=std::move(copied);const auto destination=moved.candidateIdentity(3);need(destination && equalCertificate(*retained,*destination),"Catalog copy/move changed copied certificate");
            owner=Catalog{};need(!owner.candidateIdentity(3),"Replacement catalog retained old index");compareCertificate(*retained,literal.bytes,3);exerciseSmall(moved,literal);
        });
        cases.run("literal_certificate_fields_and_aliases","serialized_certificate_boundary",[&] {
            const auto loaded=Catalog::load(cases.save("literal_certificate_fields_and_aliases",literal.bytes));
            for(uint32_t i=0;i<5;++i){const auto id=loaded.candidateIdentity(i);need(bool(id),"Literal certificate missing");compareCertificate(*id,literal.bytes,i);}
            const auto a=loaded.candidateIdentity(0),b=loaded.candidateIdentity(2),c=loaded.candidateIdentity(4);
            need(a && b && c && a->sourcePath=="certificate/alpha.snu" && b->sourcePath=="certificate/gamma.snu" && a->sourceSha256!=b->sourceSha256 &&
                 c->sourceIndex==3 && c->streamIndex==4 && c->ordinal==1 && c->headerOffset==0x80 && c->audioOffset==0x900 && c->audioBytes==17,
                 "Literal alias/source-vs-stream certificate association changed");exerciseSmall(loaded,literal);
        });
        cases.run("copied_duplicate_path_digest_indices","serialized_certificate_boundary",[&] {
            auto bytes=literal.bytes;replacePath(bytes,2,"certificate/alpha.snu");std::copy_n(bytes.begin()+76+32,32,bytes.begin()+76+2*64+32);
            const auto loaded=Catalog::load(cases.save("copied_duplicate_path_digest_indices",bytes));const auto a=loaded.candidateIdentity(0),b=loaded.candidateIdentity(2);
            need(a && b && a->sourcePath==b->sourcePath && a->sourceSha256==b->sourceSha256 && a->streamIndex==0 && b->streamIndex==2 && a->sourceIndex==0 && b->sourceIndex==2,"Duplicate metadata certificates were deduplicated");
            compareCertificate(*a,bytes,0);compareCertificate(*b,bytes,2);exerciseSmall(loaded,literal);
        });
        cases.run("digest_mutation_is_certificate_only","serialized_certificate_boundary",[&] {
            const auto originalPath=cases.save("digest_mutation_before",literal.bytes);const auto before=Catalog::load(originalPath);
            auto bytes=literal.bytes;bytes[76+32+17]^=0x80;const auto changed=Catalog::load(cases.save("digest_mutation_after",bytes));
            const auto a=before.candidateIdentity(0),b=changed.candidateIdentity(0);need(a && b && a->sourceSha256!=b->sourceSha256,"Digest mutation did not change copied certificate");
            auto expected=*a;expected.sourceSha256[17]^=0x80;need(equalCertificate(expected,*b),"Digest mutation changed non-digest identity");compareCertificate(*b,bytes,0);exerciseSmall(before,literal);exerciseSmall(changed,literal);
            std::fill_n(bytes.begin()+76+32,32,0);const auto zero=Catalog::load(cases.save("digest_all_zero",bytes));const auto z=zero.candidateIdentity(0);
            need(z && z->sourceSha256==std::array<uint8_t,32>{},"Serialized zero digest acquired a new validation rule");compareCertificate(*z,bytes,0);exerciseSmall(zero,literal);
        });
        cases.run("shipped_alias_order_duplicates","serialized_certificate_boundary",[&] {
            need(aliases==std::vector<uint32_t>({9455,9463,9467}),"Complete original alias indices changed");
            std::vector<std::string> paths,digests;
            for(uint32_t index:aliases){const auto id=shipped.candidateIdentity(index);need(bool(id),"Original alias certificate missing");compareCertificate(*id,original,index);paths.push_back(id->sourcePath);digests.push_back(digestHex(id->sourceSha256));}
            need(paths[0]!=paths[1] && paths[0]!=paths[2] && paths[1]!=paths[2] && digests[0]!=digests[1] && digests[0]!=digests[2] && digests[1]!=digests[2],"Original full aliases lost distinct source certificates");
            const std::array<uint32_t,4> prior{{9467,9455,9467,9463}};const auto last=block(root/"audiostreams/yr_xxx_0/d_mayr_xxx_0006241.exa.snu",uint32_t(sequence-1));
            const auto repeat=shipped.match(last.header,prior,sequence,last.bytes);
            need(repeat.candidates==std::vector<uint32_t>(prior.begin(),prior.end()) && shipped.complete(repeat.candidates,sequence),"Accessor/order calls changed original duplicate aliases");
        });
        cases.run("original_matching_noninterference","serialized_certificate_boundary",[&] {
            cases.observations.push_back("control_scope=fixed_original_host_matching_not_original_reader_setup");
            const std::array<OriginalBlock,4> originals{{block(root/"audiostreams/mr_xxx_0/d_homr_xxx_0000afe.exa.snu",0),block(root/"audiostreams/cb_xxx_0/d_chcb_xxx_0006700.exa.snu",0),block(root/"audiostreams/amb_airc/moh_amb_aircraft_carrier_qd.exa.snu",0),block(root/"audiostreams/amb_airc/moh_amb_aircraft_carrier_qd.exa.snu",1)}};
            const auto a=shipped.match(originals[0].header,{},1,originals[0].bytes),b=shipped.match(originals[1].header,{},1,originals[1].bytes),c=shipped.match(originals[2].header,{},1,originals[2].bytes),d=shipped.match(originals[3].header,c.candidates,2,originals[3].bytes);
            for(uint32_t index:{0u,1253u,3810u,3811u,9469u})need(bool(shipped.candidateIdentity(index)),"Noninterference valid accessor missing");
            for(uint32_t index:{9470u,(std::numeric_limits<uint32_t>::max)()})need(!shipped.candidateIdentity(index),"Noninterference invalid accessor admitted");
            const auto aa=shipped.match(originals[0].header,{},1,originals[0].bytes),bb=shipped.match(originals[1].header,{},1,originals[1].bytes),cc=shipped.match(originals[2].header,{},1,originals[2].bytes),dd=shipped.match(originals[3].header,cc.candidates,2,originals[3].bytes);
            need(a.candidates==aa.candidates && b.candidates==bb.candidates && c.candidates==cc.candidates && d.candidates==dd.candidates && shipped.complete(aliases,sequence),"Copied metadata queries altered original matching/completion");
        });
    } else {
        // Dependent lookup leaves the pre-API baseline compileable. This is a
        // feature diagnostic after every original and independent loader case,
        // not a fabricated accessor or a valid-original-resource rejection.
        cases.run("metadata_accessor_presence","feature_presence",[&]{throw XmaSourceError("Audio catalog metadata accessor missing");});
    }
    cases.finish(present);
}
}
int main(int argc,char** argv) {
    try {
        need(argc==3,"Expected catalog and original asset root");
        {
            // This identifies the input catalog bytes, not any asserted source file.
            const auto bytes=catalogFile(argv[1]);
            const auto path=std::filesystem::absolute(argv[1]).lexically_normal().generic_string();
            const auto receipt=std::string("{\"schema\":1,\"kind\":\"original_catalog_load\",\"phase\":\"before_validation\",\"name\":\"original_catalog_load\"")+
                ",\"classification\":\"serialized_certificate_boundary\",\"caller\":\"main\",\"helper\":\"AudioCatalog::load\""+
                ",\"mission\":\"offline_catalog_certificate\",\"last_action\":\"catalog_bytes_read_before_load\""+
                ",\"ownership\":\"catalog_input_buffer_owned_target_unloaded\",\"scope\":\"host_catalog_fixture\""+
                ",\"source_identity\":\"serialized_certificate_only\",\"identity_scope\":\"catalog_file_only\",\"path\":"+jsonText(path)+
                ",\"sha256\":"+jsonText(digestHex(catalogDigest(bytes)))+",\"bytes\":"+std::to_string(bytes.size())+"}";
            need(std::printf("AUDIT_AUDIO_CATALOG_ORIGINAL_PREVALIDATION %s\n",receipt.c_str())>=0,
                 "Fixture original catalog prevalidation receipt write failed");
            need(std::fflush(stdout)==0 && !std::ferror(stdout),"Fixture original catalog prevalidation receipt flush failed");
        }
        const auto catalog=AudioCatalog::load(argv[1]);
        need(catalog.sourceCount()==7430 && catalog.streamCount()==9470 && catalog.blockCount()==377643,
             "Complete original SNU/MUS stream inventory changed");
        const auto root=std::filesystem::path(argv[2]);
        const auto homer=block(root/"audiostreams/mr_xxx_0/d_homr_xxx_0000afe.exa.snu",0);
        const auto first=catalog.match(homer.header,{},1,homer.bytes);
        need(!first.candidates.empty() && first.block.bytes==homer.bytes.size(),
             "Recorded Homer reaction is absent from full catalog");
        const auto parsed=parseCatalogEaXmaBlock(homer.bytes,1,first.block);
        need(parsed.declaredFrames==first.block.frames && parsed.layers.size()==1,
             "Catalog selected a different original XMA block");
        // The later keyboard recording stopped at scene 1174 on this exact
        // 1,562-byte reader-owned block (SHA256 2e06ecc6...).
        const auto crash=block(root/"audiostreams/cb_xxx_0/d_chcb_xxx_0006700.exa.snu",0);
        const auto admitted=catalog.match(crash.header,{},1,crash.bytes);
        need(!admitted.candidates.empty() && admitted.block.bytes==1562 &&
             parseCatalogEaXmaBlock(crash.bytes,1,admitted.block).declaredFrames==4736,
             "Previously rejected chocolate-rabbit dialogue is not admitted");
        const auto ambient=block(root/"audiostreams/amb_airc/moh_amb_aircraft_carrier_qd.exa.snu",0);
        const auto candidates=catalog.match(ambient.header,{},1,ambient.bytes);
        need(candidates.candidates.size()>1,"Same-header/first-block collision disappeared");
        const auto ambientSecond=block(root/"audiostreams/amb_airc/moh_amb_aircraft_carrier_qd.exa.snu",1);
        const auto narrowed=catalog.match(ambient.header,candidates.candidates,2,ambientSecond.bytes);
        need(!narrowed.candidates.empty() && narrowed.candidates.size()<candidates.candidates.size(),
             "Ordered second block did not narrow ambiguous stream candidates");
        const auto loopPath=root/"audiostreams/80b_crow/amb_80b_crowd_qd_01.exa.snu";
        const auto loopIntro=block(loopPath,0);
        const auto* loopStream=catalog.findHeader(loopIntro.header);
        need(loopStream && loopStream->loop && loopStream->loopStartSample==1 &&
             loopStream->loopOffsetRelative==loopIntro.bytes.size(),
             "Original streamed loop introduction/seek offset is not cataloged");
        const auto intro=catalog.match(loopIntro.header,{},1,loopIntro.bytes);
        need(intro.block.frames==1 && parseCatalogEaXmaBlock(loopIntro.bytes,4,intro.block).layers.size()==2,
             "Original four-channel loop introduction was not admitted");
        const auto loopBody=block(loopPath,1);
        const auto body=catalog.match(loopIntro.header,intro.candidates,2,loopBody.bytes);
        need(body.block.frames==4736 && parseCatalogEaXmaBlock(loopBody.bytes,4,body.block).layers.size()==2,
             "Original four-channel loop body start was not admitted");
        const auto replay=catalog.match(loopIntro.header,body.candidates,2,loopBody.bytes);
        need(replay.candidates==body.candidates && replay.block.frames==body.block.frames,
             "Original loop body block cannot be matched again after a reader seek");
        const auto decodeLoopBody=[&]() {
            NativeXmaFactory factory;
            const std::array<XmaFormat,2> formats{{{2,48000,XmaVariant::Xma2},
                                                   {2,48000,XmaVariant::Xma2}}};
            XmaSource decoder(factory,1,formats,{});
            XmaSource::Segment segment;
            segment.sequence=2;segment.slot=0;segment.declaredFrames=body.block.frames;
            segment.continuity=XmaSource::Continuity::FreshContext;
            segment.layers=parseCatalogEaXmaBlock(loopBody.bytes,4,body.block).layers;
            for(auto& layer:segment.layers) layer.skipFrames=384;
            const auto accepted=decoder.prepare(segment);
            need(accepted.status==XmaSource::PrepareStatus::Accepted,
                 "Original loop body could not start a fresh native decoder");
            const auto staged=decoder.stage(accepted.receipt,256);
            need(staged.status==XmaSource::StageStatus::Complete && bool(staged.ticket),
                 "Original loop body fresh decoder did not produce a bounded quota");
            std::array<std::vector<float>,4> pcm;
            for(uint32_t channel=0;channel<pcm.size();++channel) {
                const auto plane=staged.ticket->plane(channel);
                pcm[channel].assign(plane.begin(),plane.end());
            }
            return pcm;
        };
        const auto firstBodyPcm=decodeLoopBody(),replayedBodyPcm=decodeLoopBody();
        for(uint32_t channel=0;channel<firstBodyPcm.size();++channel)
            need(firstBodyPcm[channel].size()==256 && replayedBodyPcm[channel].size()==256 &&
                 std::memcmp(firstBodyPcm[channel].data(),replayedBodyPcm[channel].data(),
                             256*sizeof(float))==0,
                 "Original four-channel loop body PCM changed after a fresh-context replay");
        const auto aliasPath=root/"audiostreams/yr_xxx_0/d_mayr_xxx_0006241.exa.snu";
        const auto aliasFirst=block(aliasPath,0);
        const auto* aliasStream=catalog.findHeader(aliasFirst.header);
        need(aliasStream && !aliasStream->loop,"Known repeated dialogue header was not cataloged");
        std::vector<uint32_t> aliases;uint64_t frames=0,sequence=0;
        while(frames<aliasStream->frames) {
            const auto original=block(aliasPath,uint32_t(sequence));
            auto next=catalog.match(original.header,aliases,++sequence,original.bytes);
            frames+=next.block.frames;aliases=std::move(next.candidates);
        }
        need(frames==aliasStream->frames && aliases.size()>1 && catalog.complete(aliases,sequence),
             "Identical audio aliases did not survive exact ordered completion");
        std::printf("PASS audio catalog: %zu sources, %zu streams, %zu blocks; recorded rejection admitted; collision %zu to %zu\n",
                    catalog.sourceCount(),catalog.streamCount(),catalog.blockCount(),
                    candidates.candidates.size(),narrowed.candidates.size());
        std::printf("AUDIT_AUDIO_CATALOG_ORIGINAL_CHECKS matching=passed loop_decoder_replay=passed ordered_alias_completion=passed scope=existing_original_checks\n");
        catalogMetadataRegression(catalog,argv[1],root,aliases,sequence);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"FAIL audio catalog: %s\n",error.what());return 1;}
}
