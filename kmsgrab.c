// SPDX-License-Identifier: GPL-2.0-only
/*
 * KMS/DRM screenshot tool
 *
 * Copyright (c) 2021 Paul Cercueil <paul@crapouillou.net>
 */

#include <drm.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <png.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

typedef struct {
	uint8_t r, g, b;
} uint24_t;

static inline uint24_t rgb16_to_24(uint16_t px)
{
	uint24_t pixel;

	pixel.b = (px & 0x1f)   << 3;
	pixel.g = (px & 0x7e0)  >> 3;
	pixel.r = (px & 0xf800) >> 8;

	return pixel;
}


static inline uint24_t rgb32_to_24(uint32_t px)
{
	uint24_t pixel;

	pixel.b = px & 0xff;
	pixel.g = (px >> 8) & 0xff;
	pixel.r = (px >> 16) & 0xff;

	return pixel;
}


static inline void convert_to_24(drmModeFB *fb, uint24_t *to, void *from)
{
	unsigned int len = fb->width * fb->height;

	if (fb->bpp == 16) {
		uint16_t *ptr = from;
		while (len--)
			*to++ = rgb16_to_24(*ptr++);
	} else {
		uint32_t *ptr = from;
		while (len--)
			*to++ = rgb32_to_24(*ptr++);
	}
}

static int save_png(drmModeFB *fb, int prime_fd, const char *png_fn)
{
	png_bytep *row_pointers;
	png_structp png;
	png_infop info;
	FILE *pngfile;
	void *buffer, *picture;
	unsigned int i;
	int ret;

	picture = malloc(fb->width * fb->height * 4);
	if (!picture)
		return -ENOMEM;

	buffer = mmap(NULL, (fb->bpp >> 3) * fb->width * fb->height,
		      PROT_READ, MAP_PRIVATE, prime_fd, 0);
	if (buffer == MAP_FAILED) {
		ret = -errno;
		fprintf(stderr, "Unable to mmap prime buffer\n");
		goto out_free_picture;
	}

	/* Drop privileges, to write PNG with user rights */
	seteuid(getuid());

	pngfile = fopen(png_fn, "w+");
	if (!pngfile) {
		ret = -errno;
		goto out_unmap_buffer;
	}

	png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
				NULL, NULL, NULL);
	if (!png) {
		ret = -errno;
		goto out_fclose;
	}

	info = png_create_info_struct(png);
	if (!info) {
		ret = -errno;
		goto out_free_png;
	}

	png_init_io(png, pngfile);
	png_set_IHDR(png, info, fb->width, fb->height, 8,
				PNG_COLOR_TYPE_RGB,
				PNG_INTERLACE_NONE,
				PNG_COMPRESSION_TYPE_BASE,
				PNG_FILTER_TYPE_BASE);
	png_write_info(png, info);

	// Convert the picture to a format that can be written into the PNG file (rgb888)
	convert_to_24(fb, picture, buffer);

	row_pointers = malloc(sizeof(*row_pointers) * fb->height);
	if (!row_pointers) {
		ret = -ENOMEM;
		goto out_free_info;
	}

	// And save the final image
	for (i = 0; i < fb->height; i++)
		row_pointers[i] = picture + i * fb->width * 3;

	png_write_image(png, row_pointers);
	png_write_end(png, info);

	ret = 0;

	free(row_pointers);
out_free_info:
	png_destroy_write_struct(NULL, &info);
out_free_png:
	png_destroy_write_struct(&png, NULL);
out_fclose:
	fclose(pngfile);
out_unmap_buffer:
	munmap(buffer, (fb->bpp >> 3) * fb->width * fb->height);
out_free_picture:
	free(picture);
	return ret;
}

int main(int argc, char **argv)
{
	int err, drm_fd, prime_fd, retval = EXIT_FAILURE;
	unsigned int i, card;
	uint32_t fb_id;
	drmModeRes *res;
	drmModeCrtc *crtc;
	drmModeFB *fb;
	char buf[256];
	uint64_t has_dumb;

	if (argc < 2) {
		printf("Usage: kmsgrab <output.png>\n");
		goto out_return;
	}

	for (card = 0; ; card++) {
		snprintf(buf, sizeof(buf), "/dev/dri/card%u", card);

		drm_fd = open(buf, O_RDWR | O_CLOEXEC);
		if (drm_fd < 0) {
			fprintf(stderr, "Could not open KMS/DRM device.\n");
			goto out_return;
		}

		if (drmGetCap(drm_fd, DRM_CAP_DUMB_BUFFER, &has_dumb) >= 0 &&
		    has_dumb)
			break;

		close(drm_fd);
	}

	drm_fd = open(buf, O_RDWR | O_CLOEXEC);
	if (drm_fd < 0) {
		fprintf(stderr, "Could not open KMS/DRM device.\n");
		goto out_return;
	}

	if (drmSetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
		fprintf(stderr, "Unable to set atomic cap.\n");
		goto out_close_fd;
	}

	if (drmSetClientCap(drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
		fprintf(stderr, "Unable to set universal planes cap.\n");
		goto out_close_fd;
	}

	res = drmModeGetResources(drm_fd);
	if (!res) {
		fprintf(stderr, "Unable to get mode resources.\n");
		goto out_close_fd;
	}

	for (i = 0; i < res->count_crtcs; i++) {
		crtc = drmModeGetCrtc(drm_fd, res->crtcs[i]);
		fb_id = crtc->buffer_id;
		drmModeFreeCrtc(crtc);

		if (fb_id != 0)
			break;
	}

	if (i == res->count_crtcs) {
		fprintf(stderr, "No CRTC found\n");
		goto out_free_resources;
	}

	fb = drmModeGetFB(drm_fd, fb_id);
	if (!fb) {
		fprintf(stderr, "Failed to get framebuffer %"PRIu32": %s\n",
			fb_id, strerror(errno));
		goto out_free_resources;
	}

	err = drmPrimeHandleToFD(drm_fd, fb->handle, O_RDONLY, &prime_fd);
	if (err < 0) {
		fprintf(stderr, "Failed to retrieve prime handler: %s\n",
			strerror(-err));
		goto out_free_fb;
	}

	err = save_png(fb, prime_fd, argv[1]);
	if (err < 0) {
		fprintf(stderr, "Failed to take screenshot: %s\n",
			strerror(-err));
		goto out_close_prime_fd;
	}

	retval = EXIT_SUCCESS;

out_close_prime_fd:
	close(prime_fd);
out_free_fb:
	drmModeFreeFB(fb);
out_free_resources:
	drmModeFreeResources(res);
out_close_fd:
	close(drm_fd);
out_return:
	return retval;
}
