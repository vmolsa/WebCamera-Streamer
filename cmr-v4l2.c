#include "config.h"

static int xioctl(int fh, int request, void *arg) {
	int r;

	do {
		r = ioctl(fh, request, arg);
	} while (-1 == r && EINTR == errno);

	return r;
}

static void v4l2_on_frame(uv_poll_t *handler, int status, int events) {
	cmr_config_t *cfg = (cmr_config_t *) handler->data;
	static struct v4l2_buffer buf;

	if (events == UV_READABLE) {
		memset(&buf, 0, sizeof(struct v4l2_buffer));

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;

		if (xioctl(cfg->fd, VIDIOC_DQBUF, &buf) < 0) {
			switch (errno) {
				case EAGAIN:
					break;
				case EIO:
				default:
					errno_exit("VIDIOC_DQBUF");
			}
		}

		assert(buf.index < cfg->buffer_count);

		if (cfg->cb(cfg->arg, cfg->buffers[buf.index].ptr, buf.bytesused) < 0) {
			uv_stop(uv_default_loop());
		}

                if (xioctl(data->fd, VIDIOC_QBUF, &buf) < 0) {
                        errno_exit("VIDIOC_QBUF");
		}
	}
}

static void setCmrCb(cmr_config_t *cfg, on_frame cb, void *arg) {
	cfg->cb = cb;
	cfg->arg = arg;
}

static void setCmrSettings(cmr_config_t *cfg, char *device, int width, int height, unsigned long fourcc, float fps) {
	cfg->device = device;
	cfg->width = width;
	cfg->height = height;
	cfg->fourcc = fourcc;
	cfg->fps = fps;
}

static int openCmr(cmr_config_t *cfg) {
	static struct v4l2_requestbuffers req;
	static struct v4l2_capability cap;
	static struct v4l2_cropcap cropcap;
	static struct v4l2_streamparm fps;
	static struct v4l2_crop crop;
	static struct v4l2_format fmt;
	static struct v4l2_buffer buf;
	static enum v4l2_buf_type type;
	static struct buffer *buffers;

	static unsigned int i;
	static unsigned int min;

	cfg->fd = -1;
	cfg->buffers = NULL;
	cfg->buffer_count = -1;

	if ((cfg->fd = open(cfg->device, O_RDWR, 0)) < 0) {
		LOG("Unable to open device(%s)\n", cfg->devname);
		return -1; 
	}

	memset(&cap, 0, sizeof(struct v4l2_capability));

	if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
		if (EINVAL == errno) {
			return -1;
		} else {
			LOG("Unable to get device query capture options!\n");
			return -1;
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		LOG("Device can't capture video!\n");
		return -1;
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		LOG("Device doesn't support video streaming!\n");
		return -1;
	}

	memset(&fmt, 0, sizeof(struct v4l2_format));

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = cfg->width;
	fmt.fmt.pix.height = cfg->height;
	fmt.fmt.pix.pixelformat = cfg->fourcc;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;

	if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
		LOG("Unable to set video format!\n");
		return -1;
	}

	memset(&fps, 0, sizeof(struct v4l2_streamparm));

	fps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fps.parm.capture.timeperframe.numerator = 1;
	fps.parm.capture.timeperframe.denominator = cfg->fps;

	if (xioctl(fd, VIDIOC_S_PARM, &fps) < 0) {
		LOG("Unable tocmr_config_t set video FPS!\n");
		return -1;
	}

	if (xioctl(fd, VIDIOC_G_PARM, &fps) < 0) {
		LOG("Unable to get video FPS settings!\n");
		return -1;
	}

	if (xioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
		LOG("Unable to get video format settings!\n");
		return -1;
	}

	min = fmt.fmt.pix.width * 2;

	if (fmt.fmt.pix.bytesperline < min) {
		fmt.fmt.pix.bytesperline = min;
	}

	min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;

	if (fmt.fmt.pix.sizeimage < min) {
		fmt.fmt.pix.sizeimage = min;
	}

	memset(&req, 0, sizeof(struct v4l2_requestbuffers));

	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (xioctl(cfg->fd, VIDIOC_REQBUFS, &req) < 0) {
		if (EINVAL == errno) {
			return -1;
		} else {
			LOG("Unable to get video buffer count!\n");
			return -1;
		}
	}

	if ((cfg->buffers = calloc(req.count, sizeof(v4l2_buffer_t))) == NULL) {
		LOG("Unable to allocate buffers!\n");
		return -1;
	}

	for (cfg->buffer_count = 0; cfg->buffer_coun < req.count; cfg->buffer_count++) {
		memset(&buf, 0, sizeof(struct v4l2_buffer));

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = cfg->buffer_count;

		if (xioctl(cfg->fd, VIDIOC_QUERYBUF, &buf) < 0) {
			LOG("Unable to get video buffers!\n");
			return -1;
		}

		cfg->buffers[cfg->buffer_count].size = buf.length;
		cfg->buffers[cfg->buffer_count].ptr = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, cfg->fd, buf.m.offset);

		if (cfg->buffers[cfg->buffer_count].ptr == MAP_FAILED) {
			LOG("Unable to MMAP() video buffers!\n");
			return -1;
		}
	}

	for (i = 0; i < cfg->buffer_count; ++i) {
		memset(&buf, 0, sizeof(struct v4l2_buffer));

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (xioctl(cfg->fd, VIDIOC_QBUF, &buf) < 0) {
			LOG("Unable to get Frame!\n");
			return -1;
		}
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (xioctl(cfg->fd, VIDIOC_STREAMON, &type) < 0) {
		LOG("Unable to start streaming!\n");
		return -1;
	}

	cfg->handler.data = cfg;

	uv_poll_init(uv_default_loop(), &cfg->handler, cfg->fd);
	uv_poll_start(&cfg->handler, UV_READABLE, v4l2_on_frame);

	return 0;
}

static void closeCmr(cmr_config_t *cfg) {
	int i;

	enum v4l2_buf_type type;

	if (cfg->fd != -1) {
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (xioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
			LOG("Unable to stop stream!\n");
		}

		if (cfg->buffers != NULL) {
			if (cfg->buffer_count >= 0) {
				for (i = 0; i < cfg->buffer_count; i++) {
					if (munmap(cfg->buffers[i].ptr, cfg->buffers[i].size) < 0) {
						LOG("Unable to MUNMAP() buffers!\n");
					}
				}
			}

			free(cfg->buffers);
		}

		close(fd);
	}
}
