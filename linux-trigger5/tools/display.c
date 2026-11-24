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

/*
 * Generate a mode with the CVT algorithm (the same integer math as the
 * kernel's drm_cvt_mode(..., reduced=false)).  Used for device-table modes
 * that the monitor EDID does not already describe.
 */
static void cvt_mode(drmModeModeInfo *mode, int hdisplay, int vdisplay,
		     int vrefresh)
{
	const int hv_factor = 1000;
	const int clock_step = 250;
	int hdisplay_rnd, vdisplay_rnd, vsync;
	int hperiod, vsyncandback_porch, hblank;
	unsigned int hblank_percentage;
	long long tmp1, tmp2, tmp;
	int vfieldrate = vrefresh ? vrefresh : 60;

	if (hdisplay <= 0 || vdisplay <= 0)
		return;

	memset(mode, 0, sizeof(*mode));

	hdisplay_rnd = hdisplay;
	vdisplay_rnd = vdisplay;

	if (!(vdisplay % 3) && (vdisplay * 4 / 3) == hdisplay)
		vsync = 4;
	else if (!(vdisplay % 9) && (vdisplay * 16 / 9) == hdisplay)
		vsync = 5;
	else if (!(vdisplay % 10) && (vdisplay * 16 / 10) == hdisplay)
		vsync = 6;
	else if (!(vdisplay % 4) && (vdisplay * 5 / 4) == hdisplay)
		vsync = 7;
	else if (!(vdisplay % 9) && (vdisplay * 15 / 9) == hdisplay)
		vsync = 7;
	else
		vsync = 10;

	mode->hdisplay = hdisplay_rnd;
	mode->vdisplay = vdisplay_rnd;

	tmp1 = (long long)hv_factor * 1000000 -
	       550LL * hv_factor * vfieldrate;
	tmp2 = (vdisplay_rnd + 3) * 2;
	hperiod = (int)(tmp1 * 2 / (tmp2 * vfieldrate));

	tmp1 = 550LL * hv_factor / hperiod + 1;
	if (tmp1 < vsync + 3)
		vsyncandback_porch = vsync + 3;
	else
		vsyncandback_porch = (int)tmp1;

	mode->vtotal = vdisplay_rnd + vsyncandback_porch + 3;

	hblank_percentage = 30 * hv_factor - 300 * hperiod / 1000;
	if (hblank_percentage < 20 * hv_factor)
		hblank_percentage = 20 * hv_factor;
	hblank = mode->hdisplay * hblank_percentage /
		 (100 * hv_factor - hblank_percentage);
	hblank -= hblank % 16;
	mode->htotal = mode->hdisplay + hblank;
	mode->hsync_end = mode->hdisplay + hblank / 2;
	mode->hsync_start = mode->hsync_end - (mode->htotal * 8) / 100;
	mode->hsync_start += 8 - mode->hsync_start % 8;
	mode->vsync_start = mode->vdisplay + 3;
	mode->vsync_end = mode->vsync_start + vsync;

	tmp = (long long)mode->htotal * hv_factor * 1000 / hperiod;
	mode->clock = (int)tmp;
	mode->clock -= mode->clock % clock_step;
	mode->vrefresh = vfieldrate;
	mode->type = DRM_MODE_TYPE_DRIVER;
	mode->flags = DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_NHSYNC;
	snprintf(mode->name, sizeof(mode->name), "%dx%d", hdisplay, vdisplay);
}

/* Prefer the CEA-861 timings for the two common HDTV modes. */
static bool cea_mode(drmModeModeInfo *mode, int width, int height,
		     int refresh)
{
	memset(mode, 0, sizeof(*mode));

	if (width == 1280 && height == 720 && refresh == 60) {
		mode->clock = 74250;
		mode->hdisplay = 1280;
		mode->hsync_start = 1390;
		mode->hsync_end = 1430;
		mode->htotal = 1650;
		mode->vdisplay = 720;
		mode->vsync_start = 725;
		mode->vsync_end = 730;
		mode->vtotal = 750;
	} else if (width == 1920 && height == 1080 && refresh == 60) {
		mode->clock = 148500;
		mode->hdisplay = 1920;
		mode->hsync_start = 2008;
		mode->hsync_end = 2052;
		mode->htotal = 2200;
		mode->vdisplay = 1080;
		mode->vsync_start = 1084;
		mode->vsync_end = 1089;
		mode->vtotal = 1125;
	} else {
		return false;
	}

	mode->vrefresh = refresh;
	mode->type = DRM_MODE_TYPE_DRIVER;
	mode->flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
	snprintf(mode->name, sizeof(mode->name), "%dx%d", width, height);
	return true;
}

