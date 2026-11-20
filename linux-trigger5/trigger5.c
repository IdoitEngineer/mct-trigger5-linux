// SPDX-License-Identifier: GPL-2.0-only

#include <linux/array_size.h>
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/iosys-map.h>
#include <linux/jiffies.h>
#include <linux/math.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/usb.h>
#include <linux/version.h>

/*
 * drm_client_setup.h moved from drm/ to drm/clients/ in the 7.x series.
 * Feature-test instead of guessing the kernel version.
 */
#if __has_include(<drm/clients/drm_client_setup.h>)
#include <drm/clients/drm_client_setup.h>
#else
#include <drm/drm_client_setup.h>
#endif
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_format_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_rect.h>

/*
 * "struct drm_atomic_state" was renamed to "struct drm_atomic_commit" in
 * Linux 7.2 (commit 5164f7e7ff8e).  Keep one name in the callbacks and map
 * it per API generation, so every 6.12.x and 7.x kernel builds.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 2, 0)
#define TRIGGER5_ATOMIC_COMMIT struct drm_atomic_commit
#else
#define TRIGGER5_ATOMIC_COMMIT struct drm_atomic_state
#endif

#define DRIVER_NAME            "trigger5"
#define DRIVER_DESC            "MCT Trigger 5 USB display adapter"

#define DRIVER_MAJOR           1
#define DRIVER_MINOR           0

#define TRIGGER5_NUM_URBS      8
#define TRIGGER5_URB_BUF_SIZE  SZ_128K
#define TRIGGER5_MAX_FRAME_SIZE        SZ_8M

struct trigger5_transfer {
       struct trigger5_device *trigger5;
       struct urb *urb;
       struct completion done;
};

struct trigger5_device {
       struct drm_device drm;
       struct usb_interface *intf;
       unsigned int bulk_pipe;

       struct drm_connector connector;
       struct drm_plane plane;
       struct drm_crtc crtc;
       struct drm_encoder encoder;

       u16 frame_counter;

       /* Set on modeset/enable so the next update pushes a whole frame. */
       bool full_frame;

       /*
        * EDID read at probe time. Kept for the lifetime of the device:
        * reading it while bulk transfers are running returns corrupt data.
        */
       const struct drm_edid *edid;

       int next_transfer;
       struct trigger5_transfer transfers[TRIGGER5_NUM_URBS];

       atomic_t needs_reset;
};

struct trigger5_pll {
       u8 prediv;
       u8 mul1;
       u8 mul2;
       u8 div1;
       u8 div2;
} __packed;

struct trigger5_mode_request {
       __be16 height;
       __be16 width;
       __be16 line_total_pixels; /* minus one */
       __be16 line_sync_pulse; /* minus one */
       __be16 line_back_porch; /* minus one */
       __be16 unknown1;
       __be16 unknown2;
       __be16 width_minus_one;
       __be16 frame_total_lines; /* minus one */
       __be16 frame_sync_pulse; /* minus one */
       __be16 frame_back_porch; /* minus one */
       __be16 unknown3;
       __be16 unknown4;
       __be16 height_minus_one;
       struct trigger5_pll pll;
       u8 hsync_polarity;
       u8 vsync_polarity;
} __packed;

/* gm12u320.c uses the same header format */
struct trigger5_bulk_header {
       u8 magic; /* 0xfb */
       u8 length; /* 0x14 */
       __le16 counter; /* lower 12-bit counter, upper 4-bit packet flags */
       __le16 horizontal_offset; /* lower 13-bit offset, upper 3-bit unknown */
       __le16 vertical_offset; /* lower 13-bit offset, upper 3-bit unknown */
       __le16 width; /* lower 13-bit width, upper 3-bit unknown */
       __le16 height; /* lower 13-bit height, upper 3-bit unknown */
       __le32 payload_length; /* lower 28-bit length, upper 4-bit flags */
       u8 flags; /* bit 0 must be set */
       u8 unknown1;
       u8 unknown2;
       u8 checksum;
} __packed;

#define TRIGGER5_REQUEST_KEEPALIVE             0x91
#define TRIGGER5_REQUEST_GET_REGISTER          0xA5
#define TRIGGER5_REQUEST_GET_STATUS            0xA6
#define TRIGGER5_REQUEST_GET_EDID              0xA8
#define TRIGGER5_REQUEST_SET_MODE              0xC3
#define TRIGGER5_REQUEST_SET_REGISTER          0xC4
#define TRIGGER5_REQUEST_SET_CURSOR_POSITION   0xC8
#define TRIGGER5_REQUEST_FIRMWARE_RESET                0xD1

#define TRIGGER5_BULK_TIMEOUT_MS               2000

#define to_trigger5(x) container_of_const(x, struct trigger5_device, drm)

