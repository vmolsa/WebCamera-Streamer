all:
	make -C libuv/ all
	gcc -o v4l2-streamer v4l2.c -Ilibuv/include/ -luv -Llibuv/ -lpthread --debug

clean:
	make -C libuv clean
	rm -f v4l2-streamer
