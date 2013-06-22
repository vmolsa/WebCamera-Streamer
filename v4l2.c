#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <signal.h>
#include <assert.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <linux/videodev2.h>

#include <uv.h>

#define DEV "/dev/video0"

struct buffer {
	int fd;
	void *start;
	size_t length;
};

static void errno_exit(const char *s) {
	fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
	exit(EXIT_FAILURE);
}

static int xioctl(int fh, int request, void *arg) {
	int r;

	do {
		r = ioctl(fh, request, arg);
	} while (-1 == r && EINTR == errno);

	return r;
}

typedef struct _magic_header_t {
	char magic[4];
	unsigned int size;
} magic_header_t;

static magic_header_t header = { "V4L2", 0 };
static float fps = 0;
static unsigned int counter = 0;
static struct timespec start, cur, diff;
static int looprun = 1;

void handleFrame(void *ptr, size_t size) {
	clock_gettime(CLOCK_MONOTONIC, &cur);

	int timems = 0;
	counter++;

	if ((cur.tv_sec - start.tv_sec) < 0) {
		diff.tv_sec = cur.tv_sec - start.tv_sec - 1;
		diff.tv_nsec = 1000000000 + cur.tv_nsec - start.tv_nsec;
	}

	else {
		diff.tv_sec = cur.tv_sec - start.tv_sec;
		diff.tv_nsec = cur.tv_nsec - start.tv_nsec;
	}

	timems = ((diff.tv_sec * 1000) + ((diff.tv_nsec / 1000000) & 1000));

	if (timems >= 1000) {
		fps = ((counter * 1000) / timems);
		start.tv_sec = cur.tv_sec;
		start.tv_nsec = cur.tv_nsec;
		counter = 0;
	}

	fprintf(stderr, "\r          Got frame! (size=%zu, fps=%.2f)                    \r", size, fps);
}

static void on_close(uv_handle_t* handle) {

}

static void on_send(uv_udp_send_t* req, int status) {
	if (status != 0) {
		fprintf(stderr, "Unable to send socket!\n");
		uv_close((uv_handle_t*) req->handle, on_close);
	}
}

static void doexit(uv_signal_t* handler, int signum) {
	if (signum == SIGINT) {
		fprintf(stderr, "\n\nGot Signal!\n\n");
		uv_signal_stop(handler);
		uv_stop(uv_default_loop());
	}
}

struct handler_t {
	int fd;
	uv_udp_t *sd;
	int count;
	uv_udp_send_t req;
	void *mem;
	unsigned int msize;
	struct sockaddr_in addr;
	struct buffer *buffers;
};

static void sendBuf(uv_udp_send_t *req, uv_udp_t *handler, struct sockaddr_in addr, void *mem, unsigned int msize, void *ptr, unsigned int psize) {
	unsigned int offset = 0, segment = 0;
	uv_buf_t buffer;

	while (offset < psize) {
		segment = ((psize - offset) % msize);
		memcpy(mem, (ptr + offset), segment);
		buffer = uv_buf_init((char *) mem, segment);

		if (uv_udp_send(req, handler, &buffer, 1, addr, on_send) < 0) {
                        uv_stop(uv_default_loop());
                        break;
                }

		offset += segment;
	}
}

static void on_frame(uv_poll_t *handler, int status, int events) {
	struct handler_t *data = (struct handler_t *) handler->data;
	struct v4l2_buffer buf;

	ssize_t offset = 0, size = 0;

	if (events == UV_READABLE) {
		memset(&buf, 0, sizeof(struct v4l2_buffer));

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;

		if (xioctl(data->fd, VIDIOC_DQBUF, &buf) < 0) {
			switch (errno) {
				case EAGAIN:
					break;
				case EIO:
				default:
					errno_exit("VIDIOC_DQBUF");
			}
		}

		assert(buf.index < data->count);
		handleFrame(data->buffers[buf.index].start, buf.bytesused);

		header.size = buf.bytesused;

		sendBuf(&data->req, data->sd, data->addr, data->mem, data->msize, &header, sizeof(magic_header_t));
		sendBuf(&data->req, data->sd, data->addr, data->mem, data->msize, data->buffers[buf.index].start, buf.bytesused);

                if (xioctl(data->fd, VIDIOC_QBUF, &buf) < 0) {
                        errno_exit("VIDIOC_QBUF");
		}
	}
}

