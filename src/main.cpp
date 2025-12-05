#include <X11/X.h>
#include <X11/Xlib.h>
#include <cstdint>
#include <cstdlib>
#include <stdint.h>
#include <stdlib.h>

#define internal        static
#define local_persist   static
#define global_variable static

struct offScreenBuffer
{
    void* Memory;
    int Width;
    int Height;
    int Pitch;
    int BytesPerPixel;
};

struct windowDimension {
    int Width;
    int Height;
};

internal void
RenderWeirdGradient(offScreenBuffer* Buffer, int BlueOffset, int GreenOffset)
{
    uint8_t *Row = (uint8_t *)Buffer->Memory;
    for(int Y = 0;
        Y < Buffer->Height;
        ++Y)
    {
        uint32_t* Pixel = (uint32_t*)Row;
        for(int X = 0;
            X < Buffer->Width;
            ++X)
        {
            uint8_t Blue = (X + BlueOffset);
            uint8_t Green = (X + GreenOffset);

            *Pixel++ = ((Green << 8) | Blue);
        }
        Row += Buffer->Pitch;
    }
}

int
main(int argc, char *argv[])
{
    offScreenBuffer Buffer;
    Buffer.Width = 1280;
    Buffer.Height = 720;
    Buffer.BytesPerPixel = 4;
    Buffer.Pitch = 320*Buffer.BytesPerPixel;
    int BitMapMemorySize = (Buffer.Width*Buffer.Height)*Buffer.BytesPerPixel;
    Buffer.Memory = malloc(BitMapMemorySize);
    if (!Buffer.Memory)
    {
        return -1;
    }

    Display *dpy = XOpenDisplay(NULL);
    int scr = DefaultScreen(dpy);
    Window wnd = XCreateSimpleWindow(dpy, RootWindow(dpy, scr),
                                     0, 0, Buffer.Width, Buffer.Height, 0,
                                     WhitePixel(dpy, scr),
                                     BlackPixel(dpy, scr));
    XStoreName(dpy, wnd, "Hello, X11");
    XSelectInput(dpy, wnd, ExposureMask | KeyPressMask);
    XMapWindow(dpy, wnd);

    // uint32_t drawing[BitMapWidth*BitMapHeight];
    // for (int i = 0; i < BitMapWidth * BitMapHeight; i++) {
    //     drawing[i] = 0x00FF0000; // Red in ARGB format
    // }

    RenderWeirdGradient(&Buffer, 0, 0);

    GC gc = XCreateGC(dpy, wnd, 0, 0);
    XImage *img = XCreateImage(dpy, DefaultVisual(dpy, 0), 24, ZPixmap, 0,
                              (char*)Buffer.Memory,
                               Buffer.Width, Buffer.Height,
                               32, 0);
    for (;;)
    {
        XEvent e;
        XNextEvent(dpy, &e);
        // handle events here
        XPutImage(dpy, wnd, gc, img, 0, 0, 0, 0,
                  Buffer.Width, Buffer.Height);
        XFlush(dpy);
    }
    return XCloseDisplay(dpy);
}
