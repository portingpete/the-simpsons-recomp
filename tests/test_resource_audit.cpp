#include "runtime/resource_audit.h"
#include <chrono>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>
using namespace Simpsons;
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv) {try {
    need(argc==2,"Receipt directory required");
    const auto receiptDirectory=std::filesystem::path(argv[1]);
    const auto receiptStem="resource-audit-test-"+std::to_string(GetCurrentProcessId())+"-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto path=receiptDirectory/(receiptStem+".jsonl");
    for(unsigned collision=1;std::filesystem::exists(path);++collision)
        path=receiptDirectory/(receiptStem+"-"+std::to_string(collision)+".jsonl");
    {
        ResourceAudit audit;audit.configure(path);audit.mission("brt");audit.action("attack\n\"special\"");
        audit.observe("effect_pass","source:8200FB98",0x8273B4E0,"stride=80","unvalidated",12,"owner=123");
        audit.observe("effect_pass","source:8200FB98",0x8273B4E0,"stride=80","unvalidated",13,"owner=456");
        audit.failure("invalid\tflags");
        std::thread worker([&]{
            audit.observe("audio_reader","ring",0x8238C678,"ring=0","allocator-pending",0,"stream=987");
            audit.failure("worker rejection");
        });worker.join();
        audit.failure("main remains associated with scene");
        audit.observe("effect_pass","source:8200FB98",0x82701648,"shader=65 stride=80","selected",13);
        audit.observe("texture","builtin:reflection",0x826FF140,"width=16 height=16","allocated",13);
        audit.failure("texture rejection retains original scene owner");
        audit.observe("effect_pass","source:8200FB98",0x8273B4E0,"stride=80","released",14,"owner=456");
        audit.mission("loc");
        audit.observe("effect_pass","source:8200FB98",0x8273B4E0,"stride=80","released",15);
        audit.action(std::string("reload:")+char(0x80)+char(0xff));
        audit.observe("effect_pass","source:8200FB98",0x8273B4E0,"stride=80","released",16);
        audit.failure("Native runtime shutdown");audit.failure("Native window closed");
        audit.lifecycle("map_ready","tree_hugger/tree_hugger.str",0x823BB5D8,"checkpoint=zero","original-ready",17,"owner=111 generation=1");
        audit.lifecycle("map_ready","tree_hugger/tree_hugger.str",0x823BB5D8,"checkpoint=zero","original-ready",18,"owner=111 generation=2");
        audit.failure("latest lifetime owner");
    }
    std::ifstream input(path);std::string line;std::vector<std::string> rows;
    while(std::getline(input,line))rows.push_back(line);
    need(rows.size()==16,"Combination dedup or lifecycle/mission/action grouping differs");
    need(rows[0].find("attack\\u000a\\\"special\\\"")!=std::string::npos,"JSON control/quote escaping differs");
    need(rows[1].find("owner=456")!=std::string::npos&&rows[1].find("\"scene\":13")!=std::string::npos,"Duplicate lost latest pre-validation snapshot");
    need(rows[3].find("worker rejection")!=std::string::npos&&rows[3].find("stream=987")!=std::string::npos,"Worker failure lost its own encounter");
    need(rows[2].find("\"scene_context\":\"\"")!=std::string::npos&&rows[3].find("\"scene_context\":\"\"")!=std::string::npos,"Worker inherited a different thread's scene context");
    need(rows[4].find("source:8200FB98")!=std::string::npos,"Worker encounter contaminated main thread failure");
    for(unsigned index:{1u,4u,5u,6u,7u,8u,9u,10u})
        need(rows[index].find("\"scene_context\":\"owner=456\"")!=std::string::npos,"Latest original owner was lost after a duplicate, selected pass or texture encounter");
    need(rows[5].find("\"instance\":\"\"")!=std::string::npos&&rows[6].find("\"instance\":\"\"")!=std::string::npos,"Selected pass or texture introduced a fabricated instance");
    need(rows[7].find("\"kind\":\"texture\"")!=std::string::npos&&rows[7].find("texture rejection")!=std::string::npos,"Failure lost the latest texture encounter");
    need(rows[8].find("released")!=std::string::npos&&rows[9].find("\"mission\":\"loc\"")!=std::string::npos,"Owner/mission changes were deduplicated away");
    need(rows[10].find("\"last_action\":\"reload:\\u0080\\u00ff\"")!=std::string::npos,"Action-only change or high-byte JSON escaping was lost");
    for(unsigned index:{11u,12u})need(rows[index].find("\"event\":\"shutdown\"")!=std::string::npos&&
        rows[index].find("\"reason\":")!=std::string::npos&&rows[index].find("owner=456")!=std::string::npos,
        "Terminal shutdown was labelled a new rejection or lost context");
    for(unsigned index:{13u,14u})need(rows[index].find("\"event\":\"lifecycle\"")!=std::string::npos,
        "Repeated original lifetime boundary was deduplicated or mislabelled");
    need(rows[13].substr(rows[13].find("\"group\":"))==rows[14].substr(rows[14].find("\"group\":")),
        "Lifetime generation contaminated the stable group");
    need(rows[15].find("generation=2")!=std::string::npos&&rows[15].find("\"scene\":18")!=std::string::npos,
        "Repeated lifetime boundary did not refresh the latest snapshot");
    for(const auto& row:rows)for(unsigned char byte:row)
        need(byte>=32&&byte<128,"Receipt contains unescaped control or high bytes");
    // The context epoch lets callers cache "already observed" keys: it must advance exactly when the
    // mission or last action VALUE changes, and never when the same value is merely re-announced.
    {
        ResourceAudit audit;const auto start=audit.epoch();
        audit.action("held");need(audit.epoch()==start+1,"A new action did not advance the context epoch");
        for(unsigned repeat=0;repeat<3;++repeat)audit.action("held");
        need(audit.epoch()==start+1,"Re-announcing the same action advanced the context epoch");
        audit.mission("loc");need(audit.epoch()==start+2,"A new mission did not advance the context epoch");
        audit.mission("loc");need(audit.epoch()==start+2,"Re-announcing the same mission advanced the context epoch");
        audit.action("released");need(audit.epoch()==start+3,"A changed action did not advance the context epoch");
    }    // Diagnostic events are append-only evidence. They must never become the
    // latest primary encounter, scene owner or dedup entry for a later failure.
    auto diagPath=receiptDirectory/(receiptStem+"-diagnostic.jsonl");
    for(unsigned collision=1;std::filesystem::exists(diagPath);++collision)
        diagPath=receiptDirectory/(receiptStem+"-diagnostic-"+std::to_string(collision)+".jsonl");
    {
        ResourceAudit audit;audit.configure(diagPath);audit.mission("diag");audit.action("primary-action");
        static_assert(noexcept(std::declval<ResourceAudit&>().diagnostic(std::declval<const std::string&>(),std::declval<const std::string&>(),
            uint32_t{},std::declval<const std::string&>(),std::declval<const std::string&>(),uint64_t{},
            std::declval<const std::string&>(),std::declval<const std::string&>())),"Diagnostic reporting must never throw");
        audit.observe("effect_pass","source:AAAA0000",0x1000,"stride=80","unvalidated",21,"owner=1");           // row 0: primary
        audit.action("capture-action");
        audit.diagnostic("rigid_capture","source:AAAA0000",0x2000,"phase=first_raw status=complete","diagnostic_only=1",99,"dir=a");       // row 1
        audit.diagnostic("rigid_capture","source:AAAA0000",0x2000,"phase=first_raw status=skipped","diagnostic_only=1",99,"dir=b","budget \"exceeded\"");  // row 2
        audit.diagnostic("rigid_capture","source:AAAA0000",0x2000,"phase=first_raw status=skipped","diagnostic_only=1",99,"dir=b","budget \"exceeded\"");  // row 3: identical, never deduplicated
        audit.diagnostic("rigid_capture","source:AAAA0000",0x2000,"phase=first_draw status=failed","diagnostic_only=1",99,"dir=c","Cannot write\n");     // row 4
        audit.failure("real primary failure");                                                                    // row 5: binds to row 0
        // A primary with the same combination as row 1 is still fresh: diagnostics never enter the dedup set.
        audit.observe("rigid_capture","source:AAAA0000",0x2000,"phase=first_raw status=complete","diagnostic_only=1",7,"dir=p");    // row 6: primary
        std::thread worker([&]{
            audit.diagnostic("rigid_capture","source:BBBB0000",0x3000,"phase=rejected_upload status=failed","diagnostic_only=1",0,"dir=w","worker diagnostic"); // row 7
            audit.failure("worker has no primary encounter");                                                     // row 8: unattributed
        });worker.join();
        audit.failure("main failure binds to the later primary");                                                 // row 9: binds to row 6
        ResourceAudit unconfigured;
        unconfigured.diagnostic("rigid_capture","source:CCCC0000",1,"p","o",0,"i","r");
        unconfigured.failure("not enabled");
    }
    std::ifstream diagInput(diagPath);std::vector<std::string> diag;
    while(std::getline(diagInput,line))diag.push_back(line);
    need(diag.size()==10,"Diagnostic append-only/dedup row count differs");
    const auto has=[&](unsigned index,const char* text){return diag[index].find(text)!=std::string::npos;};
    need(has(0,"\"event\":\"encounter\"")&&has(0,"\"kind\":\"effect_pass\""),"Primary row missing");
    uint64_t previousSequence=0;
    for(unsigned index:{1u,2u,3u,4u}) {
        need(has(index,"\"event\":\"diagnostic\"")&&has(index,"\"kind\":\"rigid_capture\""),"Diagnostic row has the wrong event or kind");
        need(has(index,"\"scene\":99")&&has(index,"\"last_action\":\"capture-action\""),"Diagnostic lost its own caller-supplied scene or the current action");
        need(has(index,"\"scene_context\":\"owner=1\""),"Diagnostic did not copy the current scene context");
        const auto at=diag[index].find("\"sequence\":");need(at!=std::string::npos,"Diagnostic sequence missing");
        const auto sequence=std::stoull(diag[index].substr(at+11));need(sequence>previousSequence,"Diagnostic sequence is not increasing");previousSequence=sequence;
    }
    need(has(2,"\"reason\":\"budget \\\"exceeded\\\"\"")&&has(4,"\"reason\":\"Cannot write\\u000a\""),"Diagnostic reason was not preserved or escaped");
    // Rows 2 and 3 carry identical combinations; both being present proves diagnostics are never deduplicated.
    need(has(3,"\"reason\":\"budget \\\"exceeded\\\"\"")&&has(3,"\"instance\":\"dir=b\""),"Identical diagnostic was deduplicated away");
    // The real failure keeps the primary tuple, scene and action despite four intervening diagnostics.
    need(has(5,"\"event\":\"failure\"")&&has(5,"\"kind\":\"effect_pass\"")&&has(5,"\"asset\":\"source:AAAA0000\"")&&has(5,"\"caller\":4096")&&
         has(5,"\"scene\":21")&&has(5,"\"instance\":\"owner=1\"")&&has(5,"\"last_action\":\"primary-action\"")&&!has(5,"rigid_capture"),
         "A diagnostic replaced the latest primary encounter");
    need(diag[5].substr(diag[5].find("\"group\":"),diag[5].find(",\"reason\":")-diag[5].find("\"group\":"))==
         diag[0].substr(diag[0].find("\"group\":"),diag[0].size()-1-diag[0].find("\"group\":")),
         "The failure's group differs from its primary encounter's stable group");
    // Same combination as a diagnostic, but a primary: it is a fresh encounter, not suppressed.
    need(has(6,"\"event\":\"encounter\"")&&has(6,"\"kind\":\"rigid_capture\"")&&has(6,"\"instance\":\"dir=p\"")&&has(6,"\"scene\":7"),
         "A diagnostic suppressed a later primary with the same combination");
    need(has(7,"\"event\":\"diagnostic\"")&&has(7,"worker diagnostic"),"Worker diagnostic row differs");
    need(has(8,"\"event\":\"failure\"")&&has(8,"\"kind\":\"unattributed\"")&&!has(8,"rigid_capture"),
         "A worker diagnostic became that thread's primary encounter");
    need(has(9,"\"event\":\"failure\"")&&has(9,"\"instance\":\"dir=p\"")&&has(9,"\"scene\":7")&&has(9,"main failure binds"),
         "Main failure bound to a diagnostic or another thread's encounter instead of its latest primary");
    for(const auto& row:diag)for(unsigned char byte:row)
        need(byte>=32&&byte<128,"Diagnostic receipt contains unescaped control or high bytes");
    std::cout<<"PASS pre-validation receipts / dedup / escaping / independent worker context / inherited scene owner / ownership, mission and action changes / non-attributing diagnostics: "<<path<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
