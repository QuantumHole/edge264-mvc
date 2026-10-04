// Contract test of the public API (edge264mvc.h), run by `make check`.
//
// The other harnesses decode streams through the API; this one checks the
// promises the API itself makes to a caller: the version and the defaults,
// the INVALID results, the end of a stream (END after send_end, and END
// again), a flush after the end followed by the same stream again, and the
// pts / user_data passthrough. It uses one committed MVC stream and one 2D
// stream, each single-threaded and with EDGE264MVC_THREADS worker threads.
// The struct layouts are checked when it is compiled.
//
// Usage: api_check <stream.264>...

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "edge264mvc.h"

// The struct layouts are the ABI of the major version: bindings in other
// languages mirror them field by field (Oku3D's Rust binding asserts the
// sizes), so a change here needs a new major version. Pinned on 64-bit
// targets, where pointers are 8 bytes.
#if UINTPTR_MAX == UINT64_MAX
#define LAYOUT(type, field, offset) _Static_assert(offsetof(type, field) == offset, #type "." #field " moved");
LAYOUT(Edge264MvcSettings, n_threads, 0) LAYOUT(Edge264MvcSettings, max_frame_pixels, 4)
LAYOUT(Edge264MvcSettings, log_cb, 8) LAYOUT(Edge264MvcSettings, log_arg, 16)
LAYOUT(Edge264MvcSettings, log_mbs, 24) LAYOUT(Edge264MvcSettings, reserved, 28)
_Static_assert(sizeof(Edge264MvcSettings) == 88, "Edge264MvcSettings changed size");
LAYOUT(Edge264MvcView, planes, 0) LAYOUT(Edge264MvcView, pts, 24) LAYOUT(Edge264MvcView, user_data, 32)
LAYOUT(Edge264MvcView, display_order, 40) LAYOUT(Edge264MvcView, poc, 48) LAYOUT(Edge264MvcView, decode_order, 52)
LAYOUT(Edge264MvcView, flags, 56) LAYOUT(Edge264MvcView, reserved, 60)
_Static_assert(sizeof(Edge264MvcView) == 64, "Edge264MvcView changed size");
LAYOUT(Edge264MvcFrame, views, 0) LAYOUT(Edge264MvcFrame, width_Y, 128) LAYOUT(Edge264MvcFrame, height_Y, 132)
LAYOUT(Edge264MvcFrame, width_C, 136) LAYOUT(Edge264MvcFrame, height_C, 140) LAYOUT(Edge264MvcFrame, stride_Y, 144)
LAYOUT(Edge264MvcFrame, stride_C, 148) LAYOUT(Edge264MvcFrame, bit_depth_Y, 152) LAYOUT(Edge264MvcFrame, bit_depth_C, 156)
LAYOUT(Edge264MvcFrame, crop, 160) LAYOUT(Edge264MvcFrame, handle, 176) LAYOUT(Edge264MvcFrame, reserved, 184)
_Static_assert(sizeof(Edge264MvcFrame) == 216, "Edge264MvcFrame changed size");
#endif

#define RED "\e[0;31m"
#define GREEN "\e[0;32m"
#define RESET "\e[0m"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { printf(RED "FAIL" RESET " " __VA_ARGS__); printf("\n"); failures++; } } while (0)

typedef struct {
	int frames;
	uint64_t hash;
	int pts_errors; // frames whose pts is not one that was sent with a NAL
	int64_t max_pts;
} Output;

static void receive_all(Edge264MvcDecoder *dec, Output *o) {
	Edge264MvcFrame f;
	while (edge264mvc_receive_frame(dec, &f) == EDGE264MVC_OK) {
		for (int v = 0; v < 2; v++) {
			if (f.views[v].planes[0] == NULL)
				continue;
			for (int y = 0; y < f.height_Y; y++)
				for (int x = 0; x < f.width_Y; x++)
					o->hash = (o->hash ^ f.views[v].planes[0][(size_t)y * f.stride_Y + x]) * 1099511628211ull;
			// every NAL was sent with pts = 1000 + its offset and user_data = -pts
			if (f.views[v].pts < 1000 || f.views[v].pts > o->max_pts || f.views[v].user_data != -f.views[v].pts)
				o->pts_errors++;
		}
		o->frames++;
		edge264mvc_release_frame(dec, &f);
	}
}

// Decodes the whole stream and ends it; returns the result of the receive
// after the last frame, which must be END.
static int decode_stream(Edge264MvcDecoder *dec, const uint8_t *buf, size_t size, Output *o) {
	memset(o, 0, sizeof(*o));
	o->hash = 14695981039346656037ull;
	o->max_pts = 1000 + (int64_t)size;
	size_t pos = edge264mvc_find_start_code(buf, size);
	while (pos < size) {
		size_t start = pos + 3;
		size_t next = start + edge264mvc_find_start_code(buf + start, size - start);
		int64_t pts = 1000 + (int64_t)start;
		for (int rounds = 0; edge264mvc_send_nal(dec, buf + start, next - start, pts, -pts) == EDGE264MVC_AGAIN && rounds < 64; rounds++)
			receive_all(dec, o);
		receive_all(dec, o);
		pos = next;
	}
	CHECK(edge264mvc_send_end(dec) == EDGE264MVC_OK, "send_end did not return OK");
	receive_all(dec, o);
	Edge264MvcFrame f;
	return edge264mvc_receive_frame(dec, &f);
}

