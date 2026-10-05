# Synthetic 2D robustness fixtures

Small, self-contained bitstreams that pin edge264-mvc's decoded output for
real-world decode-robustness cases the ITU conformance vectors do not cover
(the ITU vectors are conformant by construction, so they never exercise the
non-conformant-but-common patterns real encoders and remuxers emit). Run by
`make check` via `tests/conformance_check.c`; hashes are 128-bit FNV-1a of the
cropped output, same as the rest of `manifest.txt`.

Unlike `2d/` (official ITU vectors) these are anchored to **FFmpeg's** decode:
`conformance_check emit` self-verifies edge264's output against a sibling `.yuv`
produced by `ffmpeg -i <name>.264 -f rawvideo -pix_fmt yuv420p <name>.yuv`, and
the committed hash is only accepted when they match (`check=OK`). The `.yuv` is
not committed (regenerable from the `.264`).

## `over_level_dpb.264`

Guards the over-level DPB/reference-count fix. A stream whose frame size exceeds
its signaled `level_idc` (non-conformant, but extremely common - encoders and
muxers routinely under-declare the level) makes the level-derived `MaxDpbFrames`
smaller than the stream's own signaled `max_num_ref_frames`. Clamping the
reference set down to that made the sliding-window marking (8.2.5.3) retire
pictures the slices still reference, so inter prediction read stale/reused DPB
slots: **silently wrong pixels** in single-thread and a **nondeterministic
multi-thread** decode (buffer-reuse race), with no error flagged. FFmpeg (and
this fixture's reference) honour the signaled reference count regardless of level.

352x288, High, CABAC, 4 reference frames, B-frames, 24 frames; the `level_idc`
in the SPS is downgraded to 1.1 (`MaxDpbMbs = 900`, `900 / 396 mbs = 2 < 4 refs`)
so the bug triggers. Reproduce:

    ffmpeg -f lavfi -i testsrc2=size=352x288:rate=25 -frames:v 24 \
      -c:v libx264 -profile:v high -pix_fmt yuv420p \
      -x264-params ref=4:bframes=2:keyint=100:min-keyint=100:scenecut=0 \
      -f h264 base.264
    # then set SPS level_idc (RBSP byte 2) to 11

Without the fix this fixture's line FAILs (wrong base hash, and nondeterministic
under `EDGE264MVC_THREADS`); with it, single- and multi-thread both match the
FFmpeg-anchored hash.

## `pps_scaling_fallback.264`

Guards the PPS scaling-list fall-back fix (H.264 Table 7-2). A PPS that sets
`pic_scaling_matrix_present_flag = 1` with **every** `pic_scaling_list_present_flag[i] = 0`
over an SPS with `seq_scaling_matrix_present_flag = 0` must derive its scaling
lists from **Fall-Back Rule Set A** - the `Default_4x4_Intra/Inter` and
`Default_8x8_Intra/Inter` matrices (tables 7-3/7-4), *not* the flat-16 lists the
SPS carries. edge264 used to inherit the SPS's `Flat_16` for the absent lists
(rule set B), so every coefficient dequantized with the wrong weighting -
whole-picture colour-block corruption on any stream using this legal, common PPS
shape (observed on a commercial 3D Blu-ray, in the plain AVC base view and,
through inter-view prediction, the dependent view). This is the mirror image of
the (rejected) upstream PR #26, which wrongly changed the *SPS* flat-16 default;
here it is the *PPS* fall-back that was wrong. FFmpeg (the reference) applies
rule set A.

128x96, High, CABAC, 8x8 transform, B-frames, 6 frames. x264 does not emit this
shape, so it is produced by encoding a flat-CQM stream and bit-patching the PPS
(set the flag to 1, insert the eight absent-list flags), which leaves the encoded
coefficients dequantized under rule A instead of flat-16:

    ffmpeg -f lavfi -i testsrc2=size=128x96:rate=25 -frames:v 6 \
      -c:v libx264 -profile:v high -preset veryslow -pix_fmt yuv420p \
      -x264-params 8x8dct=1:cabac=1:bframes=1:keyint=6:no-scenecut=1:cqm=flat \
      -f h264 syn.264
    # then flip PPS pic_scaling_matrix_present_flag 0->1 and insert 8 zero
    # pic_scaling_list_present_flag bits (see patch_pps_scaling.py in the fix notes)

