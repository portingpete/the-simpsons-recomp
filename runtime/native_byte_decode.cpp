// Native execution of the original byte decoder 8281E9B0 (EA RefPack tokens).
//
// The loader calls it with &source, &destination and an output budget; it decodes
// whole tokens until the destination reaches the budget (returns 0) or a stop token
// (returns 1), then stores both cursors. Executed as translated code, every byte goes
// through the inline guest accessors and ctx register spills: loading screens and
// in-world streaming spent 20-35 ms per frame here.
//
// This definition replaces the generated weak symbol. Its prologue, epilogue, trace,
// cursor accesses and final register file are those of the original. Tokens whose
// complete footprint lies in validated plain guest memory run as host copies; every
// other token (import page, opaque object page, low demand-mapped pages, armed
// write-watch blocks, aperture crossings, unmapped or protected pages, wrapped cursors)
// runs through an exact transcription of the original instructions, so faults,
// partial writes and watch invalidations happen at the same access.
#include "aot_inline_fast_memory.h"
#include "stall_profiler.h"
#include <algorithm>
#include <cstring>

PPC_EXTERN_FUNC(__savegprlr_29);
PPC_EXTERN_FUNC(__restgprlr_29);

namespace {
using Simpsons::Runtime;
constexpr uint32_t apertureAdjustment[8]={0,0,0,0,0,0,0xE0000000u,0xC0001000u};
constexpr uint32_t pageSize=0x1000;

// Original instructions from loc_8281E9D4 through one token. Returns true at the
// stop token (loc_8281EBE8), false at loc_8281EBA8.
bool exactToken(PPCContext& __restrict ctx,uint8_t* base) {
	ctx.r8.u64 = PPC_FAST_INLINE_LOAD_U8(ctx.r10.u32 + 0);
	ctx.r10.s64 = ctx.r10.s64 + 1;
	ctx.r9.u64 = ctx.r8.u64;
	ctx.r7.u64 = __builtin_rotateleft64(ctx.r9.u32 | (ctx.r9.u64 << 32), 0) & 0x80;
	ctx.cr6.compare<uint32_t>(ctx.r7.u32, 0, ctx.xer);
	if (!ctx.cr6.eq) goto loc_8281EA58;
	ctx.r8.u64 = ctx.r9.u32 & 0x3;
	ctx.r7.u64 = PPC_FAST_INLINE_LOAD_U8(ctx.r10.u32 + 0);
	ctx.r10.s64 = ctx.r10.s64 + 1;
	ctx.cr6.compare<uint32_t>(ctx.r8.u32, 0, ctx.xer);
	if (ctx.cr6.eq) goto loc_8281EA1C;
loc_8281EA00:
	ctx.r6.u64 = PPC_FAST_INLINE_LOAD_U8(ctx.r10.u32 + 0);
	ctx.r8.s64 = ctx.r8.s64 + -1;
	ctx.r10.s64 = ctx.r10.s64 + 1;
	ctx.cr6.compare<uint32_t>(ctx.r8.u32, 0, ctx.xer);
	PPC_FAST_INLINE_STORE_U8(ctx.r11.u32 + 0, ctx.r6.u8);
	ctx.r11.s64 = ctx.r11.s64 + 1;
	if (!ctx.cr6.eq) goto loc_8281EA00;
loc_8281EA1C:
	ctx.r8.u64 = __builtin_rotateleft64(ctx.r9.u32 | (ctx.r9.u64 << 32), 3) & 0x300;
	ctx.r7.u64 = ctx.r7.u32 & 0xFF;
	ctx.r8.s64 = ctx.r11.s64 - ctx.r8.s64;
	ctx.r9.u64 = __builtin_rotateleft64(ctx.r9.u32 | (ctx.r9.u64 << 32), 30) & 0x7;
	ctx.r7.s64 = ctx.r8.s64 - ctx.r7.s64;
	ctx.r8.s64 = ctx.r9.s64 + 2;
	ctx.r9.s64 = ctx.r7.s64 + -1;
loc_8281EA38:
	ctx.r7.u64 = PPC_FAST_INLINE_LOAD_U8(ctx.r9.u32 + 0);
	ctx.cr6.compare<uint32_t>(ctx.r8.u32, 0, ctx.xer);
	ctx.r9.s64 = ctx.r9.s64 + 1;
	ctx.r8.s64 = ctx.r8.s64 + -1;
	PPC_FAST_INLINE_STORE_U8(ctx.r11.u32 + 0, ctx.r7.u8);
	ctx.r11.s64 = ctx.r11.s64 + 1;
	if (!ctx.cr6.eq) goto loc_8281EA38;
	return false;
loc_8281EA58:
	ctx.r7.u64 = __builtin_rotateleft64(ctx.r9.u32 | (ctx.r9.u64 << 32), 0) & 0x40;
	ctx.cr6.compare<uint32_t>(ctx.r7.u32, 0, ctx.xer);
	if (!ctx.cr6.eq) goto loc_8281EAD8;
	ctx.r7.u64 = PPC_FAST_INLINE_LOAD_U8(ctx.r10.u32 + 0);
	ctx.r10.s64 = ctx.r10.s64 + 1;
	ctx.r8.u64 = __builtin_rotateleft64(ctx.r7.u32 | (ctx.r7.u64 << 32), 26) & 0x3FFFFFF;
	ctx.cr6.compare<uint32_t>(ctx.r8.u32, 0, ctx.xer);
	ctx.r6.u64 = PPC_FAST_INLINE_LOAD_U8(ctx.r10.u32 + 0);
	ctx.r10.s64 = ctx.r10.s64 + 1;
	if (ctx.cr6.eq) goto loc_8281EA9C;
loc_8281EA80:
	ctx.r5.u64 = PPC_FAST_INLINE_LOAD_U8(ctx.r10.u32 + 0);
	ctx.r8.s64 = ctx.r8.s64 + -1;
	ctx.r10.s64 = ctx.r10.s64 + 1;
	ctx.cr6.compare<uint32_t>(ctx.r8.u32, 0, ctx.xer);
	PPC_FAST_INLINE_STORE_U8(ctx.r11.u32 + 0, ctx.r5.u8);
	ctx.r11.s64 = ctx.r11.s64 + 1;
	if (!ctx.cr6.eq) goto loc_8281EA80;
loc_8281EA9C:
	ctx.r8.u64 = __builtin_rotateleft64(ctx.r7.u32 | (ctx.r7.u64 << 32), 8) & 0x3F00;
	ctx.r7.u64 = ctx.r6.u32 & 0xFF;
	ctx.r8.s64 = ctx.r11.s64 - ctx.r8.s64;
	ctx.r9.u64 = ctx.r9.u32 & 0x3F;
	ctx.r7.s64 = ctx.r8.s64 - ctx.r7.s64;
	ctx.r8.s64 = ctx.r9.s64 + 3;
	ctx.r9.s64 = ctx.r7.s64 + -1;
loc_8281EAB8:
	ctx.r7.u64 = PPC_FAST_INLINE_LOAD_U8(ctx.r9.u32 + 0);
	ctx.cr6.compare<uint32_t>(ctx.r8.u32, 0, ctx.xer);
	ctx.r9.s64 = ctx.r9.s64 + 1;
	ctx.r8.s64 = ctx.r8.s64 + -1;
	PPC_FAST_INLINE_STORE_U8(ctx.r11.u32 + 0, ctx.r7.u8);
	ctx.r11.s64 = ctx.r11.s64 + 1;
	if (!ctx.cr6.eq) goto loc_8281EAB8;
	return false;
loc_8281EAD8:
	ctx.r7.u64 = __builtin_rotateleft64(ctx.r9.u32 | (ctx.r9.u64 << 32), 0) & 0x20;
	ctx.cr6.compare<uint32_t>(ctx.r7.u32, 0, ctx.xer);
	if (!ctx.cr6.eq) goto loc_8281EB70;
	ctx.r7.u64 = PPC_FAST_INLINE_LOAD_U8(ctx.r10.u32 + 0);
	ctx.r10.s64 = ctx.r10.s64 + 1;
	ctx.r8.u64 = ctx.r9.u32 & 0x3;
	ctx.cr6.compare<uint32_t>(ctx.r8.u32, 0, ctx.xer);
	ctx.r6.u64 = PPC_FAST_INLINE_LOAD_U8(ctx.r10.u32 + 0);
	ctx.r10.s64 = ctx.r10.s64 + 1;
	ctx.r5.u64 = PPC_FAST_INLINE_LOAD_U8(ctx.r10.u32 + 0);
	ctx.r10.s64 = ctx.r10.s64 + 1;
	if (ctx.cr6.eq) goto loc_8281EB24;
loc_8281EB08:
	ctx.r29.u64 = PPC_FAST_INLINE_LOAD_U8(ctx.r10.u32 + 0);
	ctx.r8.s64 = ctx.r8.s64 + -1;
	ctx.r10.s64 = ctx.r10.s64 + 1;
	ctx.cr6.compare<uint32_t>(ctx.r8.u32, 0, ctx.xer);
	PPC_FAST_INLINE_STORE_U8(ctx.r11.u32 + 0, ctx.r29.u8);
	ctx.r11.s64 = ctx.r11.s64 + 1;
	if (!ctx.cr6.eq) goto loc_8281EB08;
loc_8281EB24:
	ctx.r8.u64 = __builtin_rotateleft64(ctx.r9.u32 | (ctx.r9.u64 << 32), 12) & 0x10000;
	ctx.r7.u64 = __builtin_rotateleft64(ctx.r7.u32 | (ctx.r7.u64 << 32), 8) & 0xFF00;
	ctx.r8.s64 = ctx.r11.s64 - ctx.r8.s64;
	ctx.r6.u64 = ctx.r6.u32 & 0xFF;
	ctx.r29.s64 = ctx.r8.s64 - ctx.r7.s64;
	ctx.r8.u64 = __builtin_rotateleft64(ctx.r9.u32 | (ctx.r9.u64 << 32), 6) & 0x300;
	ctx.r7.u64 = ctx.r5.u32 & 0xFF;
	ctx.r9.s64 = ctx.r29.s64 - ctx.r6.s64;
	ctx.r8.u64 = ctx.r8.u64 + ctx.r7.u64;
	ctx.r9.s64 = ctx.r9.s64 + -1;
	ctx.r8.s64 = ctx.r8.s64 + 4;
loc_8281EB50:
	ctx.r7.u64 = PPC_FAST_INLINE_LOAD_U8(ctx.r9.u32 + 0);
	ctx.cr6.compare<uint32_t>(ctx.r8.u32, 0, ctx.xer);
	ctx.r9.s64 = ctx.r9.s64 + 1;
	ctx.r8.s64 = ctx.r8.s64 + -1;
	PPC_FAST_INLINE_STORE_U8(ctx.r11.u32 + 0, ctx.r7.u8);
	ctx.r11.s64 = ctx.r11.s64 + 1;
	if (!ctx.cr6.eq) goto loc_8281EB50;
	return false;
loc_8281EB70:
	ctx.r9.u64 = ctx.r9.u32 & 0x1F;
	ctx.r9.s64 = ctx.r9.s64 + 1;
	ctx.r9.u64 = __builtin_rotateleft64(ctx.r9.u32 | (ctx.r9.u64 << 32), 2) & 0xFFFFFFFC;
	ctx.cr6.compare<uint32_t>(ctx.r9.u32, 112, ctx.xer);
	if (ctx.cr6.gt) goto loc_8281EBC0;
	ctx.cr6.compare<uint32_t>(ctx.r9.u32, 0, ctx.xer);
	if (ctx.cr6.eq) return false;
loc_8281EB8C:
	ctx.r8.u64 = PPC_FAST_INLINE_LOAD_U8(ctx.r10.u32 + 0);
	ctx.r9.s64 = ctx.r9.s64 + -1;
	ctx.r10.s64 = ctx.r10.s64 + 1;
	ctx.cr6.compare<uint32_t>(ctx.r9.u32, 0, ctx.xer);
	PPC_FAST_INLINE_STORE_U8(ctx.r11.u32 + 0, ctx.r8.u8);
	ctx.r11.s64 = ctx.r11.s64 + 1;
	if (!ctx.cr6.eq) goto loc_8281EB8C;
	return false;
loc_8281EBC0:
	ctx.r9.u64 = ctx.r8.u32 & 0x3;
	ctx.cr6.compare<uint32_t>(ctx.r9.u32, 0, ctx.xer);
	if (ctx.cr6.eq) return true;
loc_8281EBCC:
	ctx.r8.u64 = PPC_FAST_INLINE_LOAD_U8(ctx.r10.u32 + 0);
	ctx.r9.s64 = ctx.r9.s64 + -1;
	ctx.r10.s64 = ctx.r10.s64 + 1;
	ctx.cr6.compare<uint32_t>(ctx.r9.u32, 0, ctx.xer);
	PPC_FAST_INLINE_STORE_U8(ctx.r11.u32 + 0, ctx.r8.u8);
	ctx.r11.s64 = ctx.r11.s64 + 1;
	if (!ctx.cr6.eq) goto loc_8281EBCC;
	return true;
}

// Forward byte order: a store may feed a later load of the same copy (overlapping
// backreferences replicate their pattern; in-place literals read updated bytes).
// Most tokens copy a few bytes, where a library call costs more than the copy.
inline void forwardCopy(uint8_t* to,const uint8_t* from,size_t count) {
    if(count>32 && (from+count<=to || to+count<=from)) {std::memcpy(to,from,count);return;}
    for(size_t i=0;i<count;++i)to[i]=from[i];
}

enum class Fast {Exact,Budget,Stop};

// Validated half-open guest windows. A page is admitted only when the inline path
// would reach exactly base+canonical for each of its bytes: an ordinary page with the
// required access (original aperture permission) and one aperture per window.
// Windows grow page by page on demand and never retry a rejected page. Write-watch
// state is checked per token, immediately before its stores, like each original store.
class Windows {
    const Runtime* runtime{};
    uint8_t* base{};
    uint32_t sourceAperture{},outputAperture{};
    uint32_t sourceLimit{},writeLow{},writeLimit{},readLow{};
    bool sourceBlocked{},writeBlocked{},readBlocked{};
    static uint64_t apertureEnd(uint32_t aperture) {
        return std::min<uint64_t>((uint64_t(aperture)+1)<<29,0xFFD00000ull);
    }
    bool plainPage(uint32_t page,uint8_t required) const {
        if(page<(0x10000u>>12) || page>=(0xFFD00000u>>12) || page==0x82000u ||
           page==(Runtime::threadObjectType>>12))return false;
        return (runtime->pageAccess.load(page,std::memory_order_acquire)&required)==required;
    }
    // Grow [.., limit) upward in whole pages until it covers `wanted`.
    bool growUp(uint32_t& limit,bool& blocked,uint32_t aperture,uint64_t wanted,uint8_t required) {
        if(wanted<=limit)return true;
        if(blocked)return false;
        const uint64_t end=apertureEnd(aperture);
        if(wanted>end){blocked=true;return false;}
        // Validate a little ahead so short tokens do not revisit the table.
        const uint64_t target=std::min<uint64_t>(end,(wanted+0x7FFF)&~uint64_t(pageSize-1));
        while(limit<target) {
            if(!plainPage(limit>>12,required)) {blocked=true;break;}
            limit+=pageSize;
        }
        return wanted<=limit;
    }
public:
    Windows(const PPCContext& ctx,uint8_t* guestBase) {
        runtime=Simpsons::active;
        if(!runtime || guestBase!=runtime->base) {runtime=nullptr;return;}
        base=guestBase;
        const uint32_t source=ctx.r10.u32,output=ctx.r11.u32;
        sourceAperture=source>>29;outputAperture=output>>29;
        sourceLimit=source&~(pageSize-1);
        writeLow=writeLimit=readLow=output&~(pageSize-1);
    }
    Fast run(PPCContext& __restrict ctx);
};

Fast Windows::run(PPCContext& __restrict ctx) {
    if(!runtime || (ctx.r10.u64>>32) || (ctx.r11.u64>>32))return Fast::Exact;
    uint32_t source=ctx.r10.u32,output=ctx.r11.u32;
    if((source>>29)!=sourceAperture || (output>>29)!=outputAperture || output<writeLow)return Fast::Exact;
    const uint32_t end=ctx.r31.u32;
    const uint32_t sourceShift=apertureAdjustment[sourceAperture],outputShift=apertureAdjustment[outputAperture];
    uint64_t r5=ctx.r5.u64,r6=ctx.r6.u64,r7=ctx.r7.u64,r8=ctx.r8.u64,r9=ctx.r9.u64,r29=ctx.r29.u64;
    Fast result=Fast::Exact;
    for(;;) {
        // loc_8281E9D4 with output < end. A token header has at most four bytes,
        // all read before any of the token's stores.
        if(!growUp(sourceLimit,sourceBlocked,sourceAperture,uint64_t(source)+4,1))break;
        const uint8_t* const token=base+uint32_t(source+sourceShift);
        const uint32_t b0=token[0];
        uint32_t header,literals,length=0,distance=0,b1=0,b2=0,b3=0;
        enum {Short,Medium,Long,Run,Stop} kind;
        if(!(b0&0x80)) {
            kind=Short;header=2;b1=token[1];
            literals=b0&3;length=((b0>>2)&7)+3;distance=((b0&0x60)<<3)+b1+1;
        } else if(!(b0&0x40)) {
            kind=Medium;header=3;b1=token[1];b2=token[2];
            literals=b1>>6;length=(b0&0x3F)+4;distance=((b1&0x3F)<<8)+b2+1;
        } else if(!(b0&0x20)) {
            kind=Long;header=4;b1=token[1];b2=token[2];b3=token[3];
            literals=b0&3;length=((b0&0x0C)<<6)+b3+5;distance=((b0&0x10)<<12)+(b1<<8)+b2+1;
        } else if(b0<=0xFB) {kind=Run;header=1;literals=((b0&0x1F)+1)*4;}
        else {kind=Stop;header=1;literals=b0&3;}
        if(!growUp(sourceLimit,sourceBlocked,sourceAperture,uint64_t(source)+header+literals,1))break;
        if(!growUp(writeLimit,writeBlocked,outputAperture,uint64_t(output)+literals+length,3))break;
        const int64_t copied=int64_t(output)+literals-int64_t(distance);
        if(length) {
            if(copied<0)break;
            while(uint64_t(copied)<readLow && !readBlocked) {
                if(readLow<pageSize || ((readLow-pageSize)>>29)!=outputAperture || !plainPage((readLow>>12)-1,1)) {
                    readBlocked=true;break;
                }
                readLow-=pageSize;
            }
            if(uint64_t(copied)<readLow)break;
        }
        // Any armed page among the token's stores needs the original barrier order.
        const uint32_t canonical=output+outputShift,stored=literals+length;
        if(Simpsons::guestWatchBlocks[canonical>>16].load(std::memory_order_relaxed) ||
           Simpsons::guestWatchBlocks[(canonical+stored-1)>>16].load(std::memory_order_relaxed))break;
        // The token's whole footprint is validated plain memory: perform it in
        // the original order (header, literals, backreference) with its results.
        uint8_t* const to=base+canonical;
        if(literals)forwardCopy(to,token+header,literals);
        const uint8_t last=literals?to[literals-1]:0; // Each literal store writes the loaded byte.
        source+=header+literals;
        if(kind==Run || kind==Stop) {
            output+=literals;r7=0x20;r9=0;
            if(kind==Run)r8=last;
            else {r8=literals?last:b0;result=Fast::Stop;break;}
        } else {
            const uint64_t afterLiterals=uint64_t(output)+literals;
            uint8_t* const copyTo=to+literals;
            forwardCopy(copyTo,base+uint32_t(uint32_t(copied)+outputShift),length);
            r7=copyTo[length-1];r8=~uint64_t(0);r9=uint64_t(copied)+length;
            if(kind==Short) {if(literals)r6=last;}
            else if(kind==Medium) {r6=b2;if(literals)r5=last;}
            else {r5=b3;r6=b2;r29=afterLiterals-((b0&0x10)<<12)-(uint64_t(b1)<<8);}
            output+=literals+length;
        }
        if(output>=end) {result=Fast::Budget;break;} // loc_8281EBA8
    }
    ctx.r5.u64=r5;ctx.r6.u64=r6;ctx.r7.u64=r7;ctx.r8.u64=r8;ctx.r9.u64=r9;ctx.r29.u64=r29;
    ctx.r10.u64=source;ctx.r11.u64=output;
    if(result==Fast::Stop) ctx.cr6.compare<uint32_t>(0,0,ctx.xer);
    else ctx.cr6.compare<uint32_t>(ctx.r11.u32,ctx.r31.u32,ctx.xer);
    return result;
}
}

