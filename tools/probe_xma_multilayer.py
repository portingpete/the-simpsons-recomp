"""Bounded original EA-XMA layers through the owned native codec and stock CLI.

Only writes build/audio-multilayer. No device, EOF on raw contexts, game build,
reference DLL, or change to installed codec/originals. Four-channel originals
are all looping: the selected case is a contiguous four-block loop-body prefix,
not a nonloop file or a loop-replay test. --self-test performs no writes.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import unittest

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
ORIGINAL = ROOT / "Simpsons Game, The (USA)"
OUT = ROOT / "build/audio-multilayer"
INSTALL = ROOT / "build/audio-codec/install"
CLI = Path("C:/Program Files (x86)/Steam/steamapps/common/ShareX/ShareX/ffmpeg.exe")
CASES = (
    {"label": "stereo", "path": "audiostreams/fe_xxx_0/d_mvfe_xxx_000091c.exa.snu",
     "sha256": "01b6d13b00b790e0e3d76a957e96186ab5baec59269d7af537d8a889d66e937d",
     "channels": 2, "samples": 89999, "loop": False},
    {"label": "four-prefix", "path": "audiostreams/80b_crow/amb_80b_crowd_qd_01.exa.snu",
     "sha256": "33b002d487566dd1fe52116028925fafbc75dd17db5df9a79befb9195c66745f",
     "channels": 4, "samples": 997219, "loop": True, "first_block": 1, "blocks": 4},
    {"label": "six-mus52", "path": "audiostreams/bin.mus", "mus_index": 52,
     "sha256": "df2a94000d13059fca163d17eeaf4892a3bc0a4d3bf2c920016e953625f3d965",
     "channels": 6, "samples": 9305, "loop": False},
)


def need(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def artifact(name):
    need(name and Path(name).name == name and not any(c in name for c in "\\/"), "Unsafe output name")
    root = OUT.resolve()
    need(root == ROOT.resolve() / "build/audio-multilayer", "Output root redirected")
    path = OUT / name
    need(path.resolve().parent == root, "Output path redirected")
    return path


def write(name, data):
    artifact(name).write_bytes(data)


def json_bytes(value):
    return (json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n").encode()


def word(data, offset):
    need(0 <= offset <= len(data)-4, "Truncated word")
    return int.from_bytes(data[offset:offset+4], "big")


def split_blocks(data, start, end, channels):
    """Strict byte framing only; preserve every payload bit, record FF restoration."""
    need(channels in (1, 2, 4, 6) and 0 <= start < end <= len(data), "Unsupported extent/channels")
    result = []
    offset = start
    while offset < end:
        raw = word(data, offset)
        flag, length = raw >> 24, raw & 0xffffff
        need(flag in (0, 0x80) and length >= 8 and offset+length <= end, "Invalid EA block extent/flag")
        count = word(data, offset+4)
        need(count > 0, "Zero block sample extent")
        pos = offset+8
        layers = []
        for index in range((channels+1)//2):
            need(pos+4 <= offset+length, "Truncated layer header")
            header = word(data, pos)
            size = header >> 2
            need(header & 3 == 3 and size >= 8 and pos+size <= offset+length, "Invalid EA layer extent/rate")
            payload = data[pos+4:pos+size]
            need(payload[:4] == b"\x08\0\0\0", "Unqualified layer packet prefix")
            layers.append({"index": index, "layer_header_offset": pos,
                           "layer_header_word": header, "payload_offset": pos+4,
                           "payload_bytes": len(payload), "payload_sha256": sha(payload),
                           "restored_ff_bytes": (-len(payload)) % 2048})
            pos += size
        padding = data[pos:offset+length]
        need(not padding or flag == 0x80 and len(padding) <= 63 and not any(padding), "Invalid EA block padding")
        result.append({"offset": offset, "bytes": length, "flag": flag, "samples": count, "layers": layers})
        offset += length
    need(offset == end, "EA extent not exhausted")
    return result


def packets_for(data, blocks, layer):
    packets = bytearray()
    provenance = []
    for block in blocks:
        span = block["layers"][layer]
        start, size = span["payload_offset"], span["payload_bytes"]
        payload = data[start:start+size]
        need(len(payload) == size and sha(payload) == span["payload_sha256"], "Layer payload changed")
        at = len(packets)
        packets += payload + b"\xff" * span["restored_ff_bytes"]
        provenance.append({"block_offset": block["offset"], "block_samples": block["samples"],
                           **span, "adapter_offset": at})
        need(packets[at:at+size] == payload, "Original packet bits changed")
    need(0 < len(packets) <= 256*1024 and len(packets) % 2048 == 0, "Packet probe exceeds finite bound")
    return bytes(packets), provenance


def riff(packets, channels=2):
    # A NEW diagnostic envelope; no encoded/play/loop count limits or remix.
    need(channels in (1, 2), "Diagnostic RIFF is one layer only")
    fmt = bytearray(52)
    struct.pack_into("<HHIIHHH", fmt, 0, 0x166, channels, 48000, 0, channels*2, 16, 34)
    struct.pack_into("<HIII", fmt, 18, 1, 4 if channels == 1 else 3, 0, 65536)
    fmt[49] = 4
    struct.pack_into("<H", fmt, 50, (len(packets)+65535)//65536)
    body = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(packets)) + packets
    return b"RIFF" + struct.pack("<I", len(body)) + body


CPP = r'''
#include "audio/native_xma_codec.h"
#include <windows.h>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace Simpsons::Audio;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
    static_assert(sizeof(float)==4 && std::endian::native==std::endian::little);
    need(argc==7,"mode/channels/packet-input/PCM-output/CSV-output/split required");
    std::string mode=argv[1];need(mode=="xma1"||mode=="xma2","bad mode");
    uint32_t channels=uint32_t(std::stoul(argv[2]));need(channels==1||channels==2,"bad channels");
    const bool split=std::string(argv[6])=="1";need(split||std::string(argv[6])=="0","bad schedule");
    for(auto name:{"avcodec-simpsonsxma-62.dll","avutil-simpsonsxma-60.dll","libwinpthread-1.dll"}){
        char path[32768]{};need(GetModuleFileNameA(GetModuleHandleA(name),path,sizeof(path))!=0,"module identity missing");
        std::cout<<"module "<<name<<' '<<path<<'\n';
    }
    std::ifstream in(argv[3],std::ios::binary|std::ios::ate);need(bool(in),"packet input open");
    const auto size=in.tellg();need(size>0 && size<=262144 && size%2048==0,"bounded full packets required");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));in.seekg(0);
    need(bool(in.read(reinterpret_cast<char*>(bytes.data()),bytes.size())),"packet input read");
    NativeXmaCodec codec({channels,48000,mode=="xma1"?XmaVariant::Xma1:XmaVariant::Xma2});
    std::ofstream pcm(argv[4],std::ios::binary),csv(argv[5]);need(bool(pcm)&&bool(csv),"output open");
    csv<<"read_index,accepted_packets,frame_offset,frames\n";
    std::array<float,1024> buffer{};
    const std::array<uint32_t,8> chunks={1,7,31,511,3,127,513,17};
    size_t accepted=0,frames=0,reads=0,zeroReads=0,needDrain=0,schedule=0;
    for(int i=0;i<3;++i){need(codec.read(std::span(buffer).first(channels))==0,"empty context invented PCM");++zeroReads;}
    auto drain=[&]{size_t produced=0;
        for(unsigned attempt=0;attempt<16384;++attempt){
            const uint32_t capacity=split?chunks[schedule++%chunks.size()]:512;
            // The wrapper can return a partial retained frame and owns its tail.
            std::vector<float> output(size_t(capacity)*channels,123.0f);
            uint32_t count=codec.read(output);need(count<=capacity,"read exceeded capacity");
            if(!count){++zeroReads;return produced;}
            for(size_t i=0;i<size_t(count)*channels;++i)need(std::isfinite(output[i]),"nonfinite PCM");
            for(size_t i=size_t(count)*channels;i<output.size();++i)need(output[i]==123.0f,"partial read overwrote guard");
            pcm.write(reinterpret_cast<const char*>(output.data()),size_t(count)*channels*sizeof(float));
            csv<<reads++<<','<<accepted<<','<<frames<<','<<count<<'\n';
            frames+=count;produced+=count;need(frames<=262144,"decoded frame bound exceeded");
        }throw std::runtime_error("drain progress bound exceeded");};
    for(size_t offset=0;offset<bytes.size();offset+=2048){
        for(unsigned retry=0;;++retry){need(retry<128,"send retry bound exceeded");
            auto result=codec.send(std::span(bytes).subspan(offset,2048));
            if(result==PacketResult::Accepted)break;
            need(result==PacketResult::NeedDrain,"unknown packet result");++needDrain;
            need(drain()>0,"send/read made no progress");
        }
        ++accepted;
        // Alternate schedule intentionally sends ahead to exercise backpressure.
        if(!split || accepted%3==0)drain();
        if(!split){for(int i=0;i<2;++i){need(codec.read(std::span(buffer).first(channels))==0,"underflow generated PCM");++zeroReads;}}
    }
    drain();for(int i=0;i<3;++i){need(codec.read(std::span(buffer).first(channels))==0,"starvation generated a tail");++zeroReads;}
    need(frames>0 && frames%512==0,"raw total not full codec frames");
    pcm.close();csv.close();need(bool(pcm)&&bool(csv),"output write failed");
    std::cout<<"result "<<accepted<<' '<<frames<<' '<<reads<<' '<<zeroReads<<' '<<needDrain<<" no_eof_sent\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"multilayer probe failure: "<<e.what()<<'\n';return 1;}}
'''


def floats(data):
    need(len(data) % 8 == 0 and len(data) <= 4*1024*1024, "Invalid/big stereo PCM extent")
    values = [v[0] for v in struct.iter_unpack("<f", data)]
    need(values and all(map(math.isfinite, values)), "Empty/nonfinite PCM")
    return values


def compare(raw, stock, offset, swapped=False):
    rn, sn = len(raw)//2, len(stock)//2
    start, end = max(0, -offset), min(sn, rn-offset)
    need(end > start, "No comparison overlap")
    channel = []
    for c in range(2):
        diffs = [raw[2*(i+offset)+(1-c if swapped else c)]-stock[2*i+c] for i in range(start,end)]
        channel.append({"index": c, "rms_difference": math.sqrt(math.fsum(x*x for x in diffs)/len(diffs)),
                        "max_abs_difference": max(map(abs,diffs)), "exact_equal_samples": sum(x == 0 for x in diffs)})
    return {"offset": offset, "channel_swapped": swapped, "overlap_frames": end-start,
            "stock_start": start, "stock_end": end, "stock_frames": sn, "raw_frames": rn,
            "raw_prefix_unpaired": start+offset, "raw_suffix_unpaired": rn-end-offset,
            "stock_suffix_unpaired": sn-end, "channels": channel}


def alignment(raw, stock):
    rn, sn = len(raw)//2, len(stock)//2
    window = 256
    stop = min(sn, rn-2048)-window
    need(stop >= 1024, "Too short for independent alignment search")
    candidates = range(1024, stop+1, 256)
    start = max(candidates, key=lambda p: math.fsum(x*x for x in stock[2*p:2*(p+window)]))
    energy = math.fsum(x*x for x in stock[2*start:2*(start+window)])
    need(energy > 1e-12, "No nontrivial interior alignment window")
    scores = []
    for offset in range(-1024,2049):
        error = math.fsum((raw[2*(i+offset)+c]-stock[2*i+c])**2
                          for i in range(start,start+window) for c in range(2))/(window*2)
        scores.append((error,offset))
    best = sorted(scores)[:5]
    offsets = sorted({0,384,512,576,640,*(o for _,o in best)})
    chosen = best[0][1]
    return {"definition": "stock frame i/channel c versus raw frame i+offset/channel c; no output trimming",
            "window_start": start, "window_frames": window, "window_energy": energy,
            "searched_offsets": [-1024,2048], "best_window_candidates": [{"offset": o,"mse": e} for e,o in best],
            "full_overlap": [compare(raw,stock,o) for o in offsets],
            "swapped_at_best": compare(raw,stock,chosen,True),
            "unpaired_stock_tail": signal_stats(stock[2*min(sn,rn-chosen):])}


def signal_stats(values):
    return {"float_values": len(values), "nonzero_values": sum(x != 0 for x in values),
            "peak": max(map(abs,values),default=0),
            "rms": math.sqrt(math.fsum(x*x for x in values)/len(values)) if values else 0}


def run(command, env, log, timeout=45):
    result = subprocess.run([str(x) for x in command], cwd=OUT, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    write(log,result.stdout+result.stderr)
    need(result.returncode == 0, f"Command failed ({result.returncode}); see {log}")
    return result


def selected_originals(inspector, inventory):
    indexed = {f["path"]: f for f in inventory["files"]}
    four = [f for f in inventory["files"] if f.get("inspection",{}).get("header",{}).get("channels")==4]
    need(len(four)==62, "Four-channel inventory profile changed")
    # Independently verify all 62 actual header bytes, without hashing/loading
    # their large audio payloads merely to establish the loop/nonloop limitation.
    for f in four:
        with (ORIGINAL/f["path"]).open("rb") as stream:
            header=inspector.eaac_header(stream.read(32),16)
        need(header["channels"]==4 and header["loop"], "Nonloop 4ch candidate exists; reconsider selection")
    for case in CASES:
        source=(ORIGINAL/case["path"]).resolve(strict=True)
        need(source.is_relative_to(ORIGINAL.resolve()),"Source escaped originals")
        data=source.read_bytes()
        need(sha(data)==case["sha256"]==indexed[case["path"]]["sha256"],"Original source hash mismatch")
        info=inspector.inspect_mus(data)["streams"][case["mus_index"]] if "mus_index" in case else inspector.inspect_snu(data)
        header,audio=info["header"],info["audio"]
        need(all(header[k]==case[k] for k in ("channels","samples","loop")) and header["sample_rate"]==48000,"Original profile changed")
        blocks=split_blocks(data,audio["audio_offset"],audio["audio_offset"]+audio["audio_size"],header["channels"])
        need(sum(b["samples"] for b in blocks)==header["samples"],"Block/header sample mismatch")
        first=case.get("first_block",0);count=case.get("blocks",len(blocks))
        chosen=blocks[first:first+count];need(len(chosen)==count,"Selected block range missing")
        if case["loop"]:
            need(first==1 and blocks[0]["flag"]==0x80 and blocks[0]["samples"]==1 and
                 chosen[0]["offset"]==audio["audio_offset"]+header["loop_offset_relative"],"Loop-body prefix profile changed")
        yield case,source,data,info,chosen,blocks


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test",action="store_true",help="Run in-memory malformed framing/footprint tests only")
    args=parser.parse_args()
    if args.self_test:
        suite=unittest.defaultTestLoader.loadTestsFromTestCase(Contracts)
        need(unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful(),"Probe self-tests failed")
        return
    artifact("report.json")
    OUT.mkdir(parents=True,exist_ok=True)
    temp=artifact("tmp");temp.mkdir(exist_ok=True)
    inspector=module("multilayer_assets",ROOT/"tools/inspect_assets.py")
    toolchain=module("multilayer_toolchain",ROOT/"tests/test_host_fp.py")
    compiler,env=toolchain.toolchain();env["TEMP"]=env["TMP"]=str(temp)
    run([sys.executable,"-B",ROOT/"tools/build_native_audio_codec.py","--verify"],env,"codec-verify-before.log")
    paths=[Path(__file__),ROOT/"tools/inspect_assets.py",ROOT/"tests/test_host_fp.py",Path(compiler),
           ROOT/"analysis/assets.json",ROOT/"audio/native_xma_codec.h",ROOT/"audio/native_xma_codec.cpp",
           ROOT/"tools/build_native_audio_codec.py",INSTALL/"PROVENANCE.json",CLI]
    paths += [INSTALL/"bin"/n for n in ("avcodec-simpsonsxma-62.dll","avutil-simpsonsxma-60.dll","libwinpthread-1.dll")]
    paths += [INSTALL/"lib"/n for n in ("avcodec-simpsonsxma.lib","avutil-simpsonsxma.lib")]
    frozen={str(p):sha(p.read_bytes()) for p in paths}
    inventory=json.loads((ROOT/"analysis/assets.json").read_text(encoding="utf-8"))
    write("harness.cpp",CPP.encode())
    command=[compiler,"/nologo","/std:c++20","/EHsc","/MD","/O2","/fp:strict","/DNOMINMAX","/DWIN32_LEAN_AND_MEAN",
             f"/I{ROOT}",f"/I{INSTALL/'include'}",artifact("harness.cpp"),ROOT/"audio/native_xma_codec.cpp",
             "/Fo"+str(OUT)+"\\","/Fe"+str(artifact("harness.exe")),"/link",
             INSTALL/"lib/avcodec-simpsonsxma.lib",INSTALL/"lib/avutil-simpsonsxma.lib","/INCREMENTAL:NO"]
    run(command,env,"compile.log",90)
    env["PATH"]=str(INSTALL/"bin")+os.pathsep+env["PATH"]
    version=run([CLI,"-version"],env,"stock-version.log")
    report={"schema":1,"scope":"bounded original per-layer software decode; no device/guest playback/hardware quantization",
            "four_channel_selection":"all62 original headers loop; four-block prefix of smallest file's loop body, no replay",
            "frozen_inputs":frozen,"compile_command":[str(x) for x in command],"stock_version":version.stdout.decode(errors="replace").splitlines()[0],
            "raw_eof_sent":False,"cases":[]}
    for case,source,data,info,blocks,allblocks in selected_originals(inspector,inventory):
        result={**case,"source_bytes":len(data),"parsed":info,"original_block_count":len(allblocks),
                "selected_block_count":len(blocks),"selected_samples":sum(b["samples"] for b in blocks),
                "selection_complete_nonloop":not case["loop"],"layers":[]}
        print('Decode',case['label'],'selected samples',result['selected_samples'],flush=True)
        for layer in range(case["channels"]//2):
            label=f"{case['label']}-layer{layer}"
            packet_data,spans=packets_for(data,blocks,layer)
            packet_path=artifact(label+".packets");write(packet_path.name,packet_data)
            wave=riff(packet_data);wave_path=artifact(label+".diagnostic.wav");write(wave_path.name,wave)
            entry={"layer_index":layer,"logical_channel_slots":[2*layer,2*layer+1],"speaker_mapping":"unassigned",
                   "packet_bytes":len(packet_data),"packet_sha256":sha(packet_data),"riff_sha256":sha(wave),"source_spans":spans,"native_runs":[]}
            for mode,split in (("xma1",0),("xma2",0),("xma2",1)):
                suffix=f"{label}-{mode}"+("-split" if split else "")
                pcm=artifact(suffix+".f32le");csv=artifact(suffix+".csv")
                execution=[artifact("harness.exe"),mode,"2",packet_path,pcm,csv,str(split)]
                execution_result=run(execution,env,suffix+".log")
                lines=execution_result.stdout.decode().splitlines()
                modules={}
                for line in lines:
                    if line.startswith("module "):
                        _,name,actual=line.split(" ",2)
                        need(Path(actual).resolve()==(INSTALL/"bin"/name).resolve(),"Loaded non-owned codec DLL")
                        modules[name]=actual
                need(len(modules)==3,"Missing loaded module identity")
                summary=next(line for line in lines if line.startswith("result ")).split()
                accepted,frames,reads,zero,backpressure=map(int,summary[1:6])
                raw_bytes=pcm.read_bytes();values=floats(raw_bytes)
                need(accepted*2048==len(packet_data) and frames*8==len(raw_bytes) and summary[6]=="no_eof_sent","Native output/accounting mismatch")
                entry["native_runs"].append({"mode":mode,"split_reads":bool(split),"command":[str(x) for x in execution],
                      "modules":modules,"accepted_packets":accepted,"raw_frames":frames,"codec_frames_512":frames//512,
                      "read_calls":reads,"zero_reads":zero,
                      "need_drain":backpressure,"pcm_sha256":sha(raw_bytes),"stats":signal_stats(values)})
            need(len({r["pcm_sha256"] for r in entry["native_runs"]})==1,"Raw variants/split schedules differ; inspect original layer")
            entry["raw_variants_and_schedules_byte_identical"]=True
            stock_cmd=[CLI,"-nostdin","-hide_banner","-loglevel","info","-xerror","-threads","1","-f","wav","-i",wave_path,
                       "-map","0:a:0","-c:a","pcm_f32le","-f","f32le","pipe:1"]
            stock=subprocess.run([str(x) for x in stock_cmd],cwd=OUT,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30)
            write(label+"-stock.log",stock.stderr);need(stock.returncode==0,"Stock diagnostic decode failed: "+label)
            write(label+"-stock.f32le",stock.stdout)
            stock_values=floats(stock.stdout)
            raw_values=floats(artifact(label+"-xma2.f32le").read_bytes())
            entry["stock"]={"command":[str(x) for x in stock_cmd],"frames":len(stock_values)//2,"pcm_sha256":sha(stock.stdout),
                            "stats":signal_stats(stock_values),"diagnostic_file_eof":True}
            entry["alignment"]=alignment(raw_values,stock_values)
            entry["raw_channel_hashes"]=[sha(b"".join(struct.pack("<f",v) for v in raw_values[c::2])) for c in range(2)]
            result["layers"].append(entry)
            best=entry["alignment"]["best_window_candidates"][0]
            print(label,'raw',len(raw_values)//2,'stock',len(stock_values)//2,'measured offset',best['offset'],flush=True)
        need(sha(source.read_bytes())==case["sha256"],"Original modified during probe")
        result["original_unchanged"]=True
        report["cases"].append(result)
    need(all(sha(Path(p).read_bytes())==h for p,h in frozen.items()),"Frozen codec/core/CLI input changed")
    run([sys.executable,"-B",ROOT/"tools/build_native_audio_codec.py","--verify"],env,"codec-verify-after.log")
    report["frozen_inputs_unchanged"]=True
    report["artifacts"]={p.name:sha(p.read_bytes()) for p in sorted(OUT.iterdir()) if p.is_file() and p.name!="report.json"}
    write("report.json",json_bytes(report))
    print('PASS six original layers, both raw variants and split schedules; report',artifact('report.json'),flush=True)


class Contracts(unittest.TestCase):
    @staticmethod
    def block():
        payload=b"\x08\0\0\0"+b"\x12\x34\x56\x78"
        layer=struct.pack(">I",(len(payload)+4)*4+3)+payload
        return struct.pack(">II",0x80000000+8+len(layer),4736)+layer

    def test_bits_and_ff_restoration(self):
        b=self.block();blocks=split_blocks(b,0,len(b),2)
        packet,spans=packets_for(b,blocks,0)
        self.assertEqual(packet[:8],b[12:]);self.assertEqual(packet[8:],b"\xff"*2040)
        self.assertEqual(spans[0]["payload_offset"],12)

    def test_malformed_framing(self):
        b=self.block()
        cases=[b[:7],b[:-1],bytes([0x40])+b[1:],b[:4]+bytes(4)+b[8:],b[:8]+bytes(4)+b[12:],b[:12]+b"BAD!"+b[16:]]
        for bad in cases:
            with self.subTest(bad=bad.hex()),self.assertRaises(ValueError):split_blocks(bad,0,len(bad),2)

    def test_layer_order_and_riff_is_new(self):
        one=self.block()[8:];other=one[:-1]+b"\x99"
        b=struct.pack(">II",0x80000000+8+len(one)+len(other),4736)+one+other
        blocks=split_blocks(b,0,len(b),4)
        a,_=packets_for(b,blocks,0);c,_=packets_for(b,blocks,1)
        self.assertNotEqual(a,c);self.assertEqual(a[-2040:],c[-2040:])
        envelope=riff(a);self.assertEqual(envelope[80:],a)
        self.assertEqual(struct.unpack_from("<H",envelope,22)[0],2)

    def test_output_bounds_and_finite(self):
        for name in ("../bad","sub/bad","sub\\bad",""):
            with self.assertRaises(ValueError):artifact(name)
        with self.assertRaises(ValueError):floats(struct.pack("<ff",float("nan"),0))


if __name__=="__main__":
    try:
        main()
    except (ValueError,OSError,RuntimeError,subprocess.SubprocessError,StopIteration) as error:
        print('FAIL multilayer probe:',error,file=sys.stderr)
        sys.exit(1)
