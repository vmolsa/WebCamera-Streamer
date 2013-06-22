v4l2-streamer
=============

Simple WebCamera streamer to network.
I used Logitech HD Pro Webcam C920 for this project cause it can capture full-hd h264 video on the hardware
So all "hard" work is done by camera itself so this software just "copy-paste" data to network which needs to resample on other end. 

### Build:
    
    git clone https://github.com/vmolsa/v4l2-streamer.git
    cd v4l2-streamer
    git clone https://github.com/joyent/libuv.git
    make