static void trigger5_stop_io(struct trigger5_device *trigger5)
{
       int i;

       for (i = 0; i < TRIGGER5_NUM_URBS; i++)
               usb_kill_urb(trigger5->transfers[i].urb);
}

static int trigger5_usb_suspend(struct usb_interface *interface,
                               pm_message_t message)
{
       struct trigger5_device *trigger5 = usb_get_intfdata(interface);
       int ret;

       ret = drm_mode_config_helper_suspend(&trigger5->drm);
       if (ret)
               return ret;

       trigger5_stop_io(trigger5);

       return 0;
}

static int trigger5_usb_resume(struct usb_interface *interface)
{
       struct trigger5_device *trigger5 = usb_get_intfdata(interface);

       return drm_mode_config_helper_resume(&trigger5->drm);
}

DEFINE_DRM_GEM_FOPS(trigger5_driver_fops);

static const struct drm_driver trigger5_drm_driver = {
       .driver_features = DRIVER_ATOMIC | DRIVER_GEM | DRIVER_MODESET,

       /* GEM hooks */
       .fops = &trigger5_driver_fops,
       DRM_GEM_SHMEM_DRIVER_OPS,
#ifdef DRM_FBDEV_SHMEM_DRIVER_OPS
       DRM_FBDEV_SHMEM_DRIVER_OPS,
#endif

       .name = DRIVER_NAME,
       .desc = DRIVER_DESC,
       .major = DRIVER_MAJOR,
       .minor = DRIVER_MINOR,
};

static const struct drm_mode_config_funcs trigger5_mode_config_funcs = {
       .fb_create = drm_gem_fb_create_with_dirty,
       .atomic_check = drm_atomic_helper_check,
       .atomic_commit = drm_atomic_helper_commit,
};

static u64 trigger5_calculate_pll(struct trigger5_pll *pll, int clock)
{
       u64 ref_clock = 10000000;
       u64 target_clock = (u64)clock * 1000;
       u64 calculated_clock, calculated_err, best_err = U64_MAX;
       int prediv_div2, prediv, mul1, mul2, div1, div2;

       /* Use values found in the capture */
       for (prediv_div2 = 0x02; prediv_div2 <= 0x100; prediv_div2 <<= 1) {
               for (mul1 = 1; mul1 <= 0x32; mul1++) {
                       for (mul2 = mul1; mul2 <= 0x32; mul2++) {
                               for (div1 = 1; div1 <= 0x32; div1++) {
                                       if (!best_err)
                                               break;
                                       calculated_clock =
                                               div_u64(ref_clock * mul1 * mul2,
                                                       prediv_div2 * div1);
                                       calculated_err =
                                               abs_diff(calculated_clock,
                                                        target_clock);
                                       if (prediv_div2 <= 0x10) {
                                               div2 = prediv_div2;
                                               prediv = 1;
                                       } else {
                                               div2 = 0x10;
                                               prediv = prediv_div2 >> 4;
                                       }
                                       if (calculated_err < best_err) {
                                               best_err =
                                                       calculated_err;
                                               pll->mul1 = mul1;
                                               pll->mul2 = mul2;
                                               pll->div1 = div1;
                                               pll->div2 = div2;
                                               pll->prediv = prediv;
                                       }
                               }
                       }
               }
       }
       return best_err;
}

static void trigger5_reset(struct trigger5_device *trigger5, int err)
{
       if (!atomic_xchg(&trigger5->needs_reset, 1))
               usb_queue_reset_device(trigger5->intf);

       drm_err(&trigger5->drm, "USB transfer failed: %d, resetting adapter\n",
               err);
}

static void trigger5_urb_complete(struct urb *urb)
{
       struct trigger5_transfer *transfer = urb->context;
       struct trigger5_device *trigger5 = transfer->trigger5;

       switch (urb->status) {
       case -ENOENT:
       case -ECONNRESET:
       case -ESHUTDOWN:
               break;
       case 0:
               if (urb->actual_length == urb->transfer_buffer_length)
                       break;
               fallthrough;
       default:
               trigger5_reset(trigger5, urb->status);
               break;
       }

       complete(&transfer->done);
}

static struct trigger5_transfer *
trigger5_get_next_urb(struct trigger5_device *trigger5)
{
       struct trigger5_transfer *transfer =
               &trigger5->transfers[trigger5->next_transfer];

       if (atomic_read(&trigger5->needs_reset))
               return ERR_PTR(-EIO);

       if (!wait_for_completion_timeout(&transfer->done,
                                        msecs_to_jiffies(TRIGGER5_BULK_TIMEOUT_MS))) {
               trigger5_reset(trigger5, -ETIMEDOUT);
               return ERR_PTR(-ETIMEDOUT);
       }

       trigger5->next_transfer =
               (trigger5->next_transfer + 1) % TRIGGER5_NUM_URBS;

       return transfer;
}

