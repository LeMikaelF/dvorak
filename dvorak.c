/*
 * Copyright 2018 Thomas Bocek
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

 /*
  * Why is this tool useful?
  * ========================
  *
  * Since I type with the "Dvorak" keyboard layout, the shortcuts such
  * as ctrl-c, ctrl-x, or ctrl-v are not comfortable anymore and one of them
  * require two hands to press.
  *
  * Furthermore, applications such as Intellij and Eclipse have their
  * shortcuts, which I'm used to. So for these shortcuts I prefer "Querty".
  * Since there is no way to configure this, I had to intercept the
  * keys and remap the keys from "Dvorak" to "Querty" once CTRL, ALT,
  * WIN or any of those combinations are pressed.
  *
  * With X.org I was reling on the wonderful tool from Kenton Varda,
  * which I modified a bit, to make it work when Numlock is active. Other
  * than that, it worked as expected.
  *
  * And then came Wayland. XGrabKey() works partially with some application
  * but not with others (e.g., gedit is not working). Since XGrabKey() is
  * an X.org function with some support in Wayland, I was looking for a more
  * stable solution. After a quick look to the repo https://github.com/kentonv/dvorak-qwerty
  * I saw that Kenton added a systemtap script to implement the mapping. This
  * scared me a bit to follow that path, so I implemented an other solution
  * based on /dev/uinput. The idea is to read /dev/input, grab keys with
  * EVIOCGRAB, create a virtual device that can emit the keys and pass
  * the keys from /dev/input to /dev/uinput. If CTRL/ALT/WIN is
  * pressed it will map the keys back to "Qwerty".
  *
  * Intallation
  * ===========
  *
  * make dvorak
  * //make sure your user belongs to the group "input" -> ls -la /dev/input
  * //this also applies for /dev/uinput -> https://github.com/tuomasjjrasanen/python-uinput/blob/master/udev-rules/40-uinput.rules
  * //start it in startup applications
  *
  * Related Links
  * =============
  * I used the following sites for inspiration:
  * https://www.kernel.org/doc/html/v4.12/input/uinput.html
  * https://www.linuxquestions.org/questions/programming-9/uinput-any-complete-example-4175524044/
  * https://stackoverflow.com/questions/20943322/accessing-keys-from-linux-input-device
  * https://gist.github.com/toinsson/7e9fdd3c908b3c3d3cd635321d19d44d
  *
  */


//Run with sudo ./dvorak /dev/input/by-path/platform-i8042-serio-0-event-kbd


#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <string.h>
#include <stdio.h>

static const char *const evval[3] = {
    "RELEASED",
    "PRESSED",
    "REPEATED"
};

int emit(int fd, int type, int code, int val)
{
   struct input_event ie;
   ie.type = type;
   ie.code = code;
   ie.value = val;
   /* timestamp values below are ignored */
   ie.time.tv_sec = 0;
   ie.time.tv_usec = 0;

   return write(fd, &ie, sizeof(ie));
}

//from: https://github.com/kentonv/dvorak-qwerty/tree/master/unix
static int modifier_bit(int key) {
  switch (key) {
    case 29: return 1;     // l-ctrl
    case 97: return 2;     // r-ctrl
    case 56: return 4;     // l-alt
    case 125: return 8;   // win
  }
  return 0;
}

