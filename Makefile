all:
	make -C libuv/ all
	gcc -o cmr-streamer main.c cmr-v4l2.c -Ilibuv/include/ -luv -Llibuv/ -lpthread --debug

clean:
	make -C libuv clean
	rm -f cmr-streamer