Without the fix this line FAILs (the base hash equals the flat-16 decode, i.e. the
un-patched stream's output); with it, it matches the FFmpeg-anchored rule-A hash.

## `slice_deblock_offsets.264`

Guards the per-slice deblocking parameters under multithreading. The deblocking
of a macroblock uses `slice_alpha_c0_offset_div2` and `slice_beta_offset_div2`
of the slice containing it (8.7). Slices of one picture are decoded in parallel,
and a slice whose predecessor was still being decoded used to leave its
macroblocks to whichever slice completed the picture, which deblocked them with
its own offsets: **wrong pixels that vary from run to run** whenever slices
carry different offsets, while single-threaded decoding stayed correct.

352x288, High, CABAC, all-intra (every picture an IDR), 4 slices per picture,
30 frames; the odd slices of each picture get offsets of +3/+3 instead of the
encoded 0/0. Reproduce:

    ffmpeg -f lavfi -i testsrc2=size=352x288:rate=25 -frames:v 30 \
      -c:v libx264 -profile:v high -pix_fmt yuv420p \
      -x264-params keyint=1:slices=4:qp=34:deblock=0,0 -f h264 base.264
    python3 tests/gen_slice_deblock_offsets.py base.264 slice_deblock_offsets.264

Without the fix the multithreaded passes of this fixture's line FAIL (wrong and
nondeterministic hash); with it, single- and multi-thread both match the
FFmpeg-anchored hash.

## `gap_reference.264`

Guards the content of frames inferred for a `frame_num` gap (8.2.5.2). A 176x144 libx264 encode of `testsrc2` with one reference frame and no B-frames, with picture 6 removed (`tests/gen_gap_reference.py`, which also lists the encoding command), so picture 7 predicts from the inferred frame. The decoder allocated that frame without writing its samples or macroblocks, so the pictures after the gap showed whatever the reused DPB slot held before - a different picture single- and multithreaded, and varying from run to run. FFmpeg, which this line is anchored to, fills the inferred frame with the previous reference picture.

Without the fix this line FAILs (wrong base hash, single- and multithreaded).

## `overlapping_slices.264`

Guards the handling of slices that overlap (non-conformant, 7.4.3 `first_mb_in_slice`), as a stream decoded with a picture parameter set that does not match its slices produces. Ten 1920x1080 IDR pictures, each with a slice A covering the whole picture in flat gray and a slice B covering 16 macroblocks with I_PCM samples of 200, starting in turn at the last 16 macroblocks and at macroblocks 1, 121, 4000 and 2 (`tests/gen_overlapping_slices.py`). Where slice B starts early, a worker thread that began slice A before the parser saw slice B usually decodes past B's start, and has to decode slice A again with its end known. The decoder now ends every slice where the next slice of its picture starts, as FFmpeg does with `next_slice_idx`, so slice B keeps its macroblocks and slice A is cut before them. Before, the overlap went to whichever slice claimed a macroblock first: slice A when decoding single-threaded, often slice B with worker threads. The hash is anchored to the single-threaded decode of the fixed decoder, which multithreaded decoding must match; FFmpeg resolves this overlap differently again, so it does not serve as the oracle.

Without the fix this line FAILs single-threaded (slice A wins) and, depending on the timing, multithreaded.

## `reversed_slices.264`

Guards slices of one picture that arrive out of address order. The pictures of `slice_deblock_offsets.264` with their four slices sent in reverse order (`tests/gen_reversed_slices.py`); arbitrary slice order is legal only in Baseline profile, so for this CABAC stream it is damaged input. A slice used to wait for any busy slice of its picture with a lower `first_mb_in_slice`, including one that arrived after it, and was then deblocked with its own filter offsets, while decoding single-threaded the same slice is left to the end of the picture. The output thus depended on the thread timing. A slice now waits only for slices decoded before it, and a slice whose macroblocks may overlap those of an older slice still being decoded starts after it, as it would single-threaded. The hash is anchored to the single-threaded decode.

Without the fix this line FAILs under `EDGE264MVC_THREADS`, with a different output from run to run.

## `overrun_reference.264`

