#include "gpu_timeline.h"
#include "runtime/stall_profiler.h"
#include <windows.h>
#include <d3d11_4.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Simpsons::Graphics::GpuTimeline {
namespace {
using Microsoft::WRL::ComPtr;
constexpr size_t kSlots=6,kStamps=32768;
constexpr uint64_t kReportFrames=120;
constexpr double kSlowFrameMs=33.0;
constexpr const char* kOutside="(outside scopes)";

struct Slot {
    ComPtr<ID3D11Query> disjoint;
    std::vector<ComPtr<ID3D11Query>> stamps;
    std::vector<const char*> labels;
    size_t used{};
    uint64_t frame{};
    bool recording{},submitted{},overflow{};
};
struct Total {double ms{};uint64_t intervals{};};
struct Timeline {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    DWORD owner{};
    const char* current=kOutside;
    std::vector<const char*> open; // Rendering/present scopes only, innermost last.
    std::array<Slot,kSlots> slots;
    size_t active{};
    uint64_t frames{},dropped{},overflowed{},windowFrames{};
    double windowMs{},windowMax{};
    std::unordered_map<std::string_view,Total> totals;
};
// Never destroyed: process teardown may still present after the engine exits.
std::atomic<Timeline*> instance{};

// SIMPSONS_GPU_COLUMN_PROBE=<frame>[,<x>] records a pixel history for one frame:
// at every rendering/presentation scope boundary it copies column x of the bound
// single-sample RGB10A2 color target, then reports which scope last changed each
// row of the most sampled target. Diagnostic only; the copies are GPU-ordered reads.
struct ColumnProbe {
    static constexpr UINT columns=16384,slices=8;
    uint64_t frame{};
    int x=-1;
    UINT top=900,bottom=1300; // Probed rows [top,bottom).
    bool active{},done{};
    ComPtr<ID3D11Texture2D> staging;
    struct Sample {const char* label;ID3D11Resource* resource;UINT height;};
    std::vector<Sample> samples;
};
ColumnProbe columnProbe;
void sampleColumn(Timeline& t,const char* label) noexcept {
    auto& probe=columnProbe;
    if(!probe.active || probe.samples.size()>=size_t(ColumnProbe::columns)*ColumnProbe::slices)return;
    ComPtr<ID3D11RenderTargetView> view;t.context->OMGetRenderTargets(1,&view,nullptr);
    if(!view)return;
    ComPtr<ID3D11Resource> resource;view->GetResource(&resource);
    ComPtr<ID3D11Texture2D> texture;if(FAILED(resource.As(&texture)))return;
    D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
    const bool packed=desc.Format==DXGI_FORMAT_R10G10B10A2_TYPELESS||desc.Format==DXGI_FORMAT_R10G10B10A2_UNORM||desc.Format==DXGI_FORMAT_R10G10B10A2_UINT;
    if(!packed || desc.SampleDesc.Count!=1 || desc.Height<probe.bottom)return;
    const UINT x=probe.x>=0?UINT(probe.x):desc.Width/2;
    if(x>=desc.Width)return;
    const D3D11_BOX box{x,probe.top,0,x+1,probe.bottom,1};
    const UINT index=UINT(probe.samples.size());
    t.context->CopySubresourceRegion(probe.staging.Get(),index/ColumnProbe::columns,index%ColumnProbe::columns,0,0,texture.Get(),0,&box);
    probe.samples.push_back({label,resource.Get(),desc.Height});
}
void reportColumn(Timeline& t) {
    auto& probe=columnProbe;
    std::unordered_map<ID3D11Resource*,size_t> counts;
    for(const auto& sample:probe.samples)++counts[sample.resource];
    ID3D11Resource* scene=nullptr;size_t best=0;
    for(const auto& [resource,count]:counts)if(count>best){best=count;scene=resource;}
    if(!scene){std::fprintf(stderr,"[GPU COLUMN PROBE] no samples\n");return;}
    std::array<D3D11_MAPPED_SUBRESOURCE,ColumnProbe::slices> mapped{};
    const UINT used=UINT((probe.samples.size()+ColumnProbe::columns-1)/ColumnProbe::columns);
    for(UINT slice=0;slice<used;++slice)
        if(FAILED(t.context->Map(probe.staging.Get(),slice,D3D11_MAP_READ,0,&mapped[slice]))){std::fprintf(stderr,"[GPU COLUMN PROBE] map failed\n");return;}
    const auto value=[&](size_t index,UINT row){
        const auto& m=mapped[index/ColumnProbe::columns];uint32_t v;
        std::memcpy(&v,static_cast<const uint8_t*>(m.pData)+size_t(row)*m.RowPitch+(index%ColumnProbe::columns)*4,4);return v;};
    const UINT height=probe.bottom-probe.top;
    std::fprintf(stderr,"[GPU COLUMN PROBE] frame=%llu samples=%zu scene_samples=%zu height=%u x=%d\n",
        static_cast<unsigned long long>(probe.frame),probe.samples.size(),best,height,probe.x);
    // Per row: every scope that changed it, in order, with the value it left
    // (consecutive changes by one scope merge). A stray draw family stands out
    // against neighbouring rows; runs of rows with the same writers and values are merged.
    struct Write {const char* label;uint32_t value;};
    std::vector<Write> run;UINT runStart=0;
    const auto print=[&](UINT first,UINT last) {
        std::string text;
        for(const auto& write:run) {
            char entry[192];
            std::snprintf(entry,sizeof(entry),"%s%s=%u,%u,%u",text.empty()?"":" > ",write.label,
                write.value&1023,(write.value>>10)&1023,(write.value>>20)&1023);
            text+=entry;
        }
        std::fprintf(stderr,"[GPU COLUMN PROBE] rows=%u-%u writers: %s\n",probe.top+first,probe.top+last,text.c_str());
    };
    for(UINT row=0;row<height;++row) {
        std::vector<Write> writes;uint32_t previous=0;bool seen=false;
        for(size_t i=0;i<probe.samples.size();++i) {
            if(probe.samples[i].resource!=scene)continue;
            const uint32_t v=value(i,row);
            if(seen && v!=previous) {
                const char* label=probe.samples[i].label?probe.samples[i].label:"(outside scopes)";
                if(!writes.empty() && writes.back().label==label)writes.back().value=v;
                else writes.push_back({label,v});
            }
            previous=v;seen=true;
        }
        const bool same=row>0 && writes.size()==run.size() &&
            std::equal(writes.begin(),writes.end(),run.begin(),[](const Write& a,const Write& b){return a.label==b.label && a.value==b.value;});
        if(row>0 && !same){print(runStart,row-1);runStart=row;}
        if(!same)run=writes;
    }
    if(height)print(runStart,height-1);
    for(UINT slice=0;slice<used;++slice)t.context->Unmap(probe.staging.Get(),slice);
}
void installProbe(Timeline& t) {
    const char* text=std::getenv("SIMPSONS_GPU_COLUMN_PROBE");
    if(!text || !*text)return;
    columnProbe.frame=std::strtoull(text,nullptr,10);
    int fields[3]{-1,int(columnProbe.top),int(columnProbe.bottom)};
    const char* cursor=std::strchr(text,',');
    for(int i=0;i<3 && cursor;++i){fields[i]=std::atoi(cursor+1);cursor=std::strchr(cursor+1,',');}
    columnProbe.x=fields[0];
    if(fields[1]>=0 && fields[2]>fields[1]){columnProbe.top=UINT(fields[1]);columnProbe.bottom=UINT(fields[2]);}
    D3D11_TEXTURE2D_DESC desc{};desc.Width=ColumnProbe::columns;desc.Height=columnProbe.bottom-columnProbe.top;
    desc.MipLevels=1;desc.ArraySize=ColumnProbe::slices;
    desc.Format=DXGI_FORMAT_R10G10B10A2_TYPELESS;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    if(FAILED(t.device->CreateTexture2D(&desc,nullptr,&columnProbe.staging))){std::fprintf(stderr,"[GPU COLUMN PROBE] staging creation failed\n");return;}
    columnProbe.samples.reserve(16384);
    std::fprintf(stderr,"[GPU COLUMN PROBE] armed for frame %llu\n",static_cast<unsigned long long>(columnProbe.frame));
}

void stamp(Timeline& t,Slot& slot,const char* label) noexcept {
    if(slot.used==slot.stamps.size()) {
        if(slot.used>=kStamps) {slot.overflow=true;return;}
        D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP,0};ComPtr<ID3D11Query> query;
        if(FAILED(t.device->CreateQuery(&desc,&query))) {slot.overflow=true;return;}
        slot.stamps.push_back(query);slot.labels.push_back(nullptr);
    }
    t.context->End(slot.stamps[slot.used].Get());
    slot.labels[slot.used]=label;++slot.used;
}
// Only rendering and presentation scopes issue GPU commands; guest, import and
// wait scopes are not stamped (they would also exhaust the per-frame stamp pool).
void boundary(const char* function,StallProfiler::Section section,bool begin) noexcept {
    if(section!=StallProfiler::Section::Rendering && section!=StallProfiler::Section::Present)return;
    Timeline* t=instance.load(std::memory_order_acquire);
    if(!t || GetCurrentThreadId()!=t->owner)return;
    // Probe at outermost boundaries only, coalescing repeats of the same scope.
    if((begin && t->open.empty()) || (!begin && t->open.size()==1)) {
        const char* outer=t->open.empty()?kOutside:t->open.front();
        const char* next=begin?function:kOutside;
        if(outer!=next)sampleColumn(*t,outer);
    }
    if(begin)t->open.push_back(function);
    else if(!t->open.empty())t->open.pop_back();
    t->current=t->open.empty()?kOutside:t->open.back();
    auto& slot=t->slots[t->active];
    if(slot.recording && !slot.overflow)stamp(*t,slot,t->current);
}
void begin(Timeline& t,Slot& slot) {
    slot.used=0;slot.overflow=false;slot.submitted=false;slot.recording=false;slot.frame=t.frames;
    if(!slot.disjoint) {
        D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
        if(FAILED(t.device->CreateQuery(&desc,&slot.disjoint)))return;
    }
    t.context->Begin(slot.disjoint.Get());
    slot.recording=true;
    stamp(t,slot,t.current);
}
void report(Timeline& t) {
    std::vector<std::pair<std::string_view,Total>> rows(t.totals.begin(),t.totals.end());
    std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.second.ms>b.second.ms;});
    const double frames=double(t.windowFrames);
    std::fprintf(stderr,"[GPU PROFILE] frames=%llu gpu_ms_per_frame=%.3f max_frame_ms=%.3f dropped=%llu overflowed=%llu\n",
        static_cast<unsigned long long>(t.windowFrames),t.windowMs/frames,t.windowMax,
        static_cast<unsigned long long>(t.dropped),static_cast<unsigned long long>(t.overflowed));
    for(size_t i=0;i<rows.size() && i<24;++i)
        std::fprintf(stderr,"[GPU PROFILE] rank=%zu scope=%.*s ms_per_frame=%.3f share_pct=%.1f intervals_per_frame=%.1f\n",i+1,
            int(rows[i].first.size()),rows[i].first.data(),rows[i].second.ms/frames,
            t.windowMs>0?100.0*rows[i].second.ms/t.windowMs:0.0,double(rows[i].second.intervals)/frames);
    t.totals.clear();t.windowFrames=0;t.windowMs=t.windowMax=0;
}
// Returns false while the frame's disjoint query is still in flight.
bool resolve(Timeline& t,Slot& slot) {
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
    const HRESULT result=t.context->GetData(slot.disjoint.Get(),&disjoint,sizeof(disjoint),D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if(result==S_FALSE)return false;
    slot.submitted=false;
    if(FAILED(result) || disjoint.Disjoint || !disjoint.Frequency || slot.used<2) {++t.dropped;return true;}
    std::vector<uint64_t> times(slot.used);
    for(size_t i=0;i<slot.used;++i)
        if(t.context->GetData(slot.stamps[i].Get(),&times[i],sizeof(uint64_t),D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK) {++t.dropped;return true;}
    if(slot.overflow)++t.overflowed;
    const double scale=1000.0/double(disjoint.Frequency);
    std::unordered_map<std::string_view,double> frame;
    for(size_t i=0;i+1<slot.used;++i) {
        const double ms=times[i+1]>times[i]?double(times[i+1]-times[i])*scale:0.0;
        frame[slot.labels[i]]+=ms;
        auto& total=t.totals[slot.labels[i]];total.ms+=ms;++total.intervals;
    }
    const double gpu=times.back()>times.front()?double(times.back()-times.front())*scale:0.0;
    t.windowMs+=gpu;t.windowMax=std::max(t.windowMax,gpu);++t.windowFrames;
    if(gpu>=kSlowFrameMs) {
        std::vector<std::pair<std::string_view,double>> rows(frame.begin(),frame.end());
        std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.second>b.second;});
        std::fprintf(stderr,"[GPU SLOW FRAME] frame=%llu gpu_ms=%.3f stamps=%zu",static_cast<unsigned long long>(slot.frame),gpu,slot.used);
        for(size_t i=0;i<rows.size() && i<8;++i)
            std::fprintf(stderr," %.*s=%.3f",int(rows[i].first.size()),rows[i].first.data(),rows[i].second);
        std::fputc('\n',stderr);
    }
    if(t.windowFrames>=kReportFrames)report(t);
    return true;
}
}