static int trigger5_submit_urb(struct trigger5_transfer *transfer, size_t len)
{
       int ret;

       transfer->urb->transfer_buffer_length = len;
       ret = usb_submit_urb(transfer->urb, GFP_KERNEL);
       if (ret) {
               drm_err_once(&transfer->trigger5->drm,
                            "failed to submit USB transfer: %d\n", ret);
               complete(&transfer->done);
       }

       return ret;
}

static void trigger5_free_urbs(struct trigger5_device *trigger5)
{
       int i;

       for (i = 0; i < TRIGGER5_NUM_URBS; i++)
               usb_free_urb(trigger5->transfers[i].urb);
}

static int trigger5_alloc_urbs(struct trigger5_device *trigger5)
{
       struct usb_device *udev = interface_to_usbdev(trigger5->intf);
       struct trigger5_transfer *transfer;
       struct urb *urb;
       void *buf;
       int i;

       for (i = 0; i < TRIGGER5_NUM_URBS; i++) {
               transfer = &trigger5->transfers[i];

               urb = usb_alloc_urb(0, GFP_KERNEL);
               if (!urb)
                       return -ENOMEM;
               transfer->urb = urb;

               buf = kmalloc(TRIGGER5_URB_BUF_SIZE, GFP_KERNEL);
               if (!buf)
                       return -ENOMEM;

               usb_fill_bulk_urb(urb, udev, trigger5->bulk_pipe, buf,
                                 TRIGGER5_URB_BUF_SIZE, trigger5_urb_complete,
                                 transfer);
               urb->transfer_flags |= URB_FREE_BUFFER;

               transfer->trigger5 = trigger5;
               init_completion(&transfer->done);
               complete(&transfer->done);
       }

       return 0;
}

static size_t trigger5_mode_buf_len(const struct drm_display_mode *mode)
{
       return size_add(array3_size(mode->hdisplay, mode->vdisplay, 3),
                       sizeof(struct trigger5_bulk_header));
}

static int trigger5_plane_atomic_check(struct drm_plane *plane,
                                      TRIGGER5_ATOMIC_COMMIT *state)
{
       struct drm_plane_state *new_plane_state =
               drm_atomic_get_new_plane_state(state, plane);
       struct drm_shadow_plane_state *shadow_plane_state =
               to_drm_shadow_plane_state(new_plane_state);
       struct drm_crtc *crtc = new_plane_state->crtc;
       struct drm_crtc_state *new_crtc_state = NULL;
       size_t len;
       int ret;

       if (crtc)
               new_crtc_state = drm_atomic_get_new_crtc_state(state, crtc);

       ret = drm_atomic_helper_check_plane_state(new_plane_state,
                                                 new_crtc_state,
                                                 DRM_PLANE_NO_SCALING,
                                                 DRM_PLANE_NO_SCALING,
                                                 false, false);
       if (ret || !new_plane_state->visible)
               return ret;

       /* For drm_fb_xrgb8888_to_rgb888 temp buffer */
       len = new_plane_state->fb->width * sizeof(u32);
       if (!drm_format_conv_state_reserve(&shadow_plane_state->fmtcnv_state,
                                          len, GFP_KERNEL))
               return -ENOMEM;

       return 0;
}

static u8 trigger5_bulk_header_checksum(const struct trigger5_bulk_header *header)
{
       const u8 *data = (const u8 *)header;
       u16 checksum = 0;
       size_t i;

       for (i = 0; i < sizeof(struct trigger5_bulk_header) - 1; i++)
               checksum += data[i];
       checksum &= 0xff;
       checksum = 0x100 - checksum;
       return checksum & 0xff;
}

