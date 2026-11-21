// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Jeringa
 *
 * Minimal DRM/KMS helpers for the Trigger 5 test tool.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <drm_fourcc.h>

#include "display.h"

static int get_prop(int fd, uint32_t obj, uint32_t type, const char *name,
		    uint32_t *id_out, uint64_t *value_out)
{
	drmModeObjectProperties *props;
	int ret = -1;
	unsigned int i;

	props = drmModeObjectGetProperties(fd, obj, type);
	if (!props)
		return -1;

	for (i = 0; i < props->count_props; i++) {
		drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[i]);

		if (!prop)
			continue;
		if (!strcmp(prop->name, name)) {
			if (id_out)
				*id_out = props->props[i];
			if (value_out)
				*value_out = props->prop_values[i];
			ret = 0;
		}
		drmModeFreeProperty(prop);
		if (ret == 0)
			break;
	}

	drmModeFreeObjectProperties(props);
	return ret;
}

static uint32_t prop_id(int fd, uint32_t obj, uint32_t type, const char *name)
{
	uint32_t id = 0;

	if (get_prop(fd, obj, type, name, &id, NULL))
		return 0;
	return id;
}

static int find_plane(struct display *d, uint64_t type, uint32_t *out)
{
	drmModePlaneRes *planes;
	unsigned int i;

	planes = drmModeGetPlaneResources(d->fd);
	if (!planes)
		return -1;

	for (i = 0; i < planes->count_planes; i++) {
		drmModePlane *plane = drmModeGetPlane(d->fd, planes->planes[i]);
		uint64_t plane_type = 0;

		if (!plane)
			continue;

		get_prop(d->fd, plane->plane_id, DRM_MODE_OBJECT_PLANE, "type",
			 NULL, &plane_type);
		if (plane_type == type &&
		    (plane->possible_crtcs & (1u << d->crtc_id_index))) {
			*out = plane->plane_id;
			drmModeFreePlane(plane);
			drmModeFreePlaneResources(planes);
			return 0;
		}
		drmModeFreePlane(plane);
	}

	drmModeFreePlaneResources(planes);
	return -1;
}

static int find_crtc_index(struct display *d, uint32_t crtc_id)
{
	unsigned int i;

	for (i = 0; i < (unsigned int)d->res->count_crtcs; i++)
		if (d->res->crtcs[i] == crtc_id)
			return (int)i;
	return -1;
}

static void fallback_mode(drmModeModeInfo *mode, int width, int height)
{
	memset(mode, 0, sizeof(*mode));
	mode->clock = 148500;
	mode->hdisplay = width;
	mode->hsync_start = width + 88;
	mode->hsync_end = width + 88 + 44;
	mode->htotal = width + 88 + 44 + 148;
	mode->vdisplay = height;
	mode->vsync_start = height + 4;
	mode->vsync_end = height + 4 + 5;
	mode->vtotal = height + 4 + 5 + 36;
	mode->vrefresh = 60;
	mode->flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
	snprintf(mode->name, sizeof(mode->name), "%dx%d", width, height);
}

