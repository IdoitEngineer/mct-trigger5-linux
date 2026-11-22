/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Jeringa */

#ifndef TRIGGER5_DISPLAY_H
#define TRIGGER5_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

struct display {
	int fd;
	char path[64];
	bool master;
	bool mode_set;

	drmModeRes *res;
	drmModeConnector *conn;
	drmModeCrtc *saved_crtc;

	uint32_t conn_id;
	uint32_t crtc_id;
	int crtc_id_index;
	uint32_t plane_id;
	uint32_t cursor_plane_id;
	uint32_t saved_rotation;
	uint32_t saved_fb_w;
	uint32_t saved_fb_h;
	drmModeModeInfo mode;
};

struct dumb_fb {
	uint32_t handle;
	uint32_t fb_id;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint64_t size;
	uint8_t *map;
};

int display_open(struct display *d, bool need_master);
void display_close(struct display *d);

int display_parse_mode(struct display *d, const char *spec);
void display_print(struct display *d);

int dumb_fb_create(struct display *d, struct dumb_fb *fb, uint32_t width,
		   uint32_t height, uint32_t format);
void dumb_fb_destroy(struct display *d, struct dumb_fb *fb);

int display_set_mode(struct display *d, struct dumb_fb *fb,
		     uint32_t drm_rotation);
int display_dirty(struct display *d, struct dumb_fb *fb,
		  const drmModeClip *clips, int count);

int display_cursor_set(struct display *d, struct dumb_fb *fb, int hot_x,
		       int hot_y);
int display_cursor_move(struct display *d, int x, int y);
int display_cursor_hide(struct display *d);
int display_restore(struct display *d);

#endif /* TRIGGER5_DISPLAY_H */