bool requested() {
    const char* text=std::getenv("SIMPSONS_GPU_PROFILE");
    return text && *text && *text!='0' && StallProfiler::enabled;
}
void attach(ID3D11Device* device,ID3D11DeviceContext* context) {
    if(!device || !context || instance.load(std::memory_order_acquire))return;
    auto* t=new Timeline;
    t->device=device;t->context=context;t->owner=GetCurrentThreadId();
    installProbe(*t);
    begin(*t,t->slots[0]);
    instance.store(t,std::memory_order_release);
    StallProfiler::gpuBoundaryHook=&boundary;
    std::fprintf(stderr,"[GPU PROFILE] timestamp attribution enabled on the graphics owner thread; timings are diagnostic\n");
}
void frame(ID3D11Device* device) {
    Timeline* t=instance.load(std::memory_order_acquire);
    if(!t || t->device.Get()!=device || GetCurrentThreadId()!=t->owner)return;
    if(columnProbe.active) {
        sampleColumn(*t,t->current);columnProbe.active=false;columnProbe.done=true;reportColumn(*t);
    } else if(!columnProbe.done && columnProbe.staging && t->frames+1==columnProbe.frame)columnProbe.active=true;
    auto& current=t->slots[t->active];
    if(current.recording) {
        stamp(*t,current,"(frame end)");
        t->context->End(current.disjoint.Get());
        current.recording=false;current.submitted=true;
    }
    for(size_t age=1;age<=kSlots;++age) {
        auto& slot=t->slots[(t->active+age)%kSlots];
        if(slot.submitted)resolve(*t,slot);
    }
    ++t->frames;t->active=(t->active+1)%kSlots;
    auto& next=t->slots[t->active];
    if(next.submitted) {++t->dropped;next.submitted=false;} // Still in flight after every other slot.
    begin(*t,next);
}
}
