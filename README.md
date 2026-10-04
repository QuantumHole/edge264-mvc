# edge264-mvc

edge264 is a cross-platform, open-source H.264/AVC **software** decoder, focused on **speed** and **ease of use**.

> [!NOTE]
> This is a maintained fork of [tvlabs/edge264](https://github.com/tvlabs/edge264). It powers:
> - **[Oku3D Media Player](https://oku3d.com/)** - a native 3D media player that plays 3D Blu-rays (MVC) and converts any 2D video to stereoscopic 3D in real time.
> - **[mvc-source](https://github.com/jens-duttke/mvc-source)** - a dependency-free AviSynth+ & VapourSynth source plugin that frame-serves both MVC views for 3D video processing on Linux and Windows.
>
> edge264-mvc makes the **MVC / H.264 Annex H decode path (3D Blu-ray, stereo)** actually work end to end:
> FFmpeg / libavcodec drop the MVC dependent view entirely, so this is the only viable open-source
> *software* MVC decoder. Stock edge264's MVC path had several bugs whose fixing PRs sat open for
> months; this fork integrates those, adds many more MVC-correctness and decode-robustness fixes, and
> implements working **multithreaded decoding** (bit-exact to single-thread, validated on the full
> 231-stream JVT conformance corpus).

![](README-benchmark.svg)

*Benchmark computed as the fastest of 10 runs of [Big Buck Bunny test video](https://test-videos.co.uk/vids/bigbuckbunny/mp4/h264/1080/Big_Buck_Bunny_1080_10s_30MB.mp4),
on GitHub-hosted runners. Each decoder is timed both single-threaded (`1T`) and multithreaded
(`MT`, all auto-detected cores) for a fair comparison at both ends; OpenH264's decoder has no
multithreading, so it is shown once. All times are wall-clock - the MT speedup is bounded by the
runner's few vCPUs, so a many-core machine gains more.*


## Features

edge264 decodes the **Progressive High** and **Stereo High (MVC 3D)** profiles, up to level 6.2. Both the **MVC 3D** path and **multithreaded decoding** are fully functional here; stock edge264 ships the same code but both are broken (see [Relation to edge264](#relation-to-edge264)).

Below is an overview of optional features versus Baseline (**BP**), Extended (**XP**), Main (**MP**), High (**HP**) and Stereo High (**SHP**) profiles. Features outside the **Progressive High** and **Stereo High** scope (higher bit depths, 4:2:2/4:4:4 chroma, interlaced coding) are intentionally out of scope and well covered by general-purpose decoders such as FFmpeg; edge264's focus is the MVC 3D path that FFmpeg cannot decode.

| Feature | BP | XP | MP | HP | SHP | edge264 |
| --- | --- | --- | --- | --- | --- | --- |
| Bit depth | 8 | 8 | 8 | 8 | 8 | 8 |
| Chroma formats | 4:2:0 | 4:2:0 | 4:2:0 | 4:0:0<br/>4:2:0 | 4:0:0<br/>4:2:0 | 4:2:0 |
| Flexible macroblock ordering | ✓ | ✓ | | | | |
| Arbitrary slice ordering | ✓ | ✓ | | | | ✓ |
| Redundant slices | ✓ | ✓ | | | | |
| Data partitioning | | ✓ | | | | |
| SI/SP slices | | ✓ | | | | |
| Interlaced coding (PAFF, MBAFF) | | ✓ | ✓ | ✓ | ✓ | |
| B slices | | ✓ | ✓ | ✓ | ✓ | ✓ |
| CABAC entropy coding | | | ✓ | ✓ | ✓ | ✓ |
| 8x8 IDCT transforms | | | | ✓ | ✓ | ✓ |
| Custom quantization matrices | | | | ✓ | ✓ | ✓ |
| Separate Cb/Cr QP control | | | | ✓ | ✓ | ✓ |
| Separate color planes | | | | | | |
| Lossless coding | | | | | | |
| Max. number of views | 1 | 1 | 1 | 1 | 2 | 2 |


## Platforms

Target system support currently includes **macOS**, **Linux**, **Windows** and **WebAssembly**.

Processor support depends on the compiler used (GNU GCC or LLVM Clang). edge264 can choose among 4 backends, the last one supporting every other little-endian CPU by relying on [Clang vector extensions](https://clang.llvm.org/docs/LanguageExtensions.html#vectors-and-extended-vectors).

| Compiler | Intel x86/x64 | ARM32/64+NEON | WASM32/64 v2+ | Other ISAs |
|-|-|-|-|-|
| Clang | ✓ | ✓ | ✓ | ✓ (v15+) |
| GCC | ✓ | ✓ | | |


## Building

For native builds:

```sh
make
```

For WebAssembly builds:

```sh
emmake make # add CFLAGS=-mrelaxed-simd to target WASM v3
```

You can find lists of targets and options and what they do in the [Makefile](Makefile).

The `VARIANTS` option allows shipping multiple builds inside a single library file. It is intended for distribution packages that must run efficiently across a wide range of x86 CPUs: the library detects the host ISA level at runtime and dispatches to the fastest available implementation. They are *not* needed for a native single-machine build, where `-march=native` already picks the best code path at compile time. For example:

```sh
make CFLAGS="-march=x86-64" VARIANTS=x86-64-v2,x86-64-v3 BUILDTEST=no
```

### CMake integration

edge264-mvc ships a `CMakeLists.txt` that wraps its Makefile, so you can
integrate it into a CMake project without writing any custom build logic.
It exposes a single imported target `edge264mvc::edge264mvc` for use with
`target_link_libraries`.

```cmake
cmake_minimum_required(VERSION 3.14)
project(my_app C)

include(FetchContent)
FetchContent_Declare(edge264mvc
  GIT_REPOSITORY https://github.com/jens-duttke/edge264-mvc.git
  GIT_TAG        <tag>  # always pin to a tag or commit hash
)
FetchContent_MakeAvailable(edge264mvc)

add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE edge264mvc::edge264mvc)
```


## Usage

```sh
make
./edge264_test --help # prints all options available
ffmpeg -i vid.mp4 -vcodec copy -bsf h264_mp4toannexb -an vid.264 # optional, converts from MP4 format
./edge264_test -d vid.264 # replace -d with -b to benchmark instead of display
```

### Transcoding (piping decoded frames to an encoder)

`edge264_test -o` writes the decoded frames to standard output as a self-describing [YUV4MPEG2 (Y4M)](https://wiki.multimedia.cx/index.php/YUV4MPEG2) stream (dimensions and frame rate in the header), so it pipes straight into any encoder. This is the practical way to re-encode a stream FFmpeg cannot decode - in particular an **MVC 3D Blu-ray**, whose dependent view FFmpeg drops:

```sh
# 2D: re-encode the base view
./edge264_test movie.264 -o | ffmpeg -i - -c:v libx264 -crf 18 out.mp4

# 3D: -O writes the two views side by side (base | dependent) as one frame.
# There is no open MVC encoder, so a frame-compatible side-by-side H.264 (playable
# on any 3D display) is the realistic single-file 3D output; frame-packing=3 tags it.
./edge264_test movie.264 -O | ffmpeg -i - -c:v libx264 -crf 18 -x264opts frame-packing=3 out_sbs3d.mp4
```

Real 3D Blu-rays carry per-access-unit unspecified NALs (type 24) that the decoder reports as unsupported; add `-k` so the decode runs to the end instead of stopping at the first one (`-ok` / `-Ok`). The frame rate is taken from the stream's VUI and can be overridden downstream (`ffmpeg -r ...`).

The input path can also be `-` to read an Annex B stream from standard input, so a demuxer can pipe straight into the decoder without a temporary file (`demux ... | edge264_test - -O | ffmpeg -i - ...`); on POSIX a named pipe (FIFO) path works the same way. Stream input is buffered one NAL unit at a time (capped at 64 MiB), while regular files stay on the memory-mapped fast path.

Here is a complete example that opens an input file in Annex B byte stream format from the command line, and writes its decoded frames (base view) in planar YUV order to standard output. See [edge264_test.c](src/edge264_test.c) for a more complete example which can also display frames.

```c
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "edge264mvc.h"

static void write_frames(Edge264MvcDecoder *dec) {
	Edge264MvcFrame frm;
	while (edge264mvc_receive_frame(dec, &frm) == EDGE264MVC_OK) {
		for (int y = 0; y < frm.height_Y; y++)
			write(1, frm.views[0].planes[0] + y * frm.stride_Y, frm.width_Y);
		for (int p = 1; p < 3; p++)
			for (int y = 0; y < frm.height_C; y++)
				write(1, frm.views[0].planes[p] + y * frm.stride_C, frm.width_C);
		edge264mvc_release_frame(dec, &frm);
	}
}

int main(int argc, char *argv[]) {
	int fd = open(argv[1], O_RDONLY);
	struct stat st;
	fstat(fd, &st);
	const uint8_t *buf = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	size_t size = st.st_size;
	Edge264MvcDecoder *dec;
	edge264mvc_open(&dec, NULL); // default settings: one thread per CPU, no trace
	size_t pos = edge264mvc_find_start_code(buf, size);
	while (pos < size) {
		size_t start = pos + 3; // skip the 00 00 01 start code
		size_t next = start + edge264mvc_find_start_code(buf + start, size - start);
		// AGAIN means: receive the ready frames, then send the same NAL again
		while (edge264mvc_send_nal(dec, buf + start, next - start, 0, 0) == EDGE264MVC_AGAIN)
			write_frames(dec);
		write_frames(dec);
		pos = next;
	}
	edge264mvc_send_end(dec);
	write_frames(dec);
	edge264mvc_close(&dec);
	munmap((void *)buf, size);
	close(fd);
	return 0;
}
```


## API reference

The whole API is declared in [edge264mvc.h](edge264mvc.h), and the library is called `edge264mvc` (`libedge264mvc.so.2`, `edge264mvc.2.dll`). Every function returns one of these results, which have the same values on every platform:

| Result | Meaning |
|---|---|
| `EDGE264MVC_OK` (0) | success |
| `EDGE264MVC_AGAIN` (-1) | `send_nal`: the decoder is full - receive the ready frames, then send the same NAL again (every such round makes progress: a frame comes out, or a picture that can never be output, such as an MVC dependent view whose base view is missing, is dropped). `receive_frame`: no frame is ready, send more NALs. |
| `EDGE264MVC_END` (-2) | `receive_frame`: every frame was returned after `send_end` |
| `EDGE264MVC_UNSUPPORTED` (-3) | the NAL uses a type or feature the decoder does not support (e.g. the unspecified NAL types 0 and 24-31 of some 3D Blu-rays, interlaced coding, or a frame larger than `max_frame_pixels`); it was skipped, send the next one |
| `EDGE264MVC_CORRUPT` (-4) | the NAL is damaged; it was skipped and the pictures it belonged to are concealed, send the next one |
| `EDGE264MVC_NOMEM` (-5) | memory allocation failed; the NAL may be sent again |
| `EDGE264MVC_INVALID` (-6) | an argument is invalid |

<code>uint32_t <b>edge264mvc_api_version</b>(void)</code> and <code>const char * <b>edge264mvc_version</b>(void)</code>

> The API version of the loaded library, as `major << 16 | minor << 8 | patch` (compare with `EDGE264MVC_API_VERSION` from the header), and its release as text.

<code>void <b>edge264mvc_default_settings</b>(settings)</code>

> Fill an `Edge264MvcSettings` with the defaults. Always call it before changing a field, so that a field added later keeps its default.
> * `int32_t n_threads` - 0 (default): one worker thread per logical CPU available to the process; 1: decode synchronously inside `send_nal`, on the calling thread; N > 1: N worker threads (at most 16 are used)
> * `int32_t max_frame_pixels` - frames larger than this (in luma pixels) are reported as unsupported; 0 (default): 8192x4352, the largest frame any level of H.264 allows
> * `void (* log_cb)(const char * line, void * log_arg)` - if not NULL, receives a YAML trace of every header (and macroblock with `log_mbs`), possibly from worker threads; requires the `logs` build variant
> * `void * log_arg` - passed to `log_cb`
> * `int32_t log_mbs` - 1 to include every macroblock in the trace

<code>int <b>edge264mvc_open</b>(decoder, settings)</code>

> Allocate a decoder with the given settings (NULL for the defaults) into `*decoder`. Returns `EDGE264MVC_OK`, `EDGE264MVC_NOMEM` or `EDGE264MVC_INVALID`.

<code>int <b>edge264mvc_send_nal</b>(decoder, nal, size, pts, user_data)</code>

> Send one NAL unit, without its 00 00 01 start code. The bytes are copied (or decoded) before the function returns, so the buffer can be reused at once. `pts` and `user_data` are passed through to the views of the picture this NAL starts, so a player keeps its timestamps attached to its frames. Returns `EDGE264MVC_OK`, `EDGE264MVC_AGAIN`, `EDGE264MVC_UNSUPPORTED`, `EDGE264MVC_CORRUPT`, `EDGE264MVC_NOMEM` or `EDGE264MVC_INVALID`.

<code>int <b>edge264mvc_send_end</b>(decoder)</code>

> Signal the end of the stream: `receive_frame` then returns every frame still held, and `EDGE264MVC_END` afterwards. Sending a NAL afterwards starts a new stream.

<code>int <b>edge264mvc_receive_frame</b>(decoder, frame)</code>

> Return the next frame in display order. After `send_nal` returned `EDGE264MVC_AGAIN`, or after `send_end`, it waits for the worker threads to finish the frames that are due instead of returning `EDGE264MVC_AGAIN` at once. For MVC streams a frame carries both views of one access unit, paired by picture order count.
>
> ```c
> typedef struct Edge264MvcView {
> 	const uint8_t *planes[3]; // Y, Cb, Cr, already cropped; NULL in a frame without this view
> 	int64_t pts; // values given to send_nal with the first NAL of this picture
> 	int64_t user_data;
> 	int64_t display_order; // strictly increasing in output order, per view
> 	int32_t poc; // picture order count as coded, reset by every IDR
> 	int32_t decode_order; // increasing in decoding order, per decoder
> 	uint32_t flags; // EDGE264MVC_VIEW_CONCEALED (part of the picture was missing or damaged and was concealed), EDGE264MVC_VIEW_IDR
> 	uint32_t reserved;
> } Edge264MvcView;
>
> typedef struct Edge264MvcFrame {
> 	Edge264MvcView views[2]; // [0]: base view, [1]: dependent view (MVC)
> 	int32_t width_Y, height_Y, width_C, height_C; // after cropping
> 	int32_t stride_Y, stride_C; // in bytes, between rows of a plane
> 	int32_t bit_depth_Y, bit_depth_C;
> 	int32_t crop[4]; // pixels removed from the coded picture {top, right, bottom, left}
> 	void *handle; // internal
> 	uint8_t reserved[32];
> } Edge264MvcFrame;
> ```

<code>void <b>edge264mvc_release_frame</b>(decoder, frame)</code>

> Give a received frame back to the decoder, which may then reuse its memory. Every received frame must be released, and frames held by the caller limit how far the decoder can run ahead.

<code>void <b>edge264mvc_flush</b>(decoder)</code>

> Discard every picture and the decoding state, e.g. to seek. Decoding resumes at the next IDR picture or recovery point. Frames already received stay valid until released.

<code>void <b>edge264mvc_close</b>(decoder)</code>

> Stop the worker threads, free the decoder including the frames not released yet, and set `*decoder` to NULL. Accepts NULL.

<code>size_t <b>edge264mvc_find_start_code</b>(buf, size)</code>

> Return the offset of the first 00 00 01 start code in `buf[0..size)`, or `size` if there is none. Reads only inside the buffer.


## Validation and tests

`make check` builds the decoder and runs the full test suite:

```sh
make check
```

It covers a synthetic suite (tiny generated bitstreams with pixel-exact intra/inter asserts) plus several committed regression suites that edge264-mvc adds on top of stock edge264:

- **Conformance** ([`tests/conformance`](tests/conformance)) - decodes a curated subset of real JVT conformance bitstreams and compares each stream's per-view output to a committed hash anchored to the official ITU reference YUVs, together with the MVC structural guarantees (POC pairing, display order). It ships the expected hashes, so a fresh clone runs it fully offline.
- **Liveness** ([`tests/liveness`](tests/liveness)) - decodes damaged real-world streams (truncated captures, dropped or corrupt NALs) behind a progress guard, asserting the decoder always makes forward progress instead of stalling or deadlocking.
- **Memory safety** ([`tests/asan`](tests/asan)) - decodes crafted-SEI fixtures under AddressSanitizer (`make SANITIZE=address check-asan`).
- **Stream input** ([`tests/stream_input_check.py`](tests/stream_input_check.py)) - decodes small MVC fixtures from a regular file, standard input (`-`), and a POSIX FIFO, asserting byte-identical Y4M output across all three (and across single- and multi-threaded decoding), so the incremental stream reader stays in lockstep with the memory-mapped fast path. A large filler NAL exercises the buffer-growth path that a real above-64 KiB keyframe hits.
- **Forced-flush guard** ([`tests/edge264_test_liveness.py`](tests/edge264_test_liveness.py)) - fills the DPB with unfinished pictures so ordinary `ENOBUFS` draining makes no progress, then asserts `edge264_test`'s caller-side progress guard forces an end-of-stream drain and emits the exact expected frames, through both the regular-file and stdin paths in single- and multi-threaded modes. A timeout turns a regressed spin into a failure.
- **Multithreading** - every conformance and liveness fixture is also decoded with background worker threads and asserted bit-exact to the single-threaded output.

On the full set of AVCv1, FRExt and MVC [conformance bitstreams](https://www.itu.int/wftp3/av-arch/jvt-site/draft_conformance/) (231 streams), edge264 decodes 113 bit-exact against the ITU reference YUVs, 114 use yet-unsupported features, and 4 fail - the same result as stock edge264, with multithreaded output bit-exact to single-thread on every supported stream.

For ad-hoc testing and display, `edge264_test` can browse files in a given directory, decoding each `<video>.264` file and comparing its output with each sibling file `<video>.yuv` if found.

<details>
<summary>Test roadmap - implemented tests carry a file name, the rest are planned</summary>

edge264-mvc's own tests - MVC conformance, real-world decode robustness, memory safety and multithreading - live in [`tests/conformance`](tests/conformance), [`tests/liveness`](tests/liveness) and [`tests/asan`](tests/asan) (described above) and run under `make check`. The synthetic per-branch matrix below is the original edge264's roadmap; edge264-mvc has begun filling in the MVC rows it has fixtures for (see [`tests/conformance/mvc-synthetic`](tests/conformance/mvc-synthetic)).

| General tests | Expected | Test files |
| --- | --- | --- |
| All supported types of NAL units with/without logging | All OK | supp-nals |
| All unsupported types of NAL units with/without logging | All unsupp | unsupp-nals |
| Maximal header log-wise | All OK | max-logs |
| All conditions (incl. ignored) for detecting the start of a new frame | All OK | finish-frame |
| nal_ref_idc=0 on NAL types 5, 6, 7, 8, 9, 10, 11, 12 and 15 | All OK | nal-ref-idc-0 |
| Surrounding the CPB/frame buffers with protected memory | All OK | page-boundaries |
| SEI/slice referencing an uninitialized SPS/PPS | 1 OK, 4 errors | missing-ps |
| Two non-ref frames with decreasing POC | All OK, any order | non-ref-dec-poc |
| Horizontal/vertical cropping leaving zero space | All OK, 1x1 frames | zero-cropping |
| P/B slice with nal_unit_type=5 or max_num_ref_frames=0 | 4 OK, 2 errors | no-refs-P-B-slice |
| IDR slice with frame_num>0 | All OK, clamped to 0 | pos-frame-num-idr |
| A ref that must bump out higher POCs to enter DPB (C.4.5.2) | All OK, check output order | poc-out-of-order |
| Two ref frames with the same frame_num but differing POC, then a third frame referencing both |  |  |
| Gap in frame_num while gaps_in_frame_num_value_allowed_flag=0 |  |  |
| Stream starting with non-IDR I frame |  |  |
| Stream starting with P/B frame |  |  |
| Ref slice with delta_pic_order_cnt_bottom=-2**31, then a second frame referencing it |  |  |
| Two frames A/B with intersecting top/bottom POC intervals in all possible intersections |  |  |
| A 32-bit POC overflow between 2 frames |  |  |
| A B-frame referencing frames with more than 2**16 POC diff |  |  |
| num_ref_idx_active>15 in SPS then no override in slice for L0 and L1 |  |  |
| A slice with more ref_pic_list_modifications than num_ref_idx_active/16 for L0 and L1 |  |  |
| A slice with ref_pic_list_modifications duplicating a ref then referencing the second one |  |  |
| A slice with insufficient ref frames with and without override of num_ref_idx_active for L0 and L1 |  |  |
| A modification of RefPicList[0/1] to a non-existing short/long term frame, then referencing it in mb |  |  |
| 33 IDR with long_term_reference_flag=0/1 while max_num_ref_frames=0 (8.2.5.1) |  |  |
| A new reference while max_num_ref_frames are already all long-term |  |  |
| All combinations of mmco on all non-existing/short/long refs, with at least twice each mmco |  |  |
| Two fields of the same frame being assigned different long-term frame indices then referenced |  |  |
| While all max_num_ref_frames are long-term, a ref_pic_list_modification that references all of them |  |  |
| An IDR picture with POC>0 |  |  |
| A picture with mmco=5 decoded after a picture with greater POC (8.2.1) |  |  |
| A P/B frame with zero references before or received with a gap in frame_num equal to max_ref_frames |  |  |
| A P/B frame referencing a non-existing/erroneous ref |  |  |
| A B frame with colPic set to a non-existing frame |  |  |
| A current frame mmco'ed to long-term while all max_num_ref_frames are already long-term |  |  |
| A mmco marking a non-existing picture to long-term |  |  |
| All combinations of IntraNxNPredMode with A/B/C/D unavailability with asserts for out-of-bounds reads |  |  |
| A direct Inter reference from colPic that is not present in RefPicList0 |  |  |
| A residual block with all coeffs at maximum 32-bit values |  |  |
| Two slices of the same frame separated by a currPic reset (ex. AUD) |  |  |
| Two frames with the same POC yet differing TopFieldOrderCnt/BottomFieldOrderCnt |  |  |
| Differing mmcos on two slices of the same frame |  |  |
| Sending 2 IDR, then reaching the lowest possible POC, then getting all frames |  |  |
| Two slices with mmco=5 yet frame_num>0 (to make it look like a new frame) |  |  |
| POCs spaced by more than half max bits, such that relying on a stale prevPicOrderCnt yields wrong POC |  |  |
| Filling the DPB with 16 refs then setting max_num_ref_frames=1 and adding a new ref frame |  |  |
| Adding a frame cropping after decoding a frame | Crop should not apply retroactively |  |
| Making a Direct ref_pic be used after it has been unreferenced |  |  |
| poc_type=2 and non-ref frame followed by non-ref pic, and the opposite (7.4.2.1.1) |  |  |
| direct_8x8_inference_flag=1 with frame_mbs_only_flag=0 |  |  |
| checking that a gap in frame_num with poc_type==0 does not insert refs in B slices |  |  |
| A SPS changing frame format while currPic>=0 |  |  |
| A frame allocator putting pic/mb allocs at start/end of a page boundary |  |  |
| Two escape sequences in a single refill (ex. from a Picture timing SEI message) |  |  |
| All supported NAL types with wrong omission or insertion of trailing bit |  |  |

| Parameter sets tests | Expected | Test files |
| --- | --- | --- |
| Invalid profile_idc=0/255 |  |  |
| Highest level_idc=255 |  |  |
| All unsupported values of chroma_format_idc |  |  |
| All unsupported values of bit_depth_luma/chroma |  |  |
| qpprime_y_zero_transform_bypass_flag=1 |  |  |
| All scaling lists default/fallback rules and repeated values for all indices, with residual macroblock |  |  |
| log2_max_frame_num=4 and a frame referencing another with the same frame_num%4 |  |  |
| Every unsupported feature should be reported as `EDGE264MVC_UNSUPPORTED` and make a log containing a `# unsupported` line |  |  |

| CAVLC tests | Expected | Test files |
| --- | --- | --- |
| All valid total_zeros=0-8-prefix+3-bit-suffix for TotalCoeffs in [0;15] for 4x4 and 2x2 |  |  |
| Invalid total_zeros=31/63/127-prefix for TotalCoeffs in [0;15] for 4x4 and 2x2 |  |  |
| All valid coeff_token=0-14-prefix+4-bit-suffix for nC=0/2/4, and valid 6-bit-values for nC=8 |  |  |
| Invalid coeff_token=31/63/127-prefix for nC=0/2/4, and invalid 6-bit-values for nC=8 |  |  |
| All valid levelCode=25-prefix+suffixLength-bit-suffix for all values of suffixLength |  |  |
| All valid run_before for all values of zerosLeft<=7 |  |  |
| Invalid run_before=31/63/127 for zerosLeft=7 |  |  |
| Macroblock of maximal size for all values of mb_type |  |  |
| mb_qp_delta=-26/25 that overflows on both sides |  |  |
| All valid inferences of nC for all values of nA/nB=unavail/other-slice/0-16 |  |  |
| All coded_block_pattern=[0;47] for I and P/B slices |  |  |
| All combinations of intra_chroma_pred_mode and Intra4x4/8x8/16x16PredMode with A/B-unavailability |  |  |
| All values of mb_type+sub_mb_types for I/P/B with ref_idx/mvds different than values from B_Direct |  |  |
| mvd=[-32768/0/32767,-32768/0/32767] in a single 16x16 macroblock |  |  |
| TotalCoeff=16 for a Intra16x16 AC block |  |  |
| A residual block with run_length=14 making zerosLeft negative |  |  |

| CABAC tests | Expected | Test files |
| --- | --- | --- |
| Mixing CAVLC and CABAC in a same frame |  |  |
| Single slice with at least 8 cabac_zero_word |  |  |

| MVC tests | Expected | Test files |
| --- | --- | --- |
| All wrong combinations of non_idr_flag with nal_unit_type=1/5 and nal_ref_idc=0/1 |  |  |
| nal_unit_type=14 then filler unit then nal_unit_type=1/5 |  |  |
| An nal_unit_type=5 view paired with a non_idr_flag=0 P view, or a non_idr_flag=1 view |  |  |
| Missing a base or non-base view | No stall: lone base emitted, orphan dependent dropped | mvc_unpaired_base, mvc_orphan_dependent |
| Receiving a SSPS yet only base views then |  |  |
| 16 ref base views while non base are non-refs |  |  |
| A SSPS with different pic_width_in_mbs/pic_height_in_mbs/chroma_format_idc than its SPS |  |  |
| A SSPS with num_views=1 |  |  |
| A non-base view with weighted_bipred_idc=2 |  |  |
| A non-base view with its base in RefPicList1[0] and direct_spatial_mv_pred_flag=0 (H.7.4.3) |  |  |
| A slice with num_ref_idx_l0_active>8 |  |  |
| svc_extension_flag=1 on a MVC stream |  |  |
| SSPS with additional_extension2_flag=1 and more trailing data |  |  |
| Gap in frame_num of 16 frames on both views |  |  |
| Specifying extra_frames=1 |  |  |
| Receiving a non-base view before its base | Paired, 2 frames | mvc-synthetic/mvc_dep_before_base |
| A stream sending non-base views after a few frames have been output | 2D then stereo, 4 frames | mvc-synthetic/mvc_late_dependent |

| Error recovery tests | Expected | Test files |
| --- | --- | --- |
| Tests to implement |  |  |
| A complete frame received twice |  |  |
| A slice of a frame received twice |  |  |
| Frame with correct and erroneous slice |  |  |
| All combinations erroneous/correct and all interval intersections on 2 slices |  |  |
| All failures of malloc |  |  |
| All (dis-)allowed bit positions at the end without rbsp_trailing_bit |  |  |

</details>


## Design

edge264 was created to experiment with programming techniques that improve performance and reduce code size over existing decoders; several of them were presented at [FOSDEM'24](https://fosdem.org/2024/schedule/event/fosdem-2024-2931-innovations-in-h-264-avc-software-decoding-architecture-and-optimization-of-a-block-based-video-decoder-to-reach-10-faster-speed-and-3x-code-reduction-over-the-state-of-the-art-/), [FOSDEM'25](https://fosdem.org/2025/schedule/event/fosdem-2025-5455-more-innovations-in-h-264-avc-software-decoding/) and [FOSDEM'26](https://fosdem.org/2026/schedule/event/ADXJMU-innovations-with-yaml-cabac-simd-in-h264-decoding/).

1. [Single header file](src/edge264_internal.h) - It contains all struct definitions, common constants and enums, SIMD aliases, inline functions and macros, and exported functions for each source file. To understand the code base you should look at this file first.
2. [Code blocks instead of functions](src/edge264_slice.c) - The main decoding loop is a forward pipeline designed as a DAG loosely resembling hardware decoders, with nodes being non-inlined functions and edges being tail calls. It helps mutualize code branches wherever possible, thus reduces code size to help fit in L1 cache.
3. [Tree branching](src/edge264_intra.c) - Directional intra modes are implemented with a jump table to the leaves of a tree then unconditional jumps down to the trunk. It allows sharing the bottom code among directional modes, to reduce code size.
4. ~~Global context register - The pointer to the main structure holding context data is assigned to a register when supported by the compiler (GCC).~~ This technique was dropped as Clang eventually reached on-par performance, so there is little incentive to maintain this hack.
5. [Default neighboring values](src/edge264_internal.h) (search `unavail_mb`) - Tests for availability of neighbors are replaced with fake neighboring macroblocks around each frame. It reduces the number of conditional tests inside the main decoding loop, thus reduces code size and branch predictor pressure.
6. [Relative neighboring offsets](src/edge264_internal.h) (look for `A4x4_int8` and related variables) - Access to left/top macroblock values is done with direct offsets in memory instead of copying their values to a buffer beforehand. It helps to reduce the reads and writes in the main decoding loop.
7. [Parsing uneven block shapes](src/edge264_slice.c) (look at function `parse_P_sub_mb`) - Each Inter macroblock paving specified with mb_type and sub_mb_type is first converted to a bitmask, then iterated on set bits to fetch the correct number of reference indices and motion vectors. This helps to reduce code size and number of conditional blocks.
8. [Using vector extensions](src/edge264_internal.h) - GCC's vector extensions are used along vector intrinsics to write more compact code. All intrinsics from Intel are aliased with shorter names, which also provides an enumeration of all SIMD instructions used in the decoder.
9. [Register-saturating SIMD](src/edge264_deblock.c) - Some critical SIMD algorithms use more simultaneous vectors than available registers, effectively saturating the register bank and generating stack spills on purpose. In some cases this is more efficient than splitting the algorithm into smaller bits, and has the additional benefit of scaling well with later CPUs.
10. [Piston cached bitstream reader](src/edge264_bitstream.c) - The bitstream bits are read in a size_t\[2\] intermediate cache with a trailing set bit to keep track of the number of cached bits, giving access to 32/64 bits per read from the cache, and allowing wide refills from memory.
11. [On-the-fly SIMD unescaping](src/edge264_bitstream.c) - The input bitstream is unescaped on the fly using vector code, avoiding a full preprocessing pass to remove escape sequences, and thus reducing memory reads/writes.
12. [Multiarch SIMD programming](src/edge264_internal.h) - Using vector extensions along with aliased intrinsics allows supporting both Intel SSE and ARM NEON with around 80% common code and few #if #else blocks, while keeping state-of-the-art performance for both architectures.
13. [The Structure of Arrays pattern](src/edge264_internal.h) - The frame buffer is stored with arrays for each distinct field rather than an array of structures, to express operations on frames with bitwise and vector operators (see [AoS and SoA](https://en.wikipedia.org/wiki/AoS_and_SoA)). The task buffer for multithreading also relies on it partially.
14. [Deferred error checking](src/edge264_headers.c) - Error detection is performed once in each type of NAL unit (search for `return` statements), by clamping all input values to their expected ranges, then expecting `rbsp_trailing_bit` afterwards (with _very high_ probability of catching an error if the stream is corrupted). This design choice is detailed in [A case about parsing errors](https://traffaillac.github.io/parsing.html).
15. [YAML logging output](src/edge264_headers.c) - The YAML format is used for logging, which makes debugging easier, enables reencoding (used for creation of custom bitstreams) and data analysis.
16. [CABAC decoding](src/edge264_bitstream.c) - The CABAC internal state is extended to use the full bit range of CPU registers, allowing less frequent renormalization trips to memory, and the batch-decoding of *bypass* bits with a hardware division.


## Relation to edge264

edge264-mvc is a standalone decoder derived from [tvlabs/edge264](https://github.com/tvlabs/edge264) by Thibault Raffaillac, which grew up as a research effort on new software engineering practices (most notably C vector extensions in place of hand-crafted assembly). `main` adds the fixes and speed-ups below on top of the edge264 codebase; each fix also lives on its own `fix/*`, `pick/*` or `port/*` branch, each speed-up on its own `perf/*` branch, and cherry-picked PRs keep their original authorship.

Multithreaded decoding is the headline addition. By default `edge264mvc_open` starts one worker thread per logical CPU available to the process; `n_threads = 1` in the settings decodes on the calling thread instead. Stock edge264's experimental multi-thread path was broken (pre-existing, reproducible on pristine edge264 even for non-MVC streams - a teardown deadlock, out-of-order output, an MVC stereo-pairing stall and data races); edge264-mvc makes multithreaded output **bit-exact to single-thread on every supported stream of the 231-stream JVT corpus**, hang-free under heavy thread oversubscription, and ThreadSanitizer-clean. It also keeps PR #25's single-threaded decode-hang fix ([PR #25](https://github.com/tvlabs/edge264/pull/25) · @intrepidsilence, `ready_tasks == 0`). Since version 2 the library has its own API, `edge264mvc` (see the API reference above): the original edge264 API returned platform-dependent `errno` values, passed no timestamps through, never reported concealed pictures and shared the original's library name with a different frame layout.

Worker threads decode consecutive pictures at the same time, the way FFmpeg's frame threading does: a picture starts as soon as the pictures it predicts from have started, and waits row by row until the reference rows it reads are decoded and deblocked. Slices of one picture are decoded in parallel too, and deblocked in order with their own parameters. Damaged pictures are concealed only where nothing was published yet, so the output stays the same whatever the number of threads. On a 1080p High Profile stream with 16 threads this is about 3x faster than the original scheduler, which only started a picture once its references were complete.

**Performance** - every change keeps the output identical (verified on the full JVT corpus, single- and multithreaded), and each was measured by running the old and new build side by side on the same machine, so that other load hits both alike. Decoding several pictures at once is limited by memory bandwidth rather than by computation, so several of these changes reduce the bytes written and read per macroblock. Measured on an 8-core / 16-thread laptop CPU against FFmpeg on the same streams, decoding is 5% (4K) to 22% (1080p) faster with 16 threads, and 11% to 26% faster single-threaded (FFmpeg cannot decode the MVC dependent view, so MVC was compared on the base view):

| Change | Effect |
|---|---|
| Start decoding a picture as soon as the pictures it predicts from are being decoded, waiting row by row for the reference rows it reads | about 3x faster on 1080p High Profile with 16 threads |
| Wake only the tasks waiting on the picture that progressed, and let them sleep until two rows beyond what they need | about 95% fewer context switches |
| Skip the weighted blend for inter predictions without weights | 5% to 7% fewer instructions on High Profile |
| Skip the deblocking of macroblocks without any filtered edge | 6% to 21% fewer instructions, depending on the content |
| Copy the chroma rows of integer motion vectors instead of interpolating them | 2% fewer instructions |
| Average implicitly weighted predictions of equal weights like default ones | 1.7% fewer instructions on content using implicit weights |
| Let a slice decoded before the preceding slice of its picture is finished leave its deblocking to the thread that finishes that slice, instead of blocking its worker | 6% faster on MVC Blu-ray streams (several slices per picture) with 16 threads |
| Back large picture buffers with transparent huge pages on Linux | 2% to 3% faster with 16 threads, 3% single-threaded |
| Prefetch the reference rows of the next macroblocks | 2.5% faster single-threaded, 1% to 1.5% with threads |
| Keep the macroblock values only read by neighbours in a small per-thread ring instead of the per-picture array (304 to 192 bytes per macroblock) | 11% faster with 16 threads on MVC and 1080p, 3% single-threaded |
| Leave the unused L1 motion vectors of P macroblocks unwritten | 5% faster on MVC with 16 threads, 1% on 1080p |
| Stop prefetching the colocated macroblock of every B macroblock | 4% faster on 4K with 16 threads, 2% on MVC, 1% on 1080p |

**MVC / stereo correctness** - the reason this project exists; verified on three commercial 1080p MVC streams with **0 pairing / 0 ordering errors over 2,500+ frame pairs**:

| Fix | Source |
|---|---|
| MVC DPB slot aliasing (corrupt dependent view) | [PR #23](https://github.com/tvlabs/edge264/pull/23) · @intrepidsilence |
| MVC subset-SPS DPB undersizing (assert / single-thread hang) | edge264-mvc |
| Strict subset-SPS trailing bits (remuxed Blu-rays) | edge264-mvc |
| Unpairable MVC base view deadlock (dropped/corrupt dependent NAL) | edge264-mvc |
| Orphan MVC dependent view deadlock (dropped/corrupt base NAL) | edge264-mvc |
| MVC DPB stall on small-resolution streams (0 frames, ENOBUFS spin) | edge264-mvc |
| Export per-view POC / monotonic display POC | [issue #27](https://github.com/tvlabs/edge264/issues/27) · @vkapartzianis |
| Stereo view desync (wrong base/dependent pairing) | [issue #27](https://github.com/tvlabs/edge264/issues/27) · @vkapartzianis |
| Jittery playback (decode- vs display-order) | [issue #27](https://github.com/tvlabs/edge264/issues/27) · @vkapartzianis ([issue #16](https://github.com/tvlabs/edge264/issues/16)) |
| MVC view mispairing at a mid-stream IDR (dependent view dropped each GOP) | edge264-mvc |
| MVC same-POC view mispairing (non-deterministic dependent view under multithreading / paced DPB overflow) | edge264-mvc |
| MVC inter-view reference appended before the RefPicList1-vs-RefPicList0 swap (wrong dependent-view L1 / direct-mode reference on a single-temporal-reference B slice) | edge264-mvc |
| MVC per-view MMCO5 reset cleared the co-decoded view's long-term frame indices (marking must be per view component) | edge264-mvc |
| Keep the exported display POC strictly monotonic across real-world POC discontinuities (open-GOP LSB wrap and same-POC access units on commercial 3D Blu-rays) | edge264-mvc |
| MVC deep-B two-view output deadlock / C.4.5 fullness abort (the output-buffer gate counted both view queues against one 16-slot budget, halving MVC capacity below the reorder window; a GOP16 hierarchical-B stream that filled both view reference sets then deadlocked under multithreading and aborted in debug) | edge264-mvc |
| MVC base-view display-order swap on a stream coded decode-order != display-order (a reverse-pairing pass queued a base when its dependent was seen, i.e. in decode order, stamping its display rank out of order; output is now strictly base-driven, so the base view again matches FFmpeg) | edge264-mvc |
| MVC multithreaded deadlock on a base-less dependent-view tail, as a byte-trimmed 3D-BD stream or an unequal-length two-file base+dependent feed produces (the orphan-dependent liveness valve dropped a dependent view whose decode tasks were still running - or that was still being parsed - freeing its DPB slot for reallocation while the stale tasks still wrote it; their `remaining_mbs` subtractions corrupted the new occupant's completion counter, dependent slices misfired `EBADMSG`, the frame never finalized, and once every task slot waited on it `edge264_decode_NAL` blocked forever at 0% CPU. The valve now defers the drop until the dependent has no in-flight tasks, and the slot allocator refuses any slot still written by a busy task) | edge264-mvc |

**Decode robustness on real-world streams** - found by a broad decode audit over a large, heterogeneous sample corpus (crashes, hangs, wrong output and decode failures that the synthetic and conformance suites do not exercise). Each carries a committed regression fixture ([`tests/liveness`](tests/liveness), [`tests/asan`](tests/asan) or [`tests/conformance`](tests/conformance)) and is **inert on the full JVT conformance set** (identical results before and after, zero regressions); each was verified against FFmpeg on real captures:

| Fix | Failure mode it removes |
|---|---|
| Floor `max_num_ref_frames` at 1 so a reference IDR fits the DPB | reference IDR didn't fit the DPB (C.4.5 fullness assert) |
| Floor derived `max_dec_frame_buffering` at the reference count | a resolution-exceeds-signaled-level stream aborted a C.4.5 assert |
| Keep the signaled `max_num_ref_frames` on an over-level stream (bound by DPB capacity, not the level) | a frame-exceeds-signaled-level stream clamped its reference set below the count its own slices use - silently wrong inter prediction (single-thread) and a nondeterministic multithreaded decode |
| Report an FMO PPS (`num_slice_groups > 1`) as `ENOTSUP` instead of a decode error | the unparsed slice-group map left the bit position mid-syntax, so the PPS misfired `EBADMSG` - a valid but unsupported stream looked corrupt instead of cleanly skippable |
| Read `frame_mbs_only_flag` before bounding `pic_height_in_map_units` | tall progressive frames clamped / stalled |
| Reject `frame_num` gap with no reclaimable slot (instead of aborting) | a frame-num gap aborted the decoder |
| Harden SEI parsing against crafted `payloadType` / `payloadSize` | out-of-bounds read / multi-second CPU burn on a crafted SEI |
| Skip an unhandled SEI message by its cache-aware bit count, not the refill pointer | a small trailing SEI that fit entirely in the bitstream cache (a `recovery_point` closing a base-view access unit, as commercial 3D Blu-rays carry) was not advanced past, so its payload was re-parsed as bogus follow-on messages and misfired `EBADMSG`, failing an otherwise valid stream in the header-logging / trace path |
| Advance past a *handled* SEI message by its declared `payloadSize`, not just byte-alignment | a handled SEI message that consumed fewer bytes than its `payloadSize` (a short-reading or forward-compatible payload with trailing reserved data) left the tail in the stream, re-parsed as bogus follow-on messages, so the reader overran the RBSP trailing and misfired `EBADMSG` on a valid stream (the mirror of the unhandled-message skip above, on the success path) |
| Emit an incomplete final picture at end-of-stream | a capture truncated mid-frame (broadcast TS/M2TS) deadlocked the drain |
| Recover an orphaned undelivered picture on a flush drain | a corrupt-slice frame stalled the DPB and lost the last picture |
| Conceal every picture still incomplete at the end-of-stream flush (`bump_all_frames`), including queued MVC dependent views | a stream truncated in the middle of its last dependent-view picture held the complete base view for that dependent forever, so the drain returned `ENOBUFS` without end; and an incomplete picture emitted at the flush showed whatever its DPB slot held before in its undecoded part, which differed between single- and multithreaded decoding |
| Conceal a picture left incomplete as soon as no task writes it and the next picture starts, keeping the macroblocks its slices decoded or recovered (`release_terminal_task_dependencies`, `conceal_frame`) | an incomplete picture awaiting output was overtaken by every later picture delivered before the end-of-stream flush concealed it, so where it appeared in the output depended on thread timing (and a run of them could fill the DPB until the caller forced a drain that dropped the rest of the stream) |
| Conceal only the unpublished part of a damaged picture, keeping the macroblocks decoded before the first missing or damaged slice (`conceal_frame`) | a picture missing one slice, or with one corrupt slice, was replaced as a whole by neutral samples (or by its base view for an MVC dependent view), discarding every correctly decoded macroblock |
| Flush held pictures at an `end_of_sequence` NAL, not only at the end of the buffer (`parse_end_of_sequence` now sets the same flushing valve the end-of-buffer drain uses) | a multi-clip 3D player that ends a clip on its `end_of_sequence` unit, whose trailing MVC base view had no paired dependent, left that base held and spun `ENOBUFS` in the draining caller - losing the clip's last base frame instead of emitting it |
| Let a format change through a new SPS emit the unpairable MVC base pictures of the previous sequence, like an `end_of_sequence` does | the change waits until every picture of the previous sequence is taken, but a base whose dependent view never came was held for it, so the SPS returned `ENOBUFS` without any frame to drain until the caller drained the whole stream - the stall a player has to detect with a progress guard; found by fuzzing |
| Reject an MVC dependent-view slice whose subset SPS declares another frame size than the base SPS (`parse_slice_layer_without_partitioning`) | such a subset SPS cleared the base SPS through a format change, and the dependent slices that followed allocated their frames from its zero size - a 4 GB allocation per frame; found by fuzzing |
| Clamp out-of-range RefPicList entries | stack overrun / access violation on a non-conformant ref list |
| Never take the current picture as a reference when fixing up an out-of-range `RefPicList`, and reject a slice that has no other picture to refer to (`parse_ref_pic_list_modification`) | the fix-up above replaced missing references with slot 0, which can be the picture being decoded: single-threaded it then predicted from its own undecoded samples, multithreaded its task waited forever on its own progress (a hang on a damaged stream starting with an inter slice) |
| Leave a `ref_pic_list_modification` that names a missing picture to the out-of-range fix-up instead of inserting the last short-term picture iterated | the picture inserted then depended on which DPB slots the references occupied, which differs between single- and multithreaded decoding - a damaged stream predicted from different references in each mode |
| Reject a base-less inter-coded MVC dependent-view slice (type 20, P/B) whose base view was never decoded | a stream carrying dependent-view slices but no decodable base view (no base-view SPS) left every dependent slice's inter-view reference resolving to its own not-yet-decoded frame slot, so its decode task depended on its own picture; multithreaded, those self-dependent tasks piled up until the parser deadlocked waiting for a free task slot and `edge264_decode_NAL` never returned (FFmpeg likewise reports the missing base view and produces no frame) |
| Tolerate a VUI that over-reads past the SPS rbsp | whole stream dropped over a common encoder defect |
| Tolerate a CABAC slice that over-reads past its NAL when complete | a dense 4K multi-slice CABAC frame stalled mid-stream |
| Tolerate non-1 `cabac_alignment_one_bit` padding | every slice rejected -> mid-stream stall, 0 frames |
| Reject a corrupt MVC dependent-view slice header instead of taking it for a new picture (`parse_slice_layer_without_partitioning`), and conceal the damaged dependent picture from the base view of its access unit rather than with neutral samples | one damaged dependent-view slice on an otherwise intact 3D stream was read as a new dependent picture after a `frame_num` gap, which seeded the dependent view's `PrevRefFrameNum` / `prevPicOrderCnt` with garbage; every later dependent POC then mismatched its base, the base-driven pairing never queued them, and `edge264_decode_NAL` returned `ENOBUFS` forever with nothing left to drain - a caller spun at 100% CPU on a file, or took the jam for end-of-stream and silently truncated the video when fed from a pipe |
| Reject an MVC dependent-view IDR slice that would open a new picture on `idr_pic_id` alone, while a dependent IDR picture is open and the slice does not start at macroblock 0 (`parse_slice_layer_without_partitioning`) - arbitrary slice order is not allowed in the MVC profiles (H.10.1.1, H.10.1.2), so a slice starting further in is a damaged continuation, not a new picture ([PR #13](https://github.com/jens-duttke/edge264-mvc/pull/13) · @cbusillo) | the companion to the corrupt slice header above, one syntax element over: that guard needs an inter-coded slice, so it cannot see an intra dependent slice whose `frame_num` and `pic_order_cnt` stay intact while only `idr_pic_id` is damaged. Accepting it closed the open picture and opened a second dependent picture carrying the same `(FrameNum, POC)` as the first; the base of the access unit paired with one of them, the other was never queued for output, and the DPB filled until `edge264_decode_NAL` returned `ENOBUFS` forever - the same 100% CPU spin on a file, or a silently truncated video from a pipe |
| Generalise both guards above to the invariant behind them: a type-20 slice with `first_mb_in_slice > 0` cannot start a picture at all, so any new-picture trigger firing on one marks a damaged header (`parse_slice_layer_without_partitioning`) | the two guards above each closed a single trigger, leaving the same jam reachable one syntax element over - a damaged `nal_ref_idc` or `pic_order_cnt` on a slice of an open dependent IDR picture still opened a second dependent picture carrying the first one's `(FrameNum, POC)`, so the base of the access unit paired with one of them, the other was never queued, and `edge264_decode_NAL` returned `ENOBUFS` forever. Arbitrary slice order is not allowed in either MVC profile (H.10.1.1, H.10.1.2), so by 7.4.3 a picture's first slice always addresses macroblock 0; the check now covers `frame_num`, `nal_ref_idc`, `idr_pic_id` and `pic_order_cnt` in one place |
| Compare the reference block of a motion vector against the picture bounds in signed arithmetic (`decode_inter`) | on a picture one macroblock (16 pixels) wide, the right bound went negative and wrapped in the unsigned comparison, so a motion vector pointing left of the picture skipped the edge propagation and the interpolation read before the reference picture (an out-of-bounds read, found by fuzzing) |
| Reject a slice whose `first_mb_in_slice` is outside the current picture | out-of-bounds macroblock write / crash when interleaved multi-resolution streams (e.g. main + secondary/PiP video) reach one decoder |
| Fall back to the Default scaling matrices (Fall-Back Rule Set A, Table 7-2) when a PPS declares `pic_scaling_matrix_present_flag = 1` with absent lists over a `seq_scaling_matrix_present_flag = 0` SPS | the absent PPS lists inherited the SPS's `Flat_16` instead of the spec-mandated `Default_4x4`/`Default_8x8` weighting, so a High Profile stream using this (legal, common) combination dequantized every coefficient wrong - whole-picture colour-block corruption that a commercial 3D Blu-ray's MVC stream showed in **both** views (the base view is mis-decoded in the plain AVC path, and the dependent view inherits it through inter-view prediction) |

**Spec conformance & cross-ISA correctness** - decoder-correctness fixes verified arithmetically or by construction against the H.264 spec and the other SIMD backends rather than by a captured stream. None is observed on a real capture or the JVT conformance set (they cover spec-boundary cases, high-QP / custom-scaling-matrix arithmetic, cross-SIMD-backend bit-exactness and undefined-behaviour hardening); each is **inert on the full JVT conformance set** (identical results before and after):

| Fix | Failure mode it removes |
|---|---|
| Round SSE temporal-direct motion vectors with the spec's +128 bias | a signed `DistScaleFactor` sign-extended into the packed rounding constant, biasing the round to +129 for every negative scale factor (the ordinary "current picture between the collocated picture and its reference" case), so B-slice and MVC inter-view direct motion vectors on the shipped x86/SSE build diverged by up to one quarter-pel from the NEON / WASM / scalar backends |
| Derive the POC of inferred `frame_num`-gap frames the same way as the main path for `pic_order_cnt_type == 1` | the gap-fill loop multiplied by `PicOrderCntDeltas[cycle]` (one past the only written entries `[0..cycle-1]`, reading into the next SPS field at the maximum cycle of 255) and dropped the `-1` on the cycle quotient and remainder, so a synthesized non-existing reference frame got a wrong POC |
| Dequantize high-QP 8x8 residuals at 32-bit precision | the `qP >= 36` branch scaled `weightScale8x8 * normAdjust8x8` up in 16-bit lanes; with a custom (non-flat) 8x8 scaling matrix at qP 49-51 the shift overflowed int16 and wrapped to a sign-flipped operand -> garbage dequantized coefficients (the `qP < 36` branch and the 4x4 path already scale at 32 bits) |
| Guard the 8.2.5.3 sliding-window marking on a short-term reference actually existing | a non-conformant stream that filled every reference slot with long-term frames (short-term count 0) made the process fabricate a short-term reference at slot 0 and demote it out of the long-term set, corrupting the reference set |
| Avoid left shifts of signed values (explicit weighted-prediction offsets, the picture order count lsb difference, the CAVLC level offset, the weight packing and the DPB slot masks at slot 31) | undefined behaviour the compiler may optimise on any stream - negative explicit offsets are legal, so valid weighted-prediction streams were affected too; found by fuzzing with UndefinedBehaviorSanitizer |
| Bound the leading-zero count of a CAVLC `run_before` escape code to the 10 zeros a valid code can have | on a damaged stream the bit cache could be all zeros, and counting its leading zeros is undefined (garbage on CPUs without `lzcnt`, which the portable x86-64 baseline build runs on older CPUs), followed by an undefined 64-bit shift and trailing-zero count in the bit reader; found by fuzzing |
| Point the colocated macroblock of `recover_slice` to the current one outside B slices, like `initialize_context` | concealing the rest of a damaged P slice computed and advanced a pointer from the colocated array, which is NULL there - undefined behaviour, found by fuzzing |
| Load unaligned 32- and 64-bit values through `memcpy` in the SSE backend (`loadu32`, `loadu32x4`, `loadu64x2`), like the NEON and generic backends | the SSE versions dereferenced cast integer pointers on unaligned reference rows, which is undefined behaviour on every stream with motion compensation (harmless with today's compilers on x86, but not guaranteed); found by fuzzing |
| Count the bits left in a NAL in 64 bits (`bits_left`, used by `rbsp_end`, the SPS trailing-bits check and the SEI skip) | a reader that overran a damaged NAL by hundreds of MB overflowed the 32-bit `(end - CPB) * 8` - undefined behaviour; found by fuzzing |
| Address the neighbouring motion vectors of sub-macroblock partitions through a pointer formed from the macroblock address | the prediction subscripted `mvs_s[32]` with negative offsets into the previous macroblocks - undefined behaviour on every stream with sub-macroblock partitions, which the compiler may assume never happens; found by fuzzing |
| Guard the default frame allocator against `NULL + size` pointer arithmetic on a failed allocation | undefined behaviour on OOM (masked by the caller's NULL check, but flagged by UBSan / strict builds) |
| Parse `redundant_pic_cnt` in the slice header, and skip a redundant coded picture, instead of reporting the PPS that enables it as unsupported | a PPS with `redundant_pic_cnt_present_flag = 1` was rejected with `ENOTSUP`, dropping every stream that carries primary-picture redundancy (legal in Baseline and Extended Profile) as a whole. The field sits between the picture order count fields and the slice-type dependent ones (7.3.3), so leaving it unread shifts every later header field and lands `cabac_alignment_one_bit` inside slice data - hence the blanket rejection. Primary slices (`redundant_pic_cnt = 0`) now decode; a redundant copy is a lower quality duplicate of content the primary already carries (7.4.3), so it is skipped as `ENOTSUP` rather than overwriting the primary |
| Clamp the portable-C (non-SIMD) `pow2x4` shift count and saturate its `temporal_scale` narrow | on the non-shipped generic-vector backend, `1 << (negative count)` for an empty-`RefPicList` slot was UB (masked to 0 afterwards) and the motion-vector narrow truncated where SSE / NEON / WASM saturate - the shipped backends were already correct |

**Multithreaded decoding** - stock edge264's background-thread path was unusable; these make it bit-exact to single-thread and hang-free, validated over the full JVT corpus and with ThreadSanitizer. All are inert in the single-threaded path (`n_threads = 0`):

| Fix | Failure mode it removes |
|---|---|
| Join worker threads on teardown instead of cancel-and-destroy | `edge264_free` deadlocked in `pthread_cond_destroy` after every multithreaded decode |
| Hold an MVC base frame until its lagging dependent view is ready | dependent view stranded in its queue -> hard stall on MVC streams under thread contention |
| Emit frames in monotonic display order, waiting on the in-flight earliest | out-of-order output / ballooning `DisplayPoc` across a GOP boundary under multithreading |
| Make `next_deblock_addr` accesses atomic (acquire/release) | data race on the deblock-frontier / completion flag (benign on x86-64, torn/stale on ARM) |
| Persist the auto-detected logical-core count so teardown joins every worker | `edge264_alloc(-1)` left the raw `-1` in the join-loop bound, so `edge264_free` joined no workers and freed the decoder under still-live threads (teardown access violation, surfaced on the Windows/MinGW build) |
| Clamp the requested worker count to the fixed-size thread pool | an explicit `n_threads` above 16 overran the internal 16-slot `threads` array in the spawn and join loops |
| Copy slice NALs into decoder-owned memory so worker threads outlive the caller's buffer | a caller that reused or freed its NAL buffer once `decode_NAL` returned (correct single-threaded, where decoding is synchronous) corrupted the slice still being decoded by a background worker - CABAC desync, then a DPB stall, on real multi-slice MVC streams |
| Run pending tasks' unref callbacks on teardown | a slice NAL copied for an asynchronous worker but not yet taken by one leaked when `edge264_free` stopped the workers (only the flush path drained them) |
| Make the DPB bump sequence deterministic: queue an MVC dependent view on the parsing thread as soon as its base is queued, give the immediate-output bump a display rank, and hold a picture whose slices are still being parsed | multithreaded output order varied from run to run around IDR/GOP boundaries on real MVC streams (every frame decoded correctly, but emitted in a schedule-dependent order): `get_frame`'s consumer-side pairing valve fired on worker timing and perturbed the parse-side bump triggers, the then-reachable immediate-output path reused a stale display rank so a new GOP's IDR overtook queued frames, and a multi-slice picture could momentarily escape the display-order hold between two of its slices |
| Conceal a terminal incomplete dependency before any task-completion wait | an asynchronously failed or truncated reference slice left `remaining_mbs > 0` after its last writer exited; later slices waited on that frame while every worker slept because no task was ready, deadlocking at task-pool saturation, EOS/flush, or DPB-slot pressure (the synchronous path's no-ready-task safety net kept progressing) |
| Deblock and publish the slices of a picture in order, each with its own deblocking parameters, and take tasks in decoding order | a slice decoded while its predecessor was still running left its macroblocks to whichever slice completed the picture, which deblocked them with its own `slice_alpha_c0_offset_div2` / `slice_beta_offset_div2`: wrong, run-to-run varying pixels on multi-slice pictures whose slices carry different offsets |

**Build / cross-compile** - the Windows DLL is cross-built with MinGW-w64; wasm via Node:

| Fix | Source |
|---|---|
| Route aligned allocations through a MinGW-compatible CRT pair (`_aligned_malloc`/`_aligned_free`) | edge264-mvc |
| Stop MinGW's `stdlib.h` `min`/`max` macros from shadowing the typed helpers | edge264-mvc |
| Probe Node for relaxed-SIMD flag support in the wasm `make check` | edge264-mvc |
| Guard the multithreaded ref-dependency mask against empty `RefPicList` slots on the portable non-SIMD path | [issue #28](https://github.com/tvlabs/edge264/issues/28) |
| Pair `-march=native` with `-mtune=generic` on native builds - GCC's per-microarch cost model schedules measurably slower code than generic tuning for this hand-written-SIMD codebase (bit-exact; cross-compiled / `-march=x86-64-v*` distribution builds already tune generic and are unaffected) | edge264-mvc |

**Deliberately not included:**

- [PR #26](https://github.com/tvlabs/edge264/pull/26) (scaling-matrix defaults): the
  full JVT conformance run shows it breaks 5 High Profile streams with
  `seq_scaling_matrix_present_flag = 0` (the spec mandates flat-16 for the SPS's own
  lists there, and `parse_scaling_lists` already implements the value cascade correctly).
  Note this is a different case from the genuine PPS fall-back bug fixed above: PR #26
  wrongly changed the *SPS* flat-16 default, whereas the fix above corrects which
  fall-back rule set (A vs B) applies when a *PPS* declares its own scaling matrix.
- Unspecified NAL types (0, 24-31) - including the type-24 units some 3D Blu-rays carry
  ([issue #20](https://github.com/tvlabs/edge264/issues/20)) - are reported as unsupported (`EDGE264MVC_UNSUPPORTED`) by design,
  matching stock edge264's tested contract. Skip them in your decode loop rather than treating them
  as fatal (a caller-side concern, not a library change).

Credits: [@intrepidsilence](https://github.com/intrepidsilence) and [@vkapartzianis](https://github.com/vkapartzianis) for the edge264 PRs / patches this project builds on, [@cbusillo](https://github.com/cbusillo) for contributions to this fork, and Thibault Raffaillac (tvlabs) for edge264 itself.


## Contributing

Any help is welcome - bug reports, bug fixes and new tests. Reviews can take a while.

Bug fixes should preferably come with a test stream that demonstrates the fix; these are added to the test suite after stripping most of the image content. See the [tests](tests/) directory for examples of custom bitstreams.


## License

edge264-mvc is distributed under the [BSD 3-Clause license](LICENSE_BSD.txt).
