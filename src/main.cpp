#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <linux/input.h>
#include <linux/joystick.h>

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>

#define internal        static
#define local_persist   static
#define global_variable static

#define pixel(f, x, y) ((f)->buf[((y) * (f)->width) + (x)])
#define MAX_CONTROLLERS 4

struct Buffer {
    const char *title;
    int width;
    int height;
    uint32_t* buf;
    int keys[256]; /* keys are mostly ASCII, but arrows are 17..20 */
    int mod;       /* mod is 4 bits mask, ctrl=1, shift=2, alt=4, meta=8 */
    int x;
    int y;
    int mouse;
    /* For animation */
    int XOffset;
    int YOffset;
    /* Linux(X11) specifics */
    Display* dpy;
    Window w;
    GC gc;
    XImage *img;
};

typedef struct {
    short left_stick_x; // -32767 to 32767
    short left_stick_y;
    short right_stick_x;
    short right_stick_y;
    unsigned char left_trigger; // 0-255
    unsigned char right_trigger;
    unsigned short buttons; // bitmask A=0x0001, B=0x0002, etc. 0000_0000_0000_YXBA
    char dpad_x; // -1,0,1
    char dpad_y;
    uint32_t connected;
} XInputState;

global_variable int GLOBAL_JoyFDs[MAX_CONTROLLERS] = {-1, -1, -1, -1};
global_variable XInputState GLOBAL_JoyStates[MAX_CONTROLLERS];

/* from https://github.com/zserge/fenster/blob/main/fenster.h */
const global_variable int FENSTER_KEYCODES[124] = {XK_BackSpace,8,XK_Delete,127,XK_Down,18,XK_End,5,XK_Escape,27,XK_Home,2,XK_Insert,26,XK_Left,20,XK_Page_Down,4,XK_Page_Up,3,XK_Return,10,XK_Right,19,XK_Tab,9,XK_Up,17,XK_apostrophe,39,XK_backslash,92,XK_bracketleft,91,XK_bracketright,93,XK_comma,44,XK_equal,61,XK_grave,96,XK_minus,45,XK_period,46,XK_semicolon,59,XK_slash,47,XK_space,32,XK_a,65,XK_b,66,XK_c,67,XK_d,68,XK_e,69,XK_f,70,XK_g,71,XK_h,72,XK_i,73,XK_j,74,XK_k,75,XK_l,76,XK_m,77,XK_n,78,XK_o,79,XK_p,80,XK_q,81,XK_r,82,XK_s,83,XK_t,84,XK_u,85,XK_v,86,XK_w,87,XK_x,88,XK_y,89,XK_z,90,XK_0,48,XK_1,49,XK_2,50,XK_3,51,XK_4,52,XK_5,53,XK_6,54,XK_7,55,XK_8,56,XK_9,57};

internal void
MaDraw(Buffer* buffer)
{
    buffer->buf[100] = 0x00ff00;
    pixel(buffer, 64, 64) = 0xffff00;

    for (int Y = 0; Y < buffer->height; ++Y)
    {
        for (int X = 0; X < buffer->width; ++X)
        {
            pixel(buffer, X, Y) = 0;
        }
    }
}

internal void
RenderWeirdGradient_rw(Buffer* Window, int BlueOffset, int GreenOffset)
{
    for(int Y = 0; Y < Window->height; ++Y)
    {
        for(int X = 0; X < Window->width; ++X)
        {
            uint8_t Blue = (X);
            uint8_t Green = (Y);

            pixel(Window, X, Y) = ((Green << 8) | Blue);
        }
    }
}

/*
 * This algorithm uses the pixels on a memory buffer as a data structure and we
 * go on hoping with the pointers on each of them to make a cool, weird drawing
 * on memory.
*/
internal void
RenderWeirdGradient(Buffer* Window)
{
    uint8_t BytesPerPixel = 4;
    int Pitch = Window->width * BytesPerPixel; // size of the line (pitch)
    uint8_t* Memory = (uint8_t*)Window->buf;

    uint8_t* Row = Memory;  // points at first row
    for(int Y = 0; Y < Window->height; ++Y)
    {
        uint32_t* Pixel = (uint32_t*)Row; // points to the first pixen on the row
        for(int X = 0; X < Window->width; ++X)
        {
            uint8_t Blue = (X + Window->XOffset);
            uint8_t Green = (Y + Window->YOffset);

            // blue is in the big end, green in the next 8 bytes.
            // they'll be offset
            *Pixel++ = ((Green << 8) | Blue);  // Write & advance
        }
        Row += Pitch;  // Jump Row pointer to next row
    }
}

