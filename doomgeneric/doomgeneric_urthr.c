// To avoid conflicts with the Linux input key definitions.
#include <linux/input.h>

enum
{
  LINUX_KEY_ENTER = KEY_ENTER,
  LINUX_KEY_TAB = KEY_TAB,
  LINUX_KEY_BACKSPACE = KEY_BACKSPACE,
  LINUX_KEY_PAUSE = KEY_PAUSE,
  LINUX_KEY_MINUS = KEY_MINUS,
  LINUX_KEY_F1 = KEY_F1,
  LINUX_KEY_F10 = KEY_F10,
  LINUX_KEY_F11 = KEY_F11,
  LINUX_KEY_F12 = KEY_F12,
};

#undef KEY_BACKSPACE
#undef KEY_CAPSLOCK
#undef KEY_END
#undef KEY_ENTER
#undef KEY_F1
#undef KEY_F2
#undef KEY_F3
#undef KEY_F4
#undef KEY_F5
#undef KEY_F6
#undef KEY_F7
#undef KEY_F8
#undef KEY_F9
#undef KEY_F10
#undef KEY_F11
#undef KEY_F12
#undef KEY_HOME
#undef KEY_MINUS
#undef KEY_NUMLOCK
#undef KEY_PAUSE
#undef KEY_TAB

#include "doomgeneric.h"
#include "doomkeys.h"
#include "i_system.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

static struct timespec startTime;
static int fbFd;
static unsigned int fbWidth;
static unsigned int fbHeight;
static unsigned int fbStride;
static unsigned int fbBytesPerPixel;
static unsigned int fbOffsetX;
static unsigned int fbOffsetY;
static unsigned int fbRedOffset;
static unsigned int fbGreenOffset;
static unsigned int fbBlueOffset;
static unsigned int fbRedLength;
static unsigned int fbGreenLength;
static unsigned int fbBlueLength;
static unsigned int fbAlphaMask;

const char FbDeviceName[] = "/dev/fb0";
const char InputDeviceName[] = "/dev/input/event0";

// Input events are read in batches.
#define INPUT_BATCH 16

static int inputFd = -1;
static struct input_event inputEvents[INPUT_BATCH];
static int inputHead;
static int inputCount;

void DG_Init()
{
  int ret;
  struct fb_var_screeninfo varInfo;
  struct fb_fix_screeninfo fixedInfo;

  fbFd = open(FbDeviceName, O_RDWR);
  if (fbFd < 0)
  {
    I_Error("Failed to open %s: %s", FbDeviceName, strerror(errno));
  }

  ret = ioctl(fbFd, FBIOGET_VSCREENINFO, &varInfo);
  if (ret != 0)
  {
    I_Error("Failed to get framebuffer info: %s", strerror(errno));
  }

  ret = ioctl(fbFd, FBIOGET_FSCREENINFO, &fixedInfo);
  if (ret != 0)
  {
    printf("Failed to get framebuffer info: %s", strerror(errno));
  }

  if (varInfo.bits_per_pixel != 32 && varInfo.bits_per_pixel != 16)
  {
    I_Error("Unsupported framebuffer depth: %u bpp", varInfo.bits_per_pixel);
  }

  fbWidth = varInfo.xres;
  fbHeight = varInfo.yres;
  fbBytesPerPixel = varInfo.bits_per_pixel / 8;
  fbStride = (ret == 0) ? fixedInfo.line_length : fbWidth * fbBytesPerPixel;

  fbRedOffset = varInfo.red.offset;
  fbGreenOffset = varInfo.green.offset;
  fbBlueOffset = varInfo.blue.offset;
  fbRedLength = varInfo.red.length;
  fbGreenLength = varInfo.green.length;
  fbBlueLength = varInfo.blue.length;
  fbAlphaMask = (varInfo.transp.length == 0) ? 0 : (((1U << varInfo.transp.length) - 1) << varInfo.transp.offset);

  // Center the game screen.
  fbOffsetX = (fbWidth > DOOMGENERIC_RESX) ? ((fbWidth - DOOMGENERIC_RESX) / 2) * fbBytesPerPixel : 0;
  fbOffsetY = (fbHeight > DOOMGENERIC_RESY) ? ((fbHeight - DOOMGENERIC_RESY) / 2) * fbStride : 0;

  // Open input device.
  inputFd = open(InputDeviceName, O_RDONLY | O_NONBLOCK);
  if (inputFd < 0)
  {
    I_Error("Failed to open %s: %s", InputDeviceName, strerror(errno));
  }

  if (ioctl(inputFd, EVIOCGRAB, 1UL) != 0)
  {
    I_Error("Failed to grab %s: %s", InputDeviceName, strerror(errno));
  }

  // Record the start time.
  clock_gettime(CLOCK_MONOTONIC, &startTime);
}