//from: https://github.com/kentonv/dvorak-qwerty/tree/master/unix
static int qwerty2dvorak(int key) {
  switch (key) {

    //TODO No mapping for KEY_BACKSLASH
	// KEY_Q -> KEY_M
  case 16: return 50;
  // KEY_W -> KEY_RIGHTBRACE
  case 17: return 27;
  // KEY_E -> KEY_F
  case 18: return 33;
  // KEY_R -> KEY_L
  case 19: return 38;
  // KEY_T -> KEY_J
  case 20: return 36;
  // KEY_Y -> KEY_X
  case 21: return 45;
  // KEY_U -> KEY_S
  case 22: return 31;
  // KEY_I -> KEY_D
  case 23: return 32;
  // KEY_O -> KEY_R
  case 24: return 19;
  // KEY_P -> KEY_E
  case 25: return 18;
  // TODO No mappings for KEY_LEFTBRACE and KEY_RIGHTBRACE
  // KEY_A -> KEY_A
  case 30: return 30;
  // KEY_S -> KEY_K
  case 31: return 37;
  // KEY_D -> KEY_I
  case 32: return 23;
  // KEY_F -> KEY_SLASH
  case 33: return 53;
  // KEY_G -> KEY_COMMA
  case 34: return 51;
  // KEY_H -> KEY_DOT
  case 35: return 52;
  // KEY_J -> KEY_P
  case 36: return 25;
  // KEY_K -> KEY_B
  case 37: return 48;
  // KEY_L -> KEY_O
  case 38: return 24;
  // TODO No mappings for KEY_SEMICOLON and KEY_APOSTROPHE
  // KEY_Z -> KEY_LEFTBRACE
  case 44: return 26;
	// KEY_X -> KEY_C
	case 45: return 46;
	// KEY_C -> KEY_H
	case 46: return 35;
	// KEY_V -> KEY_U
	case 47: return 22;
  // KEY_B -> KEY_Q
  case 48: return 16;
  // KEY_N -> KEY_SEMICOLON
  case 49: return 39;
  // KEY_M -> KEY_APOSTROPHE
  case 50: return 40;
  // KEY_COMMA -> KEY_G
  case 51: return 34;
  // KEY_DOT -> KEY_V
  case 52: return 47;

}
  return key;
}