int display_open(struct display *d, bool need_master)
{
	drmModeEncoder *enc = NULL;
	drmVersion *version;
	uint64_t rotation = DRM_MODE_ROTATE_0;
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
	d->saved_rotation = DRM_MODE_ROTATE_0;

	if (!get_prop(d->fd, d->plane_id, DRM_MODE_OBJECT_PLANE, "rotation",
		      NULL, &rotation))
		d->saved_rotation = (uint32_t)rotation;

	if (d->saved_crtc && d->saved_crtc->buffer_id) {
		drmModeFB2 *fb2 = drmModeGetFB2(d->fd,
						d->saved_crtc->buffer_id);

		if (fb2) {
			d->saved_fb_w = fb2->width;
			d->saved_fb_h = fb2->height;
			drmModeFreeFB2(fb2);
		}
	}

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
	int w, h, refresh = 0;
	int fallback = -1;

	if (sscanf(spec, "%dx%d@%d", &w, &h, &refresh) < 2 ||
	    w <= 0 || h <= 0) {
		fprintf(stderr,
			"invalid mode '%s' (expected WxH or WxH@refresh)\n",
			spec);
		return -1;
	}

	for (i = 0; i < (unsigned int)d->conn->count_modes; i++) {
		drmModeModeInfo *m = &d->conn->modes[i];

		if (m->hdisplay != w || m->vdisplay != h)
			continue;

		if (refresh) {
			if ((int)m->vrefresh != refresh)
				continue;
		} else {
			if (fallback < 0)
				fallback = (int)i;
			if (m->vrefresh != 60)
				continue;
		}

		d->mode = *m;
		printf("selected mode %s %dx%d@%d\n", m->name,
		       m->hdisplay, m->vdisplay, m->vrefresh);
		return 0;
	}

	if (!refresh && fallback >= 0) {
		drmModeModeInfo *m = &d->conn->modes[fallback];

		d->mode = *m;
		printf("selected mode %s %dx%d@%d\n", m->name,
		       m->hdisplay, m->vdisplay, m->vrefresh);
		return 0;
	}

	fprintf(stderr,
		"mode %dx%d%s not in the connector list, using standard timings\n",
		w, h, refresh ? " at the requested refresh" : "");
	if (!cea_mode(&d->mode, w, h, refresh ? refresh : 60))
		cvt_mode(&d->mode, w, h, refresh ? refresh : 60);
	return 0;
}

static const char *card_name(const struct display *d)
{
	const char *slash = strrchr(d->path, '/');

	return slash ? slash + 1 : d->path;
}

/*
 * Read the driver's cached 0xA4 table from the card device sysfs node:
 *   count N
 *   index width height refresh timing bpp
 */
int display_read_default_modes(const char *card,
			       struct display_mode_entry *modes, int max)
{
	char path[512], buf[4096];
	int fd, n, count = 0, header = 0;
	char *line, *next;

	if (!card || !modes || max <= 0)
		return 0;

	snprintf(path, sizeof(path), "/sys/class/drm/%s/device/default_modes",
		 card);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return 0;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return 0;
	buf[n] = '\0';

	for (line = buf; line && *line; line = next) {
		unsigned int index, width, height, refresh, timing, bpp;

		next = strchr(line, '\n');
		if (next)
			*next++ = '\0';

		if (!header) {
			header = 1;
			continue;
		}
		if (sscanf(line, "%u %u %u %u %u %u", &index, &width,
			   &height, &refresh, &timing, &bpp) != 6)
			continue;
		if (count >= max)
			break;
		modes[count].index = index;
		modes[count].width = width;
		modes[count].height = height;
		modes[count].refresh = refresh;
		modes[count].timing = timing;
		modes[count].bpp = bpp;
		count++;
	}

	return count;
}

