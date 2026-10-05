#pragma once
// Execute only original CPU source/macro builders, with their original stage
// helper and commit. No console shader compiler, shader object or draw follows.
void im2dShaderCpuContracts(Simpsons::Runtime& runtime,Simpsons::EngineCpuCalls& cpu,uint8_t* base) {
    const auto first=checks;const PPCContext incoming=cpu.registers();
    const auto nativeBefore=runtime.engineDriver->effectiveState();
    require(!PPC_LOAD_U32(0x82D10114) && !PPC_LOAD_U32(0x82D10118),"Shader CPU fixture requires drained original state queues");
    struct Saved {uint8_t* p;std::vector<uint8_t> bytes;};std::vector<Saved> saved;
    for(auto [a,n]:std::array<std::pair<uint32_t,uint32_t>,4>{{{0x82D0D170,0x2FBC},{0x82E3D160,0xB24},{0x82D501E0,0x140},{0x82D50328,0xB8}}}) {
        auto* p=runtime.pointer(a,n,true);saved.push_back({p,{p,p+n}});
    }
    const uint32_t source=runtime.allocatePhysical(0,4096,PAGE_READWRITE,0,UINT32_MAX,4096);
    require(source!=0,"Im2D CPU shader fixture failed allocation");
    auto text=[&](uint32_t address,uint32_t bound=512) {
        std::string result;
        for(uint32_t i=0;i<bound;++i){const auto byte=*runtime.pointer(address+i,1,false);if(!byte)return result;result+=char(byte);}
        throw Simpsons::Failure("Original shader text is unterminated");
    };
    try {
        // The exact stage0 requests in82408CC0; later stages are the original
        // disabled startup records. Scalar19E/19F are CPU-only and zero here.
        // The third schedule reproduces actual162: SELECTARG2 after a
        // textured draw leaves the unused texture ARG1 in original state.
        for(uint32_t profile=0;profile<3;++profile) {
            const bool textured=profile==1,unusedTexture=profile==2;
            const char* profileName=textured?"TEXTURED":unusedTexture?"FLAT_UNUSED_TEXTURE":"FLAT";
            cpu.invoke(0x82400170,0x19E,0);cpu.invoke(0x82400170,0x19F,0);
            cpu.invoke(0x824001E0,0,1,textured?4:3);
            if(textured)cpu.invoke(0x824001E0,0,2,2);
            cpu.invoke(0x824001E0,0,3,0);cpu.invoke(0x824001E0,0,4,textured?4:3);
            if(textured)cpu.invoke(0x824001E0,0,5,2);
            cpu.invoke(0x824001E0,0,6,0);cpu.invoke(0x82400040);
            std::fill_n(runtime.pointer(source,4096,true),4096,0xA5);
            std::fill_n(runtime.pointer(source+16,2048,true),2048,0); // Original8240EDF4/EDF8 initialize the complete output before concatenation.
            require(cpu.invoke(0x8240EAA8,source+16,2048)==0,"Original Im2D pixel-source builder failed");
            const auto shader=text(source+16,2048);
            require(!shader.empty() && shader.size()<2048,"Original pixel source has invalid bounded extent");
            auto tokens=shader;tokens.erase(std::remove_if(tokens.begin(),tokens.end(),[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}),tokens.end());
            const std::string expected=textured?
                "uniformexternsamplerg_texsamp0:register(s0);float4main(infloat4diffuse:COLOR0,infloat2texcoord0:TEXCOORD0):COLOR{float4current=1.f;"
                "float4stage0_texel=tex2D(g_texsamp0,texcoord0);float3stage0_color_arg1=stage0_texel.xyz;float3stage0_color_arg2=diffuse.xyz;"
                "current.xyz=stage0_color_arg1*stage0_color_arg2;floatstage0_alpha_arg1=stage0_texel.w;floatstage0_alpha_arg2=diffuse.w;"
                "current.w=stage0_alpha_arg1*stage0_alpha_arg2;returncurrent;}":unusedTexture?
                "uniformexternsamplerg_texsamp0:register(s0);float4main(infloat4diffuse:COLOR0,infloat2texcoord0:TEXCOORD0):COLOR{float4current=1.f;"
                "float4stage0_texel=tex2D(g_texsamp0,texcoord0);float3stage0_color_arg2=diffuse.xyz;"
                "current.xyz=stage0_color_arg2;floatstage0_alpha_arg2=diffuse.w;current.w=stage0_alpha_arg2;returncurrent;}":
                "float4main(infloat4diffuse:COLOR0):COLOR{float4current=1.f;float3stage0_color_arg2=diffuse.xyz;"
                "current.xyz=stage0_color_arg2;floatstage0_alpha_arg2=diffuse.w;current.w=stage0_alpha_arg2;returncurrent;}";
            require(tokens==expected,"Original generated pixel program changed from its native expression");
            for(uint32_t i=0;i<16;++i)require(PPC_LOAD_U8(source+i)==0xA5 && PPC_LOAD_U8(source+2064+i)==0xA5,
                "Original pixel-source builder exceeded caller storage");
            std::fprintf(stderr,"[ORIGINAL IM2D %s PIXEL SOURCE]\n%s\n[END ORIGINAL PIXEL SOURCE]\n",profileName,shader.c_str());
            // Original vertex key prefix82410A74..82410B94, with the screen
            // extent set by the original Im2D override leaf8240F210.
            cpu.invoke(0x8240F210,1280,720);
            const uint32_t key=source+2112;
            auto put=[&](uint32_t i,uint32_t v){PPC_STORE_U32(key+4*i,v);};
            auto stageValue=[&](uint32_t a,bool high){const auto v=PPC_LOAD_U32(a);return v==0xFFFFFFFF?0u:high?v&0xFFFF0000:v;};
            put(0,PPC_LOAD_U32(0x82E3DC10));put(1,0);
            put(2,stageValue(0x82E3D1C0,false));put(3,stageValue(0x82E3D244,false));
            put(4,stageValue(0x82E3D18C,true));put(5,stageValue(0x82E3D210,true));
            put(6,cpu.invoke(0x8240F0B0,0));put(7,cpu.invoke(0x8240F0B0,1));
            put(8,cpu.invoke(0x8240F150,0));put(9,cpu.invoke(0x8240F150,4));
            for(auto [i,a]:std::array<std::pair<uint32_t,uint32_t>,12>{{{10,0x82E3DBD8},{11,0x82E3DBF0},{12,0x82E3DC04},
                {13,0x82E3DBDC},{14,0x82E3DBFC},{15,0x82D503D4},{16,0x82E3DC14},{17,0x82E3DC18},
                {18,0x82E3DC1C},{19,0x82E3DC20},{20,0x82D503D8},{21,0x82D503DC}}})put(i,PPC_LOAD_U32(a));
            require(PPC_LOAD_U32(key+8*4)==1 && PPC_LOAD_U32(key+15*4)==1 && PPC_LOAD_U32(key+14*4)==0,
                "Original Im2D shader key lost color0/screen-position/unlit selection");
            const auto macros=cpu.invoke(0x82410588,key);
            require(macros==0x82D50328,"Original vertex macro builder changed its output owner");
            std::fprintf(stderr,"[ORIGINAL IM2D %s VERTEX MACROS]\n",profileName);
            bool screen=false,color=false,unlit=false;
            const std::array<std::pair<const char*,const char*>,20> expectedMacros={{{"K_NORMALIZENORMALS","false"},
                {"K_TWEENENABLE","false"},{"K_FOGENABLE","false"},{"K_RANGEFOGENABLE","true"},
                {"K_SPECULARENABLE","false"},{"K_LIGHTING","false"},{"K_SCREENSPACEPOSITIONS","true"},
                {"K_DIFFUSEMATERIALSOURCE","D3DMCS_MATERIAL"},{"K_SPECULARMATERIALSOURCE","D3DMCS_MATERIAL"},
                {"K_AMBIENTMATERIALSOURCE","D3DMCS_MATERIAL"},{"K_EMISSIVEMATERIALSOURCE","D3DMCS_MATERIAL"},
                {"K_TEXTURETRANSFORMFLAGS0","D3DTTFF_DISABLE"},{"K_TEXTURETRANSFORMFLAGS1","D3DTTFF_DISABLE"},
                {"K_TEXCOORDINDEX0","D3DTSS_TCI_PASSTHRU"},{"K_TEXCOORDINDEX1","D3DTSS_TCI_PASSTHRU"},
                {"K_COLOR0ENABLE","true"},{"K_COLOR1ENABLE","false"},
                {"K_TEXCOORDINDEX0ENABLE",(textured||unusedTexture)?"true":"false"},{"K_TEXCOORDINDEX1ENABLE","false"},
                {"K_FOGVERTEXMODE","D3DFOG_LINEAR"}}};
            for(uint32_t i=0;i<20;++i) {
                const auto name=PPC_LOAD_U32(macros+8*i),value=PPC_LOAD_U32(macros+8*i+4);
                if(!name){require(!value,"Original vertex macro terminator has a value");break;}
                const auto n=text(name),v=text(value);std::fprintf(stderr,"%s=%s\n",n.c_str(),v.c_str());
                require(n==expectedMacros[i].first && v==expectedMacros[i].second,"Original complete Im2D vertex option list differs");
                if(n=="K_SCREENSPACEPOSITIONS")screen=v=="true";
                if(n=="K_COLOR0ENABLE")color=v=="true";
                if(n=="K_LIGHTING")unlit=v=="false";
            }
            require(screen && color && unlit,"Original macros did not select the reviewed Im2D shader branch");
            require(PPC_LOAD_U32(macros+160)==0 && PPC_LOAD_U32(macros+164)==0,"Original vertex macro list is not terminated at20 entries");
            const auto physicalCount=runtime.physicalAllocations.size();
            Simpsons::qualifyIm2DProgram(runtime,cpu,base,textured);
            require(runtime.physicalAllocations.size()==physicalCount,"Native program qualifier leaked its successful scratch");
            const auto builds=runtime.im2dProgramCache->builds,hits=runtime.im2dProgramCache->hits;
            std::array<uint8_t,168> originalMacros{};std::memcpy(originalMacros.data(),runtime.pointer(0x82D50328,168,false),168);
            std::memset(runtime.pointer(0x82D50328,168,true),0xA7,168);
            Simpsons::qualifyIm2DProgram(runtime,cpu,base,textured);
            require(runtime.im2dProgramCache->builds==builds&&runtime.im2dProgramCache->hits==hits+1&&
                !std::memcmp(originalMacros.data(),runtime.pointer(0x82D50328,168,false),168),"Identical program inputs rebuilt or failed to restore original macro output");
            const auto pixelOp=PPC_LOAD_U32(0x82D501EC);PPC_STORE_U32(0x82D501EC,5);
            rejects([&]{Simpsons::qualifyIm2DProgram(runtime,cpu,base,textured);});PPC_STORE_U32(0x82D501EC,pixelOp);
            require(runtime.im2dProgramCache->builds==builds,"Rejected pixel state entered the program cache");
            rejects([&]{Simpsons::qualifyIm2DProgram(runtime,cpu,base,!textured);});
            require(runtime.physicalAllocations.size()==physicalCount,"Rejected pixel program leaked native scratch");
            // Lighting/fog/texture-transform changes must be discovered through
            // the ORIGINAL macro builder, not silently mapped to the flat VS.
            for(uint32_t field:{0x82E3DBFCu,0x82E3DBD8u,0x82E3D1C0u}) {
                const auto before=PPC_LOAD_U32(field);PPC_STORE_U32(field,1);
                bool rejected=false;
                try {Simpsons::qualifyIm2DProgram(runtime,cpu,base,textured);}
                catch(const Simpsons::Failure& e){rejected=std::string(e.what()).find("vertex program options")!=std::string::npos;}
                PPC_STORE_U32(field,before);
                require(rejected,"Original alternative vertex program did not reject at the native program gate");
                require(runtime.physicalAllocations.size()==physicalCount,"Rejected vertex program leaked native scratch");
            }
            std::fprintf(stderr,"[END ORIGINAL VERTEX MACROS]\n");cpu.invoke(0x8240F230);
        }
    } catch(...) {
        for(auto& s:saved)std::copy(s.bytes.begin(),s.bytes.end(),s.p);
        cpu.registers()=incoming;runtime.freePhysical(source);throw;
    }
    for(auto& s:saved)std::copy(s.bytes.begin(),s.bytes.end(),s.p);
    cpu.registers()=incoming;runtime.freePhysical(source);
    for(const auto& field:Simpsons::Graphics::scalarStateEvidence())
        require(runtime.engineDriver->effectiveState().scalar(field.id)==nativeBefore.scalar(field.id),"CPU shader builder changed native scalar state");
    std::fprintf(stderr,"PASS original Im2D shader CPU:%zu checks; generated pixel source/vertex macros, no shader compilation or draw\n",checks-first);
}
