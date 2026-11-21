// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Jeringa
 *
 * Read-only probe of the MCT Trigger 5 adapter: USB descriptors, sysfs,
 * module parameters and the DRM/KMS objects exposed by the kernel driver.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libusb.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <drm_fourcc.h>

#include "probe.h"

#define TRIGGER5_VID 0x0711
#define TRIGGER5_PID 0x5805

struct edid_info {
	int valid;
	char manufacturer[4];
	unsigned int product;
	unsigned int serial;
	int week;
	int year;
	int version;
	int revision;
	int width_cm;
	int height_cm;
	char name[16];
	char serial_name[16];
	size_t bytes;
};

static ssize_t read_binary(const char *path, unsigned char *buf, size_t len)
{
	ssize_t n;
	int fd = open(path, O_RDONLY | O_CLOEXEC);

	if (fd < 0)
		return -1;
	n = read(fd, buf, len);
	close(fd);
	return n;
}

static ssize_t read_path(const char *path, char *buf, size_t len)
{
	ssize_t n;
	int fd = open(path, O_RDONLY | O_CLOEXEC);

	if (fd < 0)
		return -1;
	n = read(fd, buf, len - 1);
	close(fd);
	if (n < 0)
		return -1;
	buf[n] = '\0';
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
		buf[--n] = '\0';
	return n;
}

static int read_attr(const char *dir, const char *name, char *buf, size_t len)
{
	char path[1024];
	int n;

	n = snprintf(path, sizeof(path), "%s/%s", dir, name);
	if (n < 0 || (size_t)n >= sizeof(path))
		return -1;
	return read_path(path, buf, len) >= 0 ? 0 : -1;
}

static void json_string(FILE *out, const char *s)
{
	fputc('"', out);
	for (; s && *s; s++) {
		unsigned char c = (unsigned char)*s;

		switch (c) {
		case '"':
			fputs("\\\"", out);
			break;
		case '\\':
			fputs("\\\\", out);
			break;
		case '\n':
			fputs("\\n", out);
			break;
		case '\r':
			fputs("\\r", out);
			break;
		case '\t':
			fputs("\\t", out);
			break;
		default:
			if (c < 0x20)
				fprintf(out, "\\u%04x", c);
			else
				fputc(c, out);
			break;
		}
	}
	fputc('"', out);
}

static const char *speed_name(int speed)
{
	switch (speed) {
	case LIBUSB_SPEED_LOW:
		return "1.5 Mbps (low speed)";
	case LIBUSB_SPEED_FULL:
		return "12 Mbps (full speed)";
	case LIBUSB_SPEED_HIGH:
		return "480 Mbps (high speed)";
	case LIBUSB_SPEED_SUPER:
		return "5 Gbps (SuperSpeed)";
	case LIBUSB_SPEED_SUPER_PLUS:
		return "10 Gbps (SuperSpeed+)";
	default:
		return "unknown";
	}
}

static const char *transfer_name(uint8_t type)
{
	switch (type) {
	case LIBUSB_TRANSFER_TYPE_CONTROL:
		return "CONTROL";
	case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
		return "ISO";
	case LIBUSB_TRANSFER_TYPE_BULK:
		return "BULK";
	case LIBUSB_TRANSFER_TYPE_INTERRUPT:
		return "INTERRUPT";
	default:
		return "?";
	}
}

static int find_usb_sysfs(char *out, size_t len)
{
	DIR *dir = opendir("/sys/bus/usb/devices");
	struct dirent *de;

	if (!dir)
		return -1;

	while ((de = readdir(dir))) {
		char path[512];
		char vid[16], pid[16];

		if (de->d_name[0] == '.' || strchr(de->d_name, ':'))
			continue;
		snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s",
			 de->d_name);
		if (read_attr(path, "idVendor", vid, sizeof(vid)) ||
		    read_attr(path, "idProduct", pid, sizeof(pid)))
			continue;
		if (strtol(vid, NULL, 16) == TRIGGER5_VID &&
		    strtol(pid, NULL, 16) == TRIGGER5_PID) {
			snprintf(out, len, "%s", path);
			closedir(dir);
			return 0;
		}
	}

	closedir(dir);
	return -1;
}

static void parse_edid(const unsigned char *buf, size_t len,
		       struct edid_info *info)
{
	static const unsigned char header[8] = {
		0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
	};
	unsigned int mfg;
	unsigned int sum = 0;
	size_t i;
	int d;

