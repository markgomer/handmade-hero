#include <X11/Xlib.h>
#include <stdint.h>
#include <stdlib.h>

#define internal        static
#define local_persist   static
#define global_variable static

#define pixel(f, x, y) ((f)->buf[((y) * (f)->width) + (x)])

struct Buffer {
    const char *title;
    int width;
    int height;
    uint32_t *buf;
    int keys[256]; /* keys are mostly ASCII, but arrows are 17..20 */
    int mod;       /* mod is 4 bits mask, ctrl=1, shift=2, alt=4, meta=8 */
    int x;
    int y;
    int mouse;
    /* Linux(X11) specifics */
    Display *dpy;
    Window w;
    GC gc;
    XImage *img;
};

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

internal void
RenderWeirdGradient(Buffer* Window, int BlueOffset, int GreenOffset)
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
            uint8_t Blue = (X + BlueOffset);
            uint8_t Green = (Y + GreenOffset);

            // blue is in the big end, green in the next 8 bytes.
            // they'll be offset
            *Pixel++ = ((Green << 8) | Blue);  // Write & advance
        }
        Row += Pitch;  // Jump Row pointer to next row
    }
}

internal int
OpenWindow(struct Buffer* wnd)
{
    wnd->dpy = XOpenDisplay(NULL);
    int screen = DefaultScreen(wnd->dpy);
    wnd->w = XCreateSimpleWindow(wnd->dpy, RootWindow(wnd->dpy, screen), 0, 0,
                                 wnd->width, wnd->height, 0,
                                 BlackPixel(wnd->dpy, screen),
                                 WhitePixel(wnd->dpy, screen));
    wnd->gc = XCreateGC(wnd->dpy, wnd->w, 0, 0);
    XSelectInput(wnd->dpy, wnd->w,
                 ExposureMask | KeyPressMask | KeyReleaseMask |
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                 StructureNotifyMask);
    XStoreName(wnd->dpy, wnd->w, wnd->title);
    XMapWindow(wnd->dpy, wnd->w);

    // Enable window closing
    Atom wmDelete = XInternAtom(wnd->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(wnd->dpy, wnd->w, &wmDelete, 1);

    XSync(wnd->dpy, wnd->w);
    wnd->img = XCreateImage(wnd->dpy, DefaultVisual(wnd->dpy, 0), 24, ZPixmap,
                            0, (char *)wnd->buf,
                            wnd->width, wnd->height,
                            32, 0);
    return 0;
}

internal int
HandleLoop(struct Buffer* wnd)
{
    uint8_t ShouldQuit = 0;
    XEvent ev;
    XPutImage(wnd->dpy, wnd->w, wnd->gc, wnd->img, 0, 0, 0, 0,
              wnd->width, wnd->height);
    XFlush(wnd->dpy);

    while (XPending(wnd->dpy))
    {
        XNextEvent(wnd->dpy, &ev);
        switch (ev.type)
        {
            case ButtonPress:
            {
            } break;
            case ButtonRelease:
            {
                wnd->mouse = (ev.type == ButtonPress);
            } break;
            case MotionNotify:
            {
                wnd->x = ev.xmotion.x, wnd->y = ev.xmotion.y;
            } break;
            case KeyPress:
            {
            } break;
            case KeyRelease:
            {
            } break;
            case ConfigureNotify:  // Window resizing
            {
                XConfigureEvent xce = ev.xconfigure;
                if (xce.width != wnd->width || xce.height != wnd->height)
                {
                    wnd->width = xce.width;
                    wnd->height = xce.height;

                    // Free the old XImage structure (not the data)
                    wnd->img->data = NULL;  // Prevent XLib from freeing our buffer
                    XFree(wnd->img);

                    // Reallocate buffer
                    wnd->buf = (uint32_t*)realloc(wnd->buf, wnd->width * wnd->height * sizeof(uint32_t));

                    // Create completely new XImage
                    wnd->img = XCreateImage(wnd->dpy, DefaultVisual(wnd->dpy, 0), 24, ZPixmap,
                                            0, (char *)wnd->buf,
                                            wnd->width, wnd->height, 32, 0);
                }
            } break;
            case ClientMessage:
            {
                // Window close button
                if (ev.xclient.data.l[0] ==
                        (long)XInternAtom(wnd->dpy, "WM_DELETE_WINDOW", False))
                {
                    ShouldQuit = 1;
                }
            } break;
        }
    }
    return ShouldQuit;
}

int
main(int argc, char *argv[])
{
    int W = 600, H = 480;
    // uint32_t buf[W * H];
    uint32_t *buf = (uint32_t*)malloc(W * H * sizeof(uint32_t));
    struct Buffer buffer = {
        .title = "hello",
        .width = W,
        .height = H,
        .buf = buf,
    };
    OpenWindow(&buffer);
    while(HandleLoop(&buffer) == 0)
    {
        // MaDraw(&buffer);
        RenderWeirdGradient(&buffer, 0, 0);
    }
    XCloseDisplay(buffer.dpy);
}