static void trigger5_plane_atomic_update(struct drm_plane *plane,
                                        TRIGGER5_ATOMIC_COMMIT *atomic_state)
{
       struct drm_plane_state *old_state =
               drm_atomic_get_old_plane_state(atomic_state, plane);
       struct drm_plane_state *state =
               drm_atomic_get_new_plane_state(atomic_state, plane);
       struct drm_shadow_plane_state *shadow_plane_state =
               to_drm_shadow_plane_state(state);
       struct trigger5_device *trigger5 = to_trigger5(plane->dev);
       struct trigger5_transfer *transfer;
       struct trigger5_bulk_header *header;
       struct drm_rect current_rect, src_rect, clip;
       struct iosys_map data_map;
       size_t payload_len;
       int width, height, lines_per_urb;
       int idx, y;

       if (!drm_dev_enter(plane->dev, &idx))
               return;

       src_rect = drm_plane_state_src(state);

       /* Match drm_atomic_helper_damage_iter_init() rounding. */
       src_rect.x1 >>= 16;
       src_rect.y1 >>= 16;
       src_rect.x2 = (src_rect.x2 >> 16) + !!(src_rect.x2 & 0xffff);
       src_rect.y2 = (src_rect.y2 >> 16) + !!(src_rect.y2 & 0xffff);

       /*
        * After a modeset or when userspace switches to a new framebuffer
        * (which is what X does when it enables software rotation), damage
        * tracking only describes the changed part of the *previous* frame.
        * Push a complete frame instead, otherwise stale rows remain on the
        * panel.
        */
       if (trigger5->full_frame || !old_state->fb ||
           old_state->fb != state->fb || old_state->crtc != state->crtc) {
               current_rect = src_rect;
               trigger5->full_frame = false;
       } else {
               if (!drm_atomic_helper_damage_merged(old_state, state,
                                                    &current_rect))
                       goto exit;

               /* Never send pixels outside the visible source area. */
               if (!drm_rect_intersect(&current_rect, &src_rect))
                       goto exit;
       }

       width = drm_rect_width(&current_rect);
       height = drm_rect_height(&current_rect);
       if (width <= 0 || height <= 0)
               goto exit;

       payload_len = array3_size(width, height, 3);
       lines_per_urb = TRIGGER5_URB_BUF_SIZE / (width * 3);

       if (drm_gem_fb_begin_cpu_access(state->fb, DMA_FROM_DEVICE) < 0)
               goto exit;

       transfer = trigger5_get_next_urb(trigger5);
       if (IS_ERR(transfer))
               goto exit_end_cpu_access;

       header = transfer->urb->transfer_buffer;
       header->magic = 0xfb;
       header->length = 0x14;
       /* flags 0: uncompressed 24-bit RGB888. */
       header->counter =
               cpu_to_le16((trigger5->frame_counter++) & 0xfff);
       header->horizontal_offset = cpu_to_le16(current_rect.x1 - src_rect.x1);
       header->vertical_offset = cpu_to_le16(current_rect.y1 - src_rect.y1);
       header->width = cpu_to_le16(width);
       header->height = cpu_to_le16(height);
       header->payload_length = cpu_to_le32((u32)payload_len);
       header->flags = 0x1;
       header->unknown1 = 0;
       header->unknown2 = 0;
       header->checksum = trigger5_bulk_header_checksum(header);

       if (trigger5_submit_urb(transfer, sizeof(*header)))
               goto exit_end_cpu_access;

       clip = current_rect;
       for (y = current_rect.y1; y < current_rect.y2; y += lines_per_urb) {
               clip.y1 = y;
               clip.y2 = min(y + lines_per_urb, current_rect.y2);

               transfer = trigger5_get_next_urb(trigger5);
               if (IS_ERR(transfer))
                       break;

               iosys_map_set_vaddr(&data_map, transfer->urb->transfer_buffer);
               drm_fb_xrgb8888_to_rgb888(&data_map, NULL,
                                         &shadow_plane_state->data[0],
                                         state->fb, &clip,
                                         &shadow_plane_state->fmtcnv_state);

               if (trigger5_submit_urb(transfer,
                                       width * 3 * drm_rect_height(&clip)))
                       break;
       }

exit_end_cpu_access:
       drm_gem_fb_end_cpu_access(state->fb, DMA_FROM_DEVICE);
exit:
       drm_dev_exit(idx);
}

static const struct drm_plane_helper_funcs trigger5_plane_helper_funcs = {
       DRM_GEM_SHADOW_PLANE_HELPER_FUNCS,
       .atomic_check = trigger5_plane_atomic_check,
       .atomic_update = trigger5_plane_atomic_update,
};

static const struct drm_plane_funcs trigger5_plane_funcs = {
       .update_plane = drm_atomic_helper_update_plane,
       .disable_plane = drm_atomic_helper_disable_plane,
       .destroy = drm_plane_cleanup,
       DRM_GEM_SHADOW_PLANE_FUNCS,
};

static const u32 trigger5_plane_formats[] = {
       DRM_FORMAT_XRGB8888,
};