Guards a slice that a worker thread decodes past the start of the next slice before the parser has seen that slice. 128 pairs of a 160x64 IDR picture and a P picture of P_Skip macroblocks copying it (`tests/gen_overrun_reference.py`). Each IDR picture has three slices with deblocking on: the second one runs to the end of the picture, and the third one starts a few macroblocks after it, in the first or the second macroblock row; their macroblocks are Intra_16x16 with a DC that changes from one macroblock to the next, so that deblocking across slice edges changes samples. `tests/slice_overrun_check.c` feeds the stream with a pause before every slice that does not start a picture, so that the second slice is decoded, deblocked and published to the end of the picture before its end is known, and the P picture follows the third slice at once. The second slice then has to take back what it published past its end, and restore the samples of the first slice that its deblocking changed, before the P picture reads them. The parser now waits for that before it starts the next picture (`settle_mb_bounds`), a slice waiting for a reference picture learns its end there too, and only the samples of the slices before it are restored, not those of a slice decoding the same rows at the same time. The hash is anchored to the single-threaded decode; FFmpeg resolves the overlap differently.

Without the wait, or without the restored samples, most multithreaded runs of `slice_overrun_check` differ from the single-threaded output.

## `plane_without_d.264`

Guards Intra_16x16 and chroma Plane prediction where the macroblock above-left of the current one (D) is not available. Three 1920x1080 IDR pictures of two slices each (`tests/gen_plane_without_d.py`): the first slice ends with an I_PCM macroblock in the middle of a row, the second one starts after it, and its macroblock one row below that start signals Intra_16x16 Plane prediction for luma and chroma, with its left and upper neighbours in its own slice but its above-left neighbour in the first slice. Plane prediction needs that sample (8.3.3.4, 8.3.4.4), and a macroblock of another slice is not available for intra prediction, so the stream is non-conformant; the JM reference decoder stops on it. The decoder used to select Plane prediction from the availability of the left and upper neighbours alone and read the sample anyway: single-threaded it got the I_PCM sample, with worker threads usually whatever was there before the first slice wrote it, so the output varied from run to run. Plane prediction now falls back to DC prediction from the left and upper neighbours when D is not available, as it already did when one of those is missing. The hash is anchored to the single-threaded decode; FFmpeg reads the sample of the other slice and differs.

Without the fix this line FAILs single-threaded, and multithreaded decoding differs from the single-threaded output from run to run.

## `weighted_denom7.264`

Guards explicit weighted bi-prediction at the largest weight denominator (`luma_log2_weight_denom` and `chroma_log2_weight_denom` 7). A 64x32 IDR picture and a P picture of I_PCM macroblocks with sample gradients over the whole 0..255 range are the two references, and eight B pictures between them, every macroblock skipped (direct spatial prediction, so each one blends both references), carry weight tables near the limits of 7.4.3.2: bright samples blended with weights summing to 127 and large offsets, negative weights and offsets, and the weight of 128 inferred when none is signalled, paired with odd negative weights (`tests/gen_weighted_denom7.py`). The SIMD blends fold the offsets into the weighted sum before the shift, a 16-bit sum that saturates at this denominator, so bright samples came out as at most 127; and as 128 does not fit their 8-bit weights, they halved both weights, putting an odd weight paired with it off by one. Bi-prediction at denominator 7 is now blended apart in plain C, as in 8-276 (`blend_wide`), which leaves the SIMD blends of all other weighted prediction as they were. The hash is anchored to the JM reference decoder. FFmpeg's C code gives the same output (`ffmpeg -cpuflags 0 -i weighted_denom7.264 -f rawvideo -pix_fmt yuv420p weighted_denom7.yuv`), its SIMD code does not.

Without the fix this line FAILs.

## `crop_top_left.264`

Three 64x48 IDR pictures of I_PCM macroblocks with sample gradients, cropped by 8 samples on every side to 48x32 (`tests/gen_crop_top_left.py`), so that the macroblocks along every edge lie partly outside the output picture. Here it pins the cropped output, anchored to FFmpeg's decode; `tests/edge264mvc_test_yuv_check.py` also uses it to check that `edge264mvc_test` reports a differing sample at each corner of the picture. Printing such an edge macroblock used to read the reference YUV outside the picture, before its start for the top rows, so `edge264mvc_test` crashed on a difference in the first frame instead of reporting it; it now prints the cropped-off samples as blanks without reading them.