int display_open(struct display *d, bool need_master)
{
	drmModeEncoder *enc = NULL;
	drmVersion *version;
	unsigned int i;
	int card;

	memset(d, 0, sizeof(*d));
	d->fd = -1;

	for (card = 0; card < 10; card++) {
		snprintf(d->path, sizeof(d->path), "/dev/dri/card%d", card);
		d->fd = open(d->path, O_RDWR | O_CLOEXEC);
		if (d->fd < 0)
			continue;

		version = drmGetVersion(d->fd);
		if (version && version->name && !strcmp(version->name, "trigger5")) {
			fprintf(stderr, "using %s (driver %s %d.%d.%d)\n", d->path,
				version->name, version->version_major,
				version->version_minor, version->version_patchlevel);
			drmFreeVersion(version);
			break;
		}
		drmFreeVersion(version);
		close(d->fd);
		d->fd = -1;
	}

	if (d->fd < 0) {
		fprintf(stderr, "no Trigger 5 DRM card found (is the module loaded?)\n");
		return -1;
	}

	if (need_master && drmSetMaster(d->fd)) {
		fprintf(stderr,
			"cannot become DRM master on %s: %s\n"
			"Stop the display manager and run from a text console, e.g.\n"
			"  sudo systemctl stop lightdm   # or sddm/gdm\n"
			"  sudo trigger5-tool test --landscape\n",
			d->path, strerror(errno));
		close(d->fd);
		d->fd = -1;
		return -1;
	}
	d->master = need_master;

	drmSetClientCap(d->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	drmSetClientCap(d->fd, DRM_CLIENT_CAP_ATOMIC, 1);

	d->res = drmModeGetResources(d->fd);
	if (!d->res) {
		fprintf(stderr, "drmModeGetResources: %s\n", strerror(errno));
		goto err;
	}

	for (i = 0; i < (unsigned int)d->res->count_connectors; i++) {
		drmModeConnector *conn =
			drmModeGetConnector(d->fd, d->res->connectors[i]);

		if (!conn)
			continue;
		if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes) {
			d->conn = conn;
			d->conn_id = conn->connector_id;
			break;
		}
		drmModeFreeConnector(conn);
	}

	/*
	 * The adapter's status request can fail and leave the connector at
	 * "unknown" even though the cached EDID and its modes are available.
	 * Fall back to any connector that has a mode list.
	 */
	if (!d->conn) {
		for (i = 0; i < (unsigned int)d->res->count_connectors; i++) {
			drmModeConnector *conn =
				drmModeGetConnector(d->fd, d->res->connectors[i]);

			if (!conn)
				continue;
			if (conn->connection != DRM_MODE_DISCONNECTED &&
			    conn->count_modes) {
				d->conn = conn;
				d->conn_id = conn->connector_id;
				fprintf(stderr,
					"connector %u reports %s, using it because it has modes\n",
					conn->connector_id,
					conn->connection == DRM_MODE_CONNECTED ?
						"connected" : "unknown");
				break;
			}
			drmModeFreeConnector(conn);
		}
	}

	if (!d->conn) {
		fprintf(stderr, "no connected connector with modes\n");
		goto err;
	}

	if (d->conn->encoder_id)
		enc = drmModeGetEncoder(d->fd, d->conn->encoder_id);
	if (!enc && d->conn->count_encoders)
		enc = drmModeGetEncoder(d->fd, d->conn->encoders[0]);

	if (enc && enc->crtc_id)
		d->crtc_id = enc->crtc_id;
	else if (d->res->count_crtcs)
		d->crtc_id = d->res->crtcs[0];
	drmModeFreeEncoder(enc);

	if (!d->crtc_id) {
		fprintf(stderr, "connector has no usable CRTC\n");
		goto err;
	}

	d->crtc_id_index = find_crtc_index(d, d->crtc_id);
	if (d->crtc_id_index < 0) {
		fprintf(stderr, "CRTC %u not found in resources\n", d->crtc_id);
		goto err;
	}

	if (find_plane(d, DRM_PLANE_TYPE_PRIMARY, &d->plane_id)) {
		fprintf(stderr, "no primary plane for CRTC %u\n", d->crtc_id);
		goto err;
	}
	if (find_plane(d, DRM_PLANE_TYPE_CURSOR, &d->cursor_plane_id))
		d->cursor_plane_id = 0; /* cursor tests will be skipped */

	d->mode = d->conn->modes[0];
	d->saved_crtc = drmModeGetCrtc(d->fd, d->crtc_id);

	printf("connector %s-%u, CRTC %u, primary plane %u, cursor plane %u\n",
	       drmModeGetConnectorTypeName(d->conn->connector_type),
	       d->conn->connector_type_id, d->crtc_id, d->plane_id,
	       d->cursor_plane_id);
	printf("mode: %s %dx%d@%d\n", d->mode.name, d->mode.hdisplay,
	       d->mode.vdisplay, d->mode.vrefresh);

	return 0;

err:
	display_close(d);
	return -1;
}

void display_close(struct display *d)
{
	if (d->saved_crtc)
		drmModeFreeCrtc(d->saved_crtc);
	if (d->conn)
		drmModeFreeConnector(d->conn);
	if (d->res)
		drmModeFreeResources(d->res);
	if (d->fd >= 0) {
		if (d->master)
			drmDropMaster(d->fd);
		close(d->fd);
	}
	memset(d, 0, sizeof(*d));
	d->fd = -1;
}