static void trigger5_crtc_atomic_enable(struct drm_crtc *crtc,
                                       TRIGGER5_ATOMIC_COMMIT *state)
{
       struct trigger5_device *trigger5 = to_trigger5(crtc->dev);
       struct usb_device *udev = interface_to_usbdev(trigger5->intf);
       struct drm_crtc_state *crtc_state =
               drm_atomic_get_new_crtc_state(state, crtc);
       struct drm_display_mode *mode = &crtc_state->mode;
       struct trigger5_mode_request request = {};
       u8 data[4];
       u64 clk;
       int idx, ret;

       if (!drm_dev_enter(crtc->dev, &idx))
               return;

       trigger5_stop_io(trigger5);

       /* Sequence recovered from USB captures. */
       ret = usb_control_msg_recv(udev, 0,
                                  TRIGGER5_REQUEST_FIRMWARE_RESET,
                                  USB_DIR_IN | USB_TYPE_VENDOR |
                                          USB_RECIP_DEVICE,
                                  0x0000, 0x0000, data, 1,
                                  USB_CTRL_GET_TIMEOUT, GFP_KERNEL);
       if (ret)
               goto err;

       request.height = cpu_to_be16(mode->vdisplay);
       request.height_minus_one = cpu_to_be16(mode->vdisplay - 1);
       request.width = cpu_to_be16(mode->hdisplay);
       request.width_minus_one = cpu_to_be16(mode->hdisplay - 1);

       request.line_total_pixels = cpu_to_be16(mode->htotal - 1);
       request.line_sync_pulse =
               cpu_to_be16(mode->hsync_end - mode->hsync_start - 1);
       request.line_back_porch =
               cpu_to_be16(mode->htotal - mode->hsync_end - 1);

       request.frame_total_lines = cpu_to_be16(mode->vtotal - 1);
       request.frame_sync_pulse =
               cpu_to_be16(mode->vsync_end - mode->vsync_start - 1);
       request.frame_back_porch =
               cpu_to_be16(mode->vtotal - mode->vsync_end - 1);
       request.unknown1 = cpu_to_be16(0xff);
       request.unknown2 = cpu_to_be16(0xff);
       request.unknown3 = cpu_to_be16(0xff);
       request.unknown4 = cpu_to_be16(0xff);

       request.hsync_polarity = (mode->flags & DRM_MODE_FLAG_PHSYNC) ? 0 : 1;
       request.vsync_polarity = (mode->flags & DRM_MODE_FLAG_PVSYNC) ? 0 : 1;

       trigger5_calculate_pll(&request.pll, mode->clock);
       clk = div_u64(10000000ULL * request.pll.mul1 * request.pll.mul2,
                     (u32)request.pll.prediv * request.pll.div1 *
                             request.pll.div2 * 1000);
       drm_dbg(&trigger5->drm,
               "pll: %02x %02x %02x %02x %02x -> %llu kHz (want %d kHz)\n",
               request.pll.prediv, request.pll.mul1, request.pll.mul2,
               request.pll.div1, request.pll.div2, clk, mode->clock);

       /* wValue can be any value since we are sending a custom mode */
       ret = usb_control_msg_send(udev, 0, TRIGGER5_REQUEST_SET_MODE,
                                  USB_DIR_OUT | USB_TYPE_VENDOR |
                                          USB_RECIP_DEVICE,
                                  0, 0, &request, sizeof(request),
                                  USB_CTRL_SET_TIMEOUT, GFP_KERNEL);
       if (ret)
               goto err;

       ret = usb_control_msg_recv(udev, 0,
                                  TRIGGER5_REQUEST_FIRMWARE_RESET,
                                  USB_DIR_IN | USB_TYPE_VENDOR |
                                          USB_RECIP_DEVICE,
                                  0x0201, 0x0000, data, 1,
                                  USB_CTRL_GET_TIMEOUT, GFP_KERNEL);
       if (ret)
               goto err;

       ret = usb_control_msg_recv(udev, 0,
                                  TRIGGER5_REQUEST_GET_REGISTER,
                                  USB_DIR_IN | USB_TYPE_VENDOR |
                                          USB_RECIP_DEVICE,
                                  0x0000, 0xec34, data, sizeof(data),
                                  USB_CTRL_GET_TIMEOUT, GFP_KERNEL);
       if (ret)
               goto err;

       data[0] = 0x60;
       data[1] = 0x00;
       data[2] = 0x00;
       data[3] = 0x10;
       ret = usb_control_msg_send(udev, 0,
                                  TRIGGER5_REQUEST_SET_REGISTER,
                                  USB_DIR_OUT | USB_TYPE_VENDOR |
                                          USB_RECIP_DEVICE,
                                  0x0000, 0xec34, data, sizeof(data),
                                  USB_CTRL_SET_TIMEOUT, GFP_KERNEL);
       if (ret)
               goto err;

       /*
        * A successful re-initialisation clears any latched transfer error
        * and makes the next plane update send a complete frame.
        */
       atomic_set(&trigger5->needs_reset, 0);
       trigger5->full_frame = true;

       goto exit;

err:
       drm_err_once(&trigger5->drm, "failed to configure display mode: %d\n", ret);
exit:
       drm_dev_exit(idx);
}

