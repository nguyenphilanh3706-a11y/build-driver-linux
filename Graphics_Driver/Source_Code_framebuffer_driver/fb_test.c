#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static void pixel(uint8_t *fb, int stride, int x, int y, int on)
{
    uint8_t mask = 0x80u >> (x & 7);
    uint8_t *p = &fb[y * stride + (x >> 3)];
    if (on) *p |= mask;
    else    *p &= (uint8_t)~mask;
}

static void line(uint8_t *fb, int stride, int x0, int y0, int x1, int y1)
{
    int dx = abs(x1-x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1-y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pixel(fb, stride, x0, y0, 1);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void rect(uint8_t *fb, int stride, int x, int y, int w, int h)
{
    line(fb,stride,x,y,x+w-1,y);
    line(fb,stride,x,y+h-1,x+w-1,y+h-1);
    line(fb,stride,x,y,x,y+h-1);
    line(fb,stride,x+w-1,y,x+w-1,y+h-1);
}

int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "/dev/fb0";
    struct fb_fix_screeninfo fix;
    struct fb_var_screeninfo var;
    int fd = open(dev, O_RDWR);
    if (fd < 0) { perror("open framebuffer"); return 1; }

    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0 ||
        ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
        perror("ioctl"); close(fd); return 1;
    }

    printf("%s: %ux%u, %ubpp, stride=%u, mem=%u bytes\n",
           dev, var.xres, var.yres, var.bits_per_pixel,
           fix.line_length, fix.smem_len);

    if (var.bits_per_pixel != 1) {
        fprintf(stderr, "Expected a 1-bpp framebuffer\n");
        close(fd); return 1;
    }

    uint8_t *fb = mmap(NULL, fix.smem_len, PROT_READ|PROT_WRITE,
                       MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    memset(fb, 0, fix.smem_len);

    rect(fb, fix.line_length, 0, 0, var.xres, var.yres);
    rect(fb, fix.line_length, 5, 5, var.xres-10, var.yres-10);
    line(fb, fix.line_length, 0, 0, var.xres-1, var.yres-1);
    line(fb, fix.line_length, var.xres-1, 0, 0, var.yres-1);

    for (int x = 8; x < (int)var.xres-8; x += 8)
        pixel(fb, fix.line_length, x, var.yres/2, 1);

    msync(fb, fix.smem_len, MS_SYNC);
    sleep(3);

    munmap(fb, fix.smem_len);
    close(fd);
    return 0;
}
