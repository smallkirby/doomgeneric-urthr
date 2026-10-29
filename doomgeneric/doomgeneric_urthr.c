#include "doomgeneric.h"
#include "i_system.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
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

const char FbDeviceName[] = "/dev/fb0";

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

  if (varInfo.bits_per_pixel != 32)
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

  // Center the game screen.
  fbOffsetX = (fbWidth > DOOMGENERIC_RESX) ? ((fbWidth - DOOMGENERIC_RESX) / 2) * fbBytesPerPixel : 0;
  fbOffsetY = (fbHeight > DOOMGENERIC_RESY) ? ((fbHeight - DOOMGENERIC_RESY) / 2) * fbStride : 0;

  // Record the start time.
  clock_gettime(CLOCK_MONOTONIC, &startTime);
}

void DG_DrawFrame()
{
  static unsigned char *frame;
  const size_t size = (size_t)fbStride * fbHeight;
  const unsigned int width = (fbWidth < DOOMGENERIC_RESX) ? fbWidth : DOOMGENERIC_RESX;
  const unsigned int height = (fbHeight < DOOMGENERIC_RESY) ? fbHeight : DOOMGENERIC_RESY;
  const int isNativeOrder = (fbRedOffset == 16 && fbGreenOffset == 8 && fbBlueOffset == 0);
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
    uint32_t *dst = (uint32_t *)(frame + fbOffsetY + (size_t)y * fbStride + fbOffsetX);

    if (isNativeOrder)
    {
      memcpy(dst, src, width * sizeof(uint32_t));
      continue;
    }

    for (x = 0; x < width; x++)
    {
      const uint32_t p = src[x];
      dst[x] = (((p >> 16) & 0xFF) << fbRedOffset) |
               (((p >> 8) & 0xFF) << fbGreenOffset) |
               (((p >> 0) & 0xFF) << fbBlueOffset);
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

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
  (void)pressed;
  (void)doomKey;
  return 0;
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
