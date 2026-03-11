#include "game.cpp"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#include "linux_game.h"

#define MAX_CONTROLLERS 4

#ifndef AUDIO_SAMPLE_RATE
#define AUDIO_SAMPLE_RATE 48000
#endif

#define FRAMES_PER_SECOND 30
#define AUDIO_CHANNELS 2

/* from https://github.com/zserge/fenster/blob/main/fenster.h */
const static int FENSTER_KEYCODES[124] = {XK_BackSpace,8,XK_Delete,127,XK_Down,18,XK_End,5,XK_Escape,27,XK_Home,2,XK_Insert,26,XK_Left,20,XK_Page_Down,4,XK_Page_Up,3,XK_Return,10,XK_Right,19,XK_Tab,9,XK_Up,17,XK_apostrophe,39,XK_backslash,92,XK_bracketleft,91,XK_bracketright,93,XK_comma,44,XK_equal,61,XK_grave,96,XK_minus,45,XK_period,46,XK_semicolon,59,XK_slash,47,XK_space,32,XK_a,65,XK_b,66,XK_c,67,XK_d,68,XK_e,69,XK_f,70,XK_g,71,XK_h,72,XK_i,73,XK_j,74,XK_k,75,XK_l,76,XK_m,77,XK_n,78,XK_o,79,XK_p,80,XK_q,81,XK_r,82,XK_s,83,XK_t,84,XK_u,85,XK_v,86,XK_w,87,XK_x,88,XK_y,89,XK_z,90,XK_0,48,XK_1,49,XK_2,50,XK_3,51,XK_4,52,XK_5,53,XK_6,54,XK_7,55,XK_8,56,XK_9,57};


static int
LinuxAudioOpen(snd_pcm_t** pcm)
{
    int err = snd_pcm_open(pcm, "default",
                           SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0)
    {
        fprintf(stderr, "Cannot open PCM device: %s\n", snd_strerror(err));
        return -1;
    }
    unsigned short IsLittleEndian = 1;
    int fmt = (*(char*)(&IsLittleEndian)) ?
        SND_PCM_FORMAT_S16_LE : SND_PCM_FORMAT_S16_BE;
    return snd_pcm_set_params(*pcm, (snd_pcm_format_t)fmt, (snd_pcm_access_t)3,
                              AUDIO_CHANNELS, AUDIO_SAMPLE_RATE, 1, 100000);
}

static int
LinuxAudioAvailable(snd_pcm_t* pcm)
{
    int n = snd_pcm_avail(pcm);
    if (n < 0)
    {
        snd_pcm_recover(pcm, n, 0);
    }
    return n;
}

static void
LinuxAudioWrite(snd_pcm_t* pcm, int16* buf, size_t size)
{
    int t = LinuxAudioAvailable(pcm);
    if (t > 0)
    {
        int r = snd_pcm_writei(pcm, buf, size);
        if (r < 0)
        {
            snd_pcm_recover(pcm, r, 0);
        }
    }
}

static void
LinuxAudioClose(snd_pcm_t* pcm)
{
    snd_pcm_close(pcm);
}


