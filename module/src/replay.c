#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "penbridge.h"

struct pb_input_id { uint16_t bustype, vendor, product, version; };
struct pb_uinput_setup { struct pb_input_id id; char name[80]; uint32_t ff; };

#define UI_DEV_CREATE _IO('U', 1)
#define UI_DEV_DESTROY _IO('U', 2)
#define UI_DEV_SETUP _IOW('U', 3, struct pb_uinput_setup)
#define UI_SET_EVBIT _IOW('U', 100, int)
#define UI_SET_KEYBIT _IOW('U', 101, int)
#define UI_SET_PHYS _IOW('U', 108, char *)
#define EVIOCGBIT(ev, len) _IOC(_IOC_READ, 'E', 0x20 + (ev), len)

static int has_bit(const unsigned char *bits, int b)
{
    return (bits[b >> 3] >> (b & 7)) & 1;
}

static int write_event(int fd, uint16_t type, uint16_t code, int32_t value)
{
    struct pb_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    return write(fd, &ev, sizeof ev) == (ssize_t)sizeof ev ? 0 : -1;
}

int replay_forward(int fd, const struct pb_event *ev)
{
    return write(fd, ev, sizeof *ev) == (ssize_t)sizeof *ev ? 0 : -1;
}

/* One edge in, one complete press pair out. DOWN and UP are written in a single
 * batch with no interval: the app sees an ordinary instantaneous key press, and
 * no synthetic key is ever left held (see docs/DESIGN.md, release safeguard). */
int replay_pair(int fd, unsigned code)
{
    struct pb_event batch[4];
    memset(batch, 0, sizeof batch);
    batch[0].type = PB_EV_KEY; batch[0].code = (uint16_t)code; batch[0].value = 1;
    batch[1].type = PB_EV_SYN; batch[1].code = PB_SYN_REPORT;
    batch[2].type = PB_EV_KEY; batch[2].code = (uint16_t)code; batch[2].value = 0;
    batch[3].type = PB_EV_SYN; batch[3].code = PB_SYN_REPORT;
    return write(fd, batch, sizeof batch) == (ssize_t)sizeof batch ? 0 : -1;
}

/* Mirror the pen's key set so the replay device is indistinguishable from the
 * interface it replaces; F19 itself is included for the pairs above. */
int replay_open(int src_fd)
{
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        pb_log("uinput unavailable: %s", strerror(errno));
        return -1;
    }
    ioctl(fd, UI_SET_EVBIT, PB_EV_KEY);
    ioctl(fd, UI_SET_EVBIT, PB_EV_SYN);

    unsigned char keys[(PB_KEY_MAX + 8) / 8];
    memset(keys, 0, sizeof keys);
    if (ioctl(src_fd, EVIOCGBIT(PB_EV_KEY, sizeof keys), keys) >= 0)
        for (int k = 0; k <= PB_KEY_MAX; k++)
            if (has_bit(keys, k))
                ioctl(fd, UI_SET_KEYBIT, k);
    ioctl(fd, UI_SET_KEYBIT, PB_KEY_F19);

    struct pb_uinput_setup us;
    memset(&us, 0, sizeof us);
    snprintf(us.name, sizeof us.name, "Xiaomi Focus Pen Pro Keyboard");
    ioctl(fd, UI_SET_PHYS, "penbridge-virtual");
    us.id.bustype = 0x0005;
    us.id.vendor = 0x0022;
    us.id.product = 0x5081;
    us.id.version = 0x0111;
    if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        pb_log("virtual keyboard setup failed: %s", strerror(errno));
        close(fd);
        return -1;
    }
    pb_log("virtual keyboard ready: \"%s\" bus=0005 vid=0022 pid=5081", us.name);
    return fd;
}

void replay_close(int fd)
{
    if (fd < 0)
        return;
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
}

/* A framework-only restart keeps our process alive but drops the device from
 * InputReader, so liveness is judged by the registered phys string. */
int replay_present(void)
{
    DIR *d = opendir("/sys/class/input");
    if (!d)
        return 1;
    struct dirent *de;
    int found = 0;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "event", 5) != 0)
            continue;
        char p[160], buf[128] = {0};
        snprintf(p, sizeof p, "/sys/class/input/%s/device/phys", de->d_name);
        int f = open(p, O_RDONLY | O_CLOEXEC);
        if (f < 0)
            continue;
        ssize_t n = read(f, buf, sizeof buf - 1);
        close(f);
        if (n > 0 && strstr(buf, "penbridge-virtual")) {
            found = 1;
            break;
        }
    }
    closedir(d);
    return found;
}
