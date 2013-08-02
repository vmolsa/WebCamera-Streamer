all:	
	gcc -o cmr-streamer main.c cmr-v4l2.c -luv -lpthread

clean:
	rm -f cmr-streamer
