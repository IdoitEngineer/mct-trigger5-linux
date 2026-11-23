// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Jeringa
 *
 * trigger5-tool: probe the adapter and exercise the DRM/KMS display and
 * hardware cursor paths without X or a compositor in the way.
 */

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <drm_fourcc.h>

#include "display.h"
#include "pattern.h"
#include "probe.h"

static volatile sig_atomic_t stop_requested;

static void handle_signal(int sig)
{
	(void)sig;
	stop_requested = 1;
}

static double now_seconds(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void sleep_ms(int ms)
{
	struct timespec ts;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	nanosleep(&ts, NULL);
}

static uint32_t drm_rotation(enum canvas_rotation rotation)
{
	switch (rotation) {
	case CANVAS_ROT_90:
		return DRM_MODE_ROTATE_90;
	case CANVAS_ROT_180:
		return DRM_MODE_ROTATE_180;
	case CANVAS_ROT_270:
		return DRM_MODE_ROTATE_270;
	default:
		return DRM_MODE_ROTATE_0;
	}
}

static const char *rotation_name(enum canvas_rotation rotation)
{
	switch (rotation) {
	case CANVAS_ROT_90:
		return "portrait left (90 CCW)";
	case CANVAS_ROT_180:
		return "upside down (180)";
	case CANVAS_ROT_270:
		return "portrait right (90 CW)";
	default:
		return "landscape (0)";
	}
}

struct test_options {
	enum canvas_rotation rotation;
	const char *mode;
	int seconds;
	bool animate;
	bool cursor;
	bool full;
	bool restore;
	int damage_align;
};

static int align_down(int value, int align)
{
	return value & ~(align - 1);
}

static int align_up(int value, int align)
{
	return (value + align - 1) & ~(align - 1);
}

static void cursor_position(const struct test_options *opt, double elapsed,
			    int screen_w, int screen_h, int *x, int *y)
{
	static const int preset[][2] = {
		{ 0, 0 }, { 100, 0 }, { 100, 100 }, { 0, 100 },
		{ 50, 50 }, { 0, 50 }, { 100, 50 }, { -5, -5 },
	};
	int phase;

	(void)opt;

	if (elapsed < 2.0) {
		phase = (int)(elapsed * 4.0) % 8;
		*x = preset[phase][0] * (screen_w - 64) / 100;
		*y = preset[phase][1] * (screen_h - 64) / 100;
		return;
	}

	*x = (int)((screen_w - 64) * (0.5 + 0.45 *
				      sin((elapsed - 2.0) * 0.7)));
	*y = (int)((screen_h - 64) * (0.5 + 0.42 *
				      cos((elapsed - 2.0) * 1.1)));
}

static int run_test(struct test_options *opt)
{
	struct display d;
	struct dumb_fb fb = { 0 };
	struct dumb_fb cursor_fb = { 0 };
	struct canvas canvas = { 0 };
	uint32_t fb_w, fb_h;
	uint32_t rotation;
	const char *title, *detail;
	uint32_t accent;
	int arrow;
	double start, elapsed = 0;
	unsigned int frame = 0;
	bool canvas_ready = false, fb_ready = false;
	bool cursor_fb_ready = false, cursor_ready = false;
	bool mode_set = false;
	double last_cursor_move = -1.0;
	int old_bx = -1, old_by = -1;
	int hot_x = 0, hot_y = 0;
	int block = 160;
	int ret = EXIT_FAILURE;

	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	if (display_open(&d, true))
		return EXIT_FAILURE;

	if (opt->mode && display_parse_mode(&d, opt->mode))
		goto out_close;

	rotation = drm_rotation(opt->rotation);

	if (opt->rotation == CANVAS_ROT_90 || opt->rotation == CANVAS_ROT_270) {
		fb_w = d.mode.vdisplay;
		fb_h = d.mode.hdisplay;
	} else {
		fb_w = d.mode.hdisplay;
		fb_h = d.mode.vdisplay;
	}

	if (dumb_fb_create(&d, &fb, fb_w, fb_h, DRM_FORMAT_XRGB8888))
		goto out_close;
	fb_ready = true;

	/*
	 * Draw in framebuffer orientation.  The driver applies the plane
	 * rotation, so pre-rotating the canvas here would cancel it and show a
	 * landscape card in portrait modes.
	 */
	if (canvas_init(&canvas, (uint32_t *)fb.map, fb_w, fb_h, fb.pitch,
			CANVAS_ROT_0))
		goto out_close;
	canvas.display_rotation = opt->rotation;
	canvas_ready = true;

	switch (opt->rotation) {
	case CANVAS_ROT_90:
		title = "PORTRAIT LEFT";
		detail = "ROTATE 90 CCW";
		accent = 0x0040a0ff;
		arrow = -1;
		break;
	case CANVAS_ROT_270:
		title = "PORTRAIT RIGHT";
		detail = "ROTATE 270 CW";
		accent = 0x00ff8000;
		arrow = 1;
		break;
	case CANVAS_ROT_180:
		title = "UPSIDE DOWN";
		detail = "ROTATE 180";
		accent = 0x00ff40ff;
		arrow = 0;
		break;
	default:
		title = "LANDSCAPE";
		detail = "ROTATE 0 / NORMAL";
		accent = 0x0040ff80;
		arrow = 0;
		break;
	}

	pattern_render(&canvas, title, detail, accent, arrow);
	canvas_present(&canvas);

	if (display_set_mode(&d, &fb, rotation))
		goto out_close;
	mode_set = true;

	printf("mode set: %ux%u framebuffer, CRTC %ux%u, rotation %u (%s)\n",
	       fb_w, fb_h, d.mode.hdisplay, d.mode.vdisplay, rotation,
	       rotation_name(opt->rotation));

	if (opt->cursor) {
		if (!dumb_fb_create(&d, &cursor_fb, 64, 64, DRM_FORMAT_ARGB8888)) {
			cursor_fb_ready = true;
			pattern_cursor((uint32_t *)cursor_fb.map, 64, &hot_x,
				       &hot_y);
			if (!display_cursor_set(&d, &cursor_fb, hot_x, hot_y)) {
				cursor_ready = true;
				printf("cursor: 64x64 ARGB, hotspot %d,%d (device shows its fixed arrow)\n",
				       hot_x, hot_y);
			}
		}
	}
	printf("running for %d s%s; press Ctrl-C to stop early\n", opt->seconds,
	       opt->animate ? ", animating" : "");

	start = now_seconds();
	while (!stop_requested) {
		drmModeClip clips[3];
		int clip_count = 0;
		int bx, by;
		int scale, cw, ch, cx;
		int cur_x = 0, cur_y = 0;

		elapsed = now_seconds() - start;
		if (elapsed >= opt->seconds)
			break;

		bx = (int)((canvas.logical_w - block) *
			   (0.5 + 0.45 * sin(elapsed * 0.9)));
		by = (int)((canvas.logical_h - block) *
			   (0.5 + 0.42 * cos(elapsed * 1.3)));

		if (opt->animate) {
			canvas_restore(&canvas);
			pattern_frame_counter(&canvas, frame);
			pattern_block(&canvas, bx, by, block,
				      0x00ff8000, "TRIGGER 5");
			canvas_present(&canvas);

			if (opt->full || opt->rotation != CANVAS_ROT_0) {
				clips[0].x1 = 0;
				clips[0].y1 = 0;
				clips[0].x2 = fb_w;
				clips[0].y2 = fb_h;
				clip_count = 1;
			} else {
				int x1, x2, align = opt->damage_align;

				if (align < 1)
					align = 1;

				if (old_bx >= 0) {
					x1 = align_down(old_bx, align);
					x2 = align_up(old_bx + block, align);
					if (x2 > (int)fb_w)
						x2 = fb_w;
					clips[clip_count].x1 = x1;
					clips[clip_count].y1 = old_by;
					clips[clip_count].x2 = x2;
					clips[clip_count].y2 = old_by + block;
					clip_count++;
				}

				x1 = align_down(bx, align);
				x2 = align_up(bx + block, align);
				if (x2 > (int)fb_w)
					x2 = fb_w;
				clips[clip_count].x1 = x1;
				clips[clip_count].y1 = by;
				clips[clip_count].x2 = x2;
				clips[clip_count].y2 = by + block;
				clip_count++;

				/* Frame counter box. */
				scale = canvas.logical_w / 480;
				if (scale < 3)
					scale = 3;
				if (scale > 6)
					scale = 6;
				cw = canvas_text_width(scale, "FRAME 000000") + 24;
				ch = canvas_text_height(scale) + 16;
				cx = canvas.logical_w / 2 - cw / 2;
				x1 = align_down(cx, align);
				x2 = align_up(cx + cw, align);
				if (x2 > (int)fb_w)
					x2 = fb_w;
				clips[clip_count].x1 = x1;
				clips[clip_count].y1 = 20;
				clips[clip_count].x2 = x2;
				clips[clip_count].y2 = 20 + ch;
				clip_count++;
			}

			display_dirty(&d, &fb, clips, clip_count);
			old_bx = bx;
			old_by = by;
		}

		if (cursor_ready && elapsed - last_cursor_move >= 0.05) {
			cursor_position(opt, elapsed, d.mode.hdisplay,
					d.mode.vdisplay, &cur_x, &cur_y);
			display_cursor_move(&d, cur_x, cur_y);
			last_cursor_move = elapsed;
		}

		frame++;
		sleep_ms(16);
	}

	elapsed = now_seconds() - start;
	printf("frames: %u in %.2f s (%.1f fps)\n", frame, elapsed,
	       elapsed > 0 ? frame / elapsed : 0.0);

	ret = EXIT_SUCCESS;

out_close:
	if (cursor_ready)
		display_cursor_hide(&d);
	if (mode_set && opt->restore)
		display_restore(&d);

	if (canvas_ready)
		canvas_exit(&canvas);
	if (cursor_fb_ready)
		dumb_fb_destroy(&d, &cursor_fb);
	if (fb_ready)
		dumb_fb_destroy(&d, &fb);
	display_close(&d);

	return ret;
}

static void usage(const char *argv0)
{
	printf("Usage:\n"
	       "  %s probe [--json]\n"
	       "  %s test [--landscape | --portrait-left | --portrait-right]\n"
	       "            [--mode WxH] [--seconds N] [--no-animate] [--full]\n"
	       "            [--no-cursor] [--no-restore] [--damage-align N]\n"
	       "  %s cursor [--seconds N]\n"
	       "\n"
	       "probe   read-only: USB descriptors, sysfs, module parameters and\n"
	       "        DRM/KMS objects. Can run while X is active.\n"
	       "test    claim DRM master and show the test pattern. Requires a text\n"
	       "        console or a stopped display manager.\n"
	       "cursor  shortcut for a landscape run focused on cursor movement.\n"
	       "\n"
	       "Rotation flags are separate on purpose: --portrait-left uses DRM\n"
	       "rotate-90 (counter-clockwise), --portrait-right uses rotate-270.\n",
	       argv0, argv0, argv0);
}

static int parse_test_args(int argc, char **argv, struct test_options *opt,
			   bool cursor_only)
{
	int i;

	memset(opt, 0, sizeof(*opt));
	opt->rotation = CANVAS_ROT_0;
	opt->seconds = cursor_only ? 5 : 10;
	opt->animate = true;
	opt->cursor = true;
	opt->restore = true;
	opt->damage_align = 1;

	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--landscape")) {
			opt->rotation = CANVAS_ROT_0;
		} else if (!strcmp(argv[i], "--portrait-left")) {
			opt->rotation = CANVAS_ROT_90;
		} else if (!strcmp(argv[i], "--portrait-right")) {
			opt->rotation = CANVAS_ROT_270;
		} else if (!strcmp(argv[i], "--upside-down")) {
			opt->rotation = CANVAS_ROT_180;
		} else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
			opt->mode = argv[++i];
		} else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
			opt->seconds = atoi(argv[++i]);
			if (opt->seconds < 1)
				opt->seconds = 1;
		} else if (!strcmp(argv[i], "--no-animate")) {
			opt->animate = false;
		} else if (!strcmp(argv[i], "--animate")) {
			opt->animate = true;
		} else if (!strcmp(argv[i], "--full")) {
			opt->full = true;
		} else if (!strcmp(argv[i], "--damage")) {
			opt->full = false;
		} else if (!strcmp(argv[i], "--no-cursor")) {
			opt->cursor = false;
		} else if (!strcmp(argv[i], "--cursor")) {
			opt->cursor = true;
		} else if (!strcmp(argv[i], "--damage-align") && i + 1 < argc) {
			opt->damage_align = atoi(argv[++i]);
			if (opt->damage_align < 1)
				opt->damage_align = 1;
		} else if (!strcmp(argv[i], "--no-restore")) {
			opt->restore = false;
		} else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			return 1;
		} else {
			fprintf(stderr, "unknown option: %s\n", argv[i]);
			return -1;
		}
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct test_options opt;
	const char *cmd;

	if (argc < 2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	cmd = argv[1];

	if (!strcmp(cmd, "probe")) {
		bool json = argc > 2 && !strcmp(argv[2], "--json");

		return cmd_probe(json);
	}

	if (!strcmp(cmd, "test") || !strcmp(cmd, "cursor")) {
		int rc = parse_test_args(argc - 2, argv + 2, &opt,
					 !strcmp(cmd, "cursor"));

		if (rc == 1) {
			usage(argv[0]);
			return EXIT_SUCCESS;
		}
		if (rc < 0)
			return EXIT_FAILURE;
		return run_test(&opt);
	}

	if (!strcmp(cmd, "--help") || !strcmp(cmd, "-h")) {
		usage(argv[0]);
		return EXIT_SUCCESS;
	}

	fprintf(stderr, "unknown command: %s\n", cmd);
	usage(argv[0]);
	return EXIT_FAILURE;
}
