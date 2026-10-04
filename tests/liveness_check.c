// Committed liveness regression for edge264-mvc.
//
// Decodes each (possibly deliberately-damaged) bitstream under a fixtures
// directory and asserts it delivers the expected number of output (base-view)
// frames WITHOUT stalling. This guards against M1-class deadlocks: an MVC base
// frame whose POC-matching dependent view is missing (a dropped/corrupt
// dependent NAL on a damaged 3D stream) must not block output forever - the
// decoder has to emit the unpairable base alone so a draining caller always
// makes forward progress (ffmpeg likewise decodes the base view of such a
// stream and terminates).
//
// The harness drives the documented decode protocol with a PROGRESS GUARD, so
// a regressed (stalling) decoder fails cleanly with "stall" instead of hanging
// the test suite. Manifest lines: "<name> <expected_base_frames>".
//
// Self-contained: only edge264mvc.h + libc, like tests/conformance_check.c.
// Usage: liveness_check run <manifest> <fixtures-dir>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

#include "edge264mvc.h"

#define RED "\e[0;31m"
#define GREEN "\e[0;32m"
#define RESET "\e[0m"

// Re-feeding the same NAL this many times with neither a delivered frame nor a
// result other than EDGE264MVC_AGAIN means the decoder cannot make progress => stall.
#define STALL_LIMIT 4096

// Wall-clock budget for one fixture. The progress guard above catches an EDGE264MVC_AGAIN
// spin, but NOT a decoder deadlock where edge264mvc_send_nal itself never returns
// (e.g. a multithreaded worker/parser cyclic wait). Such a hang cannot be
// detected in-process, so each fixture is decoded in a forked child bounded by
// this timeout; overrun => the child is killed and the fixture FAILs as a
// deadlock instead of hanging the whole suite. Fixtures are tiny (<10 KB) and
// finish in milliseconds, so this is orders of magnitude of slack.
#define TIMEOUT_SEC 15

static uint8_t *load_file(const char *path, size_t *size_out) {
	// read into memory rather than mmap, so that the harness builds on Windows too
	FILE *f = fopen(path, "rb");
	if (f == NULL)
		return NULL;
	uint8_t *m = NULL;
	long size = 0;
	if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0 &&
		(m = malloc(size)) != NULL && fread(m, 1, size, f) != (size_t)size) {
		free(m);
		m = NULL;
	}
	fclose(f);
	if (m != NULL)
		*size_out = size;
	return m;
}

// Returns delivered base-frame count, or -1 if the decoder stalled.
static int decode_count(const uint8_t *buf, size_t size) {
	// EDGE264MVC_THREADS lets the liveness suite run the damaged-stream fixtures
	// under multithreading (default 0 = single-thread, <0 = auto), guarding the
	// multithreaded teardown and MVC-pairing deadlock fixes against regressions.
	const char *nt = getenv("EDGE264MVC_THREADS");
	int threads = nt ? atoi(nt) : 0;
	Edge264MvcSettings settings;
	edge264mvc_default_settings(&settings);
	settings.n_threads = threads < 0 ? 0 : threads == 0 ? 1 : threads;
	Edge264MvcDecoder *dec;
	if (edge264mvc_open(&dec, &settings) != EDGE264MVC_OK)
		return -1;
	Edge264MvcFrame f;
	int frames = 0, res = EDGE264MVC_OK;
	long no_progress = 0;
	size_t pos = edge264mvc_find_start_code(buf, size);
	while (pos < size) {
		size_t start = pos + 3;
		size_t next = start + edge264mvc_find_start_code(buf + start, size - start);
		res = edge264mvc_send_nal(dec, buf + start, next - start, 0, 0);
		int delivered = 0;
		while (edge264mvc_receive_frame(dec, &f) == EDGE264MVC_OK) {
			edge264mvc_release_frame(dec, &f);
			frames++;
			delivered++;
		}
		if (res == EDGE264MVC_AGAIN) {
			// AGAIN => receive (done above) then send the same NAL again
			if (delivered == 0 && ++no_progress >= STALL_LIMIT) {
				edge264mvc_close(&dec);
				return -1;
			}
			continue;
		}
		no_progress = 0;
		if (res != EDGE264MVC_OK && res != EDGE264MVC_UNSUPPORTED)
			break; // the decode stops at the first failing NAL
		pos = next;
	}
	if (pos >= size) {
		edge264mvc_send_end(dec);
		while (edge264mvc_receive_frame(dec, &f) == EDGE264MVC_OK) {
			edge264mvc_release_frame(dec, &f);
			frames++;
		}
	}
	edge264mvc_close(&dec);
	return frames;
}