void DG_DrawFrame()
{
  static unsigned char *frame;
  const size_t size = (size_t)fbStride * fbHeight;
  const unsigned int width = (fbWidth < DOOMGENERIC_RESX) ? fbWidth : DOOMGENERIC_RESX;
  const unsigned int height = (fbHeight < DOOMGENERIC_RESY) ? fbHeight : DOOMGENERIC_RESY;
  unsigned int x, y;
  size_t done = 0;

  if (frame == NULL)
  {
    frame = calloc(1, size);
    if (frame == NULL)
    {
      I_Error("Failed to allocate the frame buffer");
    }
  }

  // Convert the game screen into the framebuffer format.
  for (y = 0; y < height; y++)
  {
    const pixel_t *src = DG_ScreenBuffer + (size_t)y * DOOMGENERIC_RESX;
    unsigned char *line = frame + fbOffsetY + (size_t)y * fbStride + fbOffsetX;

    for (x = 0; x < width; x++)
    {
      const uint32_t p = src[x];
      const uint32_t v = fbAlphaMask |
                        (((p >> 16) & 0xFF) >> (8 - fbRedLength) << fbRedOffset) |
                         (((p >> 8) & 0xFF) >> (8 - fbGreenLength) << fbGreenOffset) |
                         (((p >> 0) & 0xFF) >> (8 - fbBlueLength) << fbBlueOffset);

      if (fbBytesPerPixel == 2)
      {
        ((uint16_t *)line)[x] = (uint16_t)v;
      }
      else
      {
        ((uint32_t *)line)[x] = v;
      }
    }
  }

  // Write the whole frame at once.
  while (done < size)
  {
    ssize_t n = pwrite(fbFd, frame + done, size - done, done);
    if (n <= 0)
    {
      I_Error("Failed to write the framebuffer: %s", strerror(errno));
    }
    done += n;
  }
}

void DG_SleepMs(uint32_t ms)
{
	usleep(ms * 1000);
}

uint32_t DG_GetTicksMs()
{
	struct timespec curTime;
	long seconds, nsec;

	clock_gettime(CLOCK_MONOTONIC, &curTime);
	seconds = curTime.tv_sec - startTime.tv_sec;
	nsec = curTime.tv_nsec - startTime.tv_nsec;

	return (seconds * 1000) + (nsec / 1000000);
}

// Convert a key code of Linux input into a key of Doom.
//
// Returns 0 if the key is not used by the game.
static unsigned char ConvertToDoomKey(unsigned int code)
{
  static const char digits[] = "1234567890"; // KEY_1 - KEY_0
  static const char row_q[] = "qwertyuiop";  // KEY_Q - KEY_P
  static const char row_a[] = "asdfghjkl";   // KEY_A - KEY_L
  static const char row_z[] = "zxcvbnm";     // KEY_Z - KEY_M

  if (KEY_1 <= code && code <= KEY_0)
  {
    return digits[code - KEY_1];
  }
  if (KEY_Q <= code && code <= KEY_P)
  {
    return row_q[code - KEY_Q];
  }
  if (KEY_A <= code && code <= KEY_L)
  {
    return row_a[code - KEY_A];
  }
  if (KEY_Z <= code && code <= KEY_M)
  {
    return row_z[code - KEY_Z];
  }
  if (LINUX_KEY_F1 <= code && code <= LINUX_KEY_F10)
  {
    return KEY_F1 + (code - LINUX_KEY_F1);
  }

  switch (code)
  {
  case LINUX_KEY_ENTER:
  case KEY_KPENTER:
    return KEY_ENTER;
  case KEY_ESC:
    return KEY_ESCAPE;
  case LINUX_KEY_TAB:
    return KEY_TAB;
  case LINUX_KEY_BACKSPACE:
    return KEY_BACKSPACE;
  case LINUX_KEY_PAUSE:
    return KEY_PAUSE;
  case KEY_LEFT:
    return KEY_LEFTARROW;
  case KEY_RIGHT:
    return KEY_RIGHTARROW;
  case KEY_UP:
    return KEY_UPARROW;
  case KEY_DOWN:
    return KEY_DOWNARROW;
  case KEY_LEFTCTRL:
  case KEY_RIGHTCTRL:
    return KEY_FIRE;
  case KEY_SPACE:
    return KEY_USE;
  case KEY_LEFTSHIFT:
  case KEY_RIGHTSHIFT:
    return KEY_RSHIFT;
  case KEY_LEFTALT:
  case KEY_RIGHTALT:
    return KEY_RALT;
  case KEY_COMMA:
    return KEY_STRAFE_L;
  case KEY_DOT:
    return KEY_STRAFE_R;
  case LINUX_KEY_MINUS:
    return KEY_MINUS;
  case KEY_EQUAL:
    return KEY_EQUALS;
  case LINUX_KEY_F11:
    return KEY_F11;
  case LINUX_KEY_F12:
    return KEY_F12;
  default:
    return 0;
  }
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
  if (inputFd < 0)
  {
    return 0;
  }

  while (1)
  {
    // Feed the next batch of events.
    if (inputHead == inputCount)
    {
      ssize_t n = read(inputFd, inputEvents, sizeof(inputEvents));
      if (n < (ssize_t)sizeof(struct input_event))
      {
        return 0;
      }

      inputHead = 0;
      inputCount = n / sizeof(struct input_event);
    }

    const struct input_event *event = &inputEvents[inputHead++];

    // Key repeat is not necessary.
    if (event->type != EV_KEY || event->value == 2)
    {
      continue;
    }

    unsigned char key = ConvertToDoomKey(event->code);
    if (key == 0)
    {
      continue;
    }

    *pressed = event->value;
    *doomKey = key;

    return 1;
  }
}

void DG_SetWindowTitle(const char *title)
{
  (void)title;
}

int main(int argc, char **argv)
{
  doomgeneric_Create(argc, argv);

  while (1)
  {
    doomgeneric_Tick();
  }

  return 0;
}
