#pragma once
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Simpsons {
// Observation is deliberately separate from validation. Callers may supply
// unreadable/unknown identities; these receipts never authorize a resource.
class ResourceAudit {
    struct Encounter {
        std::string kind,asset,parameters,ownership,instance,mission,action,group,sceneContext;
        uint32_t caller{},thread{};
        uint64_t sequence{},scene{};
    };
    std::mutex mutex;
    std::ofstream file;
    std::atomic<bool> enabled{};
    // Per-encounter capture formats strings and takes a mutex on every guest
    // draw; only an explicit --resource-audit file opts into it. Without a
    // file the audit still reports failures and shutdown to stderr.
    std::atomic<bool> recording{};
    // Bumped whenever the mission or last action changes. Both are part of every stable
    // group, so a caller that caches "already observed" keys must discard them on a change.
    std::atomic<uint64_t> contextEpoch{1};
    bool failed{};
    uint64_t sequence{};
    std::string currentMission="unknown",lastAction="startup";
    std::unordered_set<std::string> seen;
    std::unordered_map<uint32_t,Encounter> last;
    std::unordered_map<uint32_t,std::string> lastScene;
    static std::string quote(const std::string& value) {
        std::string result="\"";
        for(unsigned char ch:value) {
            if(ch=='"'||ch=='\\') {result+='\\';result+=char(ch);}
            else if(ch<32||ch>=128) {char escaped[7];std::snprintf(escaped,sizeof(escaped),"\\u%04x",unsigned(ch));result+=escaped;}
            else result+=char(ch);
        }
        return result+'"';
    }
    void write(const std::string& line) {
        if(file.is_open()&&!failed) {
            file<<line<<'\n';file.flush();
            if(file)return;
            failed=true;
            std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] subsequent receipts go to stderr\n");
        }
        std::fprintf(stderr,"[RESOURCE AUDIT] %s\n",line.c_str());std::fflush(stderr);
    }
    static std::string row(const Encounter& e,const char* event,const std::string& reason={}) {
        std::ostringstream out;
        out<<"{\"schema\":1,\"event\":"<<quote(event)<<",\"sequence\":"<<e.sequence
           <<",\"thread\":"<<e.thread<<",\"scene\":"<<e.scene<<",\"kind\":"<<quote(e.kind)
           <<",\"asset\":"<<quote(e.asset)<<",\"caller\":"<<e.caller
           <<",\"parameters\":"<<quote(e.parameters)<<",\"ownership\":"<<quote(e.ownership)
           <<",\"instance\":"<<quote(e.instance)<<",\"mission\":"<<quote(e.mission)
           <<",\"scene_context\":"<<quote(e.sceneContext)
           <<",\"last_action\":"<<quote(e.action)<<",\"group\":"<<quote(e.group);
        if(!reason.empty())out<<",\"reason\":"<<quote(reason);
        return out.str()+"}";
    }
public:
    bool active() const noexcept {return recording.load(std::memory_order_relaxed);}
    uint64_t epoch() const noexcept {return contextEpoch.load(std::memory_order_acquire);}
    void configure(const std::filesystem::path& path={}) {
        std::lock_guard lock(mutex);
        if(enabled)throw std::runtime_error("Resource audit already configured");
        if(!path.empty()) {
            file.open(path,std::ios::out|std::ios::app);
            if(!file)throw std::runtime_error("Cannot open resource audit log");
        }
        enabled=true;recording=!path.empty();
    }
    void mission(const std::string& value) noexcept {try{std::lock_guard lock(mutex);if(currentMission!=value){currentMission=value;contextEpoch.fetch_add(1,std::memory_order_release);}}catch(...){std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] mission capture failed\n");}}
    void action(const std::string& value) noexcept {try{std::lock_guard lock(mutex);if(lastAction!=value){lastAction=value;contextEpoch.fetch_add(1,std::memory_order_release);}}catch(...){std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] action capture failed\n");}}
private:
    void capture(const std::string& kind,const std::string& asset,uint32_t caller,
                 const std::string& parameters,const std::string& ownership,
                 uint64_t scene,const std::string& instance,bool lifecycle) noexcept {
        try {
            std::lock_guard lock(mutex);if(!recording)return;
            Encounter e{kind,asset,parameters,ownership,instance,currentMission,lastAction,{},{},
                        caller,GetCurrentThreadId(),++sequence,scene};
            if(kind=="effect_pass"&&!instance.empty())lastScene[e.thread]=instance;
            if(const auto context=lastScene.find(e.thread);context!=lastScene.end())e.sceneContext=context->second;
            // Length-delimited JSON avoids ambiguous concatenated signatures.
            e.group="["+quote(e.kind)+","+quote(e.asset)+","+std::to_string(caller)+","+
                quote(e.parameters)+","+quote(e.ownership)+","+quote(e.mission)+","+quote(e.action)+"]";
            last[e.thread]=e;
            const bool fresh=seen.insert(e.group).second;
            if(fresh||lifecycle)write(row(e,lifecycle?"lifecycle":"encounter"));
        } catch(...) {std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] encounter capture failed\n");std::fflush(stderr);}
    }
public:
    void observe(const std::string& kind,const std::string& asset,uint32_t caller,
                 const std::string& parameters,const std::string& ownership,
                 uint64_t scene=0,const std::string& instance={}) noexcept {
        capture(kind,asset,caller,parameters,ownership,scene,instance,false);
    }
    // Repeated original lifetime boundaries remain distinct even when their
    // stable combination is identical. Instance identity is never a group key.
    void lifecycle(const std::string& kind,const std::string& asset,uint32_t caller,
                   const std::string& parameters,const std::string& ownership,
                   uint64_t scene=0,const std::string& instance={}) noexcept {
        capture(kind,asset,caller,parameters,ownership,scene,instance,true);
    }
    // Diagnostic evidence never replaces the last primary resource encounter
    // or scene owner. A subsequent primary failure keeps its original identity.
    void diagnostic(const std::string& kind,const std::string& asset,uint32_t caller,
                    const std::string& parameters,const std::string& ownership,
                    uint64_t scene=0,const std::string& instance={},const std::string& reason={}) noexcept {
        try {
            std::lock_guard lock(mutex);if(!recording)return;
            Encounter e{kind,asset,parameters,ownership,instance,currentMission,lastAction,{}, {},
                        caller,GetCurrentThreadId(),++sequence,scene};
            if(const auto context=lastScene.find(e.thread);context!=lastScene.end())e.sceneContext=context->second;
            e.group="["+quote(e.kind)+","+quote(e.asset)+","+std::to_string(caller)+","+
                quote(e.parameters)+","+quote(e.ownership)+","+quote(e.mission)+","+quote(e.action)+"]";
            // Always preserve this attempt, without writing last/lastScene/seen.
            write(row(e,"diagnostic",reason));
        }catch(...) {std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] diagnostic capture failed\n");std::fflush(stderr);}
    }
    void failure(const std::string& reason) noexcept {
        try {
            std::lock_guard lock(mutex);if(!enabled)return;
            const auto thread=GetCurrentThreadId();auto found=last.find(thread);
            Encounter e;
            if(found!=last.end())e=found->second;
            else {e.kind="unattributed";e.asset="unknown";e.ownership="unknown";e.thread=thread;
                  e.mission=currentMission;e.action=lastAction;e.group="unattributed";}
            const bool shutdown=reason=="Native runtime shutdown"||reason=="Native window closed";
            e.sequence=++sequence;write(row(e,shutdown?"shutdown":"failure",reason));
        } catch(...) {std::fprintf(stderr,"[RESOURCE AUDIT IO FAILURE] failure capture failed\n");std::fflush(stderr);}
    }
};
}
