"""Native raw-frame/stock comparison for two pinned original mono SNU assets.

Reads the existing DarkRecomp wrapper and DLLs; never builds or changes their
project. All generated files have raw-* names under build/audio-probe. No audio
output device is opened. Initial frames are retained; offsets are measurements,
not a guest priming policy. Requires the previously recorded stock PCM outputs.
"""
from pathlib import Path
import hashlib
import importlib.util
import json
import math
import os
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/audio-probe"
REFERENCE = Path("K:/DarkRecomp")
CODEC = REFERENCE / "build_native/deps/ffmpeg-darkxma"
WRAPPER = REFERENCE / "runtime/native"
CASES = [
    ("short", "audiostreams/ri_xxx_0/d_shri_xxx_0005795.exa.snu",
     "4007581a5527caaab34b130f0c603a694f7273b1188abde36b3f5da97c6855e4",
     "918d585b866f61c2eb3eafec5fc128dee96436e73e8a926e55d01685ecae35c4", 8064),
    ("documented", "audiostreams/01_xxx_0/d_as01_xxx_0003bb5.exa.snu",
     "bfd2acf74af905f1521fbe1502c1f140a82a73f003fc1cbe2dec866e92b9f7a9",
     "4b77d13706393b94bf2e25b7f7e988bfe29cdbc7f925c995b799414605b58b9e", 54901),
]