internal int
OpenWindow(struct Buffer* buffer)
{
    buffer->dpy = XOpenDisplay(NULL);
    int screen = DefaultScreen(buffer->dpy);
    buffer->w = XCreateSimpleWindow(buffer->dpy, RootWindow(buffer->dpy, screen), 0, 0,
                                 buffer->width, buffer->height, 0,
                                 BlackPixel(buffer->dpy, screen),
                                 WhitePixel(buffer->dpy, screen));
    buffer->gc = XCreateGC(buffer->dpy, buffer->w, 0, 0);
    XSelectInput(buffer->dpy, buffer->w,
                 ExposureMask | KeyPressMask | KeyReleaseMask |
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                 StructureNotifyMask);
    XStoreName(buffer->dpy, buffer->w, buffer->title);
    XMapWindow(buffer->dpy, buffer->w);

    // Enable window closing
    Atom wmDelete = XInternAtom(buffer->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(buffer->dpy, buffer->w, &wmDelete, 1);

    buffer->img = XCreateImage(buffer->dpy, DefaultVisual(buffer->dpy, 0), 24, ZPixmap,
                            0, (char *)buffer->buf,
                            buffer->width, buffer->height,
                            32, 0);
    return 0;
}

internal void
Sleeper(int64_t ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

internal int64_t
GetYerTime(void)
{
    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    return time.tv_sec * 1000 + (time.tv_nsec / 1000000);
}

// Opens a joystick device if it exists
int joy_open(int index)
{
    char path[32];
    snprintf(path, sizeof(path), "/dev/input/js%d", index);

    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
    {
        return -1;  // not present or no permission
    }

    // Optional: get info to confirm it's really a gamepad
    char name[256] = "Unknown";
    char axes = 0, buttons = 0;
    ioctl(fd, JSIOCGNAME(sizeof(name)), name);
    ioctl(fd, JSIOCGAXES, &axes);
    ioctl(fd, JSIOCGBUTTONS, &buttons);

    printf("Controller %d connected: %s (%d axes, %d buttons)\n", index, name, axes, buttons);

    return fd;
}

void
JoyClose()
{
    for (int i = 0; i < MAX_CONTROLLERS; i++)
    {
        if (GLOBAL_JoyFDs[i] >= 0)
        {
            close(GLOBAL_JoyFDs[i]);
        }
    }
}

// Call this once at startup
void
JoyInit()
{
    for (int i = 0; i < MAX_CONTROLLERS; i++)
    {
        GLOBAL_JoyFDs[i] = joy_open(i);
        GLOBAL_JoyStates[i].connected = (GLOBAL_JoyFDs[i] >= 0);
    }
}

void
JoySetStates()
{
    for (int i = 0; i < MAX_CONTROLLERS; i++)
    {
        if (GLOBAL_JoyFDs[i] < 0)
        {
            continue; // skip loop if not connected
        }

        struct js_event ev;
        XInputState* state = &GLOBAL_JoyStates[i];

        // Reset per-frame changes if needed (we'll rebuild)
        // But we just accumulate latest values
        while (read(GLOBAL_JoyFDs[i], &ev, sizeof(ev)) == sizeof(ev))
        {
            if (ev.type & JS_EVENT_BUTTON)
            {
                if (ev.value)
                {
                    state->buttons |=  (1 << ev.number); // pressed
                }
                else
                {
                    state->buttons &= ~(1 << ev.number); // released
                }
            }
            else if (ev.type & JS_EVENT_AXIS)
            {
                switch (ev.number)
                {
                    case 0:
                    {
                        state->left_stick_x  = ev.value;
                    } break;
                    case 1:
                    {
                        state->left_stick_y  = ev.value;
                    } break;
                    case 2:
                    {
                        state->left_trigger  = (ev.value + 32767) / 257; // -32767..32767 → 0..255
                    } break;
                    case 3:
                    {
                        state->right_stick_x = ev.value;
                    } break;
                    case 4:
                    {
                        state->right_stick_y = ev.value;
                    } break;
                    case 5:
                    {
                        state->right_trigger = (ev.value + 32767) / 257;
                    } break;
                    case 6:
                    {
                        state->dpad_x = (ev.value < -10000) ? -1 : (ev.value > 10000 ? 1 : 0);
                    } break;
                    case 7:
                    {
                        state->dpad_y = (ev.value < -10000) ? -1 : (ev.value > 10000 ? 1 : 0);
                    } break;
                }
            }
        }
    }
}

void
JoyHotplug()
{
    for (int i = 0; i < MAX_CONTROLLERS; i++)
    {
        if (GLOBAL_JoyFDs[i] < 0)
        {
            GLOBAL_JoyFDs[i] = joy_open(i);
            if (GLOBAL_JoyFDs[i] >= 0)
            {
                GLOBAL_JoyStates[i].connected = 1;
            }
        }
    }
}

internal int
HandleInput(Buffer* b)
{
    int has_keys = 0;
    char string[32];
    char* p = string;

    // let's check if any key press was stored in the buffer
    for (int i = 0; i < 128; i++)
    {
        if (b->keys[i])
        {
            has_keys = 1;
            *p++ = i;
        }
    }
    *p = '\0';

    // fenster_text(&buffer, 8, 8, string, 4, 0xffffff);
    if (has_keys)
    {
        // so to remember:
        // mod is a 4 bits mask, ctrl=1, shift=2, alt=4, meta=8
        if (b->mod & 1)
        {
            // fenster_text(&buffer, 8, 40, "Ctrl", 4, 0xffffff);
        }
        if (b->mod & 2)
        {
            // fenster_text(&buffer, 8, 80, "Shift", 4, 0xffffff);
        }
    }
    if (b->keys[27]) // XK_Escape
    {
        return 1;
    }
    if (b->keys[17]) // XK_Up
    {
        b->YOffset+=2;
    }
    if (b->keys[18]) // XK_Down
    {
        b->YOffset-=2;
    }
    if (b->keys[19]) // XK_Right
    {
        b->XOffset-=2;
    }
    if (b->keys[20]) // XK_Left
    {
        b->XOffset+=2;
    }
    return (0);
}

internal int
HandleLoop(struct Buffer* buffer)
{
    uint8_t shouldQuit = 0;
    XEvent ev;
    // NOTE: This is where the buffer appears!
    XPutImage(buffer->dpy, buffer->w, buffer->gc, buffer->img, 0, 0, 0, 0,
              buffer->width, buffer->height);

    while (XPending(buffer->dpy))
    {
        XNextEvent(buffer->dpy, &ev);
        switch (ev.type)
        {
            case ButtonPress:
            {
            } break;
            case ButtonRelease:
            {
                buffer->mouse = (ev.type == ButtonPress);
            } break;
            case MotionNotify:
            {
                buffer->x = ev.xmotion.x, buffer->y = ev.xmotion.y;
            } break;
            case KeyPress:
            case KeyRelease:
            {
                int m = ev.xkey.state;
                int k = XkbKeycodeToKeysym(buffer->dpy, ev.xkey.keycode, 0, 0);
                for (uint32_t i = 0; i < 124; i += 2)
                {
                    if (FENSTER_KEYCODES[i] == k)
                    {
                        buffer->keys[FENSTER_KEYCODES[i + 1]] = (ev.type == KeyPress);
                        break;
                    }
                }
                buffer->mod = (!!(m & ControlMask)) | (!!(m & ShiftMask) << 1) |
                    (!!(m & Mod1Mask) << 2) | (!!(m & Mod4Mask) << 3);
            } break;
            case ConfigureNotify:  // Window resizing
            {
                XConfigureEvent xce = ev.xconfigure;
                if (xce.width != buffer->width || xce.height != buffer->height)
                {
                    // Resize the buffer
                    buffer->width = xce.width;
                    buffer->height = xce.height;

                    // Free the old XImage structure (not the data)
                    buffer->img->data = NULL;  // Prevent XLib from freeing our buffer
                    XFree(buffer->img);

                    buffer->buf = (uint32_t*)realloc(buffer->buf, buffer->width * buffer->height * sizeof(uint32_t));

                    // Create completely new XImage
                    buffer->img = XCreateImage(buffer->dpy,
                                               DefaultVisual(buffer->dpy, 0),
                                               24, ZPixmap, 0,
                                               (char *)buffer->buf,
                                               buffer->width, buffer->height,
                                               32, 0);
                }
            } break;
            case ClientMessage:
            {
                // Window close button
                if (ev.xclient.data.l[0] ==
                        (long)XInternAtom(buffer->dpy, "WM_DELETE_WINDOW", False))
                {
                    shouldQuit = 1;
                }
            } break;
        }
    }
    return shouldQuit;
}

int
main(int argc, char *argv[])
{
    int W = 600, H = 480;
    uint32_t *buf = (uint32_t*)malloc(W * H * sizeof(uint32_t));
    struct Buffer buffer = {
        .title = "hello",
        .width = W,
        .height = H,
        .buf = buf,
    };

    OpenWindow(&buffer);
    JoyInit();

    int64_t now = GetYerTime();
    while(HandleLoop(&buffer) == 0 && HandleInput(&buffer) == 0)
    {
        RenderWeirdGradient(&buffer);

        JoyHotplug();
        JoySetStates();
        if(GLOBAL_JoyStates->dpad_x > 0) buffer.XOffset-=2;
        else if(GLOBAL_JoyStates->dpad_x < 0) buffer.XOffset+=2;
        if(GLOBAL_JoyStates->dpad_y > 0) buffer.YOffset-=2;
        else if(GLOBAL_JoyStates->dpad_y < 0) buffer.YOffset+=2;

        int64_t time = GetYerTime();
        if (time - now < 1000 / 60)
        {
            Sleeper(time - now);
        }
        now = time;
    }

    JoyClose();
    XCloseDisplay(buffer.dpy);

    return 0;
}
