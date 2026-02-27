#ifndef HANDMADE_H

#include <stdint.h>

#define int16 int16_t

#ifndef AUDIO_BUF_SIZE
#define AUDIO_BUF_SIZE 8192
#endif

struct game_offscreen_buffer
{
    // NOTE: (casey) Pixels are always 32-bit wide, Memory order BB GG RR XX
    void* Memory;
    int Width;
    int Height;
    int Pitch;
    int XOffset;
    int YOffset;
};

struct game_sound_output_buffer
{
    int SamplesPerSecond;
    int SampleCount;
    //(marco) TODO: this is the actual buffer! Convert other functions so we deal with
    // intergers instead of float.
    int16* Samples;
    // float buffer[AUDIO_BUF_SIZE];
};

struct game_button_state
{
    int HalfTransitionCount;
    bool EndedUp;
    bool EndedDown;
    bool EndedLeft;
    bool EndedRight;
};

struct game_controller_input
{
    bool IsAnalog;

    float StartX;
    float StartY;

    float MinX;
    float MinY;

    float MaxX;
    float MaxY;

    float EndX;
    float EndY;

    union
    {
        game_button_state Buttons[6];
        struct
        {
            game_button_state Up;
            game_button_state Down;
            game_button_state Left;
            game_button_state Right;
            game_button_state LeftShoulder;
            game_button_state RightShoulder;
        };
    };
};

struct game_input 
{
    game_controller_input Controllers[4];
};

static void
GameUpdateAndRender(game_input* Input,
                    game_offscreen_buffer* Buffer,
                    game_sound_output_buffer* SoundBuffer);

#define HANDMADE_H
#endif