static uint8_t *load_file(const char *path, size_t *size) {
	FILE *f = fopen(path, "rb");
	if (f == NULL)
		return NULL;
	uint8_t *m = NULL;
	long n = 0;
	if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0 &&
		(m = malloc(n)) != NULL && fread(m, 1, n, f) != (size_t)n) {
		free(m);
		m = NULL;
	}
	fclose(f);
	*size = n;
	return m;
}

static void check_stream(const char *path, int n_threads) {
	size_t size = 0;
	uint8_t *buf = load_file(path, &size);
	CHECK(buf != NULL, "cannot read %s", path);
	if (buf == NULL)
		return;
	Edge264MvcSettings settings;
	edge264mvc_default_settings(&settings);
	settings.n_threads = n_threads;
	Edge264MvcDecoder *dec = NULL;
	CHECK(edge264mvc_open(&dec, &settings) == EDGE264MVC_OK && dec != NULL, "open failed for %s", path);
	if (dec == NULL) {
		free(buf);
		return;
	}
	Output first, again;
	int end = decode_stream(dec, buf, size, &first);
	CHECK(first.frames > 0, "%s (%d threads): no frame", path, n_threads);
	CHECK(end == EDGE264MVC_END, "%s (%d threads): receive after the last frame returned %d, not END", path, n_threads, end);
	Edge264MvcFrame f;
	CHECK(edge264mvc_receive_frame(dec, &f) == EDGE264MVC_END, "%s (%d threads): END was not repeated", path, n_threads);
	CHECK(first.pts_errors == 0, "%s (%d threads): %d views carried a pts or user_data never sent", path, n_threads, first.pts_errors);
	// a flush after the end starts a new stream (a seek back to the start)
	edge264mvc_flush(dec);
	end = decode_stream(dec, buf, size, &again);
	CHECK(end == EDGE264MVC_END, "%s (%d threads): after flush, the end returned %d, not END", path, n_threads, end);
	CHECK(again.frames == first.frames && again.hash == first.hash,
		"%s (%d threads): after flush, %d frames instead of %d, or other pixels", path, n_threads, again.frames, first.frames);
	edge264mvc_close(&dec);
	CHECK(dec == NULL, "close did not clear the decoder pointer");
	free(buf);
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <stream.264>...\n", argv[0]);
		return 2;
	}
	CHECK(edge264mvc_api_version() >> 16 == EDGE264MVC_API_VERSION_MAJOR, "library major version %u, header %d",
		edge264mvc_api_version() >> 16, EDGE264MVC_API_VERSION_MAJOR);
	CHECK(edge264mvc_version() != NULL && edge264mvc_version()[0] != '\0', "empty version string");
	Edge264MvcSettings settings;
	memset(&settings, 0xff, sizeof(settings));
	edge264mvc_default_settings(&settings);
	CHECK(settings.n_threads == 0 && settings.max_frame_pixels == 0 && settings.log_cb == NULL && settings.log_mbs == 0,
		"default_settings did not set the documented defaults");
	// the INVALID results and NULL tolerance the header documents
	CHECK(edge264mvc_open(NULL, NULL) == EDGE264MVC_INVALID, "open(NULL) is not INVALID");
	CHECK(edge264mvc_send_nal(NULL, (const uint8_t *)"", 0, 0, 0) == EDGE264MVC_INVALID, "send_nal(NULL decoder) is not INVALID");
	CHECK(edge264mvc_send_end(NULL) == EDGE264MVC_INVALID, "send_end(NULL) is not INVALID");
	Edge264MvcFrame frame;
	CHECK(edge264mvc_receive_frame(NULL, &frame) == EDGE264MVC_INVALID, "receive_frame(NULL decoder) is not INVALID");
	Edge264MvcDecoder *none = NULL;
	edge264mvc_close(&none); // accepted
	edge264mvc_close(NULL);
	// a fresh decoder has nothing to give before any NAL
	Edge264MvcDecoder *dec = NULL;
	CHECK(edge264mvc_open(&dec, NULL) == EDGE264MVC_OK, "open with default settings failed");
	CHECK(edge264mvc_receive_frame(dec, &frame) == EDGE264MVC_AGAIN, "receive on a fresh decoder is not AGAIN");
	CHECK(edge264mvc_receive_frame(dec, NULL) == EDGE264MVC_INVALID, "receive_frame(NULL frame) is not INVALID");
	edge264mvc_close(&dec);
	// find_start_code reads only inside the buffer
	const uint8_t sc[] = {7, 0, 0, 1, 5};
	CHECK(edge264mvc_find_start_code(sc, sizeof(sc)) == 1, "find_start_code missed the start code");
	CHECK(edge264mvc_find_start_code(sc, 3) == 3, "find_start_code matched past the end");
	const char *nt = getenv("EDGE264MVC_THREADS");
	int threads = nt ? atoi(nt) : 4;
	for (int i = 1; i < argc; i++) {
		check_stream(argv[i], 1);
		check_stream(argv[i], threads);
	}
	if (failures) {
		printf(RED "%d API contract checks FAILED" RESET "\n", failures);
		return 1;
	}
	printf("API contract " GREEN "PASS" RESET "\n");
	return 0;
}
