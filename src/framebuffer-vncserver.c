/*
 * $Id$
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * This project is an adaptation of the original fbvncserver for the iPAQ
 * and Zaurus.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <sys/stat.h>
#include <sys/sysmacros.h>             /* For makedev() */

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/uinput.h>

#include <assert.h>
#include <errno.h>

/* libvncserver */
#include "rfb/rfb.h"
#include "rfb/keysym.h"

typedef struct _XKEY_TO_KEY {
	int	xkey;
	int vkey;
} XKEY_TO_KEY;

static const XKEY_TO_KEY keymap[] = {
	{ ' '		, KEY_SPACE },
	{ '0'		, KEY_0 },
	{ '1'		, KEY_1 },
	{ '2'		, KEY_2 },
	{ '3'		, KEY_3 },
	{ '4'		, KEY_4 },
	{ '5'		, KEY_5 },
	{ '6'		, KEY_6 },
	{ '7'		, KEY_7 },
	{ '8'		, KEY_8 },
	{ '9'		, KEY_9 },
	{ 'A'		, KEY_A },
	{ 'B'		, KEY_B },
	{ 'C'		, KEY_C },
	{ 'D'		, KEY_D },
	{ 'E'	, KEY_E },
	{ 'F'	, KEY_F },
	{ 'G'	, KEY_G },
	{ 'H'	, KEY_H },
	{ 'I'	, KEY_I },
	{ 'J'	, KEY_J },
	{ 'K'	, KEY_K },
	{ 'L'	, KEY_L },
	{ 'M'	, KEY_M },
	{ 'N'	, KEY_N },
	{ 'O'	, KEY_O },
	{ 'P'	, KEY_P },
	{ 'Q'	, KEY_Q },
	{ 'R'	, KEY_R },
	{ 'S'	, KEY_S },
	{ 'T'	, KEY_T },
	{ 'U'	, KEY_U },
	{ 'V'	, KEY_V },
	{ 'W'	, KEY_W },
	{ 'X'	, KEY_X },
	{ 'Y'	, KEY_Y },
	{ 'Z'	, KEY_Z },
	{ XK_Up		, KEY_UP },
	{ XK_Down	, KEY_DOWN },
	{ XK_Left	, KEY_LEFT },
	{ XK_Right	, KEY_RIGHT },
	{ XK_Page_Up	, KEY_PAGEUP},
	{ XK_Page_Down	, KEY_PAGEDOWN },
	{ XK_Return	, KEY_ENTER },
	{ XK_Escape	, KEY_ESC},
	{ XK_Menu	, KEY_MENU },
	{ XK_BackSpace, KEY_BACKSPACE },
	{ XK_Shift_L, KEY_LEFTSHIFT },
	{ XK_Shift_R, KEY_RIGHTSHIFT }
};

static char fb_device[256] = "/dev/fb0";
static struct fb_var_screeninfo scrinfo;
static int fbfd = -1;
static void *fbmmap = MAP_FAILED;
static size_t fbsize;
static size_t fblinesize;
static void *fbbuf;

static suseconds_t refresh = 250000;
static int vnc_port = 5900;
static rfbScreenInfoPtr screen;

static int uifd = -1;
static char hostname[128];

static int line_skip_level = 3;
static int line_skip_mask;
static int line_skip_offset;
static int line_single_offset;
static int line_skip_counter;

static int key_shift_state		= 0;
static int key_control_state	= 0;

static int translate_key(int xkey)
{
	const XKEY_TO_KEY *k, *end;
	
	if (xkey >= 'a' && xkey <= 'z')
		xkey -= 'a' - 'A';

	end = sizeof(keymap) / sizeof(keymap[0]) + keymap;
	for (k = keymap; k < end; k++)
		if (k->xkey == xkey)
			return k->vkey;

	return 0;
}

static void enable_uinput_keys(int fd)
{
	const XKEY_TO_KEY *k, *end;
	
	end = sizeof(keymap) / sizeof(keymap[0]) + keymap;
	for (k = keymap; k < end; k++)
		ioctl(uifd, UI_SET_KEYBIT, k->vkey);
}