int display_parse_mode(struct display *d, const char *spec)
{
	unsigned int i;
	int w, h;

	if (sscanf(spec, "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) {
		fprintf(stderr, "invalid mode '%s' (expected WxH)\n", spec);
		return -1;
	}

	for (i = 0; i < (unsigned int)d->conn->count_modes; i++) {
		drmModeModeInfo *m = &d->conn->modes[i];

		if (m->hdisplay == w && m->vdisplay == h) {
			d->mode = *m;
			printf("selected mode %s %dx%d@%d\n", m->name,
			       m->hdisplay, m->vdisplay, m->vrefresh);
			return 0;
		}
	}

	fprintf(stderr, "mode %dx%d not in the connector list, using generic timings\n",
		w, h);
	fallback_mode(&d->mode, w, h);
	return 0;
}

void display_print(struct display *d)
{
	unsigned int i;

	printf("  crtc %u, fb %u, %dx%d, mode_valid=%d\n", d->crtc_id,
	       d->saved_crtc ? d->saved_crtc->buffer_id : 0,
	       d->mode.hdisplay, d->mode.vdisplay,
	       d->saved_crtc ? d->saved_crtc->mode_valid : 0);
	for (i = 0; i < (unsigned int)d->conn->count_modes; i++) {
		drmModeModeInfo *m = &d->conn->modes[i];

		printf("  mode %-16s %dx%d@%d%s\n", m->name, m->hdisplay,
		       m->vdisplay, m->vrefresh, i ? "" : " (preferred)");
	}
}

int dumb_fb_create(struct display *d, struct dumb_fb *fb, uint32_t width,
		   uint32_t height, uint32_t format)
{
	struct drm_mode_create_dumb creq;
	struct drm_mode_destroy_dumb dreq;
	uint32_t handles[4] = { 0 };
	uint32_t pitches[4] = { 0 };
	uint32_t offsets[4] = { 0 };
	uint64_t offset = 0;

	memset(fb, 0, sizeof(*fb));
	memset(&creq, 0, sizeof(creq));
	creq.width = width;
	creq.height = height;
	creq.bpp = 32;

	if (ioctl(d->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
		fprintf(stderr, "CREATE_DUMB %ux%u: %s\n", width, height,
			strerror(errno));
		return -1;
	}

	fb->handle = creq.handle;
	fb->pitch = creq.pitch;
	fb->size = creq.size;
	fb->width = width;
	fb->height = height;

	handles[0] = fb->handle;
	pitches[0] = fb->pitch;
	offsets[0] = 0;
	if (drmModeAddFB2(d->fd, width, height, format, handles, pitches,
			  offsets, &fb->fb_id, 0)) {
		fprintf(stderr, "ADDFB2 %ux%u: %s\n", width, height,
			strerror(errno));
		goto err;
	}

	if (drmModeMapDumbBuffer(d->fd, fb->handle, &offset)) {
		fprintf(stderr, "MAP_DUMB: %s\n", strerror(errno));
		goto err;
	}

	fb->map = mmap(NULL, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		       d->fd, (off_t)offset);
	if (fb->map == MAP_FAILED) {
		fb->map = NULL;
		fprintf(stderr, "mmap dumb buffer: %s\n", strerror(errno));
		goto err;
	}

	memset(fb->map, 0, fb->size);
	return 0;

err:
	if (fb->fb_id)
		drmModeRmFB(d->fd, fb->fb_id);
	memset(&dreq, 0, sizeof(dreq));
	dreq.handle = fb->handle;
	ioctl(d->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
	memset(fb, 0, sizeof(*fb));
	return -1;
}

void dumb_fb_destroy(struct display *d, struct dumb_fb *fb)
{
	struct drm_mode_destroy_dumb dreq;

	if (fb->map)
		munmap(fb->map, fb->size);
	if (fb->fb_id)
		drmModeRmFB(d->fd, fb->fb_id);
	if (fb->handle) {
		memset(&dreq, 0, sizeof(dreq));
		dreq.handle = fb->handle;
		ioctl(d->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
	}
	memset(fb, 0, sizeof(*fb));
}

int display_set_mode(struct display *d, struct dumb_fb *fb,
		     uint32_t drm_rotation)
{
	drmModeAtomicReq *req = NULL;
	uint32_t crtc_id_prop, mode_id_prop, active_prop, conn_crtc_prop;
	uint32_t fb_id_prop, plane_crtc_prop, rotation_prop;
	uint32_t src_x, src_y, src_w, src_h;
	uint32_t crtc_x, crtc_y, crtc_w, crtc_h;
	uint32_t blob_id = 0;
	int ret = -1;

	req = drmModeAtomicAlloc();
	if (!req)
		return -1;

	if (drmModeCreatePropertyBlob(d->fd, &d->mode, sizeof(d->mode),
				      &blob_id)) {
		fprintf(stderr, "create MODE_ID blob: %s\n", strerror(errno));
		goto out;
	}

	crtc_id_prop = prop_id(d->fd, d->crtc_id, DRM_MODE_OBJECT_CRTC,
			       "CRTC_ID");
	mode_id_prop = prop_id(d->fd, d->crtc_id, DRM_MODE_OBJECT_CRTC,
			       "MODE_ID");
	active_prop = prop_id(d->fd, d->crtc_id, DRM_MODE_OBJECT_CRTC,
			      "ACTIVE");
	conn_crtc_prop = prop_id(d->fd, d->conn_id, DRM_MODE_OBJECT_CONNECTOR,
				 "CRTC_ID");
	fb_id_prop = prop_id(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE,
			     "FB_ID");
	plane_crtc_prop = prop_id(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE,
				  "CRTC_ID");
	rotation_prop = prop_id(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE,
				"rotation");
	src_x = prop_id(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X");
	src_y = prop_id(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y");
	src_w = prop_id(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W");
	src_h = prop_id(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H");
	crtc_x = prop_id(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X");
	crtc_y = prop_id(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
	crtc_w = prop_id(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W");
	crtc_h = prop_id(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H");

	if (!crtc_id_prop || !mode_id_prop || !conn_crtc_prop || !fb_id_prop ||
	    !plane_crtc_prop || !src_w || !src_h || !crtc_w || !crtc_h) {
		fprintf(stderr, "missing atomic properties on this driver\n");
		goto out;
	}

	drmModeAtomicAddProperty(req, d->crtc_id, crtc_id_prop, d->crtc_id);
	drmModeAtomicAddProperty(req, d->crtc_id, mode_id_prop, blob_id);
	if (active_prop)
		drmModeAtomicAddProperty(req, d->crtc_id, active_prop, 1);
	drmModeAtomicAddProperty(req, d->conn_id, conn_crtc_prop, d->crtc_id);

	drmModeAtomicAddProperty(req, d->plane_id, fb_id_prop, fb->fb_id);
	drmModeAtomicAddProperty(req, d->plane_id, plane_crtc_prop, d->crtc_id);
	if (src_x)
		drmModeAtomicAddProperty(req, d->plane_id, src_x, 0);
	if (src_y)
		drmModeAtomicAddProperty(req, d->plane_id, src_y, 0);
	drmModeAtomicAddProperty(req, d->plane_id, src_w, fb->width << 16);
	drmModeAtomicAddProperty(req, d->plane_id, src_h, fb->height << 16);
	if (crtc_x)
		drmModeAtomicAddProperty(req, d->plane_id, crtc_x, 0);
	if (crtc_y)
		drmModeAtomicAddProperty(req, d->plane_id, crtc_y, 0);
	drmModeAtomicAddProperty(req, d->plane_id, crtc_w, d->mode.hdisplay);
	drmModeAtomicAddProperty(req, d->plane_id, crtc_h, d->mode.vdisplay);
	if (rotation_prop)
		drmModeAtomicAddProperty(req, d->plane_id, rotation_prop,
					 drm_rotation);

	if (drmModeAtomicCommit(d->fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL)) {
		fprintf(stderr, "atomic modeset: %s\n", strerror(errno));
		goto out;
	}

	d->mode_set = true;
	ret = 0;

out:
	if (blob_id)
		drmModeDestroyPropertyBlob(d->fd, blob_id);
	drmModeAtomicFree(req);
	return ret;
}

int display_dirty(struct display *d, struct dumb_fb *fb,
		  const drmModeClip *clips, int count)
{
	if (drmModeDirtyFB(d->fd, fb->fb_id, (drmModeClip *)clips, count)) {
		fprintf(stderr, "dirtyfb: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int display_cursor_set(struct display *d, struct dumb_fb *fb, int hot_x,
		       int hot_y)
{
	if (!d->cursor_plane_id) {
		fprintf(stderr, "driver exposes no cursor plane; skipping cursor test\n");
		return -1;
	}

	if (drmModeSetCursor2(d->fd, d->crtc_id, fb->handle, fb->width,
			      fb->height, hot_x, hot_y)) {
		fprintf(stderr, "SET_CURSOR2: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int display_cursor_move(struct display *d, int x, int y)
{
	if (!d->cursor_plane_id)
		return -1;

	if (drmModeMoveCursor(d->fd, d->crtc_id, x, y)) {
		fprintf(stderr, "MOVE_CURSOR %d,%d: %s\n", x, y,
			strerror(errno));
		return -1;
	}
	return 0;
}

int display_cursor_hide(struct display *d)
{
	if (!d->cursor_plane_id)
		return 0;

	if (drmModeSetCursor(d->fd, d->crtc_id, 0, 0, 0)) {
		fprintf(stderr, "hide cursor: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int display_restore(struct display *d)
{
	int ret = 0;

	if (!d->fd || d->fd < 0)
		return 0;

	display_cursor_hide(d);

	if (d->mode_set && d->saved_crtc) {
		if (d->saved_crtc->mode_valid &&
		    d->saved_crtc->buffer_id) {
			ret = drmModeSetCrtc(d->fd, d->saved_crtc->crtc_id,
					     d->saved_crtc->buffer_id,
					     d->saved_crtc->x,
					     d->saved_crtc->y, &d->conn_id, 1,
					     &d->saved_crtc->mode);
		} else {
			ret = drmModeSetCrtc(d->fd, d->saved_crtc->crtc_id, 0,
					     0, 0, NULL, 0, NULL);
		}
		if (ret)
			fprintf(stderr, "restore CRTC: %s\n", strerror(errno));
	}

	d->mode_set = false;
	return ret;
}