	memset(info, 0, sizeof(*info));
	info->bytes = len;

	if (len < 128 || memcmp(buf, header, sizeof(header)))
		return;

	for (i = 0; i < 128; i++)
		sum += buf[i];
	if (sum & 0xff)
		return;

	mfg = ((unsigned int)buf[8] << 8) | buf[9];
	info->manufacturer[0] = 'A' + ((mfg >> 10) & 0x1f) - 1;
	info->manufacturer[1] = 'A' + ((mfg >> 5) & 0x1f) - 1;
	info->manufacturer[2] = 'A' + (mfg & 0x1f) - 1;
	info->manufacturer[3] = '\0';
	info->product = buf[10] | (buf[11] << 8);
	info->serial = buf[12] | (buf[13] << 8) | (buf[14] << 16) |
		       ((unsigned int)buf[15] << 24);
	info->week = buf[16];
	info->year = 1990 + buf[17];
	info->version = buf[18];
	info->revision = buf[19];
	info->width_cm = buf[21];
	info->height_cm = buf[22];

	for (d = 0; d < 4; d++) {
		const unsigned char *desc = buf + 54 + 18 * d;
		char *dst = NULL;

		if (desc[0] || desc[1] || desc[2] || desc[4])
			continue;

		switch (desc[3]) {
		case 0xfc:
			dst = info->name;
			break;
		case 0xfe:
			dst = info->serial_name;
			break;
		default:
			continue;
		}

		memcpy(dst, desc + 5, 13);
		dst[13] = '\0';
		for (i = 12; i > 0 && (dst[i] == ' ' || dst[i] == '\n' ||
				       dst[i] == '\0'); i--)
			dst[i] = '\0';
	}

	info->valid = 1;
}

static int read_edid(const char *card, const drmModeConnector *conn,
		     unsigned char *buf, size_t len)
{
	const char *type = drmModeGetConnectorTypeName(conn->connector_type);
	char path[512];
	ssize_t n;

	if (!type)
		type = "Unknown";

	snprintf(path, sizeof(path), "/sys/class/drm/%s-%s-%u/edid", card, type,
		 conn->connector_type_id);
	n = read_binary(path, buf, len);
	return n > 0 ? (int)n : -1;
}

