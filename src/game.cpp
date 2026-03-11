#include "game.h"

#define Pi32 3.14159265359f
#include <cmath>

/*
 * This algorithm uses the pixels on a memory Buffer as a data structure and we
 * go on hoping with the pointers on each of them to make a cool, weird drawing
 * on memory.
*/
static void
RenderWeirdGradient(game_offscreen_buffer* Buffer, int BlueOffset, int GreenOffset)
{
    uint8_t BytesPerPixel = 4;
    int Pitch = Buffer->Width * BytesPerPixel; // size of the line (pitch)
    uint8_t* Memory = (uint8_t*)Buffer->Memory;

    uint8_t* Row = Memory;  // points at first row
    for(int Y = 0; Y < Buffer->Height; ++Y)
    {
        uint32_t* Pixel = (uint32_t*)Row; // points to the first pixen on the row
        for(int X = 0; X < Buffer->Width; ++X)
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

static void
GameOutputSound(game_sound_output_buffer* SoundBuffer, int ToneHz)
{
    static float tSine;
    int16 ToneVolume = 3000;
    int WavePeriod = SoundBuffer->SamplesPerSecond/ToneHz;

    int16* SampleOut = (int16*)SoundBuffer->Samples;

    for(int SampleIndex = 0;
        SampleIndex < SoundBuffer->SampleCount;
        ++SampleIndex)
    {
        float SineValue = sinf(tSine);
        int16 SampleValue = (int16)(SineValue * ToneVolume);
        *SampleOut++ = SampleValue;
        *SampleOut++ = SampleValue;

        tSine += 2.0f*M_PI*1.0f/(float)WavePeriod;
        if (tSine > 2.0f*M_PI)
        {
            tSine -= 2.0f*M_PI;
        }
    }
}


static void
GameUpdateAndRender(game_memory* Memory,
                    game_input* Input,
                    game_offscreen_buffer* Buffer,
                    game_sound_output_buffer* SoundBuffer)
{
    game_state* GameState = (game_state*)Memory->PermanentStorage;
    if(!Memory->IsInitialized)
    {
        GameState->ToneHz = 256;

        // TODO: This may be more appropriate to do in the platform layer
        Memory->IsInitialized = true;
    }

    /* FIXME: the keyd process is taking position 0...
     * should tight those device discoveries... */
    game_controller_input* Input0 = &Input->Controllers[1]; 
    if(Input0->IsAnalog)
    {
        // NOTE: Use analog movement tuning
        GameState->ToneHz = 256 + (int)(128.0f*Input0->EndX);
        GameState->BlueOffset += (int)4.0f*(Input0->EndY);
    }
    else
    {
        // NOTE: Use digital movement tuning
    }

    if(Input0->Down.EndedDown)
    {
        GameState->GreenOffset -= 4;
        GameState->ToneHz = 256 - 32;
    }
    if(Input0->Up.EndedUp)
    {
        GameState->GreenOffset += 4;
        GameState->ToneHz = 256 + 32;
    }
    if(Input0->Left.EndedLeft)
    {
        GameState->BlueOffset += 4;
        GameState->ToneHz = 256 - 32;
    }
    if(Input0->Right.EndedRight)
    {
        GameState->BlueOffset -= 4;
        GameState->ToneHz = 256 + 32;
    }
    GameOutputSound(SoundBuffer, GameState->ToneHz);
    RenderWeirdGradient(Buffer, GameState->BlueOffset, GameState->GreenOffset);
}