static void keyevent(rfbBool down,rfbKeySym xkey,rfbClientPtr cl)
{
	int	vkey;

fprintf(stderr, "key %d %02x, down %d\n", xkey, xkey, down);

	if (xkey == XK_Shift_L || xkey == XK_Shift_R)
		key_shift_state = down;
		

	vkey = translate_key(xkey);
	if (!vkey)
		return;

	struct input_event ie;

	/* timestamp values below are ignored */
	ie.time.tv_sec = 0;
	ie.time.tv_usec = 0;

	ie.type = EV_KEY;
	ie.code = vkey;
	ie.value = down ? 1 : 0;
	write(uifd, &ie, sizeof(ie));

	ie.type = EV_SYN;
	ie.code = SYN_REPORT;
	ie.value = 0;
	write(uifd, &ie, sizeof(ie));
}

static void init_fb_server(int argc, char **argv)
{
    fprintf(stderr, "Initializing server...\n");

    if ((fbfd = open(fb_device, O_RDONLY)) == -1) {
        fprintf(stderr, "cannot open fb device %s\n", fb_device);
        exit(EXIT_FAILURE);
    }

    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &scrinfo) != 0) {
        fprintf(stderr, "ioctl error\n");
        exit(EXIT_FAILURE);
    }

	fbsize = scrinfo.xres * scrinfo.yres * scrinfo.bits_per_pixel>>3;
	fbmmap = mmap(NULL, fbsize, PROT_READ, MAP_SHARED, fbfd, 0);

    if (fbmmap == MAP_FAILED) {
        fprintf(stderr, "mmap failed\n");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "	width:  %d\n", (int)scrinfo.xres);
    fprintf(stderr, "	height: %d\n", (int)scrinfo.yres);
    fprintf(stderr, "	bpp:    %d\n", (int)scrinfo.bits_per_pixel);
    fprintf(stderr, "	port:   %d\n", (int)vnc_port);

    fbbuf = calloc(fbsize, 1);
    assert(fbbuf != NULL);
	memcpy(fbbuf, fbmmap, fbsize);

    screen = rfbGetScreen(&argc, argv, scrinfo.xres, scrinfo.yres, scrinfo.red.length, 
							scrinfo.bits_per_pixel / scrinfo.red.length, scrinfo.bits_per_pixel>>3);
    assert(screen != NULL);
    
	rfbPixelFormat* format = &screen->serverFormat;

	format->redShift   = scrinfo.red.offset;
	format->greenShift = scrinfo.green.offset;
	format->blueShift  = scrinfo.blue.offset;
	format->redMax     = (1 << scrinfo.red.length)   - 1;
	format->greenMax   = (1 << scrinfo.green.length) - 1;
	format->blueMax    = (1 << scrinfo.blue.length)  - 1;
    
	gethostname(hostname, sizeof(hostname));
	screen->desktopName = hostname;
    screen->frameBuffer = fbmmap;
    screen->alwaysShared = TRUE;
    screen->httpDir = NULL;
    screen->port = vnc_port;
    screen->kbdAddEvent = keyevent;

    rfbInitServer(screen);
    rfbMarkRectAsModified(screen, 0, 0, scrinfo.xres, scrinfo.yres);

	line_skip_counter = 0;
	line_skip_mask = (1 << line_skip_level) - 1;
	line_single_offset = (fbsize / scrinfo.yres) >> 2;
	line_skip_offset = line_single_offset * line_skip_mask;
}