int main(int argc, char **argv) {
	int fd = -1, ret = -1;
	int count;
	struct v4l2_requestbuffers req;
	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_streamparm fps;
	struct v4l2_crop crop;
	struct v4l2_format fmt;
	struct v4l2_buffer buf;
	enum v4l2_buf_type type;
	struct buffer *buffers;

	unsigned int i;
	unsigned int min;

	if ((fd = open(DEV, O_RDWR, 0)) < 0) {
		fprintf(stderr, "Cannot open %s\n", DEV);
		exit(EXIT_FAILURE);
	}

	memset(&cap, 0, sizeof(struct v4l2_capability));

	if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
		if (EINVAL == errno) {
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_QUERYCAP");
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		exit(EXIT_FAILURE);
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		exit(EXIT_FAILURE);
	}

	memset(&fmt, 0, sizeof(struct v4l2_format));

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = 1920;
	fmt.fmt.pix.height = 1080;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;

	if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
		errno_exit("VIDIOC_S_FMT");
	}

	memset(&fps, 0, sizeof(struct v4l2_streamparm));

	fps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fps.parm.capture.timeperframe.numerator = 1;
	fps.parm.capture.timeperframe.denominator = 24;

	if (xioctl(fd, VIDIOC_S_PARM, &fps) < 0) {
		errno_exit("VIDIOC_S_PARM");
	}

	if (xioctl(fd, VIDIOC_G_PARM, &fps) < 0) {
		errno_exit("VIDIOC_G_PARM");
	}

	if (xioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
		errno_exit("VIDIOC_G_FMT");
	}

	if (fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_H264) {
		fprintf(stderr, "Width: %d Height: %d PixelFormat: H264 FPS: %u\n", fmt.fmt.pix.width, fmt.fmt.pix.height, fps.parm.capture.timeperframe.denominator);
	}

	else {
		fprintf(stderr, "Unknow format!");
		exit(0);
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

	req.count = 10;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
		if (EINVAL == errno) {
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_REQBUFS");
		}
	}

	if ((buffers = calloc(req.count, sizeof(*buffers))) == NULL) {
		exit(EXIT_FAILURE);
	}

	for (count = 0; count < req.count; count++) {
		memset(&buf, 0, sizeof(struct v4l2_buffer));

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = count;

		if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
			errno_exit("VIDIOC_QUERYBUF");
		}

		buffers[count].length = buf.length;
		buffers[count].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);

		if (MAP_FAILED == buffers[count].start) {
			errno_exit("mmap");
		}
	}

	for (i = 0; i < count; ++i) {
		memset(&buf, 0, sizeof(struct v4l2_buffer));

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
			errno_exit("VIDIOC_QBUF");
		}
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
		errno_exit("VIDIOC_STREAMON");
	}

	clock_gettime(CLOCK_MONOTONIC, &start);

	uv_poll_t *handler = malloc(sizeof(uv_poll_t));
	struct handler_t *data = malloc(sizeof(struct handler_t));

	static uv_udp_t sd;
	struct sockaddr_in addr = uv_ip4_addr("225.0.0.37", 8000);

	uv_udp_init(uv_default_loop(), &sd);
	uv_udp_bind(&sd, uv_ip4_addr("0.0.0.0", 0), 0);
	uv_udp_set_multicast_ttl(&sd, 1);

	data->mem = malloc(1024);
	data->msize = 1024;

	data->fd = fd;
	data->sd = &sd;
	data->count = count;
	data->addr = addr;
	data->buffers = buffers;
	handler->data = data;

	uv_poll_init(uv_default_loop(), handler, fd);
	uv_poll_start(handler, UV_READABLE, on_frame);

	uv_signal_t sighandler;
	uv_signal_init(uv_default_loop(), &sighandler);
	uv_signal_start(&sighandler, doexit, SIGINT);

	uv_run(uv_default_loop(), UV_RUN_DEFAULT);

	uv_close((uv_handle_t*) data->req.handle, on_close);

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (xioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
		errno_exit("VIDIOC_STREAMOFF");
	}

	free(data->mem);
	free(buffers[count].start);

	for (i = 0; i < count; ++i) {
		if (munmap(buffers[i].start, buffers[i].length) < 0) {
			errno_exit("munmap");
		}
	}

	free(buffers);
	free(data);
	free(handler);
	close(fd);

	return 0;
}