PPC_FUNC(sub_8281E9B0) {
	PPC_FUNC_PROLOGUE();
	PPC_TRACE_ENTRY(ctx, 0x8281E9B0);
	Simpsons::StallProfiler::Scope profile(Simpsons::StallProfiler::Section::Guest,"Native.byteDecode8281E9B0",&ctx);
	ctx.r12.u64 = ctx.lr;
	ctx.lr = 0x8281E9B8;
	__savegprlr_29(ctx, base);
	ctx.r11.u64 = PPC_FAST_INLINE_LOAD_U32(ctx.r4.u32 + 0);
	ctx.r30.u64 = ctx.r3.u64;
	ctx.r3.s64 = 1;
	ctx.r31.u64 = ctx.r11.u64 + ctx.r5.u64;
	ctx.cr6.compare<uint32_t>(ctx.r11.u32, ctx.r31.u32, ctx.xer);
	ctx.r10.u64 = PPC_FAST_INLINE_LOAD_U32(ctx.r30.u32 + 0);
	bool stopped=false;
	if(ctx.cr6.lt) {
		Windows windows(ctx,base);
		do {
			const Fast fast=windows.run(ctx);
			if(fast==Fast::Stop){stopped=true;break;}
			if(fast==Fast::Budget)break;
			if(exactToken(ctx,base)){stopped=true;break;}
			ctx.cr6.compare<uint32_t>(ctx.r11.u32, ctx.r31.u32, ctx.xer);
		} while(ctx.cr6.lt);
	}
	PPC_FAST_INLINE_STORE_U32(ctx.r30.u32 + 0, ctx.r10.u32);
	if(!stopped) ctx.r3.s64 = 0; // loc_8281EBB0; the stop token keeps r3=1 (loc_8281EBE8).
	PPC_FAST_INLINE_STORE_U32(ctx.r4.u32 + 0, ctx.r11.u32);
	__restgprlr_29(ctx, base);
}
