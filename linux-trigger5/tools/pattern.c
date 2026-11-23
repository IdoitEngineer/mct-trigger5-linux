// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Jeringa
 *
 * Test pattern and minimal 5x7 text rendering for the Trigger 5 KMS tool.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pattern.h"

#define COLOR_BG	0x00101018
#define COLOR_GRID	0x002a2a38
#define COLOR_WHITE	0x00ffffff

/* Classic 5x7 glyphs, bit 4 is the leftmost column.  Uppercase only. */
static const unsigned char font5x7[128][7] = {
	[' '] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	['!'] = { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 },
	['#'] = { 0x0a, 0x0a, 0x1f, 0x0a, 0x1f, 0x0a, 0x0a },
	['%'] = { 0x19, 0x1a, 0x02, 0x04, 0x08, 0x0b, 0x13 },
	['('] = { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 },
	[')'] = { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 },
	['+'] = { 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00 },
	['-'] = { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 },
	['.'] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c },
	['/'] = { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 },
	['0'] = { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },
	['1'] = { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
	['2'] = { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },
	['3'] = { 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e },
	['4'] = { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
	['5'] = { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e },
	['6'] = { 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e },
	['7'] = { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
	['8'] = { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },
	['9'] = { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c },
	[':'] = { 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00 },
	['='] = { 0x00, 0x00, 0x1f, 0x00, 0x1f, 0x00, 0x00 },
	['A'] = { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
	['B'] = { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e },
	['C'] = { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e },
	['D'] = { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e },
	['E'] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f },
	['F'] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 },
	['G'] = { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f },
	['H'] = { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
	['I'] = { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e },
	['J'] = { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c },
	['K'] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
	['L'] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f },
	['M'] = { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 },
	['N'] = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
	['O'] = { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
	['P'] = { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 },
	['Q'] = { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d },
	['R'] = { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 },
	['S'] = { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e },
	['T'] = { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
	['U'] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
	['V'] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 },
	['W'] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11 },
	['X'] = { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 },
	['Y'] = { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 },
	['Z'] = { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f },
};

static unsigned int clamp_u(unsigned int v, unsigned int lo, unsigned int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static void put_pixel(struct canvas *c, int x, int y, uint32_t color)
{
	int fx, fy;

	if (x < 0 || y < 0 || x >= c->logical_w || y >= c->logical_h)
		return;

	switch (c->rotation) {
	case CANVAS_ROT_90:
		fx = c->fb_w - 1 - y;
		fy = x;
		break;
	case CANVAS_ROT_270:
		fx = y;
		fy = c->fb_h - 1 - x;
		break;
	case CANVAS_ROT_180:
		fx = c->fb_w - 1 - x;
		fy = c->fb_h - 1 - y;
		break;
	default:
		fx = x;
		fy = y;
		break;
	}

	if (fx < 0 || fy < 0 || fx >= c->fb_w || fy >= c->fb_h)
		return;

	c->fb[(size_t)fy * c->fb_pitch_pixels + fx] = color;
}

int canvas_init(struct canvas *c, uint32_t *fb, int fb_w, int fb_h,
		size_t pitch_bytes, enum canvas_rotation rotation)
{
	memset(c, 0, sizeof(*c));

	if (rotation == CANVAS_ROT_90 || rotation == CANVAS_ROT_270) {
		c->logical_w = fb_h;
		c->logical_h = fb_w;
	} else {
		c->logical_w = fb_w;
		c->logical_h = fb_h;
	}

	c->fb = fb;
	c->fb_w = fb_w;
	c->fb_h = fb_h;
	c->fb_pitch_pixels = pitch_bytes / 4;
	c->rotation = rotation;
	c->display_rotation = rotation;

	c->logical = calloc((size_t)c->logical_w * c->logical_h, sizeof(uint32_t));
	c->snapshot = calloc((size_t)c->logical_w * c->logical_h, sizeof(uint32_t));
	if (!c->logical || !c->snapshot) {
		canvas_exit(c);
		return -1;
	}

	return 0;
}

void canvas_exit(struct canvas *c)
{
	free(c->logical);
	free(c->snapshot);
	c->logical = NULL;
	c->snapshot = NULL;
}

void canvas_clear(struct canvas *c, uint32_t color)
{
	size_t n = (size_t)c->logical_w * c->logical_h;
	size_t i;

	for (i = 0; i < n; i++)
		c->logical[i] = color;
}

void canvas_fill_rect(struct canvas *c, int x, int y, int w, int h,
		      uint32_t color)
{
	int px, py;

	for (py = y; py < y + h; py++)
		for (px = x; px < x + w; px++)
			if (px >= 0 && py >= 0 && px < c->logical_w &&
			    py < c->logical_h)
				c->logical[(size_t)py * c->logical_w + px] = color;
}

void canvas_stroke_rect(struct canvas *c, int x, int y, int w, int h,
			int thickness, uint32_t color)
{
	canvas_fill_rect(c, x, y, w, thickness, color);
	canvas_fill_rect(c, x, y + h - thickness, w, thickness, color);
	canvas_fill_rect(c, x, y, thickness, h, color);
	canvas_fill_rect(c, x + w - thickness, y, thickness, h, color);
}

void canvas_line(struct canvas *c, int x0, int y0, int x1, int y1,
		 uint32_t color)
{
	int dx = abs(x1 - x0);
	int sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0);
	int sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;

	for (;;) {
		put_pixel(c, x0, y0, color);
		if (x0 == x1 && y0 == y1)
			break;
		if (2 * err >= dy) {
			err += dy;
			x0 += sx;
		}
		if (2 * err <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

void canvas_circle(struct canvas *c, int cx, int cy, int radius,
		   int thickness, uint32_t color)
{
	int x, y;
	int inner = radius - thickness;

	if (inner < 0)
		inner = 0;

	for (y = -radius; y <= radius; y++) {
		for (x = -radius; x <= radius; x++) {
			int d2 = x * x + y * y;

			if (d2 <= radius * radius && d2 >= inner * inner)
				put_pixel(c, cx + x, cy + y, color);
		}
	}
}

int canvas_text_height(int scale)
{
	return 8 * scale;
}

int canvas_text_width(int scale, const char *text)
{
	return (int)strlen(text) * 6 * scale;
}

void canvas_text(struct canvas *c, int x, int y, int scale, uint32_t color,
		 const char *text)
{
	int i, row, col;
	int cx = x;

	if (scale < 1)
		scale = 1;

	for (i = 0; text[i]; i++) {
		unsigned char ch = (unsigned char)text[i];

		if (ch >= 'a' && ch <= 'z')
			ch = ch - 'a' + 'A';
		if (ch >= 128)
			ch = ' ';

		for (row = 0; row < 7; row++) {
			unsigned char bits = font5x7[ch][row];

			for (col = 0; col < 5; col++) {
				if (!(bits & (1 << (4 - col))))
					continue;
				canvas_fill_rect(c, cx + col * scale,
						 y + row * scale, scale, scale,
						 color);
			}
		}
		cx += 6 * scale;
	}
}

void canvas_snapshot(struct canvas *c)
{
	memcpy(c->snapshot, c->logical,
	       (size_t)c->logical_w * c->logical_h * sizeof(uint32_t));
}

void canvas_restore(struct canvas *c)
{
	memcpy(c->logical, c->snapshot,
	       (size_t)c->logical_w * c->logical_h * sizeof(uint32_t));
}

void canvas_present(struct canvas *c)
{
	int x, y;

	for (y = 0; y < c->logical_h; y++)
		for (x = 0; x < c->logical_w; x++)
			put_pixel(c, x, y,
				  c->logical[(size_t)y * c->logical_w + x]);
}

static void canvas_arrow(struct canvas *c, int cx, int cy, int dir,
			 int length, int thickness, uint32_t color)
{
	int half = length / 2;
	int head = length / 3;
	int tip = cx + dir * half;
	int tail = cx - dir * half;
	int i;

	for (i = -thickness / 2; i <= thickness / 2; i++) {
		canvas_line(c, tail, cy + i, tip, cy + i, color);
		canvas_line(c, tip, cy + i, tip - dir * head, cy - head + i,
			    color);
		canvas_line(c, tip, cy + i, tip - dir * head, cy + head + i,
			    color);
	}
}

void pattern_render(struct canvas *c, const char *title, const char *detail,
		    uint32_t accent, int arrow)
{
	static const uint32_t bars[] = {
		0x00ffffff, 0x00ffff00, 0x0000ffff, 0x0000ff00,
		0x00ff00ff, 0x00ff0000, 0x000000ff, 0x00000000,
	};
	unsigned int w = c->logical_w;
	unsigned int h = c->logical_h;
	unsigned int scale = clamp_u(w / 200, 4, 12);
	unsigned int small = clamp_u(w / 480, 3, 6);
	unsigned int bar_top = h / 14;
	unsigned int bar_h = h / 3;
	unsigned int corner = clamp_u(w / 24, 48, 96);
	unsigned int i, nbars = sizeof(bars) / sizeof(bars[0]);
	int cx = w / 2;
	int cy = h / 2;
	char footer[160];

	canvas_clear(c, COLOR_BG);

	/* Colour bars. */
	for (i = 0; i < nbars; i++)
		canvas_fill_rect(c, (int)(i * w / nbars), (int)bar_top,
				 (int)((i + 1) * w / nbars - i * w / nbars),
				 (int)bar_h, bars[i]);
	canvas_fill_rect(c, 0, (int)(bar_top + bar_h), (int)w, 3, COLOR_WHITE);

	/* Grid. */
	for (i = 1; i < 16; i++)
		canvas_line(c, (int)(i * w / 16), (int)(bar_top + bar_h + 4),
			    (int)(i * w / 16), (int)(h - 8), COLOR_GRID);
	for (i = 1; i < 10; i++)
		canvas_line(c, 8, (int)(i * h / 10), (int)(w - 8),
			    (int)(i * h / 10), COLOR_GRID);

	canvas_stroke_rect(c, 0, 0, (int)w, (int)h, 3, COLOR_WHITE);

	/* Corner markers: TL red, TR green, BL blue, BR yellow. */
	canvas_fill_rect(c, 20, 20, (int)corner, (int)corner, 0x00ff4040);
	canvas_fill_rect(c, (int)(w - 20 - corner), 20, (int)corner,
			 (int)corner, 0x0040ff40);
	canvas_fill_rect(c, 20, (int)(h - 20 - corner), (int)corner,
			 (int)corner, 0x004080ff);
	canvas_fill_rect(c, (int)(w - 20 - corner),
			 (int)(h - 20 - corner), (int)corner, (int)corner,
			 0x00ffe040);

	canvas_text(c, 26, 24 + (int)corner / 2 - 4 * small, small,
		    0x00000000, "TL");
	canvas_text(c, (int)(w - 20 - corner) + 6,
		    24 + (int)corner / 2 - 4 * small, small, 0x00000000, "TR");
	canvas_text(c, 26, (int)(h - 20 - corner) + (int)corner / 2 -
		     4 * small, small, 0x00000000, "BL");
	canvas_text(c, (int)(w - 20 - corner) + 6,
		    (int)(h - 20 - corner) + (int)corner / 2 - 4 * small,
		    small, 0x00000000, "BR");

	/* Centre crosshair and ring. */
	canvas_line(c, cx - (int)w / 16, cy, cx + (int)w / 16, cy, COLOR_WHITE);
	canvas_line(c, cx, cy - (int)h / 12, cx, cy + (int)h / 12, COLOR_WHITE);
	canvas_circle(c, cx, cy, (int)(h / 14), 3, COLOR_WHITE);
	if (arrow)
		canvas_arrow(c, cx, cy, arrow, (int)(w / 5), 10, 0x00000000);
	if (arrow)
		canvas_arrow(c, cx, cy, arrow, (int)(w / 5), 6, accent);
	canvas_fill_rect(c, cx - 4, cy - 4, 8, 8, 0x00ff0000);

	/* Title block. */
	canvas_fill_rect(c, (int)(w / 20), (int)(h * 2 / 3) - 20,
			 (int)(w / 6), 8, accent);
	canvas_text(c, (int)(w / 20), (int)(h * 2 / 3), (int)scale, COLOR_WHITE,
		    title);
	canvas_text(c, (int)(w / 20), (int)(h * 2 / 3) + 10 * (int)scale,
		    (int)small, accent, detail);

	snprintf(footer, sizeof(footer), "%ux%u  ROT %d  FB %dx%d", w, h,
		 (int)c->display_rotation, c->fb_w, c->fb_h);
	canvas_text(c, (int)(w / 20), (int)(h - h / 16), (int)small,
		    0x00ffffff, footer);

	canvas_text(c, (int)(w - w / 20) -
		    canvas_text_width((int)small, "MCT TRIGGER 5"),
		    (int)(h - h / 16), (int)small, 0x00ffffff, "MCT TRIGGER 5");

	canvas_snapshot(c);
}

void pattern_frame_counter(struct canvas *c, unsigned int frame)
{
	char text[32];
	int scale = clamp_u(c->logical_w / 480, 3, 6);
	int w = canvas_text_width(scale, "FRAME 000000") + 24;
	int h = canvas_text_height(scale) + 16;
	int x = c->logical_w / 2 - w / 2;

	canvas_fill_rect(c, x, 20, w, h, 0x00000000);
	canvas_stroke_rect(c, x, 20, w, h, 2, 0x00808080);
	snprintf(text, sizeof(text), "FRAME %06u", frame);
	canvas_text(c, x + 12, 20 + 8, scale, 0x00ffffff, text);
}

void pattern_block(struct canvas *c, int x, int y, int size, uint32_t color,
		   const char *label)
{
	canvas_fill_rect(c, x, y, size, size, color);
	canvas_stroke_rect(c, x, y, size, size, 4, 0x00ffffff);
	canvas_line(c, x, y, x + size - 1, y + size - 1, 0x00000000);
	canvas_line(c, x + size - 1, y, x, y + size - 1, 0x00000000);
	if (label) {
		int scale = clamp_u((unsigned int)size / 40, 1, 3);
		int tw = canvas_text_width(scale, label);
		int th = canvas_text_height(scale);

		canvas_text(c, x + size / 2 - tw / 2, y + size / 2 - th / 2,
			    scale, 0x00000000, label);
	}
}

void pattern_cursor(uint32_t *pixels, int size, int *hot_x, int *hot_y)
{
	int i, t = size / 16;
	int r = size / 3;

	memset(pixels, 0, (size_t)size * size * sizeof(uint32_t));

	if (t < 2)
		t = 2;

	/* Draw a crosshair with a ring; the adapter itself shows an arrow. */
	for (i = 0; i < size; i++) {
		int j;

		for (j = 0; j < size; j++) {
			int dx = j - size / 2;
			int dy = i - size / 2;
			int d2 = dx * dx + dy * dy;
			int inner = r - t;

			if (d2 <= r * r && d2 >= inner * inner)
				pixels[i * size + j] = 0xff000000;
			else if (d2 <= (r - t) * (r - t) &&
				 d2 >= (inner - t) * (inner - t))
				pixels[i * size + j] = 0xffffffff;
		}
	}

	for (i = -r; i <= r; i++) {
		int j;

		for (j = -t; j <= t; j++) {
			int x = size / 2 + i;
			int y = size / 2 + j;

			if (x >= 0 && x < size && y >= 0 && y < size)
				pixels[y * size + x] = 0xff000000;
			x = size / 2 + j;
			y = size / 2 + i;
			if (x >= 0 && x < size && y >= 0 && y < size)
				pixels[y * size + x] = 0xff000000;
		}
	}
	for (i = -r; i <= r; i++) {
		int j;

		for (j = -(t - 1); j <= t - 1; j++) {
			int x = size / 2 + i;
			int y = size / 2 + j;

			if (x >= 0 && x < size && y >= 0 && y < size)
				pixels[y * size + x] = 0xffffffff;
			x = size / 2 + j;
			y = size / 2 + i;
			if (x >= 0 && x < size && y >= 0 && y < size)
				pixels[y * size + x] = 0xffffffff;
		}
	}

	/* Hotspot dot. */
	for (i = -3; i <= 3; i++)
		for (t = -3; t <= 3; t++)
			if (i * i + t * t <= 9)
				pixels[(size / 2 + t) * size + size / 2 + i] =
					0xffff0000;

	if (hot_x)
		*hot_x = size / 2;
	if (hot_y)
		*hot_y = size / 2;
}
