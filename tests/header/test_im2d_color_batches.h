// Real GPU differential oracle: the reference submits each original packet
// immediately. The candidate queues the same packets with caller storage reused.
void colorBatchEquivalence(NativeBackend& b) {
    constexpr UINT width=96,height=64;
    auto actual=b.createTarget(width,height,TargetFormat::RGB10A2);
    auto reference=b.createTarget(width,height,TargetFormat::RGB10A2);
    auto actualDepth=b.createDepthTarget(width,height),referenceDepth=b.createDepthTarget(width,height);
    const std::array<uint8_t,16> texels={19,241,113,0,251,31,67,85,127,193,13,170,43,79,229,255};
    const std::array<std::shared_ptr<Texture>,5> textures={nullptr,
        b.createTexture(2,2,TextureFormat::RGBA8,texels),b.createTexture(2,2,TextureFormat::BGRX8,texels),
        b.createTexture(12,8,TextureFormat::BC2,bc2Blocks),b.createTexture(8,8,TextureFormat::BC3,bc3Blocks)};
    const auto select=[&](bool candidate) {
        b.bindTargets({candidate?actual:reference,nullptr,nullptr,nullptr},candidate?actualDepth:referenceDepth);
        b.setViewport({0,0,float(width),float(height),0,1});
    };
    const auto reset=[&] {
        b.clearBindings();
        for(const auto& t:{actual,reference})b.clearTarget(t,{.125f,.375f,.625f,1});
        for(const auto& t:{actualDepth,referenceDepth})b.clearDepthTarget(t,.5f,73);
    };
    const auto compare=[&] {
        need(b.readbackTarget(actual)==b.readbackTarget(reference),"Queued color/texture packets changed packed color");
        const auto a=b.readbackDepthTarget(actualDepth),r=b.readbackDepthTarget(referenceDepth);
        for(size_t p=0;p<width*height;++p) {
            need(depthBits(a,p)==depthBits(r,p),"Queued color/texture packets changed depth order");
            need(a[p*8+4]==r[p*8+4],"Queued color/texture packets changed stencil");
        }
    };
    constexpr std::array words={0x07060706u,0x00010001u,0x00010706u,0x00010106u,0x00010186u,0x01000100u};
    for(UINT variation=0;variation<120;++variation) {
        reset();auto base=quad(width,height);base.texture=textures[variation%textures.size()];
        base.blendWord=words[variation/5%words.size()];base.expandedBlend=variation%2;
        base.alphaTest=(variation&2)!=0;base.alphaCompare=variation/3%8;base.alphaReference=.5f;
        base.depthTest=(variation&4)!=0;base.depthWrite=(variation&8)!=0;base.depthCompare=variation/7%8;
        base.reverseDepth=(variation&16)!=0;base.pixelCenterHalf=(variation&32)!=0;
        base.cullBits=variation%3==0?0:variation%3==1?2:6;base.colorWriteMask=UINT8(variation%16);
        if(variation&1)base.sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        std::vector<Im2DDraw> packets;
        for(UINT i=0;i<18;++i) {
            auto p=base;zValue(p,float(i%9+1)/10);color(p,{float(i%5+1)/6,.6f,.3f,float(i%4+1)/4});
            for(auto& v:p.vertices) {
                v.position[0]=v.position[0]*.35f+float(i%4*19)-4.25f;
                v.position[1]=v.position[1]*.45f+float(i%3*17)-2.75f;
            }
            if(i%3==1){p.primitiveType=3;p.vertices.resize(3);}packets.push_back(p);
        }
        select(false);for(const auto& p:packets)b.drawIm2D(reference,referenceDepth,p);
        select(true);const auto state=NativeIm2DProbe::state(b);const auto count=b.im2dDrawCount();
        for(auto p:packets) {
            // Different wrappers of the same immutable texture remain compatible.
            if(p.texture)p.texture=std::make_shared<Texture>(*p.texture);
            b.queueIm2D(actual,actualDepth,p);
            if(p.texture)p.texture->width=0;
            for(auto& v:p.vertices){v.position.fill(std::numeric_limits<float>::quiet_NaN());v.color.fill(0);}
        }
        need(b.im2dDrawCount()==count,"Compatible color packets submitted before their flush");
        need(NativeIm2DProbe::state(b)==state,"Accepting color batch changed caller bindings");
        b.flushIm2D();need(b.im2dDrawCount()==count+packets.size(),"Color batch lost original packet accounting");
        need(NativeIm2DProbe::state(b)==state,"Color batch failed to restore caller bindings");compare();
    }
    // Spatially separate triangles share one destination copy and DrawIndexed.
    // Statistics prove every original triangle still reaches the real pipeline.
    reset();auto packet=quad(width,height);packet.primitiveType=3;packet.vertices.resize(3);
    packet.texture=textures[1];packet.blendWord=0x07060706;
    std::vector<Im2DDraw> separate;
    for(UINT i=0;i<6;++i) {
        auto p=packet;
        p.vertices[0].position={float(4+i*16),12,0,1};
        p.vertices[1].position={float(9+i*16),12,0,1};
        p.vertices[2].position={float(4+i*16),17,0,1};separate.push_back(p);
    }
    select(false);for(const auto& p:separate)b.drawIm2D(reference,referenceDepth,p);
    select(true);auto* ctx=NativeIm2DProbe::context(b);auto* device=NativeIm2DProbe::device(b);
    D3D11_QUERY_DESC qd{D3D11_QUERY_PIPELINE_STATISTICS,0};ComPtr<ID3D11Query> query;
    need(SUCCEEDED(device->CreateQuery(&qd,&query)),"Color batch statistics query creation failed");
    const auto calls=b.im2dNativeDrawCount(),copies=b.im2dColorCopyCount();ctx->Begin(query.Get());
    for(const auto& p:separate)b.queueIm2D(actual,actualDepth,p);
    b.flushIm2D();ctx->End(query.Get());b.waitIdle();D3D11_QUERY_DATA_PIPELINE_STATISTICS stats{};
    need(ctx->GetData(query.Get(),&stats,sizeof(stats),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK,"Color batch statistics did not retire");
    need(stats.IAPrimitives==6 && stats.IAVertices==18,"Color batch omitted original triangles");
    need(b.im2dNativeDrawCount()==calls+1 && b.im2dColorCopyCount()==copies+3,
        "Disjoint textured packets did not share a real draw and destination copy");compare();

    // Each original strip contributes one triangle to each of two layers.
    // Their alternating original indices must become two contiguous draws,
    // preserving every vertex and the existing per-pixel dependency order.
    reset();std::vector<Im2DDraw> glyphs;
    for(UINT i=0;i<6;++i) {
        auto p=quad(width,height);p.texture=textures[1];p.blendWord=0x07060706;
        for(auto& v:p.vertices) {
            v.position[0]=4.0f+float(i*16)+(v.position[0]-.5f)*5/width;
            v.position[1]=12.0f+(v.position[1]-.5f)*5/height;
        }
        glyphs.push_back(p);
    }
    select(false);for(const auto& p:glyphs)b.drawIm2D(reference,referenceDepth,p);
    select(true);const auto glyphCalls=b.im2dNativeDrawCount(),glyphCopies=b.im2dColorCopyCount();
    ctx->Begin(query.Get());for(const auto& p:glyphs)b.queueIm2D(actual,actualDepth,p);
    b.flushIm2D();ctx->End(query.Get());b.waitIdle();stats={};
    need(ctx->GetData(query.Get(),&stats,sizeof(stats),D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK,"Layered glyph statistics did not retire");
    need(stats.IAPrimitives==12 && stats.IAVertices==36,"Layer compaction omitted original glyph triangles");
    need(b.im2dNativeDrawCount()==glyphCalls+2 && b.im2dColorCopyCount()==glyphCopies+4,
        "Alternating layer indices retained separate per-triangle native draws");compare();

    // Sequential packet changes must not merge across a different source,
    // sampler, blend equation, alpha test, depth policy, mask or viewport.
    for(UINT variation=0;variation<10;++variation) {
        reset();auto first=quad(width,height);first.texture=textures[1];first.blendWord=0x07060706;
        auto second=first;color(first,{1,.25f,.75f,.5f});color(second,{.25f,1,.5f,.75f});
        switch(variation) {
        case 0:second.texture=textures[2];break;
        case 1:second.sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;break;
        case 2:second.sampler.AddressU=D3D11_TEXTURE_ADDRESS_WRAP;for(auto& v:second.vertices)v.uv[0]+=1;break;
        case 3:second.blendWord=0x00010186;break;
        case 4:second.alphaTest=true;second.alphaCompare=4;second.alphaReference=.5f;break;
        case 5:second.depthTest=second.depthWrite=true;second.depthCompare=1;zValue(second,.75f);break;
        case 6:second.colorWriteMask=5;break;
        case 7:second.pixelCenterHalf=false;break;
        case 8:second.rasterWidth/=2;break;
        default:break;
        }
        const D3D11_VIEWPORT shifted={7,5,80,50,0,1};
        select(false);b.drawIm2D(reference,referenceDepth,first);
        if(variation==9)b.setViewport(shifted);
        b.drawIm2D(reference,referenceDepth,second);
        select(true);b.queueIm2D(actual,actualDepth,first);
        if(variation==9)b.setViewport(shifted);
        b.queueIm2D(actual,actualDepth,second);b.flushIm2D();compare();
    }
    // Reassigning the caller's wrapper cannot replace an accepted GPU source.
    reset();auto first=quad(width,height);first.texture=std::make_shared<Texture>(*textures[1]);
    auto second=first;second.texture=textures[2];second.colorWriteMask=5;
    select(false);b.drawIm2D(reference,referenceDepth,first);b.drawIm2D(reference,referenceDepth,second);
    select(true);b.queueIm2D(actual,actualDepth,first);*first.texture=*textures[2];
    first.colorWriteMask=5;b.queueIm2D(actual,actualDepth,first);b.flushIm2D();compare();

    // A rejected subsequent texture or sampler must flush earlier accepted
    // work without changing it or retaining a poisoned caller wrapper.
    for(UINT variation=0;variation<2;++variation) {
        reset();auto good=quad(width,height);good.texture=std::make_shared<Texture>(*textures[1]);
        select(false);b.drawIm2D(reference,referenceDepth,good);select(true);
        const auto count=b.im2dDrawCount();b.queueIm2D(actual,actualDepth,good);auto bad=good;
        if(variation)bad.sampler.Filter=D3D11_FILTER(0x7fffffff);else bad.texture->width=0;
        bool rejected=false;try{b.queueIm2D(actual,actualDepth,bad);}catch(const Error&){rejected=true;}
        need(rejected && b.im2dDrawCount()==count+1,"Bad textured packet lost earlier work or passed validation");compare();
    }
    // Writable sources remain immediate: an upload after acceptance cannot
    // retroactively recolor the first draw. Compare against the same sequence.
    reset();auto mutableTexture=b.createWritableTexture(2,2,TextureFormat::RGBA8);
    const std::array<uint8_t,16> later={241,11,23,255,241,11,23,255,241,11,23,255,241,11,23,255};
    auto mutablePacket=quad(width,height);mutablePacket.texture=mutableTexture;mutablePacket.blendWord=0x07060706;
    for(bool candidate:{false,true}) {
        select(candidate);b.writeTexture(mutableTexture,texels);
        const auto count=b.im2dDrawCount();
        if(candidate)b.queueIm2D(actual,actualDepth,mutablePacket);else b.drawIm2D(reference,referenceDepth,mutablePacket);
        need(b.im2dDrawCount()==count+1,"Writable textured draw was deferred across possible upload");
        b.writeTexture(mutableTexture,later);auto p=mutablePacket;color(p,{.5f,.5f,.5f,.5f});
        if(candidate)b.queueIm2D(actual,actualDepth,p);else b.drawIm2D(reference,referenceDepth,p);
    }
    compare();debugMessages(device);b.clearBindings();
}