static void print_usb_props(libusb_device_handle *h, libusb_device *dev,
			    const struct libusb_device_descriptor *desc, bool json)
{
	char manufacturer[256] = "";
	char product[256] = "";
	char serial[256] = "";

	if (h) {
		if (desc->iManufacturer)
			libusb_get_string_descriptor_ascii(h, desc->iManufacturer,
							   (unsigned char *)manufacturer,
							   sizeof(manufacturer));
		if (desc->iProduct)
			libusb_get_string_descriptor_ascii(h, desc->iProduct,
							   (unsigned char *)product,
							   sizeof(product));
		if (desc->iSerialNumber)
			libusb_get_string_descriptor_ascii(h, desc->iSerialNumber,
							   (unsigned char *)serial,
							   sizeof(serial));
	}

	if (!json) {
		printf("  bus %u, address %u, port %u\n",
		       libusb_get_bus_number(dev), libusb_get_device_address(dev),
		       libusb_get_port_number(dev));
		printf("  idVendor 0x%04x, idProduct 0x%04x\n", desc->idVendor,
		       desc->idProduct);
		printf("  bcdUSB %x.%02x, bcdDevice %x.%02x\n", desc->bcdUSB >> 8,
		       desc->bcdUSB & 0xff, desc->bcdDevice >> 8,
		       desc->bcdDevice & 0xff);
		printf("  class 0x%02x/0x%02x/0x%02x, max packet %u\n",
		       desc->bDeviceClass, desc->bDeviceSubClass,
		       desc->bDeviceProtocol, desc->bMaxPacketSize0);
		printf("  manufacturer: %s\n", manufacturer[0] ? manufacturer : "(none)");
		printf("  product:      %s\n", product[0] ? product : "(none)");
		printf("  serial:       %s\n", serial[0] ? serial : "(none)");
		printf("  configurations: %u\n", desc->bNumConfigurations);
	} else {
		printf("\"bus\": %u, \"address\": %u, \"port\": %u, ",
		       libusb_get_bus_number(dev), libusb_get_device_address(dev),
		       libusb_get_port_number(dev));
		printf("\"idVendor\": %u, \"idProduct\": %u, ", desc->idVendor,
		       desc->idProduct);
		printf("\"bcdUSB\": %u, \"bcdDevice\": %u, ", desc->bcdUSB,
		       desc->bcdDevice);
		printf("\"manufacturer\": ");
		json_string(stdout, manufacturer);
		printf(", \"product\": ");
		json_string(stdout, product);
		printf(", \"serial\": ");
		json_string(stdout, serial);
		printf(", \"configurations\": [");
	}

	for (int i = 0; i < desc->bNumConfigurations; i++) {
		struct libusb_config_descriptor *cfg;

		if (libusb_get_config_descriptor(dev, i, &cfg))
			continue;

		if (!json) {
			printf("    config %u: %u interface(s), attributes 0x%02x, max power %u mA\n",
			       cfg->bConfigurationValue, cfg->bNumInterfaces,
			       cfg->bmAttributes, cfg->MaxPower * 2);
		} else {
			printf("%s{\"value\": %u, \"interfaces\": [", i ? "," : "",
			       cfg->bConfigurationValue);
		}

		for (int j = 0; j < cfg->bNumInterfaces; j++) {
			const struct libusb_interface *iface = &cfg->interface[j];

			for (int k = 0; k < iface->num_altsetting; k++) {
				const struct libusb_interface_descriptor *alt =
					&iface->altsetting[k];

				if (!json) {
					printf("      interface %u alt %u: class 0x%02x/0x%02x/0x%02x, %u endpoint(s)\n",
					       alt->bInterfaceNumber,
					       alt->bAlternateSetting,
					       alt->bInterfaceClass,
					       alt->bInterfaceSubClass,
					       alt->bInterfaceProtocol,
					       alt->bNumEndpoints);
				} else {
					printf("%s{\"number\": %u, \"alt\": %u, \"class\": %u, \"subclass\": %u, \"protocol\": %u, \"endpoints\": [",
					       (j || k) ? "," : "",
					       alt->bInterfaceNumber,
					       alt->bAlternateSetting,
					       alt->bInterfaceClass,
					       alt->bInterfaceSubClass,
					       alt->bInterfaceProtocol);
				}

				for (int e = 0; e < alt->bNumEndpoints; e++) {
					const struct libusb_endpoint_descriptor *ep =
						&alt->endpoint[e];

					if (!json) {
						printf("        endpoint 0x%02x %s %s, max packet %u, interval %u\n",
						       ep->bEndpointAddress,
						       (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN) ?
							       "IN" : "OUT",
						       transfer_name(ep->bmAttributes &
								     LIBUSB_TRANSFER_TYPE_MASK),
						       ep->wMaxPacketSize,
						       ep->bInterval);
					} else {
						printf("%s{\"address\": %u, \"type\": ",
						       e ? "," : "",
						       ep->bEndpointAddress);
						json_string(stdout,
							    transfer_name(ep->bmAttributes &
									  LIBUSB_TRANSFER_TYPE_MASK));
						printf(", \"max_packet\": %u, \"interval\": %u}",
						       ep->wMaxPacketSize,
						       ep->bInterval);
					}
				}

				if (json)
					printf("]}");
			}
		}

		if (json)
			printf("]}");
		libusb_free_config_descriptor(cfg);
	}

	if (json)
		printf("]");
}

