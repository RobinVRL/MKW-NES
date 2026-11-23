// Runs on the NES Mini: creates a virtual gamepad (/dev/uinput) and feeds it
// from stdin, so a controller plugged into the PC can play over the USB link
// (padsend.py pipes its state here through ssh).
// stdin: 8-byte packets  u16 buttons (bit i = BUTTONS[i]), s16 x, s16 y, s8 hatx, s8 haty
// Little-endian. Releases everything if the PC side disconnects.
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const int BUTTONS[] = {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_TL, BTN_TR, BTN_TL2, BTN_TR2,
                              BTN_SELECT, BTN_START, BTN_MODE};
#define NBTN (int)(sizeof BUTTONS / sizeof BUTTONS[0])

static int fd;
// The kernel's (3.4, 32-bit) event layout. The toolchain defaults to 64-bit
// time_t, which makes <linux/input.h>'s struct input_event 24 bytes instead.
struct kevent { uint32_t sec, usec; uint16_t type, code; int32_t value; };
static void emit(int type, int code, int value) {
    struct kevent e;
    memset(&e, 0, sizeof e);
    e.type = type; e.code = code; e.value = value;
    if (write(fd, &e, sizeof e) < 0) {}
}

static int readAll(unsigned char* p, int n) {
    while (n > 0) {
        ssize_t r = read(0, p, n);
        if (r <= 0) return 0;
        p += r; n -= r;
    }
    return 1;
}

int main(void) {
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("/dev/uinput"); return 1; }
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    for (int i = 0; i < NBTN; ++i) ioctl(fd, UI_SET_KEYBIT, BUTTONS[i]);
    int axes[] = {ABS_X, ABS_Y, ABS_HAT0X, ABS_HAT0Y};
    struct uinput_user_dev dev;
    memset(&dev, 0, sizeof dev);
    snprintf(dev.name, UINPUT_MAX_NAME_SIZE, "PC gamepad bridge");
    dev.id.bustype = BUS_VIRTUAL; dev.id.vendor = 0x054c; dev.id.product = 0x0ce6; dev.id.version = 1;
    for (int i = 0; i < 4; ++i) {
        ioctl(fd, UI_SET_ABSBIT, axes[i]);
        dev.absmin[axes[i]] = i < 2 ? -32768 : -1;
        dev.absmax[axes[i]] = i < 2 ? 32767 : 1;
    }
    if (write(fd, &dev, sizeof dev) != sizeof dev || ioctl(fd, UI_DEV_CREATE) < 0) { perror("uinput create"); return 1; }
    fprintf(stderr, "padbridge: virtual gamepad ready\n");

    unsigned prev = 0; int px = 0, py = 0, phx = 0, phy = 0;
    unsigned char b[8];
    while (readAll(b, 8)) {
        unsigned btn = b[0] | b[1] << 8;
        int x = (int16_t)(b[2] | b[3] << 8), y = (int16_t)(b[4] | b[5] << 8);
        int hx = (int8_t)b[6], hy = (int8_t)b[7];
        for (int i = 0; i < NBTN; ++i)
            if (((btn ^ prev) >> i) & 1) emit(EV_KEY, BUTTONS[i], (btn >> i) & 1);
        if (x != px) emit(EV_ABS, ABS_X, x);
        if (y != py) emit(EV_ABS, ABS_Y, y);
        if (hx != phx) emit(EV_ABS, ABS_HAT0X, hx);
        if (hy != phy) emit(EV_ABS, ABS_HAT0Y, hy);
        emit(EV_SYN, SYN_REPORT, 0);
        prev = btn; px = x; py = y; phx = hx; phy = hy;
    }
    for (int i = 0; i < NBTN; ++i) if ((prev >> i) & 1) emit(EV_KEY, BUTTONS[i], 0);
    emit(EV_ABS, ABS_X, 0); emit(EV_ABS, ABS_Y, 0); emit(EV_ABS, ABS_HAT0X, 0); emit(EV_ABS, ABS_HAT0Y, 0);
    emit(EV_SYN, SYN_REPORT, 0);
    ioctl(fd, UI_DEV_DESTROY);
    return 0;
}
