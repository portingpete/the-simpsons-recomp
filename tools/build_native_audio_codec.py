"""Build/verify an isolated pinned native XMA codec; never build the game.

Default: use the verified local archive, or the official pinned download only
when no reusable archive exists. --verify: read-only, offline file verification;
no compiler, DLL loading, mkdir, source repair, dependency search or download.
"""
import sys
sys.dont_write_bytecode = True
import argparse
import difflib
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import shlex
import shutil
import struct
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "build/audio-codec"
COMMIT = "1c2c67c0b9f7f66ab32c19dcf7f227bcd290aa4c"
ARCHIVE_HASH = "1291ae49c285f7bd55c7c059aa43f1a0fd784a1ae22d5c76297dcd11c531248a"
DECODER_HASH = "803547a38dea1294891c00402d6b3576a16053b0f00b395768c4983740c86553"
URL = f"https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/{COMMIT}"
SEED = Path("K:/DarkRecomp/build_native/deps/ffmpeg-darkxma-upstream.tar.gz")
ARCHIVE = BASE / "ffmpeg-upstream.tar.gz"
SOURCE = BASE / "source" / ("FFmpeg-" + COMMIT)
WORK = BASE / "work"
INSTALL = BASE / "install"
PATCH = BASE / "simpsons-raw-frames.patch"
MANIFEST = BASE / "source-manifest.json"
PROVENANCE = INSTALL / "PROVENANCE.json"
BASH = Path("C:/msys64/usr/bin/bash.exe")
LLVM_LIB = Path("C:/Program Files/LLVM/bin/llvm-lib.exe")
LLVM_READOBJ = Path("C:/Program Files/LLVM/bin/llvm-readobj.exe")
WINPTHREAD = Path("C:/msys64/mingw64/bin/libwinpthread-1.dll")
WINPTHREAD_LICENSE = Path("C:/msys64/mingw64/share/licenses/winpthreads/COPYING")
GIT_CMP = Path("C:/Program Files/Git/usr/bin/cmp.exe")
SHELL_PATH = "/mingw64/bin:/usr/bin:/c/Program Files/Git/usr/bin"

def require(ok, message):
    if not ok:
        raise RuntimeError(message)

def digest(data):
    return hashlib.sha256(data).hexdigest()

def file_hash(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()

def json_bytes(value):
    return (json.dumps(value, sort_keys=True, indent=2) + "\n").encode()

def linked(path):
    return path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction())

def owned(path):
    path = Path(path)
    require(path.is_relative_to(BASE), f"Output outside owned directory: {path}")
    require(path.resolve().is_relative_to(BASE.resolve()), f"Output escapes owned directory: {path}")
    for parent in (path, *path.parents):
        require(not linked(parent), f"Refusing linked output path: {parent}")
        if parent == ROOT:
            break
    return path

def write(path, data):
    path = owned(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)

def msys(path):
    value = path.as_posix()
    require(len(value)>2 and value[1]==":", f"Absolute Windows path required: {path}")
    return "/" + value[0].lower() + value[2:]

def replace(text, before, after, count=1):
    require(text.count(before)==count, f"Pinned source anchor changed: {before!r}")
    return text.replace(before, after)