static void probe_usb(bool json)
{
	libusb_device **list = NULL;
	libusb_device *found = NULL;
	ssize_t count;
	char sysfs[512] = "";
	int rc;

	rc = libusb_init(NULL);
	if (rc) {
		fprintf(stderr, "libusb_init: %s\n", libusb_error_name(rc));
		if (json)
			printf("\"usb\": {\"error\": \"libusb init failed\"}");
		return;
	}

	count = libusb_get_device_list(NULL, &list);
	for (ssize_t i = 0; i < count; i++) {
		struct libusb_device_descriptor desc;

		if (libusb_get_device_descriptor(list[i], &desc))
			continue;
		if (desc.idVendor == TRIGGER5_VID && desc.idProduct == TRIGGER5_PID) {
			found = list[i];
			break;
		}
	}

	find_usb_sysfs(sysfs, sizeof(sysfs));

	if (json) {
		if (!found) {
			printf("\"usb\": {\"present\": false}");
		} else {
			struct libusb_device_descriptor desc;
			libusb_device_handle *h = NULL;

			libusb_get_device_descriptor(found, &desc);
			libusb_open(found, &h);
			printf("\"usb\": {\"present\": true, \"sysfs\": ");
			json_string(stdout, sysfs);
			printf(", ");
			print_usb_props(h, found, &desc, true);
			printf(", \"speed\": ");
			json_string(stdout, speed_name(libusb_get_device_speed(found)));
			printf("}");
			if (h)
				libusb_close(h);
		}
	} else {
		printf("[USB]\n");
		if (!found) {
			printf("  Trigger 5 (0711:5805) not present\n");
		} else {
			struct libusb_device_descriptor desc;
			libusb_device_handle *h = NULL;

			libusb_get_device_descriptor(found, &desc);
			libusb_open(found, &h);
			printf("  status: present\n");
			printf("  sysfs:  %s\n", sysfs[0] ? sysfs : "(unknown)");
			printf("  speed:  %s\n", speed_name(libusb_get_device_speed(found)));
			print_usb_props(h, found, &desc, false);
			if (h)
				libusb_close(h);
		}
	}

	if (list)
		libusb_free_device_list(list, 1);
	libusb_exit(NULL);
}

static void probe_module(bool json)
{
	char buf[512];
	DIR *dir;
	struct dirent *de;
	int first = 1;

	if (json) {
		if (read_path("/sys/module/trigger5/refcnt", buf, sizeof(buf)) < 0) {
			printf("\"kernel\": {\"loaded\": false}");
			return;
		}
		printf("\"kernel\": {\"loaded\": true, \"refcnt\": ");
		json_string(stdout, buf);
		printf(", \"version\": ");
		if (read_path("/sys/module/trigger5/version", buf, sizeof(buf)) < 0)
			snprintf(buf, sizeof(buf), "unversioned");
		json_string(stdout, buf);
		printf(", \"parameters\": {");
	} else {
		printf("[Kernel module]\n");
		if (read_path("/sys/module/trigger5/refcnt", buf, sizeof(buf)) < 0) {
			printf("  trigger5: not loaded\n");
			return;
		}
		printf("  trigger5: loaded, refcnt %s\n", buf);
		if (read_path("/sys/module/trigger5/version", buf, sizeof(buf)) < 0)
			snprintf(buf, sizeof(buf), "unversioned");
		printf("  version: %s\n", buf);
		printf("  parameters:\n");
	}

	dir = opendir("/sys/module/trigger5/parameters");
	if (!dir) {
		if (json)
			printf("}}");
		return;
	}

	while ((de = readdir(dir))) {
		char path[512];

		if (de->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), "/sys/module/trigger5/parameters/%s",
			 de->d_name);
		if (read_path(path, buf, sizeof(buf)) < 0)
			continue;

		if (json) {
			printf("%s", first ? "" : ", ");
			json_string(stdout, de->d_name);
			printf(": ");
			json_string(stdout, buf);
		} else {
			printf("    %s = %s\n", de->d_name, buf);
		}
		first = 0;
	}
	closedir(dir);

	if (json)
		printf("}}");
}

static const char *plane_type_name(uint64_t type, char *buf, size_t len)
{
	switch (type) {
	case DRM_PLANE_TYPE_OVERLAY:
		return "overlay";
	case DRM_PLANE_TYPE_PRIMARY:
		return "primary";
	case DRM_PLANE_TYPE_CURSOR:
		return "cursor";
	default:
		snprintf(buf, len, "unknown(%llu)", (unsigned long long)type);
		return buf;
	}
}

static void print_plane_props(int fd, uint32_t plane_id, bool json)
{
	drmModeObjectProperties *props;
	int first = 1;

	props = drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE);
	if (!props)
		return;

	if (!json)
		printf("      properties:");

	for (unsigned int i = 0; i < props->count_props; i++) {
		drmModePropertyRes *prop = drmModeGetProperty(fd, props->props[i]);
		const char *enum_name = NULL;

		if (!prop)
			continue;

		if (prop->count_enums) {
			for (int e = 0; e < prop->count_enums; e++) {
				if (prop->enums[e].value == props->prop_values[i]) {
					enum_name = prop->enums[e].name;
					break;
				}
			}
		}

		if (json) {
			printf("%s", first ? "" : ", ");
			json_string(stdout, prop->name);
			printf(": ");
			if (enum_name)
				json_string(stdout, enum_name);
			else
				printf("%llu", (unsigned long long)props->prop_values[i]);
		} else if (enum_name) {
			printf(" %s=%s", prop->name, enum_name);
		} else {
			printf(" %s=%llu", prop->name,
			       (unsigned long long)props->prop_values[i]);
		}
		first = 0;
		drmModeFreeProperty(prop);
	}

	if (!json)
		printf("\n");
	drmModeFreeObjectProperties(props);
}

