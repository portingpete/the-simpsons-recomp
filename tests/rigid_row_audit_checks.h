// Shared verification of native per-row prevalidation receipts (kind
// effect_producer_row, boundary rigid_row_prevalidation). Included INSIDE a rigid
// fixture's anonymous namespace after need/auditString/auditWord/auditHex.
// The observer copies every raw row word before any offset/header/collection read;
// these checks reconstruct that tuple from the receipts alone.
struct RigidRowCase {
    uint32_t poolFirst{},poolRow{},poolBytes{};uint64_t poolGeneration{};
    uint32_t count{},ordinal{},variant{};std::array<uint32_t,9> words{};
    const char* action{};const char* event{};const char* reason{};
};
inline std::vector<std::string> rigidRowAuditRows(const std::filesystem::path& path) {
    std::ifstream input(path);need(bool(input),"Preserved row prevalidation audit log is missing");
    std::vector<std::string> result;std::string line;
    while(std::getline(input,line))
        if(line.find("\"kind\":\"effect_producer_row\"")!=std::string::npos&&line.find("boundary=rigid_row_prevalidation ")!=std::string::npos)
            result.push_back(line);
    return result;
}
inline std::string rigidRowWords(const std::array<uint32_t,9>& words) {
    std::string text;for(size_t i=0;i<words.size();++i){if(i)text+=',';text+=auditHex(words[i]);}return text;
}
inline void verifyRigidRows(const std::vector<std::string>& rows,const std::vector<RigidRowCase>& expected,const char* mission,
                            uint32_t sourceId,uint32_t metadataAddress,uint32_t objectAddress,uint32_t typedOwner,
                            uint32_t collectionCount,uint32_t collectionAddress) {
    need(rows.size()==expected.size(),"Row prevalidation receipts missing, repeated or unexpected");
    std::set<std::string> groups;
    for(size_t i=0;i<expected.size();++i) {
        const auto& e=expected[i];const auto& line=rows[i];
        need(auditString(line,"event")==e.event&&auditString(line,"kind")=="effect_producer_row"&&auditString(line,"asset")=="source:"+auditHex(sourceId)&&
             line.find("\"caller\":"+std::to_string(0x827402B0u)+",")!=std::string::npos&&auditString(line,"mission")==mission&&
             auditString(line,"last_action")==e.action,"Row receipt lost its source/caller/mission/action");
        const auto parameters=auditString(line,"parameters"),ownership=auditString(line,"ownership"),instance=auditString(line,"instance");
        need(auditWord(parameters,"boundary","rigid_row_prevalidation")&&auditWord(parameters,"source",auditHex(sourceId))&&
             auditWord(parameters,"requested_technique","00000000")&&auditWord(parameters,"submesh_count",std::to_string(e.count))&&
             auditWord(parameters,"bones","0")&&auditWord(parameters,"collection_count",std::to_string(collectionCount))&&
             auditWord(parameters,"variant",std::to_string(e.variant))&&auditWord(parameters,"vfx","0")&&
             auditWord(parameters,"rw_selector",auditHex(e.words[0]))&&auditWord(parameters,"compiled_index",std::to_string(e.words[1]))&&
             auditWord(parameters,"primitive",std::to_string(e.words[3]))&&auditWord(parameters,"base_vertex",std::to_string(int32_t(e.words[4])))&&
             auditWord(parameters,"start_index",std::to_string(e.words[5]))&&auditWord(parameters,"index_count",std::to_string(e.words[6]))&&
             auditWord(parameters,"authored_group_count",std::to_string(e.words[7]))&&auditWord(parameters,"header_state","not-read-before-admission"),
             "Row receipt lost its source-proven consumed scalar fields");
        need(auditWord(ownership,"admission","unvalidated")&&auditWord(ownership,"runtime_match","1")&&auditWord(ownership,"thread_match","1")&&
             auditWord(ownership,"cpu_match","1")&&auditWord(ownership,"submesh_owner","observed-live")&&
             auditWord(ownership,"consumed","w0,w1,w3,w4,w5,w6")&&auditWord(ownership,"authored","w7")&&auditWord(ownership,"unclassified","w2,w8"),
             "Row receipt lost its unvalidated owner state or field-role labels");
        const uint32_t rowAddress=e.poolRow+36*e.ordinal;
        need(auditWord(instance,"row_ordinal",std::to_string(e.ordinal))&&auditWord(instance,"row_address",auditHex(rowAddress))&&
             auditWord(instance,"row_offset",std::to_string(36ull*e.ordinal))&&auditWord(instance,"words",rigidRowWords(e.words))&&
             auditWord(instance,"metadata",auditHex(metadataAddress))&&auditWord(instance,"table",auditHex(e.poolRow))&&
             auditWord(instance,"collection",auditHex(collectionAddress))&&auditWord(instance,"r3",auditHex(metadataAddress))&&
             auditWord(instance,"r4",auditHex(objectAddress))&&auditWord(instance,"r5",auditHex(typedOwner))&&auditWord(instance,"r7",auditHex(e.count))&&
             auditWord(instance,"submesh_owner_base",auditHex(e.poolFirst))&&auditWord(instance,"submesh_owner_extent",std::to_string(e.poolBytes))&&
             auditWord(instance,"submesh_owner_generation",std::to_string(e.poolGeneration)),
             "Row receipt hid the complete nine-word tuple, row address/ordinal or logical owner");
        const auto group=auditString(line,"group");
        for(const char* leaked:{"row_ordinal=","row_address=","row_offset=","words=","submesh_owner_base=","submesh_owner_generation=","table="})
            need(group.find(leaked)==std::string::npos,"Row instance data leaked into the stable combination group");
        if(std::string_view(e.event)=="failure") {
            need(i&&auditString(line,"reason")==e.reason,"Row failure lost its exact frontier");
            for(const char* key:{"group","parameters","ownership","instance","kind","asset","last_action"})
                need(auditString(line,key)==auditString(rows[i-1],key),"Row failure was not bound to its own latest attempted row");
        } else need(groups.insert(group).second,"An identical row combination was emitted twice");
    }
}