int main(int argc, char* argv[]) {

	setuid(0);

	if(argc < 2) {
		fprintf(stderr, "error: specify input device, e.g., found in "
		 "/dev/input/by-id/.\n");
        return EXIT_FAILURE;
	}

    struct input_event ev;
    ssize_t n;
    int fdi, fdo, i, mod_state, mod_current, array_counter, code;
    struct uinput_user_dev uidev;
    const char MAX_LENGTH = 32;
    int array[MAX_LENGTH];

    //init states
    mod_state = 0;
    array_counter = 0;
    for(i=0;i<MAX_LENGTH;i++) {
    	array[i] = 0;
    }

	//find the first input that exists
	for (i = 1; i < argc; i++) {
		fdi = open(argv[i], O_RDONLY);
		if (fdi != -1) {
			break;
		}
	}
	if (fdi == -1) {
		fprintf(stderr, "Cannot open any of the devices: %s.\n", strerror(errno));
		return EXIT_FAILURE;
	}

    fdo = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if(fdo == -1) {
		fprintf(stderr, "Cannot open /dev/uinput: %s.\n", strerror(errno));
        return EXIT_FAILURE;
	}

	//grab the key, from the input
	//https://unix.stackexchange.com/questions/126974/where-do-i-find-ioctl-eviocgrab-documented/126996

	//fix is implemented, will make it to ubuntu sometimes in 1.9.4
	//https://bugs.freedesktop.org/show_bug.cgi?id=101796
	//quick workaround, sleep for 200ms...
	usleep(200 * 1000);

	if(ioctl(fdi, EVIOCGRAB, 1) == -1){
		fprintf(stderr, "Cannot grab key: %s.\n", strerror(errno));
        return EXIT_FAILURE;
	}

	// Keyboard
	if (ioctl(fdo, UI_SET_EVBIT, EV_KEY) == -1) {
		fprintf(stderr, "Cannot set ev bits, key: %s.\n", strerror(errno));
        return EXIT_FAILURE;
	}
	if(ioctl(fdo, UI_SET_EVBIT, EV_SYN) == -1) {
		fprintf(stderr, "Cannot set ev bits, syn: %s.\n", strerror(errno));
        return EXIT_FAILURE;
	}
	if(ioctl(fdo, UI_SET_EVBIT, EV_MSC) == -1) {
		fprintf(stderr, "Cannot set ev bits, msc: %s.\n", strerror(errno));
        return EXIT_FAILURE;
	}

	// All keys
    for (i = 0; i < KEY_MAX; i++) {
        if (ioctl(fdo, UI_SET_KEYBIT, i) == -1) {
            fprintf(stderr, "Cannot set ev bits: %s.\n", strerror(errno));
			return EXIT_FAILURE;
		}
	}

	memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "Clavier virtuel bépo");
    uidev.id.bustype = BUS_USB;
    uidev.id.vendor  = 0x1234;
    uidev.id.product = 0x5678;
    uidev.id.version = 0;

    if (write(fdo, &uidev, sizeof(uidev)) == -1) {
        fprintf(stderr, "Cannot set device data: %s.\n", strerror(errno));
		return EXIT_FAILURE;
	}

    if (ioctl(fdo, UI_DEV_CREATE) == -1) {
		fprintf(stderr, "Cannot create device: %s.\n", strerror(errno));
		return EXIT_FAILURE;
	}

	//TODO: clear array

	while (1) {
        n = read(fdi, &ev, sizeof ev);
        if (n == (ssize_t)-1) {
            if (errno == EINTR) {
                continue;
			} else {
                break;
			}
        } else
        if (n != sizeof ev) {
            errno = EIO;
            break;
        }
		if (ev.type == EV_KEY && ev.value >= 0 && ev.value <= 2) {
			//printf("%s 0x%04x (%d), arr:%d\n", evval[ev.value], (int)ev.code, (int)ev.code, array_counter);
			//map the keys
			mod_current = modifier_bit(ev.code);
			if(mod_current > 0) {
				if(ev.value == 1) { //pressed
					mod_state |= mod_current;
				} else if(ev.value == 0) {//released
					mod_state &= ~mod_current;
				}
			}

			if(ev.code != qwerty2dvorak(ev.code) && (mod_state > 0 || array_counter > 0)) {
				code = ev.code;
				//printf("dvorak %d, %d\n", array_counter, mod_state);
				if(ev.value==1) { //pressed
					if(array_counter == MAX_LENGTH) {
						printf("warning, too many keys pressed: %d. %s 0x%04x (%d), arr:%d\n",
								MAX_LENGTH, evval[ev.value], (int)ev.code, (int)ev.code, array_counter);
						//skip dvorak mapping
					} else {
						array[array_counter] = ev.code + 1; //0 means not mapped
						array_counter++;
						code = qwerty2dvorak(ev.code); // dvorak mapping
					}
				} else if(ev.value==0) { //released
					//now we need to check if the code is in the array
					//if it is, then the pressed key was in dvorak mode and
					//we need to remove it from the array. The ctrl or alt
					//key does not need to be pressed, when a key is released.
					//A previous implementation only had a counter, which resulted
					//occasionally in stuck keys.
					for(i=0; i<array_counter; i++) {
						if(array[i] == ev.code + 1) {
							//found it, map it!
							array[i] = 0;
							code = qwerty2dvorak(ev.code); // dvorak mapping
						}
					}
					//cleanup array counter
					for(i=array_counter-1; i>=0; i--) {
						if(array[i] == 0) {
							array_counter--;
						} else {
							break;
						}
					}
				}
				emit(fdo, ev.type, code, ev.value);
			} else {
				//printf("non dvorak %d\n", array_counter);
				emit(fdo, ev.type, ev.code, ev.value);
			}
		} else {
			//printf("Not key: %d 0x%04x (%d)\n", ev.value, (int)ev.code, (int)ev.code);
			emit(fdo, ev.type, ev.code, ev.value);
		}
    }    fflush(stdout);
    fprintf(stderr, "%s.\n", strerror(errno));
    return EXIT_FAILURE;
}