CPP = r'''
#include "xma_raw_decoder.cpp" // Compile the read-only reference wrapper verbatim.
#include <windows.h>
#include <bit>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
static void need(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
// Independent probe of the SAME patched DLL's XMA2 entry, with its required
// 34-byte extradata. The unmodified reference wrapper above selects XMA1 with
// 28-byte extradata. Neither path changes the original packet bits.
class RawXma2Decoder {
    AVCodecContext* context{};
    AVFrame* frame{};
public:
    ~RawXma2Decoder() {av_frame_free(&frame);avcodec_free_context(&context);}
    bool open() {
        const AVCodec* codec=avcodec_find_decoder(AV_CODEC_ID_XMA2);
        if(!codec) return false;
        context=avcodec_alloc_context3(codec);frame=av_frame_alloc();
        if(!context || !frame) return false;
        context->sample_rate=48000;context->block_align=2048;
        av_channel_layout_default(&context->ch_layout,1);
        context->extradata=static_cast<uint8_t*>(av_mallocz(34+AV_INPUT_BUFFER_PADDING_SIZE));
        if(!context->extradata) return false;
        context->extradata_size=34;context->extradata[0]=1;context->extradata[2]=4;
        return av_opt_set_int(context->priv_data,"darkrecomp_raw_frames",1,0)>=0 && avcodec_open2(context,codec,nullptr)>=0;
    }
    int push(const uint8_t* bytes) {
        AVPacket* packet=av_packet_alloc();if(!packet) return -1;
        int result=av_new_packet(packet,2048);
        if(result>=0) {memcpy(packet->data,bytes,2048);result=avcodec_send_packet(context,packet);}
        av_packet_free(&packet);
        return result==AVERROR(EAGAIN)?-2:result;
    }
    int read(float* output) {
        av_frame_unref(frame);
        const int result=avcodec_receive_frame(context,frame);
        if(result==AVERROR(EAGAIN)) return 0;
        if(result<0) return result;
        need(frame->format==AV_SAMPLE_FMT_FLTP && frame->ch_layout.nb_channels==1 && frame->nb_samples==512,
             "XMA2 returned an unsupported raw frame shape");
        const auto* values=reinterpret_cast<const float*>(frame->extended_data[0]);
        need(values!=nullptr,"XMA2 frame plane missing");
        for(unsigned i=0;i<512;++i) {need(std::isfinite(values[i]),"XMA2 output nonfinite");output[i]=values[i];}
        return 512;
    }
};
int main(int argc,char** argv) {
    try {
        static_assert(sizeof(float)==4 && std::endian::native==std::endian::little);
        need(argc==4 || (argc==5 && std::string(argv[4])=="xma2"),"packet input, raw PCM output, frame CSV and optional xma2 required");
        const bool xma2=argc==5;
        for(auto name:{"avcodec-darkxma-62.dll","avutil-darkxma-60.dll","libwinpthread-1.dll"}) {
            char path[32768]{};
            need(GetModuleFileNameA(GetModuleHandleA(name),path,sizeof(path))!=0,"Loaded DLL path unavailable");
            std::cout<<"module "<<name<<" "<<path<<"\n";
        }
        std::cout<<"avcodec_version "<<avcodec_version()<<" avutil_version "<<avutil_version()<<"\n";
        std::ifstream input(argv[1],std::ios::binary|std::ios::ate);
        need(bool(input),"Input open failed");const auto size=input.tellg();
        need(size>0 && size<=65536 && size%2048==0,"Bounded whole-packet input required");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));input.seekg(0);
        need(bool(input.read(reinterpret_cast<char*>(bytes.data()),bytes.size())),"Input read failed");
        DarkRecomp::Native::XmaRawDecoder decoder;
        RawXma2Decoder decoder2;
        need(xma2?decoder2.open():decoder.open(48000,1),"Raw decoder/option unavailable");
        std::cout<<"mode "<<(xma2?"raw_xma2_34byte_extradata":"reference_raw_xma1_28byte_extradata")<<"\n";
        std::ofstream pcm(argv[2],std::ios::binary),csv(argv[3]);
        need(bool(pcm) && bool(csv),"Probe output open failed");
        csv<<"frame_index,accepted_packets,raw_sample_offset,samples\n";
        float frame[512]{};size_t frames=0,samples=0,accepted=0;
        auto read=[&]{return xma2?decoder2.read(frame):decoder.read(frame,512);};
        need(read()==0,"Decoder invented data before any packet");
        auto drain=[&] {
            size_t produced=0;
            for(unsigned attempts=0;attempts<128;++attempts) {
                const int count=read();
                if(count==0) return produced;
                need(count==512,"Raw frame was not a complete finite 512-sample frame");
                pcm.write(reinterpret_cast<const char*>(frame),sizeof(frame));
                csv<<frames<<','<<accepted<<','<<samples<<','<<count<<'\n';
                ++frames;samples+=count;produced+=count;
            }
            throw std::runtime_error("Raw frame drain exceeded bound");
        };
        for(size_t pos=0;pos<bytes.size();pos+=2048) {
            for(unsigned attempt=0;;++attempt) {
                need(attempt<128,"Packet send retry exceeded bound");
                const int result=xma2?decoder2.push(bytes.data()+pos):decoder.push(bytes.data()+pos);
                if(result==0) break;
                need(result==DarkRecomp::Native::XmaRawDecoder::needDrain && drain()>0,"Packet decode failed or backpressure made no progress");
            }
            ++accepted;drain();
        }
        // No null packet / drain-at-EOF operation: retain real initial frames
        // and do not request a synthetic overlap tail from the file decoder.
        for(unsigned i=0;i<3;++i) need(read()==0,"Starvation emitted an extra frame");
        pcm.close();csv.close();need(bool(pcm) && bool(csv),"Probe output write failed");
        std::cout<<"accepted_packets "<<accepted<<" frames "<<frames<<" samples "<<samples<<" no_eof_sent 1\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"raw native probe failure: "<<error.what()<<'\n';return 1;
    }
}
'''

def sha(data):
    return hashlib.sha256(data).hexdigest()

def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

def artifact(name):
    if not (name.startswith("raw-") and "/" not in name and "\\" not in name):
        raise ValueError(f"Unsafe artifact name: {name!r}")
    return OUT / name