int display_parse_device_mode(struct display *d, const char *spec)
{
	struct display_mode_entry modes[DISPLAY_MAX_DEVICE_MODES];
	const struct display_mode_entry *entry = NULL;
	unsigned long index = 0;
	bool by_index = false;
	int count, i, w = 0, h = 0, refresh = 0;
	bool cea;

	count = display_read_default_modes(card_name(d), modes,
					   DISPLAY_MAX_DEVICE_MODES);
	if (count <= 0) {
		fprintf(stderr,
			"device default modes are not available (driver 1.0.6+ required)\n");
		return -1;
	}

	{
		char *end;

		index = strtoul(spec, &end, 10);
		if (spec[0] && !*end)
			by_index = true;
	}
	if (!by_index &&
	    (sscanf(spec, "%dx%d@%d", &w, &h, &refresh) < 2 ||
	     w <= 0 || h <= 0)) {
		fprintf(stderr,
			"invalid device mode '%s' (expected index or WxH[@refresh])\n",
			spec);
		return -1;
	}

	for (i = 0; i < count; i++) {
		if (by_index) {
			if (modes[i].index == index) {
				entry = &modes[i];
				break;
			}
			continue;
		}
		if (modes[i].width != (unsigned int)w ||
		    modes[i].height != (unsigned int)h)
			continue;
		if (refresh && modes[i].refresh != (unsigned int)refresh)
			continue;
		if (!entry || (!refresh && modes[i].refresh == 60))
			entry = &modes[i];
		if (!refresh && modes[i].refresh == 60)
			break;
	}

	if (!entry) {
		fprintf(stderr,
			"device mode '%s' is not in the 0xA4 table; use --list-modes\n",
			spec);
		return -1;
	}

	/* The monitor's exact timings are better than a synthesized modeline. */
	for (i = 0; i < d->conn->count_modes; i++) {
		drmModeModeInfo *m = &d->conn->modes[i];

		if (m->hdisplay == (int)entry->width &&
		    m->vdisplay == (int)entry->height &&
		    m->vrefresh == entry->refresh) {
			d->mode = *m;
			printf("selected device mode [%u] %ux%u@%u using monitor timings\n",
			       entry->index, entry->width, entry->height,
			       entry->refresh);
			return 0;
		}
	}

	cea = cea_mode(&d->mode, entry->width, entry->height, entry->refresh);
	if (!cea)
		cvt_mode(&d->mode, entry->width, entry->height, entry->refresh);
	printf("selected device mode [%u] %ux%u@%u using %s timings\n",
	       entry->index, entry->width, entry->height, entry->refresh,
	       cea ? "CEA" : "CVT");
	return 0;
}