static void trigger5_crtc_atomic_disable(struct drm_crtc *crtc,
                                        TRIGGER5_ATOMIC_COMMIT *state)
{
       struct trigger5_device *trigger5 = to_trigger5(crtc->dev);
       struct usb_device *udev = interface_to_usbdev(trigger5->intf);
       u8 data;
       int idx, ret;

       if (!drm_dev_enter(crtc->dev, &idx))
               return;

       trigger5_stop_io(trigger5);

       ret = usb_control_msg_recv(udev, 0,
                                  TRIGGER5_REQUEST_FIRMWARE_RESET,
                                  USB_DIR_IN | USB_TYPE_VENDOR |
                                          USB_RECIP_DEVICE,
                                  0x0001, 0x0000, &data, 1,
                                  USB_CTRL_GET_TIMEOUT, GFP_KERNEL);
       if (ret)
               drm_err_once(&trigger5->drm, "failed to disable display: %d\n", ret);

       drm_dev_exit(idx);
}

static enum drm_mode_status
trigger5_crtc_mode_valid(struct drm_crtc *crtc,
                        const struct drm_display_mode *mode)
{
       struct trigger5_pll pll;
       u64 err, ppm;

       if (mode->hsync_end <= mode->hsync_start ||
           mode->htotal <= mode->hsync_end)
               return MODE_H_ILLEGAL;

       if (mode->vsync_end <= mode->vsync_start ||
           mode->vtotal <= mode->vsync_end)
               return MODE_V_ILLEGAL;

       if (trigger5_mode_buf_len(mode) > TRIGGER5_MAX_FRAME_SIZE)
               return MODE_MEM;

       err = trigger5_calculate_pll(&pll, mode->clock);
       ppm = div64_u64(err * 1000, mode->clock);
       if (ppm > 10000)
               return MODE_CLOCK_RANGE;

       return MODE_OK;
}

static const struct drm_mode_config_helper_funcs
trigger5_mode_config_helper_funcs = {
       .atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

static const struct drm_crtc_helper_funcs trigger5_crtc_helper_funcs = {
       .mode_valid = trigger5_crtc_mode_valid,
       .atomic_disable = trigger5_crtc_atomic_disable,
       .atomic_check = drm_crtc_helper_atomic_check,
       .atomic_enable = trigger5_crtc_atomic_enable,
};

static const struct drm_crtc_funcs trigger5_crtc_funcs = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 3, 0)
       .atomic_create_state = drm_atomic_helper_crtc_create_state,
#endif
       .destroy = drm_crtc_cleanup,
       .set_config = drm_atomic_helper_set_config,
       .page_flip = drm_atomic_helper_page_flip,
       .atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
       .atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static const struct drm_encoder_funcs trigger5_encoder_funcs = {
       .destroy = drm_encoder_cleanup,
};

static int trigger5_read_edid(void *data, u8 *buf, unsigned int block,
                             size_t len)
{
       struct trigger5_device *trigger5 = data;
       struct usb_device *udev = interface_to_usbdev(trigger5->intf);
       int idx, ret;

       if (!drm_dev_enter(&trigger5->drm, &idx))
               return -ENODEV;

       ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
                             TRIGGER5_REQUEST_GET_EDID,
                             USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                             block, 0, buf, len, USB_CTRL_GET_TIMEOUT);
       drm_dev_exit(idx);

       if (ret < 0)
               return ret;
       if (ret != len)
               return -EIO;

       return 0;
}

/*
 * Return the EDID read at probe time, reading it once if that failed.
 * It must never be re-read while the frame stream is active: the adapter
 * returns corrupt control replies then, which the DRM EDID parser rejects
 * ("checksum is invalid") and the connector loses its mode list.
 */
static const struct drm_edid *
trigger5_get_cached_edid(struct trigger5_device *trigger5)
{
       if (!trigger5->edid)
               trigger5->edid = drm_edid_read_custom(&trigger5->connector,
                                                     trigger5_read_edid,
                                                     trigger5);

       return trigger5->edid;
}

static int trigger5_connector_get_modes(struct drm_connector *connector)
{
       struct trigger5_device *trigger5 = to_trigger5(connector->dev);
       const struct drm_edid *edid = trigger5_get_cached_edid(trigger5);

       if (!edid)
               return 0;

       drm_edid_connector_update(connector, edid);
       return drm_edid_connector_add_modes(connector);
}

static enum drm_connector_status
trigger5_detect(struct drm_connector *connector, bool force)
{
       struct trigger5_device *trigger5 = to_trigger5(connector->dev);
       struct usb_device *udev = interface_to_usbdev(trigger5->intf);
       u8 status[2];
       int idx, ret;

       if (!drm_dev_enter(&trigger5->drm, &idx))
               return connector_status_disconnected;

       ret = usb_control_msg_recv(udev, 0, TRIGGER5_REQUEST_GET_STATUS,
                                  USB_DIR_IN | USB_TYPE_VENDOR |
                                          USB_RECIP_DEVICE,
                                  0xff, 0x3, status, sizeof(status),
                                  USB_CTRL_GET_TIMEOUT, GFP_KERNEL);
       drm_dev_exit(idx);

       if (ret)
               return connector_status_unknown;

       return status[1] == 1 ? connector_status_connected :
                            connector_status_disconnected;
}