/*****************************************************************************/
static void update_screen(void)
{
    int	min_x = 9999, max_x = -1;
    int	min_y = 9999, max_y = -1;
	int	change_detected;	

    uint32_t *f = (uint32_t*)(fbsize + (uint8_t*)fbmmap);	/* -> framebuffer         */
    uint32_t *c = (uint32_t*)(fbsize + (uint8_t*)fbbuf);	/* -> compare framebuffer */

    int xstep = sizeof(*f) / (scrinfo.bits_per_pixel>>3);

    int x, y, y_prev;
	uint32_t pixel;

    for (y = y_prev = (int)scrinfo.yres; y--;) {
		change_detected = 0;
		
		if (y == min_y) {
			f -= fblinesize;
			c -= fblinesize;
			if (y)
				--y;
			continue;
		}
		
        /* Compare every 1/2/4 pixels at a time */
        for (x = (int)scrinfo.xres; x--; ) {
            pixel = *(--f);

            if (pixel != *(--c)) {
				*c = pixel;
				change_detected = 1;

				if (max_x < x)
					max_x = x;
				if (min_x > x)
					min_x = x;
			}
        }

		if (change_detected) {
			if (max_y < y)
				max_y = y;
			if (min_y > y)
				min_y = y;

			if (y_prev - 1 != y) {
				f += line_skip_offset;
				c += line_skip_offset;
				y += line_skip_mask;
			}
			y_prev = y;
		} else if ((y & line_skip_mask) == (line_skip_counter & line_skip_mask)) {
			y_prev = y;

			f -= line_skip_offset;
			c -= line_skip_offset;
			y -= line_skip_mask;
			if (y < 0)
				y = 0;
		} else
			y_prev = y;
	}

    if (min_x < 9999) {
        fprintf(stderr, "Dirty page: %dx%d+%d+%d...\n",
                (max_x+2) - min_x, (max_y+1) - min_y,
                min_x, min_y);

        rfbMarkRectAsModified(screen, min_x, min_y,
                              max_x + 2, max_y + 1);

        rfbProcessEvents(screen, 10000);
    }
	--line_skip_counter;
}

/*****************************************************************************/

void print_usage(char **argv)
{
    fprintf(stderr, "%s [-f device] [-p port] [-h]\n"
                    "-p port: VNC port, default is 5900\n"
                    "-f device: framebuffer device node, default is /dev/fb0\n"
                    "-h : print this help\n"
            , *argv);
}

int main(int argc, char **argv)
{
    if(argc > 1)
    {
        int i=1;
        while(i < argc)
        {
            if(*argv[i] == '-')
            {
                switch(*(argv[i] + 1))
                {
                case 'h':
                    print_usage(argv);
                    exit(0);
                    break;
                case 'f':
                    i++;
                    strcpy(fb_device, argv[i]);
                    break;
                case 'p':
                    i++;
                    vnc_port = atoi(argv[i]);
                    break;
                }
            }
            i++;
        }
    }

    init_fb_server(argc, argv);

	struct uinput_user_dev uud;

	uifd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	ioctl(uifd, UI_SET_EVBIT, EV_KEY);
	ioctl(uifd, UI_SET_EVBIT, EV_SYN);
	enable_uinput_keys(uifd);

	memset(&uud, 0, sizeof(uud));
	snprintf(uud.name, UINPUT_MAX_NAME_SIZE, "framebuffer-vnc input device");
	write(uifd, &uud, sizeof(uud));
	ioctl(uifd, UI_DEV_CREATE);

    struct timeval now={0,0}, next={0,0};
	suseconds_t	remaining_time;

    /* Implement our own event loop to detect changes in the framebuffer. */
    while (1)
    {
        while (screen->clientHead == NULL)
            rfbProcessEvents(screen, 100000);

		gettimeofday(&now, NULL);
		if (now.tv_sec > next.tv_sec ||
			(now.tv_sec == next.tv_sec && now.tv_usec > next.tv_usec)) {

			next.tv_sec  = now.tv_sec;
			next.tv_usec = now.tv_usec + refresh;
			if (next.tv_usec > 1000000) {
				next.tv_usec -= 1000000;
				next.tv_sec++;
			}

			update_screen();
			gettimeofday(&now, NULL);
		}

		remaining_time  = next.tv_sec - now.tv_sec;
		remaining_time *= 1000000;
		remaining_time += next.tv_usec;
		remaining_time -= now.tv_usec;

        rfbProcessEvents(screen, remaining_time);
    }

    fprintf(stderr, "Cleaning up...\n");

	ioctl(uifd, UI_DEV_DESTROY);
	close(uifd);

    if(fbfd != -1)
        close(fbfd);
}
