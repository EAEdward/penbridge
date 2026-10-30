#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "penbridge.h"

#define EVIOCGNAME(len) _IOC(_IOC_READ, 'E', 0x06, len)
#define EVIOCGBIT(ev, len) _IOC(_IOC_READ, 'E', 0x20 + (ev), len)

static int has_bit(const unsigned char *bits, int b)
{
    return (bits[b >> 3] >> (b & 7)) & 1;
}

/* The virtual keyboard carries the same name as the pen, so identity is checked
 * by the phys string we registered instead of by name. */
static int is_ours(const char *node)
{
    char p[160], buf[64] = {0};
    snprintf(p, sizeof p, "/sys/class/input/%s/device/phys", node);
    int fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    return n > 0 && strstr(buf, "penbridge-virtual") != NULL;
}

/* Ownership target: the keyboard interface that exposes KEY_F19 (ROM keyCode 194
 * on the Pro pen's pinch). Later nodes are left alone - the pinch is delivered on
 * exactly one interface, and grabbing a second one would duplicate the edge. */
int pen_open(char *path, size_t cap)
{
    DIR *d = opendir("/dev/input");
    if (!d)
        return -1;

    struct dirent *de;
    int found = -1;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "event", 5) != 0)
            continue;
        char p[128];
        snprintf(p, sizeof p, "/dev/input/%s", de->d_name);
        int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;

        char name[128] = {0};
        unsigned char keys[(PB_KEY_MAX + 8) / 8];
        memset(keys, 0, sizeof keys);
        if (ioctl(fd, EVIOCGNAME(sizeof name - 1), name) <= 0 ||
            ioctl(fd, EVIOCGBIT(PB_EV_KEY, sizeof keys), keys) < 0 ||
            !has_bit(keys, PB_KEY_F19) ||
            is_ours(de->d_name)) {
            close(fd);
            continue;
        }
        pb_log("pen keyboard: %s (%s)", name, p);
        snprintf(path, cap, "%s", p);
        found = fd;
        break;
    }
    closedir(d);
    return found;
}