def samples(data):
    if len(data) % 4 != 0:
        raise ValueError("PCM extent is not a whole float count")
    result = [x[0] for x in struct.iter_unpack("<f", data)]
    if not all(math.isfinite(x) for x in result):
        raise ValueError("Non-finite PCM sample")
    return result

def metrics(raw, stock, offset):
    # Positive offset means stock[i] compared to raw[i+offset]. Negative
    # offsets omit the initial -offset stock samples; coverage states this.
    start = max(0, -offset)
    end = min(len(stock), len(raw)-offset)
    if not (end > start):
        raise ValueError("Empty stock/raw overlap")
    differences = [raw[i+offset]-stock[i] for i in range(start, end)]
    count = end-start
    mse = math.fsum(x*x for x in differences)/count
    energy = math.fsum(stock[i]*stock[i] for i in range(start, end))/count
    return {"raw_offset_for_stock_zero": offset, "overlap_samples": count,
            "stock_first_compared": start, "stock_end_compared": end,
            "raw_prefix_unpaired": start+offset, "raw_suffix_unpaired": len(raw)-(end+offset),
            "covers_entire_stock": start==0 and end==len(stock),
            "rms_difference": math.sqrt(mse), "normalized_rms_difference": math.sqrt(mse/energy) if energy else None,
            "max_abs_difference": max(map(abs, differences)),
            "exact_float_equal_samples": sum(abs(x) < 1e-7 for x in differences)}

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    compiler, env = module("raw_probe_toolchain", ROOT / "tests/test_host_fp.py").toolchain()
    inspector = module("raw_probe_assets", ROOT / "tools/inspect_assets.py")
    provenance = json.loads((CODEC / "PROVENANCE.json").read_text(encoding="utf-8"))
    codec_source = REFERENCE / ("build_native/deps/FFmpeg-" + provenance["upstream_commit"]) / "libavcodec/wmaprodec.c"
    assert sha(codec_source.read_bytes()) == provenance["patched_decoder_sha256"]
    reference_paths = [WRAPPER / "xma_raw_decoder.cpp", WRAPPER / "xma_raw_decoder.h", codec_source,
                       CODEC / "PROVENANCE.json", CODEC / "bin/avcodec-darkxma.lib", CODEC / "bin/avutil-darkxma.lib"]
    reference_paths += [CODEC / "bin" / name for name in ("avcodec-darkxma-62.dll", "avutil-darkxma-60.dll", "libwinpthread-1.dll")]
    reference_hashes = {str(p): sha(p.read_bytes()) for p in reference_paths}
    source = artifact("raw-codec-probe.cpp");source.write_text(CPP, encoding="utf-8")
    exe = artifact("raw-codec-probe.exe")
    command = [str(compiler), "/nologo", "/std:c++20", "/EHsc", "/MD", "/O2", "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN",
               f"/I{WRAPPER}", f"/I{CODEC}/include", str(source), f"/Fo{artifact('raw-codec-probe.obj')}", f"/Fe{exe}",
               "/link", str(CODEC / "bin/avcodec-darkxma.lib"), str(CODEC / "bin/avutil-darkxma.lib"), "/INCREMENTAL:NO"]
    built = subprocess.run(command, cwd=OUT, env=env, capture_output=True, timeout=60)
    artifact("raw-compile.log").write_bytes(built.stdout+built.stderr)
    if built.returncode:
        raise RuntimeError(built.stdout.decode(errors="replace")+built.stderr.decode(errors="replace"))
    env["PATH"] = str(CODEC / "bin") + os.pathsep + env["PATH"]
    stock_report_bytes = (OUT / "report.json").read_bytes()
    stock_report = json.loads(stock_report_bytes)
    original_root = (ROOT / "Simpsons Game, The (USA)").resolve(strict=True)
    report = {"scope": "original mono raw-frame versus recorded stock float PCM; no hardware oracle or priming policy",
              "compile_command": command, "compiler": str(compiler), "reference_provenance": provenance,
              "reference_hashes": reference_hashes, "stock_report_sha256": sha(stock_report_bytes),
              "alignment_definition": "stock[i] compared with raw[i+offset]; raw data is never trimmed",
              "cases": []}
    for label, relative, source_hash, stock_hash, declared in CASES:
        original = (original_root / relative).resolve(strict=True)
        assert original.is_relative_to(original_root)
        data = original.read_bytes();assert sha(data)==source_hash
        info = inspector.inspect_snu(data)
        assert info["header"]["channels"]==1 and info["header"]["sample_rate"]==48000 and not info["header"]["loop"]
        assert info["header"]["samples"]==declared
        baseline = next(c for c in stock_report["cases"] if c["relative_path"]==relative)
        assert baseline["source_sha256"]==source_hash and baseline["pcm_sha256"]==stock_hash
        stock_bytes = (OUT / (label+".f32le")).read_bytes();assert sha(stock_bytes)==stock_hash
        stock = samples(stock_bytes);assert len(stock)==declared
        packets = bytearray()
        for span in baseline["spans"]:
            offset,length = span["payload_offset"],span["payload_bytes"]
            assert 0<=offset and 4<=length and offset+length<=len(data)
            payload = data[offset:offset+length]
            assert sha(payload)==span["payload_sha256"] and len(packets)==span["adapter_offset"]
            assert (-length)%2048==span["restored_ff_tail_bytes"]
            packets += payload + b"\xff"*span["restored_ff_tail_bytes"]
        adapter = (OUT / (label+".diagnostic.xma.wav")).read_bytes()
        assert sha(adapter)==baseline["adapter_sha256"] and adapter[72:76]==b"data" and adapter[80:]==packets
        assert len(packets)==baseline["packet_bytes"]
        packet_path = artifact("raw-"+label+".packets");packet_path.write_bytes(packets)
        pcm_path = artifact("raw-"+label+".f32le")
        frames_path = artifact("raw-"+label+".frames.csv")
        execution = [str(exe),str(packet_path),str(pcm_path),str(frames_path)]
        decoded = subprocess.run(execution, cwd=OUT, env=env, capture_output=True, timeout=20)
        log = decoded.stdout+decoded.stderr;artifact("raw-"+label+".log").write_bytes(log)
        if decoded.returncode:
            raise RuntimeError(log.decode("utf-8", errors="replace"))
        for line in decoded.stdout.decode("utf-8", errors="replace").splitlines():
            if line.startswith("module "):
                _, name, actual = line.split(" ",2)
                if Path(actual).resolve()!=(CODEC / "bin" / name).resolve():
                    raise RuntimeError("Unowned native decoder DLL")
        raw_bytes = pcm_path.read_bytes();raw = samples(raw_bytes)
        if not (raw and len(raw)%512==0):
            raise ValueError("Raw PCM extent differs")
        raw2_path = artifact("raw-"+label+"-xma2.f32le")
        raw2_execution = [str(exe),str(packet_path),str(raw2_path),str(artifact("raw-"+label+"-xma2.frames.csv")),"xma2"]
        raw2_result = subprocess.run(raw2_execution, cwd=OUT, env=env, capture_output=True, timeout=20)
        artifact("raw-"+label+"-xma2.log").write_bytes(raw2_result.stdout+raw2_result.stderr)
        if raw2_result.returncode:
            raise RuntimeError((raw2_result.stdout+raw2_result.stderr).decode(errors="replace"))
        raw2_bytes = raw2_path.read_bytes();raw2 = samples(raw2_bytes)
        assert raw2 and len(raw2)%512==0
        # Exhaustive bounded offset scan on a nontrivial 1024-sample interior
        # window, followed by full-overlap metrics for the best candidates and
        # named offsets. Avoid choosing alignment merely from leading silence.
        window_start,window_size = 2048,1024
        assert math.fsum(x*x for x in stock[window_start:window_start+window_size])>0
        scores = []
        for offset in range(-1024,2049):
            assert window_start+offset>=0 and window_start+window_size+offset<=len(raw)
            error = math.fsum((raw[i+offset]-stock[i])**2 for i in range(window_start,window_start+window_size))/window_size
            scores.append((error,offset))
        best = sorted(scores)[:5]
        offsets = sorted(set([0,128,256,384,512,576,640,896,1024]+[offset for _,offset in best]))
        comparisons = [metrics(raw,stock,offset) for offset in offsets]
        best_offset = best[0][1]
        best_full = next(m for m in comparisons if m["raw_offset_for_stock_zero"]==best_offset)
        first,end = best_full["stock_first_compared"],best_full["stock_end_compared"]
        stock_tail = stock[end:]
        # Hash full retained output and individual initial frames; do not write
        # a shifted replacement that could be confused with a proven contract.
        case = {"label":label,"source":str(original),"source_sha256":source_hash,
                "source_unchanged":sha(original.read_bytes())==source_hash,
                "stock_sha256":stock_hash,"stock_samples":len(stock),"packet_sha256":sha(packets),
                "raw_sha256":sha(raw_bytes),"raw_samples":len(raw),"raw_frames":len(raw)//512,
                "raw_all_finite":True,"raw_peak_abs":max(map(abs,raw)),"no_eof_sent":True,
                "first_nonzero_raw_sample":next((i for i,x in enumerate(raw) if x!=0),None),
                "best_overlap_raw_sha256":sha(raw_bytes[4*(first+best_offset):4*(end+best_offset)]),
                "best_overlap_stock_sha256":sha(stock_bytes[4*first:4*end]),
                "unmatched_stock_tail":{"samples":len(stock_tail),"nonzero_samples":sum(x!=0 for x in stock_tail),
                                        "peak_abs":max(map(abs,stock_tail),default=0),"sha256":sha(stock_bytes[4*end:])},
                "initial_frame_sha256":[sha(raw_bytes[i*2048:(i+1)*2048]) for i in range(min(3,len(raw)//512))],
                "initial_frame_nonzero_samples":[sum(x!=0 for x in raw[i*512:(i+1)*512]) for i in range(min(3,len(raw)//512))],
                "scan":{"window_start":window_start,"window_samples":window_size,"offset_min":-1024,"offset_max":2048,
                        "best_candidates":[{"offset":offset,"window_rms_error":math.sqrt(error)} for error,offset in best]},
                "comparisons":comparisons,"command":execution,"decode_exit":decoded.returncode}
        case["raw_xma2"] = {"command":raw2_execution,"decode_exit":raw2_result.returncode,
                            "raw_samples":len(raw2),"raw_frames":len(raw2)//512,"sha256":sha(raw2_bytes),
                            "identical_to_reference_raw_xma1":raw2_bytes==raw_bytes,
                            "versus_raw_xma1":metrics(raw2,raw,0),
                            "versus_stock_at_reference_best":metrics(raw2,stock,best_offset),
                            "versus_stock_at_384":metrics(raw2,stock,384)}
        report["cases"].append(case)
        print(label,"raw",len(raw),"stock",len(stock),"best offset",best_offset,
              "best full",next(m for m in comparisons if m["raw_offset_for_stock_zero"]==best_offset),flush=True)
        print(label,"384",next(m for m in comparisons if m["raw_offset_for_stock_zero"]==384),flush=True)
        print(label,"raw XMA2",len(raw2),"identical to raw XMA1",raw2_bytes==raw_bytes,flush=True)
    report["references_unchanged"] = all(sha(p.read_bytes())==reference_hashes[str(p)] for p in reference_paths)
    assert report["references_unchanged"] and all(c["source_unchanged"] for c in report["cases"])
    artifact("raw-report.json").write_text(json.dumps(report,indent=2)+"\n", encoding="utf-8")

if __name__=="__main__":
    main()
