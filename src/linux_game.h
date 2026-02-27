#include <linux/input.h>
#include <linux/joystick.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <alsa/asoundlib.h>

struct linux_window
{
    Window w; // X11 window
    GC gc; // graphics context

    const char* WindowTitle;
    Display* dpy; // XDisplay
    XImage* img;
    // uint32_t* buf; // buffer to be filled to the XImage
};

typedef struct
{
    uint32_t connected;
    short left_stick_x; // -32767 to 32767
    short left_stick_y;
    short right_stick_x;
    short right_stick_y;
    unsigned short buttons; // bitmask A=0x0001, B=0x0002, etc. 0000_0000_0000_YXBA
    unsigned char left_trigger; // 0-255
    unsigned char right_trigger;
    char dpad_x; // -1,0,1
    char dpad_y;
} LinuxControllerInputState;

struct game_kb_mouse_input
{
    int keys[256]; /* keys are mostly ASCII, but arrows are 17..20 */
    int mod;       /* mod is 4 bits mask, ctrl=1, shift=2, alt=4, meta=8 */
    int x;
    int y;
    int mouse;
};

/* alsa driver functions */
int snd_pcm_open(snd_pcm_t** pcm, const char* name, snd_pcm_stream_t stream,
                 int mode);
int snd_pcm_set_params(snd_pcm_t *pcm, snd_pcm_format_t format,
                       snd_pcm_access_t access, unsigned int channels,
                       unsigned int rate, int soft_resample,
                       unsigned int latency);
snd_pcm_sframes_t snd_pcm_avail(snd_pcm_t *pcm);
snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *Offscreen_buffer,
                                 snd_pcm_uframes_t size);
int snd_pcm_recover(void *, int, int);
int snd_pcm_recover(snd_pcm_t *pcm, int err, int silent);
int snd_pcm_close(snd_pcm_t *pcm);
