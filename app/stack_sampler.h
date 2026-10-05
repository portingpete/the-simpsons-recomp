#pragma once
// Opt-in, diagnostic-only in-process stack sampler.
//
// SIMPSONS_STACK_SAMPLE=<output.txt>[,delay_s[,duration_s[,threads]]]
// Suspends one of the busiest threads every ~1 ms, reads its return-address
// chain with RtlVirtualUnwind while it is stopped (no allocation, fixed
// buffers), resumes it, and at the end writes raw samples as
// "S <tid> <module-index>:<rva> ..." lines for offline symbolization with
// tools/stack_profile.py. Sampling pauses distort FPS; use it for ratios only.
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib,"winmm.lib")
#include <tlhelp32.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace StackSampler {

struct Sample {
    DWORD tid;
    uint32_t depth;
    uint64_t pc[40];
};

inline void run(std::string spec) {
    std::string path=spec;double delay=30,duration=10;unsigned threadCount=1;
    if(auto comma=spec.find(',');comma!=std::string::npos) {
        path=spec.substr(0,comma);
        sscanf(spec.c_str()+comma+1,"%lf,%lf,%u",&delay,&duration,&threadCount);
    }
    Sleep(DWORD(delay*1000));
    timeBeginPeriod(1);
    const DWORD self=GetCurrentThreadId(),pid=GetCurrentProcessId();
    struct Candidate {DWORD tid;HANDLE handle;ULONGLONG cpu;};
    std::vector<Candidate> candidates;
    if(HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);snap!=INVALID_HANDLE_VALUE) {
        THREADENTRY32 entry{sizeof(entry)};
        for(BOOL ok=Thread32First(snap,&entry);ok;ok=Thread32Next(snap,&entry)) {
            if(entry.th32OwnerProcessID!=pid||entry.th32ThreadID==self)continue;
            HANDLE h=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,entry.th32ThreadID);
            if(!h)continue;
            FILETIME create,exit,kernel,user;
            if(!GetThreadTimes(h,&create,&exit,&kernel,&user)){CloseHandle(h);continue;}
            const auto cpu=(ULONGLONG(kernel.dwHighDateTime)<<32|kernel.dwLowDateTime)+(ULONGLONG(user.dwHighDateTime)<<32|user.dwLowDateTime);
            candidates.push_back({entry.th32ThreadID,h,cpu});
        }
        CloseHandle(snap);
    }
    std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){return a.cpu>b.cpu;});
    if(candidates.size()>threadCount)candidates.resize(threadCount);
    std::vector<Sample> samples;samples.reserve(size_t(duration*1000)+1024);
    std::vector<std::pair<uint64_t,std::string>> modules;
    const ULONGLONG end=GetTickCount64()+ULONGLONG(duration*1000);
    size_t next=0;
    while(GetTickCount64()<end&&!candidates.empty()) {
        const auto& target=candidates[next++%candidates.size()];
        Sample sample{target.tid,0,{}};
        if(SuspendThread(target.handle)==DWORD(-1))continue;
        CONTEXT context{};context.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;
        if(GetThreadContext(target.handle,&context)) {
            for(unsigned frame=0;frame<40&&context.Rip;++frame) {
                sample.pc[sample.depth++]=context.Rip;
                DWORD64 base=0;
                auto* entry=RtlLookupFunctionEntry(context.Rip,&base,nullptr);
                if(!entry){
                    if(!context.Rsp)break;
                    context.Rip=*reinterpret_cast<DWORD64*>(context.Rsp);context.Rsp+=8;
                    continue;
                }
                void* handlerData=nullptr;DWORD64 frameBase=0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,context.Rip,entry,&context,&handlerData,&frameBase,nullptr);
            }
        }
        ResumeThread(target.handle);
        if(sample.depth)samples.push_back(sample);
        Sleep(1);
    }
    FILE* out=fopen(path.c_str(),"w");
    if(!out)return;
    auto moduleIndex=[&](uint64_t pc,uint64_t& rva)->int {
        void* base=nullptr;
        if(!RtlPcToFileHeader(reinterpret_cast<void*>(pc),&base)||!base){rva=pc;return -1;}
        rva=pc-reinterpret_cast<uint64_t>(base);
        for(size_t i=0;i<modules.size();++i)if(modules[i].first==reinterpret_cast<uint64_t>(base))return int(i);
        char name[MAX_PATH]{};GetModuleFileNameA(reinterpret_cast<HMODULE>(base),name,MAX_PATH);
        modules.push_back({reinterpret_cast<uint64_t>(base),name});
        return int(modules.size()-1);
    };
    std::vector<std::string> lines;
    for(const auto& sample:samples) {
        std::string line="S "+std::to_string(sample.tid);
        for(uint32_t i=0;i<sample.depth;++i) {
            uint64_t rva=0;const int module=moduleIndex(sample.pc[i],rva);
            char text[48];snprintf(text,sizeof(text)," %d:%llx",module,(unsigned long long)rva);line+=text;
        }
        lines.push_back(std::move(line));
    }
    for(size_t i=0;i<modules.size();++i)fprintf(out,"M %zu %s\n",i,modules[i].second.c_str());
    for(const auto& line:lines)fprintf(out,"%s\n",line.c_str());
    fclose(out);
}

inline void startFromEnvironment() {
    if(const char* spec=getenv("SIMPSONS_STACK_SAMPLE");spec&&*spec)
        std::thread(run,std::string(spec)).detach();
}

}  // namespace StackSampler
