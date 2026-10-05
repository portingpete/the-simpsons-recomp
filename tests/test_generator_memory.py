"""Compile the real single-instruction emitter, then compile/run its output.

Run: python -B tests/test_generator_memory.py
Requires a GCC/Clang C++20 driver (CXX may name it). All build products live in
a temporary directory; no game image, parent build, or generated game code is used.
The instruction overload and ComputeMask are extracted verbatim to avoid linking
the unrelated executable loader/configuration tools into this focused test.
"""

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / "third_party/XenonRecomp"
SOURCE = VENDOR / "XenonRecomp/recompiler.cpp"
MEMORY_OPS = (
    "lvx", "lvx128", "lvewx", "lvewx128", "lvlx", "lvlx128",
    "lvrx", "lvrx128", "stvx", "stvx128", "dcbz", "dcbzl",
    "lwarx", "ldarx", "stwcx", "stdcx", "setjmp", "longjmp",
)
EXTRA_OPS = {op: op.rstrip(".").upper() for op in (
    "vslo", "vslo128", "vpkuwus", "vpkuwus128", "vpkuhus", "vpkuhus128", "vcmpgtuw", "vcmpgtuw.",
    "stfsu", "bdnzt", "bctr")}
EXTRA_OPS.update({"stfsu_pos": "STFSU", "stfsu_mmio": "STFSU", "bdnzt_external": "BDNZT"})
EXTRA_OPS.update({op: "BCTR" for op in (
    "switch_nop", "switch_r0", "switch_sparse", "switch_dense", "switch_badlabel", "bctr_first")})
EXTRA_OPS.update({op: "LI" for op in ("hook_return", "hook_continue", "hook_after")})
EXTRA_OPS.update({op: op.upper() for op in (
    "attn", "eieio", "lwsync", "sync", "tdlgei", "tdllei", "twi", "twlgei", "twllei",
    "cctpl", "cctpm", "db16cyc", "dcbst", "dcbste", "dcbf", "dcbt", "dcbtst")})
# (lane width, signed inputs, subtraction), limited to implemented add/sub cases.
INTEGER_SAT_ARITHMETIC = {
    "vaddsbs": (8, True, False), "vaddshs": (16, True, False),
    "vaddsws": (32, True, False), "vaddubs": (8, False, False),
    "vadduws": (32, False, False), "vsubshs": (16, True, True),
    "vsubsws": (32, True, True), "vsububs": (8, False, True),
    "vsubuhs": (16, False, True),
}
# (source width, signed inputs, signed result, saturating rather than modulo).
INTEGER_PACKS = {
    "vpkshss": (16, True, True, True), "vpkswss": (32, True, True, True),
    "vpkshus": (16, True, False, True), "vpkswus": (32, True, False, True),
    "vpkuhus": (16, False, False, True), "vpkuwus": (32, False, False, True),
    "vpkuwum": (32, False, False, False),
}
EXTRA_OPS.update({op: op.upper() for op in INTEGER_SAT_ARITHMETIC})
EXTRA_OPS.update({op + suffix: (op + suffix).upper()
                  for op in INTEGER_PACKS for suffix in ("", "128")})


def run(command, cwd):
    for attempt in range(5):
        result = subprocess.run(command, cwd=cwd, capture_output=True, text=True,
                                timeout=120)
        # A scanner can briefly hold clang's fresh temporary object on Windows,
        # failing its rename. Retry only that transient; other errors fail now.
        if not (result.returncode and "unable to rename temporary" in result.stderr):
            break
        time.sleep(0.5 * (attempt + 1))
    if result.returncode:
        raise AssertionError(f"Command failed ({result.returncode}): {command}\n"
                             f"{result.stdout}\n{result.stderr}")
    return result.stdout


