void threadPlacementContracts(Simpsons::Runtime& rt,size_t baseRegions) {
    using Simpsons::HostProcessorCore;using Simpsons::selectThreadProcessor;
    std::vector<HostProcessorCore> hybrid;
    for(unsigned i=0;i<8;++i)hybrid.push_back({uintptr_t(3)<<(i*2),1});
    for(unsigned i=16;i<32;++i)hybrid.push_back({uintptr_t(1)<<i,0});
    require(selectThreadProcessor(0xffffffffu,0,hybrid)==0x4000,"Main CPU did not select the last separate performance core");
    for(unsigned cpu=1;cpu<6;++cpu)
        require(selectThreadProcessor(0xffffffffu,cpu,hybrid)==uintptr_t(1)<<cpu,"Other guest CPU host mappings changed");
    require(selectThreadProcessor(0x3fffffffu & ~uintptr_t(0xc000),0,hybrid)==0x1000,"Restricted performance core was selected");
    require(selectThreadProcessor(0x803fu,0,hybrid)==0x8000,"Allowed sibling on a separate core was ignored");
    require(selectThreadProcessor(0x3fu,0,hybrid)==1,"Main CPU moved onto another guest CPU's sibling");
    require(selectThreadProcessor(0xff00000fu,0,hybrid)==1,"Free efficiency cores displaced available performance cores");
    require(selectThreadProcessor(0xffffffffu,0,{})==1,"Unavailable topology changed the original mapping");
    const std::array<HostProcessorCore,4> uniform={{{3,0},{12,0},{48,0},{192,0}}};
    require(selectThreadProcessor(255,0,uniform)==64,"Uniform processor topology did not use its separate core");
    for(unsigned cpu=0;cpu<6;++cpu)
        require(selectThreadProcessor(5,cpu,uniform)==(cpu%2?4u:1u),"Sparse small affinity mask lost original modulo mapping");
    const std::array<HostProcessorCore,2> wide={{{63,0},{uintptr_t(3)<<60,1}}};
    require(selectThreadProcessor(63|(uintptr_t(3)<<60),0,wide)==uintptr_t(1)<<60,"High native processor bits were truncated");
    for(auto [allowed,cpu]:std::array<std::pair<uintptr_t,unsigned>,2>{{{0,0},{1,6}}}) {
        bool rejected=false;try{selectThreadProcessor(allowed,cpu,hybrid);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"Invalid processor selector input was accepted");
    }
    // Exercise the production assignment on a real suspended native thread.
    // Only host placement may change: both original CPU fields and its mask
    // must still publish the requested original logical CPU on every change.
    auto worker=create(rt,1);DWORD_PTR allowed{},system{};
    require(GetProcessAffinityMask(GetCurrentProcess(),&allowed,&system)!=0,"Host affinity test query failed");
    std::vector<DWORD_PTR> processors;
    for(unsigned bit=0;bit<sizeof(DWORD_PTR)*8;++bit)if(allowed&(DWORD_PTR(1)<<bit))processors.push_back(DWORD_PTR(1)<<bit);
    for(unsigned cpu=0;cpu<6;++cpu) {
        Simpsons::assignThreadAffinity(rt,worker.native,worker.pcr,worker.object,1u<<cpu);
        GROUP_AFFINITY affinity{};require(GetThreadGroupAffinity(worker.native,&affinity)!=0,"Selected host affinity query failed");
        // Soft placement: the chosen CPU is the thread's ideal processor and the thread may migrate
        // among allowed performance cores, so a busy neighbour cannot starve it for a quantum.
        require(affinity.Mask && !(affinity.Mask&~allowed),"Native affinity escapes the allowed host CPUs");
        PROCESSOR_NUMBER ideal{};require(GetThreadIdealProcessorEx(worker.native,&ideal)!=0,"Ideal processor query failed");
        const DWORD_PTR idealMask=DWORD_PTR(1)<<ideal.Number;
        require(ideal.Group==0 && (affinity.Mask&idealMask),"Ideal processor is outside the thread's host affinity");
        if(cpu)require(idealMask==processors[cpu%processors.size()],"Production changed another logical CPU's host assignment");
        require(*rt.pointer(worker.pcr+0x10c,1,false)==cpu && *rt.pointer(worker.object+0xbf,1,false)==cpu &&
            PPCLoadU32(rt.base,worker.pcr+0x110)==1u<<cpu,"Host placement changed original CPU identity or mask");
    }
    finish(rt,worker,true);rt.closeHandle(worker.handle);emptyRegistry(rt,baseRegions);
    puts("PASS separate performance-core selection / restricted masks / six original CPU identities on a real native thread");
}