void display_list_modes(struct display *d)
{
	struct display_mode_entry modes[DISPLAY_MAX_DEVICE_MODES];
	int count, i;

	printf("monitor modes (%d):\n", d->conn->count_modes);
	for (i = 0; i < d->conn->count_modes; i++) {
		drmModeModeInfo *m = &d->conn->modes[i];

		printf("  [%2d] %-16s %ux%u@%u%s\n", i, m->name,
		       m->hdisplay, m->vdisplay, m->vrefresh,
		       i ? "" : " (preferred)");
	}

	count = display_read_default_modes(card_name(d), modes,
					   DISPLAY_MAX_DEVICE_MODES);
	if (count <= 0) {
		printf("device default modes: not available\n");
		return;
	}
	printf("device default modes (%d):\n", count);
	for (i = 0; i < count; i++)
		printf("  [%3u] %ux%u@%u timing=%u bpp=%u\n",
		       modes[i].index, modes[i].width, modes[i].height,
		       modes[i].refresh, modes[i].timing, modes[i].bpp);
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

static int atomic_modeset(struct display *d, const drmModeModeInfo *mode,
			  uint32_t fb_id, uint32_t fb_w, uint32_t fb_h,
			  int src_x_off, int src_y_off,
			  uint32_t drm_rotation, bool enable)
{
	drmModeAtomicReq *req = NULL;
	uint32_t mode_id_prop, active_prop, conn_crtc_prop;
	uint32_t fb_id_prop, plane_crtc_prop, rotation_prop;
	uint32_t src_x, src_y, src_w, src_h;
	uint32_t crtc_x, crtc_y, crtc_w, crtc_h;
	uint32_t blob_id = 0;
	int ret = -1;

	req = drmModeAtomicAlloc();
	if (!req)
		return -1;

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

	if (!mode_id_prop || !active_prop || !conn_crtc_prop || !fb_id_prop ||
	    !plane_crtc_prop || !src_w || !src_h || !crtc_w || !crtc_h) {
		fprintf(stderr,
			"missing atomic properties (MODE_ID=%u ACTIVE=%u connector CRTC_ID=%u FB_ID=%u plane CRTC_ID=%u SRC_W=%u SRC_H=%u CRTC_W=%u CRTC_H=%u)\n",
			mode_id_prop, active_prop, conn_crtc_prop, fb_id_prop,
			plane_crtc_prop, src_w, src_h, crtc_w, crtc_h);
		goto out;
	}

	if (enable) {
		if (!mode || !fb_id) {
			fprintf(stderr, "atomic modeset: missing mode or framebuffer\n");
			goto out;
		}
		if (drmModeCreatePropertyBlob(d->fd, mode, sizeof(*mode),
					      &blob_id)) {
			fprintf(stderr, "create MODE_ID blob: %s\n",
				strerror(errno));
			goto out;
		}

		drmModeAtomicAddProperty(req, d->crtc_id, mode_id_prop,
					 blob_id);
		drmModeAtomicAddProperty(req, d->crtc_id, active_prop, 1);
		drmModeAtomicAddProperty(req, d->conn_id, conn_crtc_prop,
					 d->crtc_id);

		drmModeAtomicAddProperty(req, d->plane_id, fb_id_prop, fb_id);
		drmModeAtomicAddProperty(req, d->plane_id, plane_crtc_prop,
					 d->crtc_id);
		if (src_x)
			drmModeAtomicAddProperty(req, d->plane_id, src_x,
						 (uint32_t)src_x_off << 16);
		if (src_y)
			drmModeAtomicAddProperty(req, d->plane_id, src_y,
						 (uint32_t)src_y_off << 16);
		drmModeAtomicAddProperty(req, d->plane_id, src_w, fb_w << 16);
		drmModeAtomicAddProperty(req, d->plane_id, src_h, fb_h << 16);
		if (crtc_x)
			drmModeAtomicAddProperty(req, d->plane_id, crtc_x, 0);
		if (crtc_y)
			drmModeAtomicAddProperty(req, d->plane_id, crtc_y, 0);
		drmModeAtomicAddProperty(req, d->plane_id, crtc_w,
					 mode->hdisplay);
		drmModeAtomicAddProperty(req, d->plane_id, crtc_h,
					 mode->vdisplay);
		if (rotation_prop)
			drmModeAtomicAddProperty(req, d->plane_id, rotation_prop,
						 drm_rotation);
	} else {
		drmModeAtomicAddProperty(req, d->crtc_id, mode_id_prop, 0);
		drmModeAtomicAddProperty(req, d->crtc_id, active_prop, 0);
		drmModeAtomicAddProperty(req, d->conn_id, conn_crtc_prop, 0);
		drmModeAtomicAddProperty(req, d->plane_id, fb_id_prop, 0);
		drmModeAtomicAddProperty(req, d->plane_id, plane_crtc_prop, 0);
	}

	if (drmModeAtomicCommit(d->fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL)) {
		fprintf(stderr, "atomic modeset: %s\n", strerror(errno));
		goto out;
	}

	ret = 0;

out:
	if (blob_id)
		drmModeDestroyPropertyBlob(d->fd, blob_id);
	drmModeAtomicFree(req);
	return ret;
}

int display_set_mode(struct display *d, struct dumb_fb *fb,
		     uint32_t drm_rotation)
{
	int ret;

	ret = atomic_modeset(d, &d->mode, fb->fb_id, fb->width, fb->height,
			     0, 0, drm_rotation, true);
	if (!ret)
		d->mode_set = true;
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

	if (!d->mode_set) {
		d->mode_set = false;
		return 0;
	}

	if (d->saved_crtc && d->saved_crtc->mode_valid &&
	    d->saved_crtc->buffer_id) {
		uint32_t fw = d->saved_fb_w;
		uint32_t fh = d->saved_fb_h;

		if (!fw || !fh) {
			fw = d->saved_crtc->mode.hdisplay;
			fh = d->saved_crtc->mode.vdisplay;
		}

		ret = atomic_modeset(d, &d->saved_crtc->mode,
				     d->saved_crtc->buffer_id, fw, fh,
				     d->saved_crtc->x, d->saved_crtc->y,
				     d->saved_rotation, true);
	} else {
		ret = atomic_modeset(d, NULL, 0, 0, 0, 0, 0,
				     DRM_MODE_ROTATE_0, false);
	}

	if (ret) {
		fprintf(stderr, "restore: could not restore the original CRTC state\n");
	} else {
		d->mode_set = false;
	}
	return ret;
}