// Runs decode_count in a forked child under a wall-clock timeout, so an internal
// decoder deadlock (a NAL that never returns - which the in-process progress
// guard cannot catch) is reported as a clean FAIL rather than hanging the suite.
// Returns the child's frame count (>=0), -1 on stall/crash, or -2 on timeout
// (deadlock). Falls back to an in-process decode if fork/pipe are unavailable
// (and on Windows, which has neither).
static int decode_count_forked(const uint8_t *buf, size_t size) {
#ifdef _WIN32
	return decode_count(buf, size); // no fork on Windows: the caller's own time limit applies
#else
	int fds[2];
	if (pipe(fds) != 0)
		return decode_count(buf, size);
	fflush(stdout); // so the child does not re-flush the parent's buffered output
	pid_t pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return decode_count(buf, size);
	}
	if (pid == 0) { // child: decode and report the count through the pipe
		close(fds[0]);
		int got = decode_count(buf, size);
		ssize_t w = write(fds[1], &got, sizeof got);
		(void)w;
		close(fds[1]);
		// _exit (not exit) so the child terminates promptly without running atexit
		// handlers: a sanitizer build's LeakSanitizer end-of-run check uses a
		// ptrace-based StopTheWorld that hangs in a forked child under a restrictive
		// yama ptrace_scope, which would defeat the timeout. Memory-safety of the
		// decode paths is covered non-forked by tests/asan; here we only need the
		// frame count, already sent through the pipe.
		_exit(0);
	}
	close(fds[1]);
	// poll for the child, killing it if it exceeds the timeout (deadlock)
	int status, got = -1;
	for (int waited_ms = 0; waited_ms < TIMEOUT_SEC * 1000; waited_ms += 20) {
		if (waitpid(pid, &status, WNOHANG) == pid) {
			ssize_t n = read(fds[0], &got, sizeof got);
			if (n != (ssize_t)sizeof got || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
				got = -1; // child crashed/aborted before reporting
			close(fds[0]);
			return got;
		}
		nanosleep(&(struct timespec){.tv_nsec = 20 * 1000 * 1000}, NULL);
	}
	kill(pid, SIGKILL);
	waitpid(pid, &status, 0);
	close(fds[0]);
	return -2; // deadlock: the child never returned within the timeout
#endif
}

static int do_run(const char *manifest, const char *dir) {
	FILE *mf = fopen(manifest, "r");
	if (!mf) {
		fprintf(stderr, "cannot open manifest %s\n", manifest);
		return 1;
	}
	char line[1024];
	int total = 0, failed = 0;
	while (fgets(line, sizeof(line), mf)) {
		if (line[0] == '#' || line[0] == '\n')
			continue;
		char name[512];
		int expected;
		if (sscanf(line, "%511s %d", name, &expected) != 2)
			continue;
		total++;
		char path[4096];
		snprintf(path, sizeof(path), "%s/%s.264", dir, name);
		size_t size = 0;
		uint8_t *buf = load_file(path, &size);
		if (!buf) {
			printf(RED "FAIL" RESET " %s (missing fixture)\n", name);
			failed++;
			continue;
		}
		int got = decode_count_forked(buf, size);
		free(buf);
		if (got == -2) {
			printf(RED "FAIL" RESET " %s (deadlock: send_nal did not return within %ds)\n", name, TIMEOUT_SEC);
			failed++;
		} else if (got < 0) {
			printf(RED "FAIL" RESET " %s (stall: no forward progress)\n", name);
			failed++;
		} else if (got != expected) {
			printf(RED "FAIL" RESET " %s (delivered %d frames, expected %d)\n", name, got, expected);
			failed++;
		}
	}
	fclose(mf);
	if (failed)
		printf("\n" RED "%d / %d liveness fixtures FAILED" RESET "\n", failed, total);
	else
		printf("%d / %d liveness fixtures " GREEN "PASS" RESET "\n", total, total);
	return failed != 0;
}

int main(int argc, char *argv[]) {
	if (argc == 4 && strcmp(argv[1], "run") == 0)
		return do_run(argv[2], argv[3]);
	fprintf(stderr, "Usage: %s run <manifest> <fixtures-dir>\n", argv[0]);
	return 2;
}
