// Whole original 823CB2A0 queue, original six-face construction and 823CB210
// CPU copies. Fixtures use the original queue reset/append services.
#include "immediate_original_helpers.h"
extern "C" void __imp__sub_823CB210(PPCContext&,uint8_t*);
namespace {
std::function<void(PPCContext&,uint8_t*,bool)> faceObserver;
struct Observation {explicit Observation(decltype(faceObserver) f){need(!faceObserver,"Nested quad-list observer");faceObserver=std::move(f);}~Observation(){faceObserver={};}};
constexpr uint32_t queue=0x60000;
void exercise(const char* path){
    ImmediateFixture f(path);auto& rt=f.rt;auto* base=f.base;auto& cpu=*f.cpu;auto& c=cpu.registers();auto& d=f.driver();
    rt.map(queue,0x3000,true,"original six-face queue");std::memset(rt.pointer(queue,0x3000,true),0,0x3000);
    Restore projection(rt,0x82DFEAE0,64),unrelated(rt,0x82DFEAA0,64),ring(rt,f.ring,0x1000);
    matrix(base,0x82DFEAE0,{.75f,0,0,0,0,.5f,0,0,0,0,1,0,.125f,-.125f,0,1});matrix(base,0x82DFEAA0,translated(4,4));
    auto invoke=[&]{seed(c);const auto before=abi(c);cpu.invoke(0x823CB2A0,queue);need(abi(c)==before,"Whole quad-list draw changed nonvolatile ABI");};
    auto reset=[&]{cpu.invoke(0x823CB168,queue);need(PPC_LOAD_U32(queue+0x280C)==queue+12,"Original quad-list reset differs");};
    auto add=[&](float y,float width,float alpha){const auto at=cpu.invoke(0x823CB178,queue);need(at>=queue+12&&at<queue+0x280C,"Original quad-list append failed");
        put(base,at,-.5f);put(base,at+4,y);put(base,at+8,.5f);put(base,at+12,.5f);put(base,at+16,y);put(base,at+20,.5f);
        put(base,at+24,width);PPC_STORE_U32(at+28,f.texture);put(base,at+32,.5f);put(base,at+36,alpha);};
    stage="empty original queue";reset();f.clear();const auto black=f.color();const auto earlyDepth=f.depth();
    const auto earlyDraws=d.immediateDrawCount();const auto earlyCursor=PPC_LOAD_U32(f.row);const auto earlyCaches=caches(base);const auto earlyState=d.effectiveState();
    invoke();need(d.immediateDrawCount()==earlyDraws&&PPC_LOAD_U32(f.row)==earlyCursor&&caches(base)==earlyCaches&&f.color()==black,"Empty quad list changed rendering");
    for(const auto& field:Graphics::scalarStateEvidence())need(d.effectiveState().scalar(field.id)==earlyState.scalar(field.id),"Empty quad list changed state");
    sameDepth(f.depth(),earlyDepth);
    auto run=[&](uint32_t records,float width,float alpha){
        reset();for(uint32_t i=0;i<records;++i)add(float(i)*.5f,width,alpha);f.clear();f.poison(0x1000);
        const auto input=snapshot(rt,queue,0x2810);const auto before=d.immediateDrawCount();uint32_t faces=0;
        std::array<std::array<uint32_t,8>,2> corners{};std::vector<std::array<float,6>> copied;
        constexpr std::array<uint32_t,6> callers={0x823CB584,0x823CB6CC,0x823CB7E4,0x823CB914,0x823CBA20,0x823CBB14};
        Observation observe([&](PPCContext& face,uint8_t* memory,bool after){
            need(memory==base&&uint32_t(face.lr)==callers[faces%6],"Quad list lost original face caller/order");
            if(!after){
                const uint32_t record=faces/6;need(record<records,"Quad list emitted extra record");const float y=float(record)*.5f;
                for(uint32_t vertex=0;vertex<4;++vertex){const auto at=face.r4.u32+12*vertex;const float x=get(base,at),dy=get(base,at+4)-y,dz=get(base,at+8)-.5f;
                    need(std::abs(std::abs(x)-.5f)<.000001f&&std::abs(std::abs(dy)-width)<.000001f&&std::abs(std::abs(dz)-width)<.000001f,"Original prism corner differs from endpoints/width");
                    if(width>0)++corners[record][(x>0?4u:0u)+(dy>0?2u:0u)+(dz>0?1u:0u)];
                    need(get(base,face.r5.u32+8*vertex)==float(vertex&1)&&get(base,face.r5.u32+8*vertex+4)==(vertex>=2?.5f:0),"Original prism UV scale/order differs");
                    copied.push_back({x,get(base,at+4),get(base,at+8),1,float(vertex&1),vertex>=2?.5f:0});
                }
            }else ++faces;
        });
        invoke();need(faces==6*records&&d.immediateDrawCount()==before+6*records&&PPC_LOAD_U32(f.row)==f.ring+768*records,"Quad list omitted or repeated original faces");
        for(uint32_t record=0;record<records;++record)if(width>0)for(auto count:corners[record])need(count==3,"Original prism does not share each corner across three faces");
        need(copied.size()==24*records,"Original quad leaf observation extent differs");
        for(uint32_t vertex=0;vertex<24*records;++vertex){const auto at=f.ring+32*vertex;
            for(uint32_t lane=0;lane<6;++lane)need(get(base,at+4*lane)==copied[vertex][lane],"Original quad leaf did not copy its CPU face inputs");
            need(PPC_LOAD_U32(at+24)==UINT32_MAX&&PPC_LOAD_U32(at+28)==UINT32_MAX,"Quad leaf overwrote unused UV lanes");}
        same(rt,queue,input,"Original draw consumed or modified queued records");f.lifetime();
        using S=Graphics::ScalarState;need(!d.effectiveState().scalar(S::AlphaTest)&&d.effectiveState().scalar(S::DepthEnable)==1&&d.effectiveState().scalar(S::DepthWrite)==1&&
            !d.effectiveState().scalar(S::BlendEnable),"Quad-list cleanup changed final immediate state");return f.color();
    };
    stage="six original faces and GPU projection";const auto full=run(1,.125f,1);need(colored(full)>100,"Original prism generated no color");
    for(uint32_t y=0;y<720;++y)for(uint32_t x=0;x<1280;++x)if(pixel(full,4*(1280*y+x))&0x3FFFFFFF)
        need(x>=478&&x<=962&&y>=380&&y<=430,"Prism ignored its original combined projection");
    const auto drawnDepth=f.depth();uint32_t changedDepth=0;
    for(size_t i=0;i<drawnDepth.size();i+=8){need(drawnDepth[i+4]==earlyDepth[i+4],"Prism changed stencil");if(std::memcmp(drawnDepth.data()+i,earlyDepth.data()+i,4)){
        float value{};std::memcpy(&value,drawnDepth.data()+i,4);need(value==.375f||value==.625f,"Prism depth does not match its original front/back faces");++changedDepth;}}
    need(changedDepth>100,"Original prism omitted depth writes");
    stage="per-record alpha and copy blending";const auto dim=run(1,.125f,.25f);uint32_t alphaChanges=0;
    for(size_t i=0;i<full.size();i+=4){const auto a=pixel(full,i),b=pixel(dim,i);need((a&0x3FFFFFFF)==(b&0x3FFFFFFF),"Prism copy blend incorrectly premultiplied alpha");alphaChanges+=(a>>30)!=(b>>30);}
    need(alphaChanges>100,"Prism ignored original record alpha");
    stage="multiple original records";need(colored(run(2,.125f,.5f))>100,"Quad list lost second record");
    stage="repeated retained queue";const auto beforeRepeat=f.color();const auto repeatDepth=f.depth();const auto repeats=d.immediateDrawCount();invoke();
    need(d.immediateDrawCount()==repeats+12&&f.color()==beforeRepeat,"Retained queue was not redrawn with original copy blending");sameDepth(f.depth(),repeatDepth);
    stage="zero-width original prism";need(run(1,0,1)==black,"Degenerate prism generated pixels");
    stage="explicit null texture first record";
    PPC_STORE_U32(0x50010,0x000000FF);need(cpu.invoke(0x823EE940,f.camera,0x50010,7)==1,"Original prism reference clear failed");
    const auto boundPixels=run(1,.125f,1);const auto referenceDepth=f.depth();
    PPC_STORE_U32(queue+12+28,0);need(cpu.invoke(0x823EE940,f.camera,0x50010,7)==1,"Original null prism clear failed");f.poison(0x1000);
    const auto clearedDepth=f.depth();const auto nullCount=d.immediateDrawCount();const auto nullInput=snapshot(rt,queue,0x2810);invoke();
    need(d.immediateDrawCount()==nullCount+6&&PPC_LOAD_U32(f.row)==f.ring+768,"Null texture omitted original faces");
    const auto nullPixels=f.color();uint32_t transparent=0;
    for(size_t i=0;i<nullPixels.size();i+=4){const auto output=pixel(nullPixels,i);need(output==0||output==pixel(black,i),"Null texture reused prior RGB/alpha");
        if(pixel(boundPixels,i)&0x3FFFFFFF){need(output==0,"Null texture did not overwrite covered geometry with transparent black");}transparent+=output==0;}
    need(transparent>100,"Null texture omitted original transparent color writes");sameDepth(f.depth(),referenceDepth);
    need(f.depth()!=clearedDepth,"Null texture omitted original depth writes");
    same(rt,queue,nullInput,"Null texture changed retained queue");f.lifetime();
    stage="invalid queue extent";reset();const auto valid=c;const auto invalidDraws=d.immediateDrawCount();const auto invalidCursor=PPC_LOAD_U32(f.row);const auto invalidCaches=caches(base);
    for(uint32_t end:{queue+13,queue+0x2834}){PPC_STORE_U32(queue+0x280C,end);rejects([&]{cpu.invoke(0x823CB2A0,queue);},"Invalid quad-list extent accepted");c=valid;
        need(d.immediateDrawCount()==invalidDraws&&PPC_LOAD_U32(f.row)==invalidCursor&&caches(base)==invalidCaches,"Rejected queue changed rendering");}
    reset();f.end();
}
}
PPC_FUNC(sub_823CB210){const auto before=abi(ctx);if(faceObserver)faceObserver(ctx,base,false);__imp__sub_823CB210(ctx,base);
    need(abi(ctx)==before,"Original quad leaf changed nonvolatile ABI");if(faceObserver)faceObserver(ctx,base,true);}
int main(int argc,char** argv){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);SetUnhandledExceptionFilter(Simpsons::exceptionFilter);
    try{need(argc==2,"Original image required");exercise(argv[1]);std::printf("PASS original quad list: %zu checks; original queue/six-face CPU path, corners/UV/alpha, GPU color/depth, retained rows, ABI\n",checks);return 0;}
    catch(const std::exception& error){std::fprintf(stderr,"FAIL original quad list: %zu checks stage=%s: %s\n",checks,stage,error.what());return 1;}}
