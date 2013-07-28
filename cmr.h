#ifndef _CMR_H
#define _CMR_H

typedef int (*on_frame_cb)(void *arg, void *ptr, size_t size);

typedef struct _cmr_buffer_t {
	void *ptr;
	size_t size;
} cmr_buffer_t;

typedef struct _cmr_config_t {
	uv_poll_t handler;
	char *device;
	unsigned long fourcc;
	int fd;
	int width;
	int height;
	float fps;
	int buffer_count;
	cmr_buffer_t *buffers;
	on_frame_cb cb;
	void *arg;
} cmr_config_t;

void setCmrCb(cmr_config_t *cfg, on_frame_cb cb, void *arg);

void setCmrSettings(cmr_config_t *cfg, char *device, int width, int height, unsigned long fourcc, float fps);

int openCmr(cmr_config_t *cfg);

void closeCmr(cmr_config_t *cfg);

#endif