def patch_decoder(original, packet_boundary_fix=True):
    require(digest(original)==DECODER_HASH, "Pinned upstream decoder hash mismatch")
    text = original.decode("utf-8")
    text = replace(text, '#include "libavutil/mem.h"', '#include "libavutil/mem.h"\n#include "libavutil/opt.h"')
    text = replace(text, "typedef struct XMADecodeCtx {", "typedef struct XMADecodeCtx {\n    const AVClass *class;\n    int raw_frames;")
    anchor = "    int i, ret = 0, eof = 0;"
    text = replace(text, anchor, anchor + '''
    /* Opt-in native raw frames. Preserve upstream packet reservoir and frame
     * math; callers own priming/sample extents. Empty input does not synthesize
     * an overlap tail. Default stock decoding remains on the path below. */
    if (s->raw_frames) {
        if (!avpkt->size) { *got_frame_ptr = 0; return 0; }
        frame->nb_samples = 512;
        if ((ret = ff_get_buffer(avctx, frame, 0)) < 0) return ret;
        return decode_packet(avctx, &s->xma[0], frame, got_frame_ptr, avpkt);
    }
''')
    anchor = "    /* init all streams (several streams of 1/2ch make Nch files) */"
    text = replace(text, anchor, '''    if (s->raw_frames && (s->num_streams != 1 || avctx->ch_layout.nb_channels > 2))
        return AVERROR(EINVAL);
''' + anchor)
    text = replace(text, "        s->frames[i] = av_frame_alloc();", "        if (s->raw_frames) s->xma[i].skip_frame = 0;\n        s->frames[i] = av_frame_alloc();")
    anchor = "    s->current_stream = 0;\n    s->flushed = 0;"
    text = replace(text, anchor, "    if (s->raw_frames) s->xma[0].skip_frame = 0;\n" + anchor)
    options = '''static const AVOption simpsons_xma_options[] = {
    { "simpsons_raw_frames", "Return raw XMA frames without container delay or trimming",
      offsetof(XMADecodeCtx, raw_frames), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1,
      AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_DECODING_PARAM },
    { NULL }
};
static const AVClass simpsons_xma_class = {
    .class_name = "Simpsons native XMA",
    .item_name = av_default_item_name,
    .option = simpsons_xma_options,
    .version = LIBAVUTIL_VERSION_INT,
};

'''
    anchor = "const FFCodec ff_xma1_decoder = {"
    text = replace(text, anchor, options + anchor)
    text = replace(text, "    .priv_data_size = sizeof(XMADecodeCtx),", "    .p.priv_class   = &simpsons_xma_class,\n    .priv_data_size = sizeof(XMADecodeCtx),", 2)
    if packet_boundary_fix:
        anchor = "    s->packet_offset = get_bits_count(gb) & 7;"
        text = replace(text, anchor, '''    /* XMA frames can end exactly at the2048-byte packet boundary with
     * their continuation bit set. The packet has still ended: otherwise the
     * next32-bit packet header is decoded as a15-bit frame length and audio.
     * Preserve strict frame validation and the untouched compressed payload. */
    if ((avctx->codec_id == AV_CODEC_ID_XMA1 || avctx->codec_id == AV_CODEC_ID_XMA2)
            && remaining_bits(s, gb) == 0)
        s->packet_done = 1;

''' + anchor)
    patched = text.encode("utf-8")
    patch = "".join(difflib.unified_diff(original.decode().splitlines(True), text.splitlines(True),
                                       fromfile="a/libavcodec/wmaprodec.c", tofile="b/libavcodec/wmaprodec.c")).encode()
    return patched, patch

def archive_contents():
    require(ARCHIVE.is_file() and file_hash(ARCHIVE)==ARCHIVE_HASH, "Missing or modified pinned archive")
    entries = {}
    with tarfile.open(ARCHIVE, "r:gz") as archive:
        for member in archive:
            parts = PurePosixPath(member.name).parts
            require(parts and parts[0]==SOURCE.name and ".." not in parts and not member.name.startswith("/"), "Unsafe archive member")
            require(member.isdir() or member.isfile(), "Archive has unsupported links/special files")
            if member.isfile():
                relative = "/".join(parts[1:])
                require(relative and relative not in entries, "Duplicate/empty archive member")
                content = archive.extractfile(member).read()
                entries[relative] = {"bytes":content, "mode":member.mode, "mtime":member.mtime}
    patched, patch = patch_decoder(entries["libavcodec/wmaprodec.c"]["bytes"])
    expected = {name:digest(item["bytes"]) for name,item in entries.items()}
    expected["libavcodec/wmaprodec.c"] = digest(patched)
    return entries, expected, patched, patch

