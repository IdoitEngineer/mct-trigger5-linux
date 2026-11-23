/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Jeringa */

#ifndef TRIGGER5_PATTERN_H
#define TRIGGER5_PATTERN_H

#include <stddef.h>
#include <stdint.h>

enum canvas_rotation {
	CANVAS_ROT_0 = 0,
	CANVAS_ROT_90 = 90,
	CANVAS_ROT_180 = 180,
	CANVAS_ROT_270 = 270,
};

/*
 * A logical landscape canvas that is presented into a (possibly rotated)
 * framebuffer.  Drawing always happens in the logical orientation, so test
 * text stays readable on a physically rotated panel.
 */
struct canvas {
	uint32_t *logical;
	uint32_t *snapshot;
	uint32_t *fb;
	int logical_w;
	int logical_h;
	int fb_w;
	int fb_h;
	size_t fb_pitch_pixels;
	enum canvas_rotation rotation;
	enum canvas_rotation display_rotation;
};

int canvas_init(struct canvas *c, uint32_t *fb, int fb_w, int fb_h,
		size_t pitch_bytes, enum canvas_rotation rotation);
void canvas_exit(struct canvas *c);

void canvas_clear(struct canvas *c, uint32_t color);
void canvas_fill_rect(struct canvas *c, int x, int y, int w, int h,
		      uint32_t color);
void canvas_stroke_rect(struct canvas *c, int x, int y, int w, int h,
			int thickness, uint32_t color);
void canvas_line(struct canvas *c, int x0, int y0, int x1, int y1,
		 uint32_t color);
void canvas_circle(struct canvas *c, int cx, int cy, int radius,
		   int thickness, uint32_t color);
void canvas_text(struct canvas *c, int x, int y, int scale, uint32_t color,
		 const char *text);
int canvas_text_width(int scale, const char *text);
int canvas_text_height(int scale);

void canvas_snapshot(struct canvas *c);
void canvas_restore(struct canvas *c);
void canvas_present(struct canvas *c);

void pattern_render(struct canvas *c, const char *title, const char *detail,
		    uint32_t accent, int arrow);
void pattern_frame_counter(struct canvas *c, unsigned int frame);
void pattern_block(struct canvas *c, int x, int y, int size, uint32_t color,
		   const char *label);
void pattern_cursor(uint32_t *pixels, int size, int *hot_x, int *hot_y);

#endif /* TRIGGER5_PATTERN_H */