static const struct drm_connector_helper_funcs trigger5_connector_helper_funcs = {
       .get_modes = trigger5_connector_get_modes,
};

static const struct drm_connector_funcs trigger5_connector_funcs = {
       .fill_modes = drm_helper_probe_single_connector_modes,
       .destroy = drm_connector_cleanup,
       .detect = trigger5_detect,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 3, 0)
       .atomic_create_state = drm_atomic_helper_connector_create_state,
#endif
       .atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
       .atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int trigger5_connector_init(struct trigger5_device *trigger5,
                                  int connector_type)
{
       int ret;

       drm_connector_helper_add(&trigger5->connector,
                                &trigger5_connector_helper_funcs);
       ret = drm_connector_init(&trigger5->drm, &trigger5->connector,
                                &trigger5_connector_funcs, connector_type);
       trigger5->connector.polled =
               DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT;
       return ret;
}

static void trigger5_edid_release(struct drm_device *drm, void *data)
{
       struct trigger5_device *trigger5 = data;

       if (trigger5->edid)
               drm_edid_free(trigger5->edid);
       trigger5->edid = NULL;
}

static int trigger5_usb_probe(struct usb_interface *interface,
                             const struct usb_device_id *id)
{
       int ret;
       int i;
       struct trigger5_device *trigger5;
       struct usb_endpoint_descriptor *bulk_out;
       struct drm_device *dev;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 0, 0)
       struct device *dma_dev;
#endif
       struct usb_device *udev = interface_to_usbdev(interface);
       /* Heuristic: Presence of audio interfaces indicates HDMI. */
       bool is_hdmi = udev->actconfig->desc.bNumInterfaces > 1;
       u8 data;

       trigger5 = devm_drm_dev_alloc(&interface->dev, &trigger5_drm_driver,
                                     struct trigger5_device, drm);
       if (IS_ERR(trigger5))
               return PTR_ERR(trigger5);

       trigger5->intf = interface;

       ret = usb_find_bulk_out_endpoint(interface->cur_altsetting, &bulk_out);
       if (ret)
               return ret;
       trigger5->bulk_pipe =
               usb_sndbulkpipe(udev, usb_endpoint_num(bulk_out));

       /* Disarm the keepalive */
       usb_control_msg_recv(udev, 0, TRIGGER5_REQUEST_KEEPALIVE,
                            USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                            0, 0, &data, 1, USB_CTRL_GET_TIMEOUT, GFP_KERNEL);

       dev = &trigger5->drm;

       ret = drmm_add_action_or_reset(dev, trigger5_edid_release, trigger5);
       if (ret)
               return ret;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 0, 0)
       dma_dev = usb_intf_get_dma_device(interface);
       if (dma_dev) {
               drm_dev_set_dma_dev(dev, dma_dev);
               put_device(dma_dev);
       } else {
               drm_warn(dev,
                        "buffer sharing not supported"); /* not an error */
       }
#endif

       ret = drmm_mode_config_init(dev);
       if (ret)
               return ret;

       /*
        * The device has a built-in mode list, however we ignore
        * the mode list because the device accepts custom modes
        */
       dev->mode_config.min_width = 1;
       dev->mode_config.max_width = 8191;
       dev->mode_config.min_height = 1;
       dev->mode_config.max_height = 8191;

       dev->mode_config.funcs = &trigger5_mode_config_funcs;
       dev->mode_config.helper_private = &trigger5_mode_config_helper_funcs;

       ret = drm_universal_plane_init(dev, &trigger5->plane, 0,
                                      &trigger5_plane_funcs,
                                      trigger5_plane_formats,
                                      ARRAY_SIZE(trigger5_plane_formats), NULL,
                                      DRM_PLANE_TYPE_PRIMARY, NULL);
       if (ret)
               return ret;

       drm_plane_helper_add(&trigger5->plane, &trigger5_plane_helper_funcs);
       drm_plane_enable_fb_damage_clips(&trigger5->plane);

       ret = drm_crtc_init_with_planes(dev, &trigger5->crtc, &trigger5->plane,
                                       NULL, &trigger5_crtc_funcs, NULL);
       if (ret)
               return ret;

       drm_crtc_helper_add(&trigger5->crtc, &trigger5_crtc_helper_funcs);

       ret = trigger5_connector_init(trigger5, is_hdmi ?
                                             DRM_MODE_CONNECTOR_HDMIA :
                                             DRM_MODE_CONNECTOR_VGA);
       if (ret)
               return ret;

       /*
        * Read the EDID before any bulk transfer can be started. The adapter
        * returns corrupt EDID replies while streaming, so this has to happen
        * here rather than lazily. Retry a few times since the monitor may
        * still be powering up.
        */
       for (i = 0; i < 3; i++) {
               if (trigger5_get_cached_edid(trigger5))
                       break;
               msleep(20);
       }

       ret = drm_encoder_init(dev, &trigger5->encoder, &trigger5_encoder_funcs,
                              is_hdmi ? DRM_MODE_ENCODER_TMDS :
                                        DRM_MODE_ENCODER_DAC, NULL);
       if (ret)
               return ret;
       trigger5->encoder.possible_crtcs = drm_crtc_mask(&trigger5->crtc);

       ret = drm_connector_attach_encoder(&trigger5->connector,
                                          &trigger5->encoder);
       if (ret)
               return ret;

       ret = trigger5_alloc_urbs(trigger5);
       if (ret)
               goto err_free_urbs;

       drm_mode_config_reset(dev);

       usb_set_intfdata(interface, trigger5);

       drm_kms_helper_poll_init(dev);

       ret = drm_dev_register(dev, 0);
       if (ret)
               goto err_poll_fini;

       /*
        * Kernels without DRM_FBDEV_SHMEM_DRIVER_OPS (6.12, 7.0/7.1) do not
        * know the fbdev_probe callback, so use the older setup helper.
        */
