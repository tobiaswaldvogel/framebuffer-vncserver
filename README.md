# framebuffer-vncserver

VNC server for Linux framebuffer devices.

[![Build Status](https://travis-ci.org/ponty/framebuffer-vncserver.svg?branch=master)](https://travis-ci.org/ponty/framebuffer-vncserver)

The goal is to check remote embedded Linux systems without X, so only the remote display is implemented. 
Partially keyboard support via uinput.

The code was optimized for best performance with low CPU load.
This fork uses directly the fb for libvncserver instead of creating an intermiediate buffer and doing bit shifting.


### build

Dependency:

	sudo apt-get install libvncserver-dev

There are 2 options: CMake or qmake

Using cmake:

	mkdir -p build && cd build
	cmake ..
	make
	
Using qmake:

	mkdir -p build && cd build
	qmake ../framebuffer-vncserver.pro
	make

 

### command-line help 

	# framebuffer-vncserver -h
	framebuffer-vncserver [-f device] [-p port] [-h]
	-p port: VNC port, default is 5900
	-f device: framebuffer device node, default is /dev/fb0
	-h : print this help
 
