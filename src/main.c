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
    system("zenity --info --text='Hello, this is a message box!'");
    return (0);
}