static void probe_drm_card(int card_index, const char *path, bool json)
{
	char card[32];
	drmVersion *version;
	drmModeRes *res;
	int fd;
	int first;

	snprintf(card, sizeof(card), "card%d", card_index);
	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return;

	version = drmGetVersion(fd);
	if (!version || !version->name || strcmp(version->name, "trigger5")) {
		drmFreeVersion(version);
		close(fd);
		return;
	}

	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);

	res = drmModeGetResources(fd);
	if (!res) {
		drmFreeVersion(version);
		close(fd);
		return;
	}

	if (json)
		printf("{\"path\": \"%s\", \"driver\": ", path);
	else
		printf("[DRM %s]\n  driver: %s %d.%d.%d (%s)\n", path,
		       version->name, version->version_major, version->version_minor,
		       version->version_patchlevel,
		       version->date ? version->date : "no date");
	if (json) {
		json_string(stdout, version->name);
		printf(", \"version\": \"%d.%d.%d\", \"date\": ",
		       version->version_major, version->version_minor,
		       version->version_patchlevel);
		json_string(stdout, version->date ? version->date : "");
		printf(", \"description\": ");
		json_string(stdout, version->desc ? version->desc : "");
	} else {
		char *busid = drmGetBusid(fd);

		printf("  description: %s\n", version->desc ? version->desc : "");
		printf("  busid: %s\n", busid ? busid : "(none)");
		drmFree(busid);
	}

	{
		uint64_t value;

		if (!json)
			printf("  capabilities:\n");
		else
			printf(", \"capabilities\": {");
		first = 1;
#define CAP(name, cap) \
		do { \
			if (!drmGetCap(fd, cap, &value)) { \
				if (json) { \
					printf("%s\"%s\": %llu", first ? "" : ", ", \
					       name, (unsigned long long)value); \
				} else { \
					printf("    %s: %llu\n", name, \
					       (unsigned long long)value); \
				} \
				first = 0; \
			} \
		} while (0)
		CAP("dumb_buffer", DRM_CAP_DUMB_BUFFER);
		CAP("cursor_width", DRM_CAP_CURSOR_WIDTH);
		CAP("cursor_height", DRM_CAP_CURSOR_HEIGHT);
		CAP("addfb2_modifiers", DRM_CAP_ADDFB2_MODIFIERS);
		CAP("prime", DRM_CAP_PRIME);
#undef CAP
		if (json)
			printf("}");
	}

	if (json)
		printf(", \"connectors\": [");
	else
		printf("  %d connector(s), %d CRTC(s), %d encoder(s)\n",
		       res->count_connectors, res->count_crtcs, res->count_encoders);

	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[i]);
		const char *type;
		unsigned char edid[4096];
		struct edid_info info;
		int edid_len;

		if (!conn)
			continue;
		type = drmModeGetConnectorTypeName(conn->connector_type);
		if (!type)
			type = "Unknown";
		edid_len = read_edid(card, conn, edid, sizeof(edid));
		parse_edid(edid, edid_len > 0 ? (size_t)edid_len : 0, &info);

		if (json) {
			printf("%s{\"id\": %u, \"type\": ", i ? "," : "",
			       conn->connector_id);
			json_string(stdout, type);
			printf(", \"type_id\": %u, \"status\": ",
			       conn->connector_type_id);
			json_string(stdout,
				    conn->connection == DRM_MODE_CONNECTED ? "connected" :
				    conn->connection == DRM_MODE_DISCONNECTED ? "disconnected" :
				    "unknown");
			printf(", \"mm_width\": %u, \"mm_height\": %u",
			       conn->mmWidth, conn->mmHeight);
			printf(", \"modes\": [");
			for (int m = 0; m < conn->count_modes; m++) {
				printf("%s{\"name\": ", m ? "," : "");
				json_string(stdout, conn->modes[m].name);
				printf(", \"width\": %u, \"height\": %u, \"refresh\": %u}",
				       conn->modes[m].hdisplay,
				       conn->modes[m].vdisplay,
				       conn->modes[m].vrefresh);
			}
			printf("], \"edid\": ");
			if (info.valid) {
				printf("{\"bytes\": %zu, \"manufacturer\": ", info.bytes);
				json_string(stdout, info.manufacturer);
				printf(", \"product\": %u, \"serial\": %u, \"week\": %d, \"year\": %d, \"version\": \"%d.%d\", \"size_cm\": [%d, %d], \"name\": ",
				       info.product, info.serial, info.week, info.year,
				       info.version, info.revision, info.width_cm,
				       info.height_cm);
				json_string(stdout, info.name);
				printf(", \"serial_string\": ");
				json_string(stdout, info.serial_name);
				printf("}");
			} else {
				printf("null");
			}
			printf("}");
		} else {
			printf("  connector %u (%s-%u): %s, %ux%u mm\n",
			       conn->connector_id, type, conn->connector_type_id,
			       conn->connection == DRM_MODE_CONNECTED ? "connected" :
			       conn->connection == DRM_MODE_DISCONNECTED ? "disconnected" :
			       "unknown", conn->mmWidth, conn->mmHeight);
			if (info.valid) {
				printf("    EDID: %zu bytes, %s product 0x%04x serial 0x%08x, week %d year %d, EDID %d.%d\n",
				       info.bytes, info.manufacturer, info.product,
				       info.serial, info.week, info.year, info.version,
				       info.revision);
				printf("    monitor: name '%s', serial '%s', size %dx%d cm\n",
				       info.name, info.serial_name, info.width_cm,
				       info.height_cm);
			} else {
				printf("    EDID: not readable from /sys/class/drm/%s-%s-%u/edid\n",
				       card, type, conn->connector_type_id);
			}
			for (int m = 0; m < conn->count_modes; m++)
				printf("    mode %-16s %ux%u@%u%s\n",
				       conn->modes[m].name, conn->modes[m].hdisplay,
				       conn->modes[m].vdisplay,
				       conn->modes[m].vrefresh,
				       m ? "" : " (preferred)");
		}
		drmModeFreeConnector(conn);
	}

	if (json)
		printf("], \"crtcs\": [");
	for (int i = 0; i < res->count_crtcs; i++) {
		drmModeCrtc *crtc = drmModeGetCrtc(fd, res->crtcs[i]);

		if (!crtc)
			continue;
		if (json) {
			printf("%s{\"id\": %u, \"fb\": %u, \"mode\": ",
			       i ? "," : "", crtc->crtc_id, crtc->buffer_id);
			if (crtc->mode_valid) {
				printf("{\"name\": ");
				json_string(stdout, crtc->mode.name);
				printf(", \"width\": %u, \"height\": %u, \"refresh\": %u}",
				       crtc->mode.hdisplay, crtc->mode.vdisplay,
				       crtc->mode.vrefresh);
			} else {
				printf("null");
			}
			printf("}");
		} else if (crtc->mode_valid) {
			printf("  crtc %u: fb %u, mode %s %ux%u@%u\n",
			       crtc->crtc_id, crtc->buffer_id, crtc->mode.name,
			       crtc->mode.hdisplay, crtc->mode.vdisplay,
			       crtc->mode.vrefresh);
		} else {
			printf("  crtc %u: disabled\n", crtc->crtc_id);
		}
		drmModeFreeCrtc(crtc);
	}

	if (json)
		printf("], \"planes\": [");
	{
		drmModePlaneRes *planes = drmModeGetPlaneResources(fd);
		char typebuf[32];

		for (unsigned int i = 0; planes && i < planes->count_planes; i++) {
			drmModePlane *plane = drmModeGetPlane(fd, planes->planes[i]);
			uint64_t plane_type = 0;
			drmModeObjectProperties *props;

			if (!plane)
				continue;

			props = drmModeObjectGetProperties(fd, plane->plane_id,
							   DRM_MODE_OBJECT_PLANE);
			for (unsigned int p = 0; props && p < props->count_props; p++) {
				drmModePropertyRes *prop =
					drmModeGetProperty(fd, props->props[p]);

				if (prop && !strcmp(prop->name, "type"))
					plane_type = props->prop_values[p];
				drmModeFreeProperty(prop);
			}
			drmModeFreeObjectProperties(props);

			if (json) {
				printf("%s{\"id\": %u, \"type\": ", i ? "," : "",
				       plane->plane_id);
				json_string(stdout,
					    plane_type_name(plane_type, typebuf,
							    sizeof(typebuf)));
				printf(", \"crtc\": %u, \"fb\": %u, \"formats\": [",
				       plane->crtc_id, plane->fb_id);
				for (uint32_t f = 0; f < plane->count_formats; f++) {
					const char *name =
						drmGetFormatName(plane->formats[f]);

					if (f)
						printf(", ");
					if (name)
						json_string(stdout, name);
					else
						printf("\"0x%08x\"", plane->formats[f]);
				}
				printf("], \"properties\": {");
				print_plane_props(fd, plane->plane_id, true);
				printf("}}");
			} else {
				printf("  plane %u: %s, crtc %u, fb %u, formats:",
				       plane->plane_id,
				       plane_type_name(plane_type, typebuf,
						       sizeof(typebuf)),
				       plane->crtc_id, plane->fb_id);
				for (uint32_t f = 0; f < plane->count_formats; f++) {
					const char *name =
						drmGetFormatName(plane->formats[f]);

					printf(" %s", name ? name : "?");
				}
				printf("\n");
				print_plane_props(fd, plane->plane_id, false);
			}
			drmModeFreePlane(plane);
		}
		drmModeFreePlaneResources(planes);
	}
	if (json)
		printf("]");

	if (json)
		printf("}");
	else
		printf("\n");

	drmModeFreeResources(res);
	drmFreeVersion(version);
	close(fd);
}