def integer_vector_cases(op, same_sources=False):
    """Python mathematical oracle; expected bytes never use the emitter/SIMD."""
    packing = op in INTEGER_PACKS
    if packing:
        bits, signed, signed_result, saturating = INTEGER_PACKS[op]
        out_bits = bits // 2
    else:
        bits, signed, subtract = INTEGER_SAT_ARITHMETIC[op]
        out_bits, signed_result, saturating = bits, signed, True
    lanes = 128 // bits
    low = -(1 << (bits-1)) if signed else 0
    high = (1 << (bits-int(signed))) - 1
    out_low = -(1 << (out_bits-1)) if signed_result else 0
    out_high = (1 << (out_bits-int(signed_result))) - 1
    inputs = []
    if packing:
        samples = sorted({v for v in (low, low+1, high-1, high, out_low-1, out_low,
                                      out_low+1, -1, 0, 1, out_high-1, out_high,
                                      out_high+1, 0x80000000, 0xFFFF1234)
                          if low <= v <= high})
        for value in samples:
            inputs.append(([value]*lanes, [value]*lanes))
            for lane in range(lanes*2):
                combined = [j for j in range(lanes*2)]
                combined[lane] = value
                inputs.append((combined[:lanes], combined[lanes:]))
        for shift in range(len(samples)):
            combined = [samples[(j+shift) % len(samples)] for j in range(lanes*2)]
            inputs.append((combined[:lanes], combined[lanes:]))
    else:
        samples = sorted({v for v in (low, low+1, -1, 0, 1, high//2,
                                      high//2+1, high-1, high) if low <= v <= high})
        pairs = [(a, b) for a in samples for b in samples]
        inputs += [([a]*lanes, [b]*lanes) for a, b in pairs]
        overflow = ([(high, -1), (low, 1)] if subtract else [(high, 1), (low, -1)]) if signed else (
            [(0, 1)] if subtract else [(high, 1)])
        for a, b in overflow:
            for lane in range(lanes):
                va, vb = list(range(lanes)), [0]*lanes
                va[lane], vb[lane] = a, b
                inputs.append((va, vb))
        for shift in range(len(pairs)):
            inputs.append(([pairs[(j+shift) % len(pairs)][0] for j in range(lanes)],
                           [pairs[(j+shift) % len(pairs)][1] for j in range(lanes)]))

    def vector(values, width):
        guest = b"".join((v & ((1 << width)-1)).to_bytes(width//8, "big") for v in values)
        assert len(guest) == 16
        return guest[::-1]  # runtime stores whole vectors in reversed byte order

    cases = []
    seen = set()
    for a, b in inputs:
        if same_sources:
            b = a
        key = tuple(a), tuple(b)
        if key in seen:
            continue
        seen.add(key)
        mathematical = a+b if packing else [x-y if subtract else x+y for x, y in zip(a, b)]
        sat = saturating and any(x < out_low or x > out_high for x in mathematical)
        result = [max(out_low, min(out_high, x)) if saturating else x & out_high
                  for x in mathematical]
        cases.append((vector(a, bits), vector(b, bits), vector(result, out_bits), sat))
    return cases


DRIVER = r'''
int main(int argc, char** argv) {
    if (argc < 3 || argc > 7) return 2;
    const std::string name = argv[1];
    const std::unordered_map<std::string, int> ids = { @IDS@ };
    Recompiler compiler;
    compiler.config.setJmpAddress = 0x5000;
    compiler.config.longJmpAddress = 0x6000;
    powerpc_opcode opcode{};
    opcode.name = argv[1];
    opcode.id = ids.at(name);
    ppc_insn insn{};
    insn.opcode = &opcode;
    insn.operands[0] = argc >= 4 ? std::stoul(argv[3]) : 6;
    insn.operands[1] = std::stoul(argv[2]);
    insn.operands[2] = argc == 7 ? std::stoul(argv[6]) : 5;
    if (argc >= 6) insn.instruction = std::stoul(argv[5]);
    if (name == "dcbz" || name == "dcbzl") {
        insn.operands[0] = insn.operands[1];
        insn.operands[1] = 5;
    }
    if (name == "setjmp") insn.operands[0] = 0x5000;
    if (name == "longjmp") insn.operands[0] = 0x6000;
    if (name.starts_with("stfsu")) {
        insn.operands[2] = insn.operands[1];
        insn.operands[1] = name == "stfsu_pos" ? 48 : uint32_t(-4);
    }
    Function fn(0x1000, 0x24);
    if (name.starts_with("hook_")) {
        insn.operands[0] = 6;
        insn.operands[1] = 123;
        RecompilerMidAsmHook hook;
        hook.name = "NativeHook";
        hook.registers = {"ctx", "base"};
        hook.ret = name == "hook_return";
        hook.returnOnTrue = name == "hook_continue";
        hook.afterInstruction = name == "hook_after";
        compiler.config.midAsmHooks.emplace(0x1004, hook);
    }
    uint32_t data[3]{};
    if (name == "switch_nop" || name == "switch_sparse") data[0] = 0x00000060;
    if (name == "switch_r0") data[0] = 0x07008038;
    if (name == "stfsu_mmio") data[2] = Recompiler::c_eieio;
    auto table = compiler.config.switchTables.end();
    if (name == "switch_dense" || name == "switch_sparse" || name == "switch_badlabel") {
        RecompilerSwitchTable recovered{};
        recovered.r = 0; recovered.labels = {0x1010, 0x1020};
        if (name == "switch_sparse") recovered.values = {4, 12};
        table = compiler.config.switchTables.emplace(0x1004, recovered).first;
        if (name != "switch_badlabel") compiler.instructionAddresses.insert(0x1010);
        compiler.instructionAddresses.insert(0x1020);
    }
    if (name.starts_with("bdnzt")) {
        insn.operands[0] = insn.operands[1]; insn.operands[1] = 0x1010;
        if (name == "bdnzt_external")
            compiler.image.symbols.emplace("tail_target", 0x1010, 4, Symbol_Function);
        else compiler.instructionAddresses.insert(0x1010);
    }
    if (argc >= 5 && std::string(argv[4]) == "local") {
        compiler.config.ctrAsLocalVariable = true;
        compiler.config.crRegistersAsLocalVariables = true;
        compiler.config.nonArgumentRegistersAsLocalVariables = true;
        compiler.config.nonVolatileRegistersAsLocalVariables = true;
    }
    RecompilerLocalVariables locals{};
    CSRState csr = CSRState::Unknown;
    if (!compiler.Recompile(fn, name == "bctr_first" ? 0x1000 : 0x1004,
                            insn, name == "bctr_first" ? data : data+1, table, locals, csr)) return 3;
    std::cout << "BEGIN_EMITTED\n" << compiler.out << "END_EMITTED\n" << compiler.diagnosticCount;
    return 0;
}
'''


# A deliberately small ABI fixture. The checked pointer maps a null base into
# separate storage, so a surviving raw base+ dereference cannot accidentally pass.
RUNTIME = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <csetjmp>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <x86/ssse3.h>
union PPCRegister {
    uint64_t u64; int64_t s64; uint32_t u32; int32_t s32;
    uint16_t u16; uint8_t u8; float f32; double f64;
};
struct alignas(16) Vector {
    union { uint8_t u8[16]; uint16_t u16[8]; uint32_t u32[4]; float f32[4]; };
};
struct Condition { bool lt{}, gt{}, eq{}, so{}; };
struct FPSCR { void disableFlushMode() {} };
struct PPCContext {
    @REGISTERS@
    PPCRegister reserved{}, ctr{};
    Condition cr0{}, cr1{}, cr2{}, cr3{}, cr4{}, cr5{}, cr6{}, cr7{}, xer{};
    FPSCR fpscr;
    @VSCR_SAT@
    uint64_t lr{};
};
@MASKS@
alignas(128) uint8_t memory[512];
uint32_t address;
unsigned width, calls, limit;
bool write_access, read_only;
struct Fault {};
struct Jump {};
unsigned jump_calls;
uint8_t* PPCGuestPointer(uint8_t* base, uint32_t addr, unsigned size, bool write) {
    assert(base == nullptr);
    address = addr; width = size; write_access = write; ++calls;
    if (uint64_t(addr) + size > limit || (write && read_only)) throw Fault{};
    return memory + addr;
}
int TestSetJmp(jmp_buf& buffer) {
    assert(reinterpret_cast<uint8_t*>(&buffer) == memory + address);
    ++jump_calls; return 0;
}
void TestLongJmp(jmp_buf& buffer, int value) {
    assert(reinterpret_cast<uint8_t*>(&buffer) == memory + address);
    assert(value == 7); ++jump_calls; throw Jump{};
}
#undef setjmp
#define setjmp(buffer) TestSetJmp(buffer)
#define longjmp(buffer, value) TestLongJmp(buffer, value)
using Operation = void(*)(PPCContext&, uint8_t*);
void store32(uint8_t* base, uint32_t addr, uint32_t value) {
    auto ptr = PPCGuestPointer(base, addr, 4, true);
    for (unsigned i = 0; i < 4; ++i) ptr[i] = uint8_t(value >> (24-8*i));
}
#define PPC_STORE_U32(addr, value) store32(base, uint32_t(addr), uint32_t(value))
#define PPC_MM_STORE_U32(addr, value) PPC_STORE_U32(addr, value)
unsigned indirect_calls, traps;
struct Trap {};
uint32_t trap_address;
const char* trap_reason;
[[noreturn]] void TestTrap(uint32_t addr, const char* reason) {
    ++traps; trap_address = addr; trap_reason = reason; throw Trap{};
}
#define PPC_RECOMP_FAILURE(ctx, addr, reason) TestTrap(addr, reason)
#define PPC_CALL_INDIRECT_FUNC(addr) (++indirect_calls)
void tail_target(PPCContext& ctx, uint8_t*) { ctx.r6.u64 = 2; }
void reset() {
    for (unsigned i = 0; i < sizeof(memory); ++i) memory[i] = uint8_t(i);
    calls = jump_calls = 0; limit = sizeof(memory); read_only = false;
}
void checked(uint32_t addr, unsigned size, bool write) {
    assert(calls == 1 && address == addr && width == size && write_access == write);
}
void denied(Operation op, PPCContext ctx, unsigned size, bool write) {
    uint8_t before[sizeof(memory)]; memcpy(before, memory, sizeof(memory));
    calls = 0;
    try { op(ctx, nullptr); assert(!"access should have failed"); } catch (Fault&) {}
    assert(calls == 1 && width == size && write_access == write);
    assert(memcmp(before, memory, sizeof(memory)) == 0);
}
'''


MEMORY_CHECKS = r'''
void vectors(Operation load, Operation left, Operation right, Operation store, bool zero_ra) {
    for (unsigned offset = 0; offset < 16; ++offset) {
        reset(); PPCContext ctx{};
        ctx.r0.u32 = 0xDEADBEEF;
        ctx.r4.u32 = 0xFFFFFFF0;
        ctx.r5.u32 = (zero_ra ? 32 : 48) + offset;
        load(ctx, nullptr); checked(32, 16, false);
        for (unsigned j = 0; j < 16; ++j) assert(ctx.v6.u8[15-j] == memory[32+j]);
        calls = 0; left(ctx, nullptr); checked(32, 16, false);
        for (unsigned j = 0; j < 16; ++j)
            assert(ctx.v6.u8[15-j] == (j + offset < 16 ? memory[32+j+offset] : 0));
        calls = 0; right(ctx, nullptr);
        if (offset) checked(32, 16, false); else assert(calls == 0);
        for (unsigned j = 0; j < 16; ++j)
            assert(ctx.v6.u8[15-j] == (j >= 16-offset ? memory[32+j-(16-offset)] : 0));
        for (unsigned j = 0; j < 16; ++j) ctx.v6.u8[j] = uint8_t(200+j);
        calls = 0; store(ctx, nullptr); checked(32, 16, true);
        for (unsigned j = 0; j < 16; ++j) assert(memory[32+j] == uint8_t(215-j));
        assert(memory[31] == 31 && memory[48] == 48);
        limit = 47; denied(load, ctx, 16, false); denied(left, ctx, 16, false);
        denied(store, ctx, 16, true);
        if (offset) denied(right, ctx, 16, false);
        limit = sizeof(memory); read_only = true; denied(store, ctx, 16, true);
    }
    reset(); PPCContext ctx{};
    ctx.r5.u32 = 0xFFFFFFF0; limit = 0;
    right(ctx, nullptr); assert(calls == 0); // aligned lvrx must not even validate
    denied(load, ctx, 16, false);
}
void zeros(Operation op, unsigned size, bool zero_ra) {
    reset(); PPCContext ctx{};
    ctx.r0.u32 = 0xDEADBEEF; ctx.r4.u32 = 0xFFFFFFF0;
    ctx.r5.u32 = (zero_ra ? 128 : 144) + 17;
    op(ctx, nullptr); checked(128, size, true);
    for (unsigned i = 0; i < sizeof(memory); ++i)
        assert(memory[i] == (i >= 128 && i < 128+size ? 0 : uint8_t(i)));
    limit = 128+size-1; denied(op, ctx, size, true);
    limit = sizeof(memory); read_only = true; denied(op, ctx, size, true);
}
void reservations(Operation load, Operation store, unsigned size, bool zero_ra) {
    reset(); PPCContext ctx{};
    ctx.r0.u32 = 0xDEADBEEF; ctx.r4.u32 = 0xFFFFFFF0;
    ctx.r5.u32 = zero_ra ? 32 : 48;
    ctx.r6.u64 = UINT64_MAX;
    load(ctx, nullptr); checked(32, size, false);
    uint64_t expected = 0;
    for (unsigned i = 0; i < size; ++i) expected = (expected << 8) | memory[32+i];
    assert(ctx.r6.u64 == expected);
    assert(memcmp(&ctx.reserved, memory+32, size) == 0); // native raw reservation
    ctx.r6.u64 = 0x98ABCDEF76543210ull; ctx.xer.so = true;
    ctx.cr0 = {true, true, false, false};
    calls = 0; store(ctx, nullptr); checked(32, size, true);
    assert(!ctx.cr0.lt && !ctx.cr0.gt && ctx.cr0.eq && ctx.cr0.so);
    for (unsigned i = 0; i < size; ++i)
        assert(memory[32+i] == uint8_t(ctx.r6.u64 >> (8*(size-i-1))));
    calls = 0; ctx.xer.so = false; store(ctx, nullptr); checked(32, size, true);
    assert(!ctx.cr0.eq && !ctx.cr0.so); // old reservation no longer matches
    limit = 32+size-1; denied(load, ctx, size, false); denied(store, ctx, size, true);
    limit = sizeof(memory); read_only = true; denied(store, ctx, size, true);
}
void jumps(Operation save, Operation restore) {
    reset(); PPCContext ctx{}; ctx.r3.u32 = 32;
    save(ctx, nullptr); checked(32, sizeof(jmp_buf), true);
    assert(jump_calls == 1 && ctx.r3.u64 == 0);
    ctx.r3.u32 = 32; ctx.r4.s32 = 7; calls = 0;
    try { restore(ctx, nullptr); assert(false); } catch (Jump&) {}
    checked(32, sizeof(jmp_buf), false); assert(jump_calls == 2);
    limit = 32+sizeof(jmp_buf)-1;
    denied(save, ctx, sizeof(jmp_buf), true);
    denied(restore, ctx, sizeof(jmp_buf), false);
    assert(jump_calls == 2);
}
'''


SATURATION_CHECKS = r'''
void pack_unsigned(Operation op, unsigned rd, bool halfwords) {
    const unsigned lanes = halfwords ? 8 : 4;
    const uint32_t maximum = halfwords ? 255 : 65535;
    const uint32_t high_bit = halfwords ? 0x8000 : 0x80000000;
    const uint32_t samples[] = {0, 1, maximum-1, maximum, maximum+1,
                               high_bit-1, high_bit, halfwords ? 65535 : UINT32_MAX};
    for (unsigned initial = 0; initial < 2; ++initial) {
        // All legal values, isolated saturation in each A/B lane, then mixed
        // unsigned edge values. Every case is followed by a nonsaturating pack
        // in the same context to verify sticky state through a function call.
        for (unsigned pattern = 0; pattern <= lanes*2+8; ++pattern) {
            PPCContext ctx{};
            assert(ctx.vscr_sat == 0);
            ctx.vscr_sat = uint8_t(initial);
            ctx.cr6 = {1, 0, 1, 1}; ctx.r3.u64 = 0x12345678;
            bool saturated = initial != 0;
            for (unsigned step = 0; step < 2; ++step) {
                uint32_t input[16];
                for (unsigned j = 0; j < lanes*2; ++j) {
                    uint32_t value = samples[j%4];
                    if (step == 0 && pattern > 0) {
                        if (pattern <= lanes*2 && j == pattern-1) value = maximum+1;
                        if (pattern > lanes*2) value = samples[(j+pattern-lanes*2-1)%8];
                    }
                    input[j] = value;
                    saturated |= value > maximum;
                    auto& source = j < lanes ? ctx.v4 : ctx.v5;
                    if (halfwords) source.u16[lanes-1-j%lanes] = uint16_t(value);
                    else source.u32[lanes-1-j%lanes] = value;
                }
                const auto before_a = ctx.v4, before_b = ctx.v5;
                op(ctx, nullptr);
                assert(ctx.vscr_sat == unsigned(saturated));
                const auto& result = rd == 4 ? ctx.v4 : rd == 5 ? ctx.v5 : ctx.v6;
                for (unsigned j = 0; j < lanes*2; ++j) {
                    uint32_t actual = halfwords ? result.u8[lanes*2-1-j] : result.u16[lanes*2-1-j];
                    assert(actual == std::min(input[j], maximum));
                }
                if (rd != 4) assert(memcmp(&ctx.v4, &before_a, sizeof(before_a)) == 0);
                if (rd != 5) assert(memcmp(&ctx.v5, &before_b, sizeof(before_b)) == 0);
                assert(ctx.cr6.lt && !ctx.cr6.gt && ctx.cr6.eq && ctx.cr6.so);
                assert(ctx.r3.u64 == 0x12345678);
            }
            // Context copies used at native hook boundaries preserve SAT too.
            const PPCContext copied = ctx;
            assert(copied.vscr_sat == ctx.vscr_sat);
        }
    }
}
'''


SEMANTIC_CHECKS = SATURATION_CHECKS + r'''
Vector& destination(PPCContext& ctx, unsigned rd) {
    return rd == 4 ? ctx.v4 : rd == 5 ? ctx.v5 : ctx.v6;
}
void shift_octets(Operation op, unsigned rd) {
    for (unsigned shift = 0; shift < 16; ++shift) {
        PPCContext ctx{};
        for (unsigned j = 0; j < 16; ++j) {
            ctx.v4.u8[15-j] = uint8_t(j+1);
            ctx.v5.u8[j] = 0xFF;
        }
        ctx.v5.u8[0] = uint8_t((shift << 3) | 0x87); // ignored upper/lower bits
        op(ctx, nullptr);
        for (unsigned j = 0; j < 16; ++j)
            assert(destination(ctx, rd).u8[15-j] == (j+shift < 16 ? j+shift+1 : 0));
    }
}
void compare_unsigned(Operation op, unsigned rd, bool record) {
    for (unsigned mask = 0; mask < 16; ++mask) {
        PPCContext ctx{};
        ctx.cr6 = {false, true, false, true};
        for (unsigned j = 0; j < 4; ++j) {
            ctx.v4.u32[j] = mask & (1u<<j) ? 0x80000000u+j : j;
            ctx.v5.u32[j] = mask & (1u<<j) ? 0x7FFFFFFFu : (j ? UINT32_MAX : 0);
        }
        op(ctx, nullptr);
        for (unsigned j = 0; j < 4; ++j)
            assert(destination(ctx, rd).u32[j] == (mask & (1u<<j) ? UINT32_MAX : 0));
        if (record) {
            assert(ctx.cr6.lt == (mask == 15) && ctx.cr6.eq == (mask == 0));
            assert(!ctx.cr6.gt && !ctx.cr6.so);
        } else assert(!ctx.cr6.lt && ctx.cr6.gt && !ctx.cr6.eq && ctx.cr6.so);
    }
}
void store_float_update(Operation op, bool positive) {
    reset(); PPCContext ctx{};
    const uint32_t old_address = positive ? 0xFFFFFFF0 : 36;
    ctx.r4.u32 = old_address; ctx.f6.f64 = -13.25;
    op(ctx, nullptr); checked(32, 4, true);
    assert(ctx.r4.u32 == 32 && ctx.f6.f64 == -13.25);
    const uint8_t expected[] = {0xC1, 0x54, 0x00, 0x00};
    assert(memcmp(memory+32, expected, 4) == 0);
    assert(memory[31] == 31 && memory[36] == 36);
    ctx.r4.u32 = old_address; limit = 35; calls = 0;
    try { op(ctx, nullptr); assert(false); } catch (Fault&) {}
    checked(32, 4, true); assert(ctx.r4.u32 == old_address);
    limit = sizeof(memory); read_only = true; calls = 0;
    try { op(ctx, nullptr); assert(false); } catch (Fault&) {}
    assert(ctx.r4.u32 == old_address);
}
void branch_condition(Operation op, unsigned bi, unsigned taken_value) {
    const uint64_t counters[] = {0, 1, 2, 0x100000001ull, 0x100000002ull};
    for (uint64_t counter : counters) for (bool value : {false, true}) {
        PPCContext ctx{}; ctx.ctr.u64 = counter; ctx.lr = 0xFEDC;
        Condition* crs[] = {&ctx.cr0,&ctx.cr1,&ctx.cr2,&ctx.cr3,&ctx.cr4,&ctx.cr5,&ctx.cr6,&ctx.cr7};
        for (auto cr : crs) *cr = {!value, !value, !value, !value};
        Condition& selected = *crs[bi/4];
        bool* fields[] = {&selected.lt, &selected.gt, &selected.eq, &selected.so};
        *fields[bi%4] = value;
        op(ctx, nullptr);
        assert(ctx.ctr.u64 == counter-1 && ctx.lr == 0xFEDC);
        assert(ctx.r6.u64 == ((uint32_t(counter-1) != 0 && value) ? taken_value : 0));
        assert(*fields[bi%4] == value);
    }
}
void recovered_switch(Operation op, bool sparse) {
    // CMPLWI-qualified tables use a word selector even if earlier 64-bit
    // arithmetic retained upper bits. The real 8284BD30 case is LWZ of
    // FFFFFFFF followed by ADDI +1: full GPR 100000000, word index zero.
    for (uint64_t upper : {0ull, 0x100000000ull, 0xdeadbeef00000000ull, 0xffffffff00000000ull}) {
        for (unsigned i = 0; i < 2; ++i) {
            PPCContext ctx{}; const uint64_t selector = upper | (sparse ? (i ? 12 : 4) : i);
            ctx.r0.u64 = selector;
            op(ctx, nullptr); assert(ctx.r6.u64 == i+1 && ctx.r0.u64 == selector);
        }
        PPCContext ctx{}; ctx.r0.u64 = upper | (sparse ? 1 : 2);
        traps = 0;
        try { op(ctx, nullptr); assert(false); } catch (Trap&) {}
        assert(traps == 1);
    }
}
void missing_switch(Operation op) {
    PPCContext ctx{}; indirect_calls = traps = 0;
    try { op(ctx, nullptr); assert(false); } catch (Trap&) {}
    assert(traps == 1 && indirect_calls == 0);
}
'''


class GeneratorMemoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
        if not cls.compiler:
            raise unittest.SkipTest("A C++20 GCC/Clang driver is required (set CXX)")
        cls.temp = tempfile.TemporaryDirectory(prefix="generator-memory-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.source = SOURCE.read_text(encoding="utf-8")
        method = re.search(r"bool Recompiler::Recompile\(\s*const Function& fn,.*?^}\n",
                           cls.source, re.S | re.M).group()
        mask = re.search(r"static uint64_t ComputeMask\(.*?^}\n", cls.source,
                         re.S | re.M).group()
        cls.includes = [VENDOR / p for p in (
            "XenonRecomp", "XenonAnalyse", "XenonUtils", "thirdparty/disasm",
            "thirdparty/simde", "thirdparty/fmt/include", "thirdparty/tomlplusplus/include",
            "thirdparty/xxHash")]
        op_ids = {op: "BL" if op in ("setjmp", "longjmp") else op.upper() for op in MEMORY_OPS}
        op_ids.update(EXTRA_OPS)
        ids = ",".join('{"%s", PPC_INST_%s}' % item for item in op_ids.items())
        # The project PCH also includes Xbox loader structs unrelated to emission
        # (some use MSVC-only anonymous aggregates). Keep the actual class header
        # and supply only the single-instruction overload's dependencies.
        header = (VENDOR / "XenonRecomp/recompiler.h").read_text(encoding="utf-8")
        header = header.replace('#include "pch.h"', "")
        includes = "\n".join(f"#include <{h}>" for h in (
            "algorithm", "cassert", "cstring", "filesystem", "iostream", "set",
            "unordered_map", "unordered_set", "dis-asm.h", "ppc.h", "function.h", "image.h",
            "fmt/core.h", "x86/sse.h")) + "\n"
        driver = (includes + header + mask + method
                  + DRIVER.replace("@IDS@", ids))
        cls.emitter = cls.compile("emitter", driver)
        cls.emitted = {}
        for op in MEMORY_OPS:
            for ra in (0, 4):
                cls.emitted[op, ra] = cls.emit(op, ra)[0]

    @classmethod
    def emit(cls, op, ra=4, rd=6, local=False, instruction=None, rb=None):
        command = [str(cls.emitter), op, str(ra), str(rd), "local" if local else "context"]
        if instruction is not None or rb is not None:
            command.append(str(instruction or 0))
        if rb is not None:
            command.append(str(rb))
        output = run(command, cls.directory)
        emitted, diagnostics = output.split("BEGIN_EMITTED\n", 1)[1].split("END_EMITTED\n")
        return emitted, int(diagnostics)

    @classmethod
    def compile(cls, name, source):
        path = cls.directory / (name + ".cpp")
        path.write_text(source, encoding="utf-8")
        exe = cls.directory / (name + (".exe" if os.name == "nt" else ""))
        run([cls.compiler, "-std=c++20", "-O0", "-DFMT_HEADER_ONLY",
             *["-I" + str(p) for p in cls.includes], str(path), "-o", str(exe)], cls.directory)
        return exe

    def test_emitted_memory_is_checked(self):
        for (op, ra), emitted in self.emitted.items():
            with self.subTest(op=op, ra=ra):
                self.assertIn("PPCGuestPointer(base,", emitted)
                self.assertNotRegex(emitted, r"\bbase\s*\+")
                if ra == 0:
                    self.assertNotIn("ctx.r0", emitted)

    def test_full_context_hook_abi(self):
        functions = []
        for name in ("hook_return", "hook_continue", "hook_after"):
            emitted, diagnostics = self.emit(name)
            self.assertEqual(diagnostics, 0)
            self.assertIn("NativeHook(ctx, base)", emitted)
            functions.append(self.wrapper(name, emitted))
        checks = r'''
struct NativeFailure {};
unsigned hook_calls = 0;
bool NativeHook(PPCContext& ctx, uint8_t* base) {
    assert(base == nullptr);
    ++hook_calls;
    if (ctx.r3.u32 == 99) throw NativeFailure{};
    ctx.r31.u64 = 0x123456789ABCDEF0ULL;
    ctx.v6.u32[3] = 0x10203040;
    ctx.lr = 0x88776655;
    ctx.r6.u32 = 456;
    return ctx.r3.u32 == 1;
}
'''
        main = r'''
int main() {
    PPCContext ctx{};
    hook_return(ctx, nullptr);
    assert(ctx.r6.u32 == 456 && hook_calls == 1);
    assert(ctx.r31.u64 == 0x123456789ABCDEF0ULL);
    assert(ctx.v6.u32[3] == 0x10203040 && ctx.lr == 0x88776655);
    ctx.r3.u32 = 0;
    hook_continue(ctx, nullptr);
    assert(ctx.r6.u32 == 123 && hook_calls == 2);
    ctx.r3.u32 = 1;
    hook_continue(ctx, nullptr);
    assert(ctx.r6.u32 == 456 && hook_calls == 3);
    hook_after(ctx, nullptr);
    assert(ctx.r6.u32 == 456 && hook_calls == 4);
    ctx.r3.u32 = 99; ctx.r6.u32 = 789;
    try {hook_continue(ctx, nullptr); assert(false);} catch (const NativeFailure&) {}
    assert(ctx.r6.u32 == 789 && hook_calls == 5);
}
'''
        exe = self.compile("context-hooks", self.runtime() + checks + "\n".join(functions) + main)
        run([str(exe)], self.directory)

    def test_context_hook_configuration(self):
        source = (VENDOR / "XenonRecomp/recompiler_config.cpp").read_text(encoding="utf-8")
        prefix = "\n".join(f"#include <{h}>" for h in (
            "cstdint", "vector", "unordered_map", "string", "string_view", "iostream",
            "toml++/toml.h", "fmt/format.h")) + "\n"
        runner = self.compile("hook-config", prefix + source + r'''
int main(int argc, char** argv) {
    try {
        RecompilerConfig config;
        config.Load(argv[1]);
        std::cout << "CONFIG_ACCEPTED";
        return 0;
    } catch (const std::exception& e) {
        std::cout << e.what();
        return 2;
    }
}
''')
        hook = '\n[[midasm_hook]]\naddress = 4096\nname = "NativeHook"\nregisters = ["ctx", "base"]\nreturn_on_true = true\n'
        flags = ("skip_lr", "skip_msr", "ctr_as_local", "xer_as_local", "reserved_as_local",
                 "cr_as_local", "non_argument_as_local", "non_volatile_as_local")
        path = self.directory / "hook-config.toml"
        cases = [("[main]\n" + hook, 0, "CONFIG_ACCEPTED")]
        cases += [(f"[main]\n{flag} = true\n" + hook, 2, "Full-context hooks require") for flag in flags]
        cases += [("[main]\n" + hook + hook, 2, "Duplicate mid-assembly hook address"),
                  ("[main]\n" + hook.replace('["ctx", "base"]', '[""]'), 2, "Empty hook register")]
        for text, expected, message in cases:
            with self.subTest(text=text):
                path.write_text(text, encoding="utf-8")
                result = subprocess.run([str(runner), str(path)], capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                self.assertIn(message, result.stdout)

    def runtime(self):
        # Use the real byte-shuffle tables, not a test reimplementation of them.
        template = (ROOT / "runtime/ppc_context.template.h").read_text(encoding="utf-8")
        masks = "\n".join("alignas(16) " + re.search(
            r"inline uint8_t " + name + r"\[\].*?^};", template, re.S | re.M).group()
            for name in ("VectorMaskL", "VectorMaskR"))
        registers = "\n".join(f"PPCRegister r{i}{{}}, f{i}{{}};" for i in range(32))
        registers += "\n" + "\n".join(f"Vector v{i}{{}};" for i in range(128))
        # Take the SAT declaration from the real ABI, so a fixture-only field
        # cannot hide a missing runtime template update.
        sat = re.search(r"\buint8_t vscr_sat\s*=\s*0;", template).group()
        return RUNTIME.replace("@REGISTERS@", registers).replace("@MASKS@", masks).replace("@VSCR_SAT@", sat)

    def test_emitted_memory_compiles_and_runs(self):
        runtime = self.runtime()
        functions = "\n".join(
            f"void op_{op}_{ra}(PPCContext& ctx, uint8_t* base) {{\n"
            "PPCRegister temp{}; PPCContext env{};\n" + emitted + "}\n"
            for (op, ra), emitted in self.emitted.items())
        checks = []
        for ra in (0, 4):
            zero = "true" if ra == 0 else "false"
            for suffix in ("", "128"):
                for load in ("lvx", "lvewx"):
                    checks.append(f"vectors(op_{load}{suffix}_{ra}, op_lvlx{suffix}_{ra}, "
                                  f"op_lvrx{suffix}_{ra}, op_stvx{suffix}_{ra}, {zero});")
            for op, size in (("dcbz", 32), ("dcbzl", 128)):
                checks.append(f"zeros(op_{op}_{ra}, {size}, {zero});")
            for load, store, size in (("lwarx", "stwcx", 4), ("ldarx", "stdcx", 8)):
                checks.append(f"reservations(op_{load}_{ra}, op_{store}_{ra}, {size}, {zero});")
        checks.append("jumps(op_setjmp_0, op_longjmp_0);")
        runner = self.compile("memory", runtime + functions + MEMORY_CHECKS
                              + "int main() {\n" + "\n".join(checks)
                              + '\nstd::cout << "memory checks passed\\n"; }')
        self.assertIn("memory checks passed", run([str(runner)], self.directory))

    def wrapper(self, name, emitted, *, local=False, labels=False, vector_locals=(), gpr_locals=()):
        prologue = "PPCRegister temp{}; Vector vTemp{}; uint32_t ea{};\n"
        epilogue = ""
        if local:
            prologue += "auto ctr = ctx.ctr;\n"
            prologue += "\n".join(f"auto cr{i} = ctx.cr{i};" for i in range(8)) + "\n"
            epilogue = "ctx.ctr = ctr;\n" + "\n".join(f"ctx.cr{i} = cr{i};" for i in range(8))
        for register in vector_locals:
            prologue += f"auto v{register} = ctx.v{register};\n"
            epilogue += f"\nctx.v{register} = v{register};\n"
        for register in gpr_locals:
            prologue += f"auto r{register} = ctx.r{register};\n"
            epilogue += f"\nctx.r{register} = r{register};\n"
        if labels:
            emitted += ("ctx.r6.u64 = 0; goto finish;\n"
                        "loc_1010: ctx.r6.u64 = 1; goto finish;\n"
                        "loc_1020: ctx.r6.u64 = 2;\nfinish:;\n")
        return f"void {name}(PPCContext& ctx, uint8_t* base) {{\n{prologue}{emitted}{epilogue}\n}}\n"

    def test_vector_and_float_semantics(self):
        functions, checks = [], []
        for op in ("vslo", "vslo128", "vpkuwus", "vpkuwus128", "vpkuhus", "vpkuhus128", "vcmpgtuw", "vcmpgtuw."):
            for rd in (4, 5, 6):  # separate destination, and both source aliases
                for local in (False, True):
                    with self.subTest(op=op, rd=rd, local=local):
                        emitted, diagnostics = self.emit(op, 4, rd, local)
                        self.assertEqual(diagnostics, 0)
                        name = f"op_{op.replace('.', '_record')}_{rd}_{int(local)}"
                        functions.append(self.wrapper(name, emitted, local=local))
                        if op.startswith("vslo"):
                            checks.append(f"shift_octets({name}, {rd});")
                        elif op.startswith(("vpkuwus", "vpkuhus")):
                            self.assertIn("ctx.vscr_sat |=", emitted)
                            checks.append(f"pack_unsigned({name}, {rd}, {'true' if op.startswith('vpkuhus') else 'false'});")
                        else:
                            checks.append(f"compare_unsigned({name}, {rd}, {'true' if op.endswith('.') else 'false'});")
        for op in ("stfsu", "stfsu_pos", "stfsu_mmio"):
            emitted, diagnostics = self.emit(op)
            self.assertEqual(diagnostics, 0)
            self.assertIn("uint32_t(ctx.r4.u32 +", emitted)
            self.assertIn("PPC_MM_STORE_U32" if op.endswith("mmio") else "PPC_STORE_U32", emitted)
            functions.append(self.wrapper("op_" + op, emitted))
            checks.append(f"store_float_update(op_{op}, {'true' if op.endswith('pos') else 'false'});")
        invalid = subprocess.run([str(self.emitter), "stfsu", "0"], cwd=self.directory, capture_output=True, timeout=60)
        self.assertEqual(invalid.returncode, 3)  # invalid update-form RA is rejected
        runner = self.compile("semantics", self.runtime() + "".join(functions) + SEMANTIC_CHECKS
                              + "int main() {\n" + "\n".join(checks) + "\n}")
        run([str(runner)], self.directory)

    def test_saturation_with_real_context(self):
        functions, checks = [], []
        for op in ("vpkuwus", "vpkuwus128", "vpkuhus", "vpkuhus128"):
            for rd in (4, 5, 6):
                emitted, diagnostics = self.emit(op, rd=rd)
                self.assertEqual(diagnostics, 0)
                name = f"real_{op}_{rd}"
                functions.append(self.wrapper(name, emitted))
                checks.append(f"pack_unsigned({name}, {rd}, {'true' if op.startswith('vpkuhus') else 'false'});")
        self.run_with_real_context("real_context", "".join(functions) + SATURATION_CHECKS
                                   + "int main() {\n" + "\n".join(checks) + "\n}")

    def run_with_real_context(self, name, body):
        # Compile the full runtime template and emitted packs together without
        # linking the parent runtime or any game output. The mock config only
        # supplies image constants needed by the unused inline dispatch helper.
        config = self.directory / "ppc_config.h"
        config.write_text("#define PPC_CONFIG_H_INCLUDED\n#define PPC_IMAGE_BASE 0u\n"
                          "#define PPC_IMAGE_SIZE 0u\n#define PPC_CODE_BASE 0u\n"
                          "#define PPC_CODE_SIZE 0u\n", encoding="utf-8")
        template = ROOT / "runtime/ppc_context.template.h"
        source = ('#include <algorithm>\n#include <cassert>\n'
                  f'#include "{template.as_posix()}"\n'
                  'using Vector = PPCVRegister;\n'
                  'using Operation = void(*)(PPCContext&, uint8_t*);\n')
        source += body
        self.includes.append(self.directory)
        try:
            runner = self.compile(name, source)
            run([str(runner)], self.directory)
        finally:
            self.includes.pop()

    def test_integer_saturation_and_modulo(self):
        implemented = {name.lower() + suffix for name, suffix in re.findall(
            r"case PPC_INST_(V(?:ADD|SUB)[SU][BHW]S|VPK[SU][HW][SU]S)(128)?:", self.source)}
        expected = set(INTEGER_SAT_ARITHMETIC) | {
            op + suffix for op, spec in INTEGER_PACKS.items() if spec[3] for suffix in ("", "128")}
        self.assertEqual(implemented, expected)  # any new implemented case needs fixtures
        data, functions, checks = [], [], []
        for op in (*INTEGER_SAT_ARITHMETIC, *INTEGER_PACKS):
            for same in (False, True):
                corpus = integer_vector_cases(op, same)
                array = f"cases_{op}_{int(same)}"
                rows = []
                for a, b, result, sat in corpus:
                    row = ["{" + ",".join(str(byte) for byte in vector) + "}" for vector in (a, b, result)]
                    rows.append("{" + ",".join(row) + (",true}" if sat else ",false}"))
                data.append(f"const IntegerCase {array}[] = {{\n" + ",\n".join(rows) + "\n};\n")
                for suffix in (("", "128") if op in INTEGER_PACKS else ("",)):
                    mnemonic = op + suffix
                    high = 64 if suffix else 14
                    arrangements = ([(4, 4, 4, False), (high, high, high+2, True)] if same else
                                    [(4, 5, rd, False) for rd in (4, 5, 6)] +
                                    [(high, high+1, rd, True) for rd in (high, high+1, high+2)])
                    for ra, rb, rd, local in arrangements:
                        emitted, diagnostics = self.emit(mnemonic, ra, rd, local, rb=rb)
                        self.assertEqual(diagnostics, 0)
                        if op == "vpkuwum":
                            self.assertNotIn("vscr_sat", emitted)
                            self.assertIn("vTemp", emitted)
                        else:
                            self.assertIn("ctx.vscr_sat |=", emitted)
                        if local:
                            self.assertIn(f"v{ra}.", emitted)
                            self.assertNotIn(f"ctx.v{ra}.", emitted)
                        name = f"integer_{mnemonic}_{ra}_{rb}_{rd}"
                        functions.append(self.wrapper(name, emitted, local=local,
                            vector_locals=sorted({ra, rb, rd}) if local else ()))
                        checks.append(f'check_integer_vectors("{name}", {name}, {array}, {ra}, {rb}, {rd});')
        support = r'''
struct IntegerCase { uint8_t a[16], b[16], result[16]; bool saturated; };
Vector& integer_vector(PPCContext& ctx, unsigned reg) {
    switch (reg) {
    case 4: return ctx.v4; case 5: return ctx.v5; case 6: return ctx.v6;
    case 14: return ctx.v14; case 15: return ctx.v15; case 16: return ctx.v16;
    case 64: return ctx.v64; case 65: return ctx.v65; case 66: return ctx.v66;
    default: assert(false); return ctx.v4;
    }
}
template<size_t N>
void check_integer_vectors(const char* name, Operation op, const IntegerCase (&cases)[N],
                           unsigned ra, unsigned rb, unsigned rd) {
    for (unsigned initial = 0; initial < 2; ++initial) {
        for (size_t i = 0; i < N; ++i) {
            PPCContext ctx{};
            ctx.vscr_sat = uint8_t(initial);
            ctx.cr0 = {1, 1, 0, 1}; ctx.cr6 = {0, 1, 1, 1}; ctx.xer = {1, 1, 1};
            ctx.r3.u64 = 0x0123456789ABCDEFull; ctx.ctr.u64 = 17; ctx.lr = 0x98765432;
            auto& a = integer_vector(ctx, ra);
            auto& b = integer_vector(ctx, rb);
            auto& dest = integer_vector(ctx, rd);
            memcpy(&a, cases[i].a, 16); memcpy(&b, cases[i].b, 16);
            const size_t dest_offset = reinterpret_cast<uint8_t*>(&dest) - reinterpret_cast<uint8_t*>(&ctx);
            const size_t sat_offset = reinterpret_cast<uint8_t*>(&ctx.vscr_sat) - reinterpret_cast<uint8_t*>(&ctx);
            const uint8_t expected_sat = uint8_t(initial || cases[i].saturated);
            for (unsigned step = 0; step < 2; ++step) {
                // A subsequent nonsaturating call must preserve the sticky bit.
                if (step) { memset(&a, 0, 16); memset(&b, 0, 16); }
                unsigned char expected[sizeof(ctx)]; memcpy(expected, &ctx, sizeof(ctx));
                if (step) memset(expected + dest_offset, 0, 16);
                else memcpy(expected + dest_offset, cases[i].result, 16);
                expected[sat_offset] = expected_sat;
                op(ctx, nullptr);
                if (memcmp(expected, &ctx, sizeof(ctx)) != 0) {
                    fprintf(stderr, "%s case=%zu prior=%u step=%u SAT=%u expected=%u\n",
                            name, i, initial, step, unsigned(ctx.vscr_sat), unsigned(expected_sat));
                    assert(!"integer result/SAT/context mismatch");
                }
            }
        }
    }
}
'''
        self.run_with_real_context("integer_sat", support + "".join(data) + "".join(functions)
                                   + "int main() {\n" + "\n".join(checks) + "\n}")

    def test_branch_and_switch_semantics(self):
        functions, checks = [], []
        for bi in range(32):
            for local in (False, True):
                emitted, diagnostics = self.emit("bdnzt", bi, local=local)
                self.assertEqual(diagnostics, 0)
                name = f"branch_{bi}_{int(local)}"
                functions.append(self.wrapper(name, emitted, local=local, labels=True))
                checks.append(f"branch_condition({name}, {bi}, 1);")
        emitted, diagnostics = self.emit("bdnzt_external", 10)
        self.assertEqual(diagnostics, 0)
        functions.append(self.wrapper("external_branch", emitted, labels=True))
        checks.append("branch_condition(external_branch, 10, 2);")
        for op in ("switch_sparse", "switch_dense", "switch_nop", "switch_r0", "switch_badlabel", "bctr", "bctr_first"):
            emitted, diagnostics = self.emit(op)
            missing = op in ("switch_nop", "switch_r0", "switch_badlabel")
            self.assertEqual(diagnostics, int(missing), op)
            functions.append(self.wrapper(op, emitted, labels=True))
            if missing:
                self.assertNotIn("PPC_CALL_INDIRECT_FUNC", emitted)
                checks.append(f"missing_switch({op});")
            elif op.startswith("switch_"):
                checks.append(f"recovered_switch({op}, {'true' if op.endswith('sparse') else 'false'});")
                local_emitted, local_diagnostics = self.emit(op, local=True)
                self.assertEqual(local_diagnostics, 0)
                functions.append(self.wrapper("local_" + op, local_emitted, local=True, labels=True, gpr_locals=(0,)))
                checks.append(f"recovered_switch(local_{op}, {'true' if op.endswith('sparse') else 'false'});")
                if op.endswith("sparse"):
                    self.assertIn("case 4:", emitted)
                    self.assertIn("case 12:", emitted)
                    self.assertNotIn("case 1:", emitted)
            else:
                self.assertIn("PPC_CALL_INDIRECT_FUNC", emitted)
                checks.append(f"{{ PPCContext ctx{{}}; indirect_calls = 0; {op}(ctx, nullptr); assert(indirect_calls == 1); }}")
        runner = self.compile("control", self.runtime() + "".join(functions) + SEMANTIC_CHECKS
                              + "int main() {\n" + "\n".join(checks) + "\n}")
        run([str(runner)], self.directory)

    def test_traps_ordering_and_hints(self):
        # Independent integer oracle: normalize guest words in Python, including
        # sign extension of SI for unsigned comparisons. No generated expression
        # or fixture C++ comparison is reused to decide the expected result.
        inputs = (0, 1, 32767, 32768, 0xFFFFFFFF, 0x100000000, 0x7FFFFFFF,
                  0x80000000, 0x7FFFFFFFFFFFFFFF, 0x8000000000000000,
                  0xFFFFFFFFFFFF7FFF, 0xFFFFFFFFFFFF8000, 0xFFFFFFFFFFFFFFFF,
                  0xFFFFFFFF00000000, 0x00000000FFFF8000)
        functions, checks = [], []
        for op, bits, options in (("twi", 32, range(32)), ("tdlgei", 64, (5,)),
                                  ("tdllei", 64, (6,)), ("twlgei", 32, (5,)),
                                  ("twllei", 32, (6,))):
            for to in options:
                for immediate in (-32768, -1, 0, 1, 32767):
                    mask = (1 << bits) - 1
                    expected = 0
                    for i, raw in enumerate(inputs):
                        unsigned = raw & mask
                        signed = (unsigned ^ (1 << (bits-1))) - (1 << (bits-1))
                        conditions = (16 if signed < immediate else 0) | (
                            8 if signed > immediate else 0) | (4 if signed == immediate else 0) | (
                            2 if unsigned < (immediate & mask) else 0) | (
                            1 if unsigned > (immediate & mask) else 0)
                        expected |= bool(to & conditions) << i
                    for ra in (0, 4):
                        instruction = ((2 if bits == 64 else 3) << 26) | (to << 21) | (
                            ra << 16) | (immediate & 0xFFFF)
                        # Deliberately unrelated operand values: trap aliases
                        # must obtain all fields from the decoded word itself.
                        emitted, diagnostics = self.emit(op, 19, 23, instruction=instruction)
                        self.assertEqual(diagnostics, 0)
                        if to:
                            self.assertIn(f"ctx.r{ra}.", emitted)
                            self.assertIn("PPC_RECOMP_FAILURE", emitted)
                            self.assertNotIn("ctx.r19", emitted)
                            self.assertNotIn("ctx.r23", emitted)
                        else:
                            self.assertNotIn("PPC_RECOMP_FAILURE", emitted)
                        name = f"trap_{op}_{to}_{immediate & 0xFFFF}_{ra}"
                        functions.append(self.wrapper(name, emitted + "++continued;\n"))
                        checks.append(f"trap_condition({name}, {ra}, {expected});")

        emitted, diagnostics = self.emit("attn")
        self.assertEqual(diagnostics, 1)
        self.assertIn("unsupported ATTN", emitted)
        functions.append(self.wrapper("attention", emitted + "++continued;\n"))
        checks.append("trap_condition(attention, 0, (1u << input_count)-1);")
        for op in ("eieio", "lwsync", "sync"):
            emitted, diagnostics = self.emit(op)
            self.assertEqual(diagnostics, 0)
            self.assertEqual(emitted.count("std::atomic_thread_fence(std::memory_order_seq_cst);"), 1)
            functions.append(self.wrapper(op, emitted + "++continued;\n"))
            checks.append(f"trap_condition({op}, 0, 0);")

        # The vendored opcode table identifies these as self-ORs with Rc=0.
        # Verify the premise for the documented priority/delay hint treatment.
        decoder = (VENDOR / "thirdparty/disasm/ppc-dis.c").read_text(encoding="utf-8")
        for op, register in (("cctpl", 1), ("cctpm", 2), ("db16cyc", 31)):
            word = int(re.search(r'\{\s*"' + op + r'",\s*(0x[0-9a-f]+)', decoder).group(1), 16)
            self.assertEqual(word, (31 << 26) | (register << 21) | (register << 16)
                             | (register << 11) | (444 << 1))
        for op in ("cctpl", "cctpm", "db16cyc", "dcbst", "dcbste", "dcbf", "dcbt", "dcbtst"):
            emitted, diagnostics = self.emit(op)
            self.assertEqual(diagnostics, 0)
            self.assertNotIn("PPC_RECOMP_FAILURE", emitted)
            functions.append(self.wrapper(op, emitted + "++continued;\n"))
            checks.append(f"trap_condition({op}, 4, 0);")
        fixture = "unsigned continued;\nconstexpr uint64_t inputs[] = {" + ",".join(
            f"0x{value:X}ull" for value in inputs) + "};\n"
        fixture += r'''
constexpr unsigned input_count = sizeof(inputs)/sizeof(inputs[0]);
void trap_condition(Operation op, unsigned ra, uint32_t expected_mask) {
    for (unsigned i = 0; i < input_count; ++i) {
        PPCContext ctx{};
        ctx.r0.u64 = ctx.r4.u64 = 0xA5A5A5A5A5A5A5A5ull;
        (ra == 0 ? ctx.r0 : ctx.r4).u64 = inputs[i];
        ctx.r1.u64 = 0x11223344; ctx.r2.u64 = UINT64_MAX; ctx.r31.u64 = 0x76543210;
        ctx.cr0 = {true, false, true, true}; ctx.cr6 = {false, true, false, true};
        ctx.ctr.u64 = 19; ctx.lr = 0x12345678;
        unsigned char before[sizeof(ctx)]; memcpy(before, &ctx, sizeof(ctx));
        bool caught = false;
        continued = traps = calls = 0;
        try { op(ctx, nullptr); } catch (Trap&) { caught = true; }
        const bool expected = (expected_mask >> i) & 1;
        assert(caught == expected && traps == unsigned(expected));
        assert(continued == unsigned(!expected) && calls == 0);
        assert(memcmp(before, &ctx, sizeof(ctx)) == 0);
        if (caught) {
            assert(trap_address == 0x1004);
            assert(strstr(trap_reason, "conditional trap") || strstr(trap_reason, "ATTN"));
        }
    }
}
'''
        runner = self.compile("traps", self.runtime() + fixture + "".join(functions)
                              + "int main() {\n" + "\n".join(checks) + "\n}")
        run([str(runner)], self.directory)


if __name__ == "__main__":
    unittest.main(verbosity=2)
