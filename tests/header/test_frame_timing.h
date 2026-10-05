void frameTimingModes() {
    using Simpsons::FrameTiming;
    LARGE_INTEGER frequency{};need(QueryPerformanceFrequency(&frequency)!=0,"Frame timing test clock unavailable");
    const auto readClock=[] {LARGE_INTEGER v{};QueryPerformanceCounter(&v);return v.QuadPart;};
    const auto fields=[](const std::string& row) {
        std::vector<std::string> result;std::istringstream input(row);std::string field;
        while(std::getline(input,field,','))result.push_back(field);return result;
    };
    for(bool detailed:{false,true}) {
        const auto path=std::filesystem::temp_directory_path()/
            ("simpsons-frame-timing-"+std::to_string(GetCurrentProcessId())+(detailed?"-detailed.csv":"-frames.csv"));
        need(!std::filesystem::exists(path),"Frame timing fixture path already exists");
        struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove(path,error);}} cleanup{path};
        int64_t firstBefore{},firstAfter{},lastBefore{},lastAfter{};
        {
            FrameTiming timing(path,detailed);
            const auto saved=PPCFPSCRRegister::getcsr();
            struct Restore {uint32_t csr;~Restore(){PPCFPSCRRegister::restoreHostCSR(csr);}} restore{saved};
            constexpr uint32_t csr=0xDFBF;constexpr DWORD error=0x713abcde;
            firstBefore=readClock();PPCFPSCRRegister::restoreHostCSR(csr);SetLastError(error);
            timing.frame(119,true);
            need(GetLastError()==error && PPCFPSCRRegister::getcsr()==csr,"Frame timing changed caller error/FP state");firstAfter=readClock();
            {
                auto scope=timing.measure(FrameTiming::Program);Sleep(12);scope.finish();scope.finish();
                need(GetLastError()==error && PPCFPSCRRegister::getcsr()==csr,"Diagnostic scope changed caller error/FP state");
            }
            if(!detailed) {
                // Summary mode must not touch pacing arguments or guest state.
                PPCContext context{};FrameTiming::beginPacing(context,nullptr);FrameTiming::endPacing();
            }
            lastBefore=readClock();SetLastError(error);timing.frame(120,false);
            need(GetLastError()==error && PPCFPSCRRegister::getcsr()==csr,"Flushed frame timing changed caller error/FP state");lastAfter=readClock();
        }
        std::ifstream input(path);std::string header,first,last,extra;
        need(bool(std::getline(input,header)) && bool(std::getline(input,first)) && bool(std::getline(input,last)) &&
            !std::getline(input,extra),"Frame timing omitted or invented presentation rows");
        const auto h=fields(header),a=fields(first),b=fields(last);const size_t count=detailed?16:4;
        need(h.size()==count && a.size()==count && b.size()==count,"Frame timing column count differs from measurement mode");
        need(h[0]=="presentation" && h[1]=="elapsed_ms" && h[2]=="frame_ms" && h.back()=="display_accepted",
            "Frame timing omitted actual interval/acceptance columns");
        need(a[0]=="119" && b[0]=="120" && a.back()=="1" && b.back()=="0","Frame timing changed actual presentation identity/acceptance");
        need(std::stod(a[1])==0 && std::stod(a[2])==0,"First frame fabricated an earlier interval");
        const double measured=std::stod(b[2]),factor=1000.0/double(frequency.QuadPart);
        need(measured>=(lastBefore-firstAfter)*factor-.00011 && measured<=(lastAfter-firstBefore)*factor+.00011,
            "Frame interval lies outside independent real clock brackets");
        need(std::stod(b[1])==measured,"Elapsed time differs from the two actual presentation timestamps");
        if(detailed)need(h[6]=="program_ms" && std::stod(b[6])>=10 && std::stod(b[6])<=measured,
            "Detailed timing stopped recording the measured scope");
        else need(header=="presentation,elapsed_ms,frame_ms,display_accepted","Unmeasured buckets were emitted as apparent measurements");
    }
    std::puts("PASS frame-only/detailed CSV / independent clock brackets / exact caller FP and error state");
}
