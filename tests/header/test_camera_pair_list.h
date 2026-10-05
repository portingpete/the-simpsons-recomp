#pragma once
// Included inside the viewport fixture namespace after need/rejects.
void cameraPairListContracts(Simpsons::Runtime& rt,Simpsons::EngineDriver& driver,
                             uint8_t* base,const Simpsons::NativeCameraBinding& binding) {
    constexpr uint32_t headField=0x82D0D01C;
    const uint32_t head=PPC_LOAD_U32(headField);
    std::vector<std::array<uint32_t,2>> nodes;
    for(uint32_t at=head;at;at=PPC_LOAD_U32(at+4)) {
        need(nodes.size()<65536,"Fixture raster list is cyclic or overlong");
        nodes.push_back({at,PPC_LOAD_U32(at)});
    }
    need(!nodes.empty() && binding.colorRaster && binding.depthRaster,"Pair fixture lacks both root rasters");
    const uint32_t tail=nodes.back()[0];
    const auto clears=driver.cameraClearCount();
    const uint32_t extra=rt.allocatePhysical(0,0x81000,PAGE_READWRITE,0,UINT32_MAX,4096);
    need(extra!=0,"Pair fixture node storage allocation failed");
    struct Restore {
        Simpsons::Runtime& rt;uint8_t* base;uint32_t head,tail,extra;
        uint8_t access;
        ~Restore() {
            rt.pageAccess[extra>>12]=access;
            PPC_STORE_U32(headField,head);PPC_STORE_U32(tail+4,0);
            rt.freePhysical(extra);
        }
    } restore{rt,base,head,tail,extra,rt.pageAccess[extra>>12].load()};
    struct ChangeWord {
        Simpsons::Runtime& rt;uint32_t address;volatile uint32_t* at;uint32_t saved;
        ChangeWord(Simpsons::Runtime& runtime,uint32_t where,uint32_t value):
            rt(runtime),address(where),at(reinterpret_cast<volatile uint32_t*>(rt.pointer(where,4,true))),saved(*at) {
            *at=__builtin_bswap32(value);
        }
        // Restoring is a guest store too: obtain a fresh write pointer so the write watch observes it.
        ~ChangeWord(){at=reinterpret_cast<volatile uint32_t*>(rt.pointer(address,4,true));*at=saved;}
    };
    const auto same=[&] {
        const auto current=driver.cameraBinding();
        need(current.camera==binding.camera && current.colorRaster==binding.colorRaster &&
            current.depthRaster==binding.depthRaster && current.colorIdentity==binding.colorIdentity &&
            current.depthIdentity==binding.depthIdentity && driver.cameraClearCount()==clears,
            "Pair validation changed the selected camera/attachments or clear count");
    };
    const auto bad=[&] {rejects([&]{driver.cameraBinding();});};
    // Both queried nodes are already in the original list. A second occurrence
    // must be found even when either queried root is near the beginning.
    for(uint32_t raster:{binding.colorRaster,binding.depthRaster}) {
        PPC_STORE_U32(extra,raster);PPC_STORE_U32(extra+4,head);
        {ChangeWord change(rt,headField,extra);bad();}
        same();
        const auto it=std::find_if(nodes.begin(),nodes.end(),[&](const auto& n){return n[1]==raster;});
        need(it!=nodes.end(),"Pair fixture root is absent from the original list");
        {ChangeWord change(rt,(*it)[0],0xFFFFFFFF);bad();}
        same();
    }
    // A late malformed tail still rejects after BOTH requested roots were found.
    PPC_STORE_U32(extra,0xFFFFFFFF);PPC_STORE_U32(extra+4,tail);
    {ChangeWord change(rt,tail+4,extra);bad();}
    PPC_STORE_U32(extra+4,extra);
    {ChangeWord change(rt,headField,extra);bad();}
    same();
    // Every node still requires current writable mapping, including unrelated
    // nodes appended after both matched roots.
    PPC_STORE_U32(extra+4,0);
    for(uint8_t access:{uint8_t(0),uint8_t(1)}) {
        ChangeWord change(rt,tail+4,extra);rt.pageAccess[extra>>12]=access;bad();
        rt.pageAccess[extra>>12]=restore.access;
    }
    same();
    // The accepted bound and first rejected length exercise the full walk;
    // stopping once both matches are found would incorrectly accept both.
    const uint32_t count=65536-uint32_t(nodes.size());
    auto* bytes=rt.pointer(extra,(count+1)*8,true);
    for(uint32_t i=0;i<=count;++i) {
        const uint32_t value=0xFFFFFFFF,next=i<count?extra+8*(i+1):0;
        const uint32_t words[2]={__builtin_bswap32(value),__builtin_bswap32(next)};
        std::memcpy(bytes+8*i,words,8);
    }
    PPC_STORE_U32(extra+8*(count-1)+4,0);
    {
        ChangeWord change(rt,tail+4,extra);same();
        PPC_STORE_U32(extra+8*(count-1)+4,extra+8*count);bad();
    }
    same();
}
