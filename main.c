#include "config.h"

#define DEV "/dev/video0"

static int _doexit = 0;

static cmr_config_t cfg;
static struct sockaddr_in addr;

static uv_buf_t buf;
static uv_timer_t timer;
static uv_write_t write_req;
static uv_tcp_t tcp;
static uv_connect_t con_req;

static void startTimer();

static void tcp_close_cb(uv_handle_t* handle) {
	LOG("Disconnected!\n");
}

static void con_close_cb(uv_handle_t* handle) {
	closeCmr(&cfg);

	uv_close((uv_handle_t*) tcp, close_cb);

	if (!_doexit) {
		startTimer();
	}
}

static void doexit(uv_signal_t* handler, int signum) {
	if (signum == SIGINT) {
		_doexit = 1;
		LOG("\n\nGot Signal!\n\n");

		uv_signal_stop(handler);
		uv_stop(uv_default_loop());
	}
}

static void write_cb(uv_write_t* req, int status) {
	if (status < 0) {
		LOG("Unable write to socket!\n");
		uv_close((uv_handle_t*) con_req, close_cb);
	}
}

static int on_frame(void *arg, void *ptr, size_t size) {
	buf = uv_buf_init(ptr, size);

	return uv_write(&write_req, (uv_stream_t*) &tcp, &buf, 1, on_write);
}

static void on_connect(uv_connect_t* req, int status) {
	if (status < 0) {
		LOG("Unable to connect!\n");
		uv_close((uv_handle_t*) req->handle, con_close_cb);

		startTimer();
	}

	else {
		LOG("Connected!\n");
		if (openCmr(&cfg) < 0) {
			uv_stop(uv_default_loop());
		}
	}
}

static void timer_close_cb(uv_handle_t* handle) {
	LOG("Timer stopped!\n");
}

static void on_timer(uv_timer_t* handle, int status) {
	LOG("Connecting...\n");
	uv_tcp_init(uv_default_loop(), &tcp);
	uv_tcp_connect(&con_req, &tcp, addr, on_connect);
	uv_close((uv_handle_t*) timer, timer_close_cb);
}

static void startTimer() {
	LOG("Starting Timer\n");

	uv_timer_init(uv_default_loop(), &timer);
	uv_timer_start(timer, on_timer, 1000, 0);
}

int main(int argc, char **argv) {
	uv_signal_t sighandler;
	uv_signal_init(uv_default_loop(), &sighandler);
	uv_signal_start(&sighandler, doexit, SIGINT);
	
//	setCmrSettings(&cfg, DEV, 1920, 1080, FOURCC('Y', 'U', '1', '2'), 24);
//	setCmrSettings(&cfg, DEV, 1920, 1080, FOURCC('Y', 'U', '1', '2'), 30);

//	setCmrSettings(&cfg, DEV, 1920, 1080, FOURCC('M', 'J', 'P', 'G'), 24);
//	setCmrSettings(&cfg, DEV, 1920, 1080, FOURCC('M', 'J', 'P', 'G'), 30);

//	setCmrSettings(&cfg, DEV, 1920, 1080, FOURCC('J', 'P', 'E', 'G'), 24);
//	setCmrSettings(&cfg, DEV, 1920, 1080, FOURCC('J', 'P', 'E', 'G'), 30);

//	setCmrSettings(&cfg, DEV, 1920, 1080, FOURCC('Y', 'U', 'Y', 'V'), 24);
//	setCmrSettings(&cfg, DEV, 1920, 1080, FOURCC('Y', 'U', 'Y', 'V'), 30);

//	setCmrSettings(&cfg, DEV, 1920, 1080, FOURCC('H', '2', '6', '4'), 24);
	setCmrSettings(&cfg, DEV, 1920, 1080, FOURCC('H', '2', '6', '4'), 30);

	setCmrCb(&cfg, on_frame, NULL);

	addr = uv_ip4_addr("192.168.1.21", 8000);

	startTimer();

	uv_run(uv_default_loop(), UV_RUN_DEFAULT);

	return 0;
}