static void probe_drm(bool json)
{
	int found = 0;

	if (json)
		printf("\"drm\": [");

	for (int i = 0; i < 10; i++) {
		char path[64];
		drmVersion *version;
		int fd;

		snprintf(path, sizeof(path), "/dev/dri/card%d", i);
		fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		version = drmGetVersion(fd);
		close(fd);
		if (!version || !version->name || strcmp(version->name, "trigger5")) {
			drmFreeVersion(version);
			continue;
		}
		drmFreeVersion(version);
		if (found++)
			printf(",");
		probe_drm_card(i, path, json);
	}

	if (json)
		printf("]");
	if (!found && !json)
		printf("[DRM]\n  no Trigger 5 card found\n");
}

static void probe_drm_sysfs(bool json)
{
	DIR *dir = opendir("/sys/class/drm");
	struct dirent *de;
	int first = 1;

	if (!dir)
		return;

	if (json)
		printf(", \"drm_sysfs\": {");
	else
		printf("[DRM sysfs]\n");

	while ((de = readdir(dir))) {
		char path[512];
		char status[64], enabled[64], dpms[64];

		if (de->d_name[0] == '.' || !strchr(de->d_name, '-'))
			continue;
		snprintf(path, sizeof(path), "/sys/class/drm/%s", de->d_name);
		if (read_attr(path, "status", status, sizeof(status)))
			continue;
		if (read_attr(path, "enabled", enabled, sizeof(enabled)))
			snprintf(enabled, sizeof(enabled), "?");
		if (read_attr(path, "dpms", dpms, sizeof(dpms)))
			snprintf(dpms, sizeof(dpms), "?");

		if (json) {
			printf("%s", first ? "" : ", ");
			json_string(stdout, de->d_name);
			printf(": {\"status\": ");
			json_string(stdout, status);
			printf(", \"enabled\": ");
			json_string(stdout, enabled);
			printf(", \"dpms\": ");
			json_string(stdout, dpms);
			printf("}");
		} else {
			printf("  %s: status=%s enabled=%s dpms=%s\n", de->d_name,
			       status, enabled, dpms);
		}
		first = 0;
	}

	if (json)
		printf("}");
	else
		printf("\n");

	closedir(dir);
}

int cmd_probe(bool json)
{
	if (json)
		printf("{\n");
	else
		printf("== Trigger 5 probe ==\n\n");

	if (json) {
		probe_usb(true);
		printf(",\n");
		probe_module(true);
		printf(",\n");
		probe_drm(true);
		probe_drm_sysfs(true);
		printf("\n}\n");
		return 0;
	}

	probe_usb(false);
	printf("\n");
	probe_module(false);
	printf("\n");
	probe_drm(false);
	probe_drm_sysfs(false);
	return 0;
}