static int
LinuxOpenX11Window(struct game_offscreen_buffer* Offscreen_buffer, struct linux_window* LinuxWindow)
{
    LinuxWindow->dpy = XOpenDisplay(NULL);
    int screen = DefaultScreen(LinuxWindow->dpy);
    LinuxWindow->w = XCreateSimpleWindow(LinuxWindow->dpy,
                                    RootWindow(LinuxWindow->dpy, screen), 0, 0,
                                    Offscreen_buffer->Width, Offscreen_buffer->Height, 0,
                                    BlackPixel(LinuxWindow->dpy, screen),
                                    WhitePixel(LinuxWindow->dpy, screen));
    LinuxWindow->gc = XCreateGC(LinuxWindow->dpy, LinuxWindow->w, 0, 0);
    XSelectInput(LinuxWindow->dpy, LinuxWindow->w,
                 ExposureMask | KeyPressMask | KeyReleaseMask |
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                 StructureNotifyMask);
    XStoreName(LinuxWindow->dpy, LinuxWindow->w, LinuxWindow->WindowTitle);
    XMapWindow(LinuxWindow->dpy, LinuxWindow->w);

    // Enable window closing
    Atom wmDelete = XInternAtom(LinuxWindow->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(LinuxWindow->dpy, LinuxWindow->w, &wmDelete, 1);

    LinuxWindow->img = XCreateImage(LinuxWindow->dpy, DefaultVisual(LinuxWindow->dpy, 0), 24,
                               ZPixmap, 0, (char *)Offscreen_buffer->Memory, Offscreen_buffer->Width,
                               Offscreen_buffer->Height, 32, 0);
    return 0;
}

static void
Sleeper(int64_t ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

static int64_t
GetYerTime(void)
{
    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    return time.tv_sec * 1000 + (time.tv_nsec / 1000000);
}

// Opens a joystick device if it exists
static int
LinuxJoyOpen(int index)
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

static void
LinuxJoyClose(int JoyFDs[], LinuxControllerInputState LinuxJoyStates[])
{
    for (int i = 0; i < MAX_CONTROLLERS; i++)
    {
        if (JoyFDs[i] >= 0)
        {
            close(JoyFDs[i]);
        }
    }
}

// Call this once at startup
static void
LinuxJoyInit(int JoyFDs[MAX_CONTROLLERS], LinuxControllerInputState LinuxJoyStates[MAX_CONTROLLERS])
{
    for (int i = 0; i < MAX_CONTROLLERS; i++)
    {
        JoyFDs[i] = LinuxJoyOpen(i);
        LinuxJoyStates[i].connected = (JoyFDs[i] >= 0);
    }
}

static void
LinuxProcessJoypadButtons(int JoyFDs[],
                          LinuxControllerInputState LinuxJoyStates[])
{
    for (int i = 0; i < MAX_CONTROLLERS; i++)
    {
        if (JoyFDs[i] < 0)
        {
            continue; // skip loop if not connected
        }

        struct js_event ev;
        LinuxControllerInputState* state = &LinuxJoyStates[i];

        // Reset per-frame changes if needed (we'll rebuild)
        // But we just accumulate latest values
        while (read(JoyFDs[i], &ev, sizeof(ev)) == sizeof(ev))
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
                        // -32767..32767 → 0..255
                        state->left_trigger = (ev.value + 32767) / 257; 
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

static void
JoyHotplug(int JoyFDs[], LinuxControllerInputState LinuxJoyStates[])
{
    for (int i = 0; i < MAX_CONTROLLERS; i++)
    {
        if (JoyFDs[i] < 0)
        {
            JoyFDs[i] = LinuxJoyOpen(i);
            if (JoyFDs[i] >= 0)
            {
                LinuxJoyStates[i].connected = 1;
            }
        }
    }
}

static int
LinuxGetKBMouseState(game_offscreen_buffer* b, game_kb_mouse_input* kb_mouse)
{
    int IsRunning = 1;
    int has_keys = 0;
    char string[32];
    char* p = string;

    // let's check if any key press was stored in the kb_mouse buffer
    for (int i = 0; i < 128; i++)
    {
        if (kb_mouse->keys[i])
        {
            has_keys = 1;
            *p++ = i;
        }
    }
    *p = '\0';

    // fenster_text(&Offscreen_buffer, 8, 8, string, 4, 0xffffff);
    if (has_keys)
    {
        // so to remember:
        // mod is a 4 bits mask, ctrl=1, shift=2, alt=4, meta=8
        if (kb_mouse->mod & 1)
        {
            // fenster_text(&Offscreen_buffer, 8, 40, "Ctrl", 4, 0xffffff);
        }
        if (kb_mouse->mod & 2)
        {
            // fenster_text(&Offscreen_buffer, 8, 80, "Shift", 4, 0xffffff);
        }
    }
    if (kb_mouse->keys[27]) // XK_Escape
    {
        return 0;
    }
    if (kb_mouse->keys[17]) // XK_Up
    {
        // TODO: WHAT TO DO?
    }
    if (kb_mouse->keys[18]) // XK_Down
    {
        // TODO: WHAT TO DO?
    }
    if (kb_mouse->keys[19]) // XK_Right
    {
        // TODO: WHAT TO DO?
    }
    if (kb_mouse->keys[20]) // XK_Left
    {
        // TODO: WHAT TO DO?
    }

    return IsRunning;
}


static int
LinuxWindowLoop(struct game_offscreen_buffer* Offscreen_buffer,
                struct linux_window* LinuxWindow, game_kb_mouse_input* kb_mouse)
{
    uint8_t IsRunning = 1;
    XEvent ev;
    // NOTE: This is where the Offscreen_buffer appears!
    XPutImage(LinuxWindow->dpy, LinuxWindow->w, LinuxWindow->gc, LinuxWindow->img, 0, 0, 0, 0,
              Offscreen_buffer->Width, Offscreen_buffer->Height);

    while (XPending(LinuxWindow->dpy))
    {
        XNextEvent(LinuxWindow->dpy, &ev);
        switch (ev.type)
        {
            case ButtonPress:
            {
            } break;
            case ButtonRelease:
            {
                kb_mouse->mouse = (ev.type == ButtonPress);
            } break;
            case MotionNotify:
            {
                kb_mouse->x = ev.xmotion.x, kb_mouse->y = ev.xmotion.y;
            } break;
            case KeyPress:
            case KeyRelease:
            {
                int m = ev.xkey.state;
                int k = XkbKeycodeToKeysym(LinuxWindow->dpy, ev.xkey.keycode, 0, 0);
                for (uint32_t i = 0; i < 124; i += 2)
                {
                    if (FENSTER_KEYCODES[i] == k)
                    {
                        kb_mouse->keys[FENSTER_KEYCODES[i + 1]] = (ev.type == KeyPress);
                        break;
                    }
                }
                kb_mouse->mod = (!!(m & ControlMask)) | (!!(m & ShiftMask) << 1) |
                    (!!(m & Mod1Mask) << 2) | (!!(m & Mod4Mask) << 3);
            } break;
            case ConfigureNotify:  // Window resizing
            {
                XConfigureEvent xce = ev.xconfigure;
                if (xce.width != Offscreen_buffer->Width || xce.height != Offscreen_buffer->Height)
                {
                    // Resize the Offscreen_buffer
                    Offscreen_buffer->Width = xce.width;
                    Offscreen_buffer->Height = xce.height;

                    // Free the old XImage structure (not the data)
                    LinuxWindow->img->data = NULL;  // Prevent XLib from freeing our Offscreen_buffer
                    XFree(LinuxWindow->img);

                    // WARN: Maybe we should allocate in another way here
                    Offscreen_buffer->Memory = (uint32_t*)realloc(Offscreen_buffer->Memory, Offscreen_buffer->Width * Offscreen_buffer->Height * sizeof(uint32_t));

                    // Create completely new XImage
                    LinuxWindow->img = XCreateImage(LinuxWindow->dpy,
                                               DefaultVisual(LinuxWindow->dpy, 0),
                                               24, ZPixmap, 0,
                                               (char *)Offscreen_buffer->Memory,
                                               Offscreen_buffer->Width, Offscreen_buffer->Height,
                                               32, 0);
                }
            } break;
            case ClientMessage:
            {
                // Window close button
                if (ev.xclient.data.l[0] ==
                        (long)XInternAtom(LinuxWindow->dpy, "WM_DELETE_WINDOW", False))
                {
                    IsRunning = 0;
                }
            } break;
        }
    }
    return IsRunning;
}

static void
LinuxProcessDigitalButton(game_button_state* OldState,
                          game_button_state* NewState,
                          uint8_t LinuxJoyButtonState,
                          uint8_t ButtonBit)
{
    bool IsDown = ((LinuxJoyButtonState & ButtonBit) != 0);
    NewState->EndedUp = IsDown;
    NewState->EndedDown = IsDown;
    NewState->EndedLeft = IsDown;
    NewState->EndedRight = IsDown;

    NewState->HalfTransitionCount =
        (OldState->EndedUp != NewState->EndedUp) ? 1 : 0;
    NewState->HalfTransitionCount =
        (OldState->EndedDown != NewState->EndedDown) ? 1 : 0;
    NewState->HalfTransitionCount =
        (OldState->EndedLeft != NewState->EndedLeft) ? 1 : 0;
    NewState->HalfTransitionCount =
        (OldState->EndedRight != NewState->EndedRight) ? 1 : 0;
    NewState->HalfTransitionCount =
        (OldState->EndedRightShoulder != NewState->EndedRightShoulder) ? 1 : 0;
    NewState->HalfTransitionCount =
        (OldState->EndedLeftShoulder != NewState->EndedLeftShoulder) ? 1 : 0;
}

int
main(int argc, char *argv[])
{
    int W = 600, H = 480;
    uint32_t *buf = (uint32_t*)malloc(W * H * sizeof(uint32_t));
    struct game_offscreen_buffer Offscreen_buffer = {
        .Memory = buf,
        .Width = W,
        .Height = H,
    };
    struct linux_window LinuxWindow = {
        .WindowTitle = "Handmade Hero",
    };

    int JoyFDs[MAX_CONTROLLERS] = {-1, -1, -1, -1};
    LinuxControllerInputState LinuxJoyStates[MAX_CONTROLLERS] = {};
    game_input Input[2] = {};
    game_input *NewInput = &Input[0];
    game_input *OldInput = &Input[1];
    game_kb_mouse_input KbMouse = {};
    LinuxJoyInit(JoyFDs, LinuxJoyStates);

    int16 Samples[(AUDIO_SAMPLE_RATE/FRAMES_PER_SECOND) * 2] = {};
    snd_pcm_t* pcm = NULL;
    LinuxAudioOpen(&pcm);

    game_sound_output_buffer GameSound = {};
    GameSound.SamplesPerSecond = AUDIO_SAMPLE_RATE,
    GameSound.SampleCount = GameSound.SamplesPerSecond / FRAMES_PER_SECOND;
    GameSound.Samples = Samples;

    game_memory GameMemory = {};
    GameMemory.PermanentStorageSize = Megabytes((uint64_t)64);
    GameMemory.TransientStorageSize = Gigabytes((uint64_t)2);
    uint64_t TotalSize = GameMemory.PermanentStorageSize + GameMemory.TransientStorageSize;

    GameMemory.PermanentStorage = malloc(TotalSize);
    // NOTE: uint8_t* because it's a byte pointer
    GameMemory.TransientStorage = ((uint8_t*)GameMemory.PermanentStorage +
                                  GameMemory.PermanentStorageSize);

    // TODO: casey wrapped this around all the rest of the code.
    if(!GameSound.Samples || !GameMemory.PermanentStorage ||
       !GameMemory.TransientStorage)
    {
        // TODO: do some logging
        return 1;
    }

    LinuxOpenX11Window(&Offscreen_buffer, &LinuxWindow);

    int64_t now = GetYerTime();
    int64_t start = now;

    int IsRunning = 1;
    while(IsRunning)
    {
        if(!LinuxWindowLoop(&Offscreen_buffer, &LinuxWindow, &KbMouse))
            IsRunning = 0;
        if(!LinuxGetKBMouseState(&Offscreen_buffer, &KbMouse))
            IsRunning = 0;

        JoyHotplug(JoyFDs, LinuxJoyStates);
        LinuxProcessJoypadButtons(JoyFDs, LinuxJoyStates);

        for (int ControllerIndex = 0;
            ControllerIndex < MAX_CONTROLLERS;
            ++ControllerIndex)
        {
            game_controller_input *OldController = &OldInput->Controllers[ControllerIndex];
            game_controller_input *NewController = &NewInput->Controllers[ControllerIndex];
            
            NewController->IsAnalog = true;
            // NOTE: Normalize the sticks!
            float X = LinuxJoyStates[ControllerIndex].left_stick_x / 32767.0f;
            float Y = LinuxJoyStates[ControllerIndex].left_stick_y / 32767.0f;
            NewController->StartX = OldController->EndX;
            NewController->StartY = OldController->EndY;
            // TODO: Min Max macros!
            NewController->MinX = OldController->MaxX = NewController->EndX = X;
            NewController->MinY = OldController->MaxY = NewController->EndY = Y;

            LinuxProcessDigitalButton(
                &OldController->Down,
                &NewController->Down,
                LinuxJoyStates[ControllerIndex].buttons,
                0x01 // NOTE: ButtonBit = A
            );
            LinuxProcessDigitalButton(
                &OldController->Right,
                &NewController->Right,
                LinuxJoyStates[ControllerIndex].buttons,
                0x02 // NOTE: ButtonBit = B
            );
            LinuxProcessDigitalButton(
                &OldController->Left,
                &NewController->Left,
                LinuxJoyStates[ControllerIndex].buttons,
                0x04 // NOTE: ButtonBit = X
            );
            LinuxProcessDigitalButton(
                &OldController->Up,
                &NewController->Up,
                LinuxJoyStates[ControllerIndex].buttons,
                0x08 // NOTE: ButtonBit = Y
            );
        }

        // NOTE: I'm saving this to lookup the keycodes
        // OldController->Down.EndedDown = LinuxJoyStates[0].dpad_y < 0;
        // Controller0->Up.EndedUp = LinuxJoyStates[0].dpad_y > 0;
        // Controller0->Left.EndedLeft = LinuxJoyStates[0].dpad_x < 0;
        // Controller0->Right.EndedRight = LinuxJoyStates[0].dpad_x > 0;
        // keyboard testing
        // Controller0->Down.EndedDown = KbMouse.keys[18];
        // Controller0->Up.EndedUp = KbMouse.keys[17];
        // Controller0->Left.EndedLeft = KbMouse.keys[20]; 
        // Controller0->Right.EndedRight = KbMouse.keys[19];

        GameUpdateAndRender(&GameMemory, NewInput, &Offscreen_buffer, &GameSound);
        LinuxAudioWrite(pcm, GameSound.Samples, GameSound.SampleCount);

        int64_t time = GetYerTime();
        if (time - now < 1000 / FRAMES_PER_SECOND) // 30 frames per second
        {
            Sleeper(time - now);
        }
        now = time;

        game_input *Temp = NewInput;
        NewInput = OldInput;
        OldInput = Temp;
    }

    LinuxAudioClose(pcm);
    LinuxJoyClose(JoyFDs, LinuxJoyStates);
    XCloseDisplay(LinuxWindow.dpy);

    return 0;
}