def files_under(directory):
    require(directory.is_dir() and not linked(directory), f"Missing/linked directory: {directory}")
    files = {}
    # os.walk does not descend through symlinks; explicitly reject them anyway.
    for root, dirs, names in os.walk(directory, followlinks=False):
        for name in dirs + names:
            require(not linked(Path(root)/name), f"Linked artifact/source: {Path(root)/name}")
        for name in names:
            path = Path(root)/name
            require(path.is_file(), f"Nonregular file: {path}")
            files[path.relative_to(directory).as_posix()] = path
    return files

def check_source(expected):
    owned(SOURCE)
    actual = files_under(SOURCE)
    require(set(actual)==set(expected), "Source tree file set differs from the pinned archive; refusing unknown edits")
    for name, path in actual.items():
        require(file_hash(path)==expected[name], f"Unknown source edit: {name}")

def prepare_source(entries, expected, patched, patch, previous_decoder_hash=None):
    if SOURCE.exists():
        existing = files_under(SOURCE)
        require(set(existing)<=set(expected), "Unexpected files in source tree; refusing overwrite")
        for name,path in existing.items():
            allowed = {expected[name], digest(entries[name]["bytes"])}
            if name == 'libavcodec/wmaprodec.c' and previous_decoder_hash:
                allowed.add(previous_decoder_hash)
            require(file_hash(path) in allowed, f"Unknown source edit: {name}; refusing overwrite")
    for name,item in entries.items():
        path = SOURCE/name
        content = patched if name=="libavcodec/wmaprodec.c" else item["bytes"]
        if not path.exists() or file_hash(path)!=digest(content):
            write(path,content)
            os.chmod(path,item["mode"] & 0o777)
            os.utime(path,(item["mtime"],item["mtime"]))
    write(PATCH,patch)
    write(MANIFEST,json_bytes(expected))
    check_source(expected)

def configuration():
    return [msys(SOURCE/"configure"), "--prefix="+msys(INSTALL), "--target-os=mingw32", "--arch=x86_64", "--cc=gcc",
            "--disable-everything", "--disable-autodetect", "--disable-programs", "--disable-doc", "--disable-network", "--disable-debug",
            "--disable-avdevice", "--disable-avfilter", "--disable-avformat", "--disable-swscale", "--disable-swresample",
            "--disable-asm", "--disable-pthreads", "--enable-w32threads", "--enable-decoder=xma1,xma2",
            "--enable-avcodec", "--enable-avutil", "--enable-shared", "--disable-static", "--build-suffix=-simpsonsxma",
            "--extra-ldflags=-static-libgcc -Wl,--no-insert-timestamp"]

def verify(expected=None, patched=None, patch=None, check_builder=True):
    # Deliberately no directory creation, tool execution, loading, network or repairs.
    owned(BASE)
    if expected is None:
        _,expected,patched,patch = archive_contents()
    check_source(expected)
    require(PATCH.read_bytes()==patch, "Source patch identity mismatch")
    require(MANIFEST.read_bytes()==json_bytes(expected), "Source manifest differs from pinned archive plus exact patch")
    record = json.loads(PROVENANCE.read_text(encoding="utf-8"))
    require(record["upstream_commit"]==COMMIT and record["archive_sha256"]==ARCHIVE_HASH and
            record["original_decoder_sha256"]==DECODER_HASH and record["patched_decoder_sha256"]==digest(patched), "Provenance source identity mismatch")
    require(record["configure"]==configuration() and record["raw_option"]=="simpsons_raw_frames", "Build configuration identity mismatch")
    if check_builder:
        require(record["builder_sha256"]==file_hash(Path(__file__)), "Builder changed since this build; rebuild deliberately before verification")
    actual_install = files_under(INSTALL)
    expected_install = {p[len("install/"):] for p in record["artifacts"] if p.startswith("install/")}
    require(set(actual_install)==expected_install | {"PROVENANCE.json"}, "Installed artifact set differs from recorded build")
    for name,hashed in record["artifacts"].items():
        path = owned(BASE/name)
        require(path.is_file() and file_hash(path)==hashed, f"Compiled/support artifact missing or modified: {name}")
    require(record["validation"]["factory_smoke"]=="PASS", "Missing successful native factory validation")
    print(f"Native audio codec VERIFIED: {len(expected)} source files, {len(record['artifacts'])} artifacts; offline read-only", flush=True)
    return record

