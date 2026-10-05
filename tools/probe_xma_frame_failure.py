"""Build a separate diagnostic codec with read-only failed-frame logging.

The production source, install, DLLs and rejection policy remain untouched.
"""
import sys
from pathlib import Path

sys.dont_write_bytecode=True
import build_native_audio_codec as b


def main():
    original_patch=b.patch_decoder
    def diagnostic(original, packet_boundary_fix=False):
        import difflib
        patched,_=original_patch(original,packet_boundary_fix=False)
        before='''            /** FIXME: not sure if this is always an error */'''
        after='''            /* Diagnostic only: inspect exactly the saved failed frame. */
            av_log(s->avctx, AV_LOG_ERROR,
                   "DIAGNOSTIC frame=%u len=%d used=%d offset=%d saved=%d subframes=%d\\n",
                   s->frame_num, len, get_bits_count(gb)-s->frame_offset,
                   s->frame_offset, s->num_saved_bits, s->parsed_all_subframes);
            av_log(s->avctx, AV_LOG_ERROR, "DIAGNOSTIC bytes=");
            for (int j=0; j<(s->num_saved_bits+7)/8; ++j)
                av_log(s->avctx, AV_LOG_ERROR, "%02x", s->frame_data[j]);
            av_log(s->avctx, AV_LOG_ERROR, "\\n");
''' + before
        text=b.replace(patched.decode(),before,after)
        patch=''.join(difflib.unified_diff(original.decode().splitlines(True),text.splitlines(True),
            fromfile='a/libavcodec/wmaprodec.c',tofile='b/libavcodec/wmaprodec.c')).encode()
        return text.encode(),patch
    b.patch_decoder=diagnostic
    baseline=b.BASE
    b.BASE=b.ROOT/'build/loc-codec-diagnostic'
    b.SEED=baseline/'ffmpeg-upstream.tar.gz'
    b.ARCHIVE=b.BASE/'ffmpeg-upstream.tar.gz'
    b.SOURCE=b.BASE/'source'/('FFmpeg-'+b.COMMIT)
    b.WORK=b.BASE/'work'
    b.INSTALL=b.BASE/'install'
    b.PATCH=b.BASE/'simpsons-raw-frames.patch'
    b.MANIFEST=b.BASE/'source-manifest.json'
    b.PROVENANCE=b.INSTALL/'PROVENANCE.json'
    b.build(8)


if __name__=='__main__':
    main()