#if defined(DRM_FBDEV_SHMEM_DRIVER_OPS)
       drm_client_setup(dev, NULL);
#else
       drm_fbdev_shmem_setup(dev, 32);
#endif

       return 0;

err_poll_fini:
       drm_kms_helper_poll_fini(dev);
       usb_set_intfdata(interface, NULL);
err_free_urbs:
       trigger5_free_urbs(trigger5);
       return ret;
}

static void trigger5_usb_disconnect(struct usb_interface *interface)
{
       struct trigger5_device *trigger5 = usb_get_intfdata(interface);
       struct drm_device *dev = &trigger5->drm;

       drm_kms_helper_poll_fini(dev);
       drm_dev_unplug(dev);
       drm_atomic_helper_shutdown(dev);
       trigger5_stop_io(trigger5);
       trigger5_free_urbs(trigger5);
}

static const struct usb_device_id id_table[] = {
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5800, 0) }, /* HDMI */
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5801, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5802, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5803, 0) },
       { USB_DEVICE(0x0711, 0x5804) }, /* VGA */
       { USB_DEVICE(0x0711, 0x5805) },
       { USB_DEVICE(0x0711, 0x5806) },
       { USB_DEVICE(0x0711, 0x5807) },
       { USB_DEVICE(0x0711, 0x5808) },
       { USB_DEVICE(0x0711, 0x5809) },
       { USB_DEVICE(0x0711, 0x580A) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x580B, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x580C, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x580D, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x580E, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x580F, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5810, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5811, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5812, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5813, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5814, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5815, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5816, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5817, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5818, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5819, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x581A, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x581B, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x581C, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x581D, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x581E, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x581F, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5820, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5821, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5822, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5823, 0) },
       { USB_DEVICE(0x0711, 0x5824) },
       { USB_DEVICE(0x0711, 0x5825) },
       { USB_DEVICE(0x0711, 0x5826) },
       { USB_DEVICE(0x0711, 0x5827) },
       { USB_DEVICE(0x0711, 0x5828) },
       { USB_DEVICE(0x0711, 0x5829) },
       { USB_DEVICE(0x0711, 0x582A) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x582B, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x582C, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x582D, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x582E, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x582F, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5830, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5831, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5832, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x5833, 0) },
       { USB_DEVICE(0x0711, 0x5834) },
       { USB_DEVICE(0x0711, 0x5835) },
       { USB_DEVICE(0x0711, 0x5836) },
       { USB_DEVICE(0x0711, 0x5837) },
       { USB_DEVICE(0x0711, 0x5838) },
       { USB_DEVICE(0x0711, 0x5839) },
       { USB_DEVICE(0x0711, 0x583A) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x583B, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x583C, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x583D, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x583E, 0) },
       { USB_DEVICE_INTERFACE_NUMBER(0x0711, 0x583F, 0) },
       {},
};
MODULE_DEVICE_TABLE(usb, id_table);

static struct usb_driver trigger5_driver = {
       .name = DRIVER_NAME,
       .probe = trigger5_usb_probe,
       .disconnect = trigger5_usb_disconnect,
       .suspend = trigger5_usb_suspend,
       .resume = trigger5_usb_resume,
       .reset_resume = trigger5_usb_resume,
       .id_table = id_table,
};
module_usb_driver(trigger5_driver);
MODULE_AUTHOR("Ho Jie Feng <hjf3108@gmail.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