SMOKE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <io.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/mem.h>
static void need(int ok,const char* message) {if(!ok){fprintf(stderr,"codec smoke: %s\n",message);exit(2);}}
static AVCodecContext* setup(enum AVCodecID id,int channels,int streams,int raw,int* result) {
    const AVCodec* codec=avcodec_find_decoder(id);need(codec!=NULL,"decoder missing");
    AVCodecContext* ctx=avcodec_alloc_context3(codec);need(ctx!=NULL,"context allocation");
    int64_t initial=-1;
    need(av_opt_get_int(ctx->priv_data,"simpsons_raw_frames",0,&initial)==0 && initial==0,"stock mode is not default");
    need(av_opt_find(ctx->priv_data,"darkrecomp_raw_frames",NULL,0,0)==NULL,"reference project option leaked");
    need(av_opt_set_int(ctx->priv_data,"simpsons_raw_frames",raw,0)==0,"raw option missing");
    ctx->sample_rate=48000;ctx->block_align=2048;av_channel_layout_default(&ctx->ch_layout,channels);
    ctx->extradata_size=id==AV_CODEC_ID_XMA1?8+20*streams:34;
    ctx->extradata=av_mallocz(ctx->extradata_size+AV_INPUT_BUFFER_PADDING_SIZE);need(ctx->extradata!=NULL,"extradata allocation");
    if(id==AV_CODEC_ID_XMA1){ctx->extradata[4]=streams;for(int i=0;i<streams;++i)ctx->extradata[25+i*20]=channels/streams;}
    else ctx->extradata[0]=streams;
    *result=avcodec_open2(ctx,codec,NULL);return ctx;
}
static void drain(AVCodecContext* ctx,AVFrame* frame,int raw) {
    for(int attempt=0;attempt<128;++attempt){
        int rc=avcodec_receive_frame(ctx,frame);if(rc==AVERROR(EAGAIN)||rc==AVERROR_EOF)return;
        need(rc==0,"native decode failed");
        need(frame->format==AV_SAMPLE_FMT_FLTP && frame->ch_layout.nb_channels==1,"unexpected output format");
        need(!raw||frame->nb_samples==512,"raw output is not 512 samples");
        float* values=(float*)frame->extended_data[0];
        for(int i=0;i<frame->nb_samples;++i)need(isfinite(values[i]),"nonfinite PCM");
        need(fwrite(values,sizeof(float),frame->nb_samples,stdout)==(size_t)frame->nb_samples,"PCM pipe write failed");
        av_frame_unref(frame);
    }
    need(0,"bounded drain exceeded");
}
int main(int argc,char** argv) {
    need(avcodec_version()==LIBAVCODEC_VERSION_INT && avutil_version()==LIBAVUTIL_VERSION_INT,"loaded DLL/header version mismatch");
    if(argc==4){
        int raw=atoi(argv[1]),rc=0;
        enum AVCodecID id=strcmp(argv[2],"xma1")==0?AV_CODEC_ID_XMA1:AV_CODEC_ID_XMA2;
        AVCodecContext* ctx=setup(id,1,1,raw,&rc);need(rc==0,"decode context open");
        AVFrame* frame=av_frame_alloc();need(frame!=NULL,"frame allocation");
        FILE* input=fopen(argv[3],"rb");need(input!=NULL,"packet input open");
        _setmode(_fileno(stdout),_O_BINARY);uint8_t bytes[2048];size_t count;int packets=0;
        while((count=fread(bytes,1,sizeof(bytes),input))!=0){
            need(count==2048 && ++packets<=32,"bounded full packets required");
            AVPacket* packet=av_packet_alloc();need(packet!=NULL&&av_new_packet(packet,2048)==0,"packet allocation");
            memcpy(packet->data,bytes,2048);rc=avcodec_send_packet(ctx,packet);av_packet_free(&packet);
            need(rc==0,"packet acceptance failed");drain(ctx,frame,raw);
        }
        need(!ferror(input),"packet read error");fclose(input);
        if(!raw){need(avcodec_send_packet(ctx,NULL)==0,"stock drain failed");drain(ctx,frame,0);}
        else for(int i=0;i<3;++i)need(avcodec_receive_frame(ctx,frame)==AVERROR(EAGAIN),"raw starvation emitted tail");
        av_frame_free(&frame);avcodec_free_context(&ctx);return 0;
    }
    need(argc==1,"invalid smoke arguments");
    for(int id=0;id<2;++id)for(int ch=1;ch<=2;++ch)for(int raw=0;raw<2;++raw){
        int rc=0;AVCodecContext* ctx=setup(id?AV_CODEC_ID_XMA2:AV_CODEC_ID_XMA1,ch,1,raw,&rc);
        need(rc==0,"mono/stereo factory open failed");AVFrame* frame=av_frame_alloc();need(frame!=NULL,"frame allocation");
        need(avcodec_receive_frame(ctx,frame)==AVERROR(EAGAIN),"empty context produced PCM");
        if(raw){need(avcodec_send_packet(ctx,NULL)==0,"empty raw drain failed");need(avcodec_receive_frame(ctx,frame)==AVERROR_EOF,"raw EOF invented a tail");
                avcodec_flush_buffers(ctx);need(avcodec_receive_frame(ctx,frame)==AVERROR(EAGAIN),"raw reset produced PCM");}
        av_frame_free(&frame);avcodec_free_context(&ctx);
    }
    for(int id=0;id<2;++id)for(int raw=0;raw<2;++raw){
        int rc=0;AVCodecContext* ctx=setup(id?AV_CODEC_ID_XMA2:AV_CODEC_ID_XMA1,4,2,raw,&rc);
        need(raw?rc<0:rc==0,"single-stream restriction changed default stock behavior");avcodec_free_context(&ctx);
    }
    printf("Versions: avcodec=%u avutil=%u\n",avcodec_version(),avutil_version());
    printf("Factory PASS: stock default, XMA1/XMA2 mono/stereo, raw reset/empty EOF, raw multistream rejection, stock multistream open\n");return 0;
}
'''

def execute(command, log, env, cwd=BASE, timeout=120):
    owned(log)
    with log.open("wb") as output:
        result = subprocess.run(command,cwd=cwd,env=env,stdout=output,stderr=subprocess.STDOUT,timeout=timeout)
    require(result.returncode==0, f"Command failed ({result.returncode}); see {log}")

def native_environment(epoch):
    env = dict(os.environ)
    for key in list(env):
        if key.upper() in {"CFLAGS","CXXFLAGS","CPPFLAGS","LDFLAGS","CC","CXX","LD","AR","AS","NM","STRIP","PKG_CONFIG_PATH","PKG_CONFIG_LIBDIR","BASH_ENV","ENV","CONFIG_SHELL"}:
            del env[key]
    env.update({"MSYSTEM":"MINGW64", "CHERE_INVOKING":"1", "SOURCE_DATE_EPOCH":str(epoch),
                "TMPDIR":msys(BASE/"tmp"), "TEMP":str(BASE/"tmp"), "TMP":str(BASE/"tmp")})
    return env

def bash_script(body):
    return "set -eu\nexport PATH="+shlex.quote(SHELL_PATH)+"\n" + body

def build(jobs):
    owned(BASE).mkdir(parents=True,exist_ok=True)
    if not ARCHIVE.exists():
        if SEED.is_file():
            require(file_hash(SEED)==ARCHIVE_HASH, "Local seed archive hash mismatch; refusing unverified reuse")
            print("Seeding the pinned ORIGINAL archive read-only; no reference binaries are copied",flush=True)
            write(ARCHIVE,SEED.read_bytes())
        else:
            print("No reusable archive: fetching the official pinned FFmpeg archive",flush=True)
            request=urllib.request.Request(URL,headers={"User-Agent":"SimpsonsNative-codec-builder"})
            with urllib.request.urlopen(request,timeout=60) as response:
                payload=response.read(64*1024*1024+1)
            require(digest(payload)==ARCHIVE_HASH,"Official archive hash mismatch")
            write(ARCHIVE,payload)
    entries,expected,patched,patch=archive_contents()
    previous_decoder_hash = None
    if PROVENANCE.exists():
        if PATCH.read_bytes() != patch:
            # Migrate only the fully verified previous owned codec. Arbitrary
            # source, artifact or patch changes still fail before any write.
            previous,previous_patch=patch_decoder(entries['libavcodec/wmaprodec.c']['bytes'],packet_boundary_fix=False)
            previous_expected=dict(expected)
            previous_decoder_hash=digest(previous)
            previous_expected['libavcodec/wmaprodec.c']=previous_decoder_hash
            verify(previous_expected,previous,previous_patch,check_builder=False)
        else:
            verify(expected,patched,patch,check_builder=False)
    prepare_source(entries,expected,patched,patch,previous_decoder_hash)
    epoch=max(item["mtime"] for item in entries.values())
    owned(BASE/"tmp").mkdir(parents=True,exist_ok=True)
    env=native_environment(epoch)
    owned(WORK).mkdir(parents=True,exist_ok=True)
    for path in [BASH,LLVM_LIB,LLVM_READOBJ,WINPTHREAD,WINPTHREAD_LICENSE,GIT_CMP,Path("C:/msys64/mingw64/bin/gcc.exe"),Path("C:/msys64/usr/bin/make.exe")]:
        require(path.is_file(), f"Required existing toolchain file missing: {path}")
    compiler_inputs={str(p):file_hash(p) for p in [BASH,LLVM_LIB,LLVM_READOBJ,GIT_CMP,Path("C:/msys64/mingw64/bin/gcc.exe"),Path("C:/msys64/mingw64/bin/ld.exe"),Path("C:/msys64/usr/bin/make.exe"),WINPTHREAD,WINPTHREAD_LICENSE]}
    execute([str(BASH),"-c",bash_script("gcc --version\nld --version\nmake --version\nbash --version\n")],BASE/"toolchain.log",env)
    configure=configuration();stamp=WORK/"configure-inputs.json"
    configure_inputs={"arguments":configure,"toolchain_hashes":compiler_inputs,"source_date_epoch":epoch,
                      "shell_PATH":SHELL_PATH,"TMPDIR":env["TMPDIR"]}
    configured=stamp.is_file() and stamp.read_bytes()==json_bytes(configure_inputs) and (WORK/"config.h").is_file()
    if not configured:
        print("Configuring isolated minimal avcodec/avutil",flush=True)
        execute([str(BASH),"-c",bash_script("cd "+shlex.quote(msys(WORK))+"\n"+" ".join(map(shlex.quote,configure))+"\n")],BASE/"configure.log",env,timeout=300)
        write(stamp,json_bytes(configure_inputs))
    print(f"Building/installing native codec only (-j{jobs}); log: {BASE/'build.log'}",flush=True)
    # Extracted sources retain archive timestamps, including patched files.
    # Force this translation unit to rebuild so a patch migration cannot reuse
    # an older object merely because its timestamp is newer than the archive.
    body="cd "+shlex.quote(msys(WORK))+f"\nmake -j{jobs} -W "+shlex.quote(msys(SOURCE/'libavcodec/wmaprodec.c'))+"\nmake install\n"
    execute([str(BASH),"-c",bash_script(body)],BASE/"build.log",env,timeout=600)
    import_commands=[]
    for name,major in [("avcodec",62),("avutil",60)]:
        definition=INSTALL/"lib"/(name+"-simpsonsxma-import.def")
        original=WORK/("lib"+name)/f"{name}-simpsonsxma-{major}.def"
        write(definition,(f"LIBRARY {name}-simpsonsxma-{major}.dll\n").encode()+original.read_bytes())
        command=[str(LLVM_LIB),"/machine:x64","/def:"+str(definition),"/out:"+str(INSTALL/"lib"/(name+"-simpsonsxma.lib"))]
        execute(command,BASE/(name+"-import.log"),env);import_commands.append(command)
    write(INSTALL/"bin/libwinpthread-1.dll",WINPTHREAD.read_bytes())
    for name in ["COPYING.LGPLv2.1","LICENSE.md"]:
        write(INSTALL/name,(SOURCE/name).read_bytes())
    write(INSTALL/"COPYING.winpthreads",WINPTHREAD_LICENSE.read_bytes())
    write(INSTALL/"simpsons-raw-frames.patch",patch)
    write(BASE/"codec-smoke.c",SMOKE.encode())
    smoke=BASE/"codec-smoke.exe"
    smoke_command=["gcc","-std=c17","-O2","-static-libgcc","-Wl,--no-insert-timestamp",msys(BASE/"codec-smoke.c"),"-I"+msys(INSTALL/"include"),
                   msys(INSTALL/"lib/libavcodec-simpsonsxma.dll.a"),msys(INSTALL/"lib/libavutil-simpsonsxma.dll.a"),"-o",msys(smoke)]
    execute([str(BASH),"-c",bash_script(" ".join(map(shlex.quote,smoke_command))+"\n")],BASE/"smoke-compile.log",env)
    native_env=dict(env);native_env["PATH"]=str(INSTALL/"bin")+os.pathsep+native_env.get("PATH","")
    execute([str(smoke)],BASE/"smoke.log",native_env,timeout=20)
    validation={"factory_smoke":"PASS", "smoke_compile":smoke_command,"original_sample_comparisons":[]}
    # Optional existing original-derived probes are read-only inputs. PCM goes
    # to a pipe and only hashes/counts are retained; no media is redistributed.
    for label,raw_hash in [("short","79c1b75fc3dc434099bd6b31e1dcec0da6edcfc05d7061c143ff365ae589aedd"),
                           ("documented","c7364922a185b457206b94e9173eaa0d206ca7af5bd134586d0ba521576fa6f9")]:
        packets=ROOT/"build/audio-probe"/("raw-"+label+".packets")
        baseline=ROOT/"build/audio-probe/raw-report.json"
        if not packets.is_file() or not baseline.is_file():
            continue
        case=next(c for c in json.loads(baseline.read_text(encoding="utf-8"))["cases"] if c["label"]==label)
        require(file_hash(packets)==case["packet_sha256"],"Optional sample packet provenance mismatch")
        for mode in ["xma1","xma2"]:
            command=[str(smoke),"1",mode,str(packets)]
            result=subprocess.run(command,env=native_env,cwd=BASE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20)
            require(result.returncode==0 and digest(result.stdout)==raw_hash,f"Owned raw {mode} differs from recorded original-sample baseline: {label}")
            validation["original_sample_comparisons"].append({"label":label,"mode":mode,"raw_samples":len(result.stdout)//4,"sha256":digest(result.stdout),"packet_sha256":file_hash(packets)})
        stock_pcm=ROOT/"build/audio-probe"/(label+".f32le")
        if stock_pcm.is_file():
            stock_bytes=stock_pcm.read_bytes()
            require(digest(stock_bytes)==case["stock_sha256"],"Optional stock baseline hash mismatch")
            result=subprocess.run([str(smoke),"0","xma2",str(packets)],env=native_env,cwd=BASE,
                                  stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=20)
            require(result.returncode==0 and len(result.stdout)==len(stock_bytes),f"Owned default stock decoder length differs: {label}")
            values=[x[0] for x in struct.iter_unpack("<f",result.stdout)]
            prior=[x[0] for x in struct.iter_unpack("<f",stock_bytes)]
            require(values and all(math.isfinite(x) for x in values),"Nonfinite/empty owned stock output")
            maximum=max(abs(a-b) for a,b in zip(values,prior))
            require(maximum<=1.2e-7,f"Owned stock-mode PCM differs from recorded native baseline: {label}")
            validation.setdefault("stock_comparisons",[]).append({"label":label,"samples":len(values),
                "max_abs_difference":maximum,"owned_sha256":digest(result.stdout),"baseline_sha256":digest(stock_bytes)})
    write(BASE/"validation.json",json_bytes(validation))
    dlls=[INSTALL/"bin"/name for name in ["avcodec-simpsonsxma-62.dll","avutil-simpsonsxma-60.dll","libwinpthread-1.dll"]]
    execute([str(LLVM_READOBJ),"--file-headers","--coff-imports",*map(str,dlls)],BASE/"pe-imports.log",env)
    check_source(expected)
    artifacts={"install/"+name:file_hash(path) for name,path in files_under(INSTALL).items() if name!="PROVENANCE.json"}
    for path in [ARCHIVE,PATCH,MANIFEST,BASE/"toolchain.log",BASE/"configure.log",BASE/"build.log",BASE/"smoke.log",BASE/"smoke-compile.log",
                 BASE/"codec-smoke.c",smoke,BASE/"validation.json",BASE/"pe-imports.log",WORK/"config.h",WORK/"config_components.h",
                 WORK/"ffbuild/config.mak",WORK/"ffbuild/config.log",stamp]:
        artifacts[path.relative_to(BASE).as_posix()]=file_hash(path)
    record={"schema":1,"upstream_commit":COMMIT,"archive_url":URL,"archive_sha256":ARCHIVE_HASH,
            "original_decoder_sha256":DECODER_HASH,"patched_decoder_sha256":digest(patched),"patch_sha256":digest(patch),
            "raw_option":"simpsons_raw_frames","raw_default":False,"builder_sha256":file_hash(Path(__file__)),
            "source_files":len(expected),"source_manifest_sha256":digest(json_bytes(expected)),"configure":configure,
            "source_date_epoch":epoch,"toolchain_hashes":compiler_inputs,"import_library_commands":import_commands,
            "build_jobs":jobs,"license":"LGPL-2.1-or-later; accompanying upstream and winpthreads notices preserved",
            "environment":{"MSYSTEM":"MINGW64","shell_PATH":SHELL_PATH,"SOURCE_DATE_EPOCH":str(epoch),
                           "TMPDIR":env["TMPDIR"],"TEMP":env["TEMP"],"TMP":env["TMP"]},
            "validation":validation,"artifacts":artifacts}
    write(PROVENANCE,json_bytes(record))
    verify(expected,patched,patch)
    print(f"READY: {INSTALL}",flush=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify",action="store_true",help="Offline read-only verification; never builds or repairs")
    parser.add_argument("--jobs",type=int,default=min(8,os.cpu_count() or 1),help="Bounded native build parallelism (1..16)")
    args=parser.parse_args()
    require(1<=args.jobs<=16,"--jobs must be 1..16")
    if args.verify:
        verify()
    else:
        build(args.jobs)

if __name__=="__main__":
    try:
        main()
    except (RuntimeError,OSError,ValueError,KeyError,subprocess.SubprocessError) as error:
        print(f"Native audio codec FAILED: {error}",file=sys.stderr)
        sys.exit(1)
