#include <X11/Xlib.h>
#include <stdint.h>
#include <stdlib.h>

#define internal        static
#define local_persist   static
#define global_variable static

global_variable int BitMapWidth;
global_variable int BitMapHeight;
global_variable int BytesPerPixel = 4;
global_variable void* BitMapMemory;

internal void
RenderWeirdGradient(int BlueOffset, int GreenOffset)
{
    int Width = BitMapWidth;
    int Height = BitMapHeight;

    int Pitch = Width * BytesPerPixel;
    uint8_t* Row = (uint8_t*)BitMapMemory;

    for(int Y = 0; Y < BitMapHeight; ++Y)
    {
        uint32_t* Pixel = (uint32_t*)Row;
        for(int X = 0; X < BitMapWidth; ++X)
        {
            uint8_t Blue = (X + BlueOffset);
            uint8_t Green = (Y + GreenOffset);
            *Pixel++ = ((Green << 8) | Blue);
        }
        Row += Pitch;
    }
}

int
main(int argc, char *argv[])
{
    Display *dpy = XOpenDisplay(NULL);
    int scr = DefaultScreen(dpy);
    Window wnd = XCreateSimpleWindow(dpy, RootWindow(dpy, scr),
                                     0, 0, 320, 240, 0,
                                     BlackPixel(dpy, scr), WhitePixel(dpy, scr));
    XStoreName(dpy, wnd, "Hello, X11");
    XSelectInput(dpy, wnd, ExposureMask | KeyPressMask);
    XMapWindow(dpy, wnd);
    for (;;)
    {
        XEvent e;
        XNextEvent(dpy, &e);
        // handle events here
    }
    return XCloseDisplay(dpy);
}
