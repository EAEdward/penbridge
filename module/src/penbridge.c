// penbridge - system-level stylus button bridge for Android
//   tx <peer-ip> <port>   : Xiaomi tablet. Grabs the Focus Pen keyboard interface,
//                           forwards KEY_F19 press/release over UDP.
//   rx <port> [devname]   : Samsung tablet. Grabs the EMR stylus device and exposes a
//                           uinput clone whose BTN_STYLUS follows the remote flag.
//   list                  : print input devices with names and ids.
//
// Self-contained (no linux/input.h) so it builds with any musl/glibc cross toolchain.
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* ---- kernel constants (stable ABI) ---- */
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_ABS 0x03
#define EV_MSC 0x04
#define EV_SW  0x05
#define SYN_REPORT 0

#define BTN_TOOL_PEN    0x140
#define BTN_TOOL_RUBBER 0x141
#define BTN_TOUCH       0x14a
#define BTN_STYLUS      0x14b

#define KEY_F19 189

#define EV_MAX  0x1f
#define KEY_MAX 0x2ff
#define ABS_MAX 0x3f
#define SW_MAX  0x11
#define PROP_MAX 0x1f

struct pb_timeval { long tv_sec; long tv_usec; };
struct input_event { struct pb_timeval time; uint16_t type; uint16_t code; int32_t value; };
struct input_id { uint16_t bustype, vendor, product, version; };
struct input_absinfo { int32_t value, minimum, maximum, fuzz, flat, resolution; };
struct uinput_setup { struct input_id id; char name[80]; uint32_t ff_effects_max; };
struct uinput_abs_setup { uint16_t code; uint16_t pad; struct input_absinfo absinfo; };

#define EVIOCGNAME(len)  _IOC(_IOC_READ, 'E', 0x06, len)
#define EVIOCGPHYS(len)  _IOC(_IOC_READ, 'E', 0x07, len)
#define EVIOCGID         _IOR('E', 0x02, struct input_id)
#define EVIOCGBIT(ev, len) _IOC(_IOC_READ, 'E', 0x20 + (ev), len)
#define EVIOCGABS(abs)   _IOR('E', 0x40 + (abs), struct input_absinfo)
#define EVIOCGPROP(len)  _IOC(_IOC_READ, 'E', 0x09, len)
#define EVIOCGRAB        _IOW('E', 0x90, int)

#define UI_DEV_CREATE   _IO('U', 1)
#define UI_DEV_DESTROY  _IO('U', 2)
#define UI_DEV_SETUP    _IOW('U', 3, struct uinput_setup)
#define UI_ABS_SETUP    _IOW('U', 4, struct uinput_abs_setup)
#define UI_SET_EVBIT    _IOW('U', 100, int)
#define UI_SET_KEYBIT   _IOW('U', 101, int)
#define UI_SET_ABSBIT   _IOW('U', 103, int)
#define UI_SET_SWBIT    _IOW('U', 109, int)
#define UI_SET_PROPBIT  _IOW('U', 110, int)
#define UI_SET_PHYS     _IOW('U', 108, char *)

#define LOG(fmt, ...) do { printf("[penbridge] " fmt "\n", ##__VA_ARGS__); fflush(stdout); } while (0)

static volatile sig_atomic_t g_stop = 0;
static void on_sig(int s) { (void)s; g_stop = 1; }

static int test_bit(int bit, const unsigned char *bits) {
    return (bits[bit >> 3] >> (bit & 7)) & 1;
}

static int dev_name(int fd, char *out, size_t len) {
    memset(out, 0, len);
    if (ioctl(fd, EVIOCGNAME(len - 1), out) < 0) return -1;
    return (int)strlen(out);
}

static int dev_is_stylus(int fd) {
    unsigned char kbits[(KEY_MAX + 8) / 8];
    memset(kbits, 0, sizeof kbits);
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof kbits), kbits) < 0) return 0;
    return test_bit(BTN_TOOL_PEN, kbits) || test_bit(BTN_STYLUS, kbits);
}

/* returns fd of the first device whose name matches, or -1 */
static int find_by_name(const char *want, char *path_out, size_t plen) {
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent *de;
    int found = -1;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;
        char path[128];
        snprintf(path, sizeof path, "/dev/input/%s", de->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        char name[128];
        if (dev_name(fd, name, sizeof name) > 0 && strcmp(name, want) == 0) {
            if (path_out) snprintf(path_out, plen, "%s", path);
            found = fd;
            break;
        }
        close(fd);
    }
    closedir(d);
    return found;
}

static int find_pen_keyboard(char *path_out, size_t plen) {
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent *de;
    int found = -1;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;
        char path[128];
        snprintf(path, sizeof path, "/dev/input/%s", de->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        char name[128];
        if (dev_name(fd, name, sizeof name) <= 0) { close(fd); continue; }
        unsigned char kbits[(KEY_MAX + 8) / 8];
        memset(kbits, 0, sizeof kbits);
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof kbits), kbits) < 0) { close(fd); continue; }
        if (test_bit(KEY_F19, kbits) && strstr(name, "Focus Pen")) {
            LOG("pen keyboard: %s (%s)", name, path);
            if (path_out) snprintf(path_out, plen, "%s", path);
            found = fd;
            break;
        }
        close(fd);
    }
    closedir(d);
    return found;
}

static const char *name_of(int fd) {
    static char buf[128];
    if (dev_name(fd, buf, sizeof buf) <= 0) return "?";
    return buf;
}

static void emit(int fd, int type, int code, int val) {
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = (uint16_t)type;
    ev.code = (uint16_t)code;
    ev.value = val;
    if (write(fd, &ev, sizeof ev) < 0) { /* best effort */ }
}

#define MAXDEV 64

static int has_key(int fd, int key) {
    unsigned char kbits[(KEY_MAX + 8) / 8];
    memset(kbits, 0, sizeof kbits);
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof kbits), kbits) < 0) return 0;
    return test_bit(key, kbits);
}

static int create_clone(int src_fd, const char *name_override);

/* ---------------------------------------------------------------- solo mode */
struct clone_ctx { int src; int ufd; int in_range; int local_btn; int emitted; };

static int run_solo(const char *kbd_sub, const char *sty_sub, const char *name_override) {
    int kbd[MAXDEV];
    struct clone_ctx cl[8];
    int ncl = 0, nkbd = 0;

    DIR *d = opendir("/dev/input");
    if (!d) { perror("opendir"); return 2; }
    struct dirent *de;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "event", 5)) continue;
        char path[128];
        snprintf(path, sizeof path, "/dev/input/%s", de->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        char name[128];
        if (dev_name(fd, name, sizeof name) <= 0) { close(fd); continue; }

        if (strstr(name, kbd_sub) && has_key(fd, KEY_F19)) {
            if (nkbd < MAXDEV) { kbd[nkbd++] = fd; LOG("keyboard: %s (%s)", name, path); }
            else close(fd);
            continue;
        }
        if (strstr(name, sty_sub) && (has_key(fd, BTN_TOOL_PEN) || has_key(fd, BTN_STYLUS))) {
            if (ncl < 8) {
                LOG("stylus: %s (%s)", name, path);
                cl[ncl].src = fd;
                cl[ncl].ufd = -1;
                cl[ncl].in_range = 0;
                cl[ncl].local_btn = 0;
                cl[ncl].emitted = 0;
                ncl++;
            } else close(fd);
            continue;
        }
        close(fd);
    }
    closedir(d);

    if (nkbd == 0) {
        // The pen is a Bluetooth HID device: at boot (or after a disconnect) it is simply not
        // there yet. Running without it would grab the stylus and never see KEY_F19, so hand
        // control back to the supervisor instead of half-starting.
        LOG("pen keyboard not connected yet, exiting for retry");
        for (int i = 0; i < ncl; i++) if (cl[i].src >= 0) close(cl[i].src);
        return 5;
    }
    if (ncl == 0) { LOG("error: no stylus matched \"%s\"", sty_sub); return 3; }

    for (int i = 0; i < nkbd; i++)
        if (ioctl(kbd[i], EVIOCGRAB, 1) < 0)
            LOG("grab keyboard failed: %s", strerror(errno));
    LOG("vendor pen gestures disabled (%d keyboard device(s))", nkbd);

    for (int i = 0; i < ncl; i++) {
        struct input_event ev;
        while (read(cl[i].src, &ev, sizeof ev) == (ssize_t)sizeof ev) {}
        if (ioctl(cl[i].src, EVIOCGRAB, 1) < 0) {
            // A stale instance still owns the device. Never run without the grab: the system
            // would then receive both the original and the cloned events.
            LOG("grab stylus failed: %s, exiting for retry", strerror(errno));
            for (int k = 0; k <= i; k++) {
                if (cl[k].ufd >= 0) { ioctl(cl[k].ufd, UI_DEV_DESTROY); close(cl[k].ufd); cl[k].ufd = -1; }
                ioctl(cl[k].src, EVIOCGRAB, 0);
                close(cl[k].src);
            }
            for (int k = 0; k < nkbd; k++) { ioctl(kbd[k], EVIOCGRAB, 0); close(kbd[k]); }
            return 6;
        }
        cl[i].ufd = create_clone(cl[i].src, name_override);
        if (cl[i].ufd < 0) {
            LOG("clone failed on %s (uinput not ready?)", name_of(cl[i].src));
            ioctl(cl[i].src, EVIOCGRAB, 0);
        }
    }
    {
        int created = 0;
        for (int i = 0; i < ncl; i++) if (cl[i].ufd >= 0) created++;
        if (created == 0) {
            // /dev/uinput may not be ready this early after boot; let the supervisor retry.
            LOG("no virtual stylus created, exiting for retry");
            for (int i = 0; i < ncl; i++) if (cl[i].src >= 0) { ioctl(cl[i].src, EVIOCGRAB, 0); close(cl[i].src); }
            for (int i = 0; i < nkbd; i++) { ioctl(kbd[i], EVIOCGRAB, 0); close(kbd[i]); }
            return 4;
        }
    }

    int remote = 0;
    while (!g_stop) {
        struct pollfd pfds[MAXDEV + 8];
        int idx_map[MAXDEV + 8];
        int n = 0;
        for (int i = 0; i < nkbd; i++) {
            pfds[n].fd = kbd[i]; pfds[n].events = POLLIN; pfds[n].revents = 0;
            idx_map[n] = -2 - i; n++;
        }
        for (int i = 0; i < ncl; i++) {
            if (cl[i].ufd < 0) continue;
            pfds[n].fd = cl[i].src; pfds[n].events = POLLIN; pfds[n].revents = 0;
            idx_map[n] = i; n++;
        }
        int pr = poll(pfds, n, 300);
        if (pr < 0 && errno == EINTR) continue;
        if (pr <= 0) continue;

        for (int k = 0; k < n; k++) {
            if (!(pfds[k].revents & POLLIN)) continue;
            int tag = idx_map[k];

            if (tag <= -2) {
                int ki = -2 - tag;
                struct input_event ev;
                ssize_t r;
                while ((r = read(kbd[ki], &ev, sizeof ev)) == (ssize_t)sizeof ev) {
                    if (ev.type == EV_KEY && ev.code == KEY_F19) {
                        int ns = ev.value ? 1 : 0;
                        if (ns == remote) continue;
                        remote = ns;
                        LOG("squeeze %s", remote ? "DOWN" : "UP");
                        for (int i = 0; i < ncl; i++) {
                            if (cl[i].ufd < 0) continue;
                            int eff = remote || cl[i].local_btn;
                            if (eff != cl[i].emitted && (cl[i].in_range || !eff)) {
                                emit(cl[i].ufd, EV_KEY, BTN_STYLUS, eff);
                                emit(cl[i].ufd, EV_SYN, SYN_REPORT, 0);
                                cl[i].emitted = eff;
                            }
                        }
                    }
                }
                continue;
            }

            int ci = tag;
            if (cl[ci].ufd < 0) continue;
            struct input_event ev;
            ssize_t r;
            while ((r = read(cl[ci].src, &ev, sizeof ev)) == (ssize_t)sizeof ev) {
                if (ev.type == EV_KEY && ev.code == BTN_TOOL_PEN) {
                    int now = ev.value ? 1 : 0;
                    if (cl[ci].in_range && !now && cl[ci].emitted) {
                        emit(cl[ci].ufd, EV_KEY, BTN_STYLUS, 0);
                        cl[ci].emitted = 0;
                    }
                    cl[ci].in_range = now;
                }
                if (ev.type == EV_KEY && ev.code == BTN_STYLUS) {
                    cl[ci].local_btn = ev.value ? 1 : 0;
                    int eff = cl[ci].local_btn || remote;
                    if (eff != cl[ci].emitted) {
                        emit(cl[ci].ufd, EV_KEY, BTN_STYLUS, eff);
                        cl[ci].emitted = eff;
                    }
                    continue;
                }
                if (ev.type == EV_MSC) continue;
                if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                    int eff = cl[ci].local_btn || remote;
                    if (eff != cl[ci].emitted) {
                        emit(cl[ci].ufd, EV_KEY, BTN_STYLUS, eff);
                        cl[ci].emitted = eff;
                    }
                }
                if (write(cl[ci].ufd, &ev, sizeof ev) < 0) {
                    if (errno == ENODEV || errno == EINVAL) break;
                }
            }
            if (r < 0 && errno != EAGAIN && errno != EINTR) {
                LOG("stylus %d lost, restarting", ci);
                g_stop = 1;
            }
        }
    }

    for (int i = 0; i < ncl; i++) {
        if (cl[i].ufd >= 0) {
            emit(cl[i].ufd, EV_KEY, BTN_STYLUS, 0);
            emit(cl[i].ufd, EV_SYN, SYN_REPORT, 0);
            ioctl(cl[i].ufd, UI_DEV_DESTROY);
            close(cl[i].ufd);
        }
        if (cl[i].src >= 0) { ioctl(cl[i].src, EVIOCGRAB, 0); close(cl[i].src); }
    }
    for (int i = 0; i < nkbd; i++) { ioctl(kbd[i], EVIOCGRAB, 0); close(kbd[i]); }
    return 0;
}

/* ---------------------------------------------------------------- tx side */
static int run_tx(const char *peer, int port) {
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof dst);
    dst.sin_family = AF_INET;
    dst.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, peer, &dst.sin_addr) != 1) {
        LOG("bad peer address %s", peer);
        return 2;
    }
    int sock = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (sock < 0) { perror("socket"); return 2; }

    while (!g_stop) {
        char path[128];
        int fd = find_pen_keyboard(path, sizeof path);
        if (fd < 0) {
            LOG("waiting for Focus Pen keyboard device...");
            sleep(2);
            continue;
        }
        if (ioctl(fd, EVIOCGRAB, 1) < 0)
            LOG("EVIOCGRAB failed on %s: %s", path, strerror(errno));
        else
            LOG("grabbed %s: vendor stylus gestures are now disabled", path);

        sendto(sock, "0", 1, 0, (struct sockaddr *)&dst, sizeof dst);
        time_t last_beat = time(NULL);
        int state = 0;

        while (!g_stop) {
            struct pollfd pfd = { .fd = fd, .events = POLLIN };
            int pr = poll(&pfd, 1, 500);
            if (pr < 0 && errno == EINTR) continue;
            if (pr > 0) {
                struct input_event ev;
                ssize_t n;
                while ((n = read(fd, &ev, sizeof ev)) == (ssize_t)sizeof ev) {
                    if (ev.type == EV_KEY && ev.code == KEY_F19) {
                        int ns = ev.value ? 1 : 0;
                        if (ns != state) {
                            state = ns;
                            sendto(sock, state ? "1" : "0", 1, 0, (struct sockaddr *)&dst, sizeof dst);
                            LOG("squeeze %s", state ? "DOWN" : "UP");
                        }
                    }
                }
                if (n < 0 && errno != EAGAIN && errno != EINTR) {
                    LOG("pen device lost (%s), rescanning", strerror(errno));
                    break;
                }
            }
            time_t now = time(NULL);
            if (now - last_beat >= 1) {
                last_beat = now;
                sendto(sock, state ? "1" : "0", 1, 0, (struct sockaddr *)&dst, sizeof dst);
            }
        }
        ioctl(fd, EVIOCGRAB, 0);
        close(fd);
        sendto(sock, "0", 1, 0, (struct sockaddr *)&dst, sizeof dst);
        if (g_stop) break;
        sleep(1);
    }
    close(sock);
    return 0;
}

/* ---------------------------------------------------------------- rx side */
static int create_clone(int src_fd, const char *name_override) {
    int ufd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (ufd < 0) { perror("open /dev/uinput"); return -1; }

    char name[128] = {0};
    dev_name(src_fd, name, sizeof name);
    if (name_override && *name_override) snprintf(name, sizeof name, "%s", name_override);

    struct input_id ids;
    memset(&ids, 0, sizeof ids);
    ioctl(src_fd, EVIOCGID, &ids);
    if (name_override && *name_override) {
        // "S Pen mode": present the clone as a Samsung EMR pen so apps that key off the
        // input device identity (name / vendor / bus) treat it as an S Pen.
        ids.bustype = 0x18; ids.vendor = 0x04e8; ids.product = 0x0000; ids.version = 0x0100;
    }

    unsigned char evbits[(EV_MAX + 8) / 8];
    memset(evbits, 0, sizeof evbits);
    ioctl(src_fd, EVIOCGBIT(0, sizeof evbits), evbits);

    ioctl(ufd, UI_SET_EVBIT, EV_SYN);

    if (test_bit(EV_KEY, evbits)) {
        unsigned char kbits[(KEY_MAX + 8) / 8];
        memset(kbits, 0, sizeof kbits);
        ioctl(src_fd, EVIOCGBIT(EV_KEY, sizeof kbits), kbits);
        ioctl(ufd, UI_SET_EVBIT, EV_KEY);
        for (int k = 0; k <= KEY_MAX; k++)
            if (test_bit(k, kbits)) ioctl(ufd, UI_SET_KEYBIT, k);
        ioctl(ufd, UI_SET_KEYBIT, BTN_STYLUS);
    }
    if (test_bit(EV_ABS, evbits)) {
        unsigned char abits[(ABS_MAX + 8) / 8];
        memset(abits, 0, sizeof abits);
        ioctl(src_fd, EVIOCGBIT(EV_ABS, sizeof abits), abits);
        ioctl(ufd, UI_SET_EVBIT, EV_ABS);
        for (int a = 0; a <= ABS_MAX; a++) {
            if (!test_bit(a, abits)) continue;
            struct input_absinfo ai;
            memset(&ai, 0, sizeof ai);
            if (ioctl(src_fd, EVIOCGABS(a), &ai) < 0) continue;
            ioctl(ufd, UI_SET_ABSBIT, a);
            struct uinput_abs_setup uas;
            memset(&uas, 0, sizeof uas);
            uas.code = (uint16_t)a;
            uas.absinfo = ai;
            if (ioctl(ufd, UI_ABS_SETUP, &uas) < 0)
                LOG("UI_ABS_SETUP(%d) failed: %s", a, strerror(errno));
        }
    }
    if (test_bit(EV_SW, evbits)) {
        unsigned char sbits[(SW_MAX + 8) / 8];
        memset(sbits, 0, sizeof sbits);
        ioctl(src_fd, EVIOCGBIT(EV_SW, sizeof sbits), sbits);
        ioctl(ufd, UI_SET_EVBIT, EV_SW);
        for (int s = 0; s <= SW_MAX; s++)
            if (test_bit(s, sbits)) ioctl(ufd, UI_SET_SWBIT, s);
    }
    {
        unsigned char pbits[(PROP_MAX + 8) / 8];
        memset(pbits, 0, sizeof pbits);
        if (ioctl(src_fd, EVIOCGPROP(sizeof pbits), pbits) >= 0)
            for (int p = 0; p <= PROP_MAX; p++)
                if (test_bit(p, pbits)) ioctl(ufd, UI_SET_PROPBIT, p);
    }
    char phys[128] = {0};
    if (ioctl(src_fd, EVIOCGPHYS(sizeof phys - 1), phys) >= 0 && phys[0])
        ioctl(ufd, UI_SET_PHYS, phys);

    struct uinput_setup us;
    memset(&us, 0, sizeof us);
    us.id = ids;
    snprintf(us.name, sizeof us.name, "%s", name);
    if (ioctl(ufd, UI_DEV_SETUP, &us) < 0) {
        LOG("UI_DEV_SETUP failed: %s", strerror(errno));
        close(ufd);
        return -1;
    }
    if (ioctl(ufd, UI_DEV_CREATE) < 0) {
        LOG("UI_DEV_CREATE failed: %s", strerror(errno));
        close(ufd);
        return -1;
    }
    LOG("virtual stylus ready: name=\"%s\" bus=%04x vid=%04x pid=%04x",
        name, ids.bustype, ids.vendor, ids.product);
    return ufd;
}

static int run_rx(const char *want, int port) {
    int sock = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (sock < 0) { perror("socket"); return 2; }
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(sock, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("bind"); return 2; }
    LOG("rx: listening on udp/%d for device \"%s\"", port, want);

    while (!g_stop) {
        char path[128];
        int src = find_by_name(want, path, sizeof path);
        if (src < 0) {
            LOG("waiting for \"%s\"...", want);
            sleep(2);
            continue;
        }
        { struct input_event ev; while (read(src, &ev, sizeof ev) == (ssize_t)sizeof ev) {} }
        if (ioctl(src, EVIOCGRAB, 1) < 0)
            LOG("EVIOCGRAB failed: %s", strerror(errno));
        int ufd = create_clone(src, NULL);
        if (ufd < 0) { ioctl(src, EVIOCGRAB, 0); close(src); sleep(2); continue; }

        int in_range = 0, local_btn = 0, remote = 0, emitted = 0;
        time_t last_packet = time(NULL);

        while (!g_stop) {
            struct pollfd pfds[2] = {
                { .fd = src,  .events = POLLIN },
                { .fd = sock, .events = POLLIN },
            };
            int pr = poll(pfds, 2, 300);
            if (pr < 0 && errno == EINTR) continue;

            if (pfds[1].revents & POLLIN) {
                char buf[64];
                ssize_t n = recv(sock, buf, sizeof buf - 1, 0);
                if (n > 0) {
                    buf[n] = 0;
                    int ns = (buf[0] == '1');
                    last_packet = time(NULL);
                    if (ns != remote) {
                        remote = ns;
                        LOG("remote squeeze %s", remote ? "DOWN" : "UP");
                        int eff = local_btn || remote;
                        if (eff != emitted && (in_range || !eff)) {
                            emit(ufd, EV_KEY, BTN_STYLUS, eff);
                            emit(ufd, EV_SYN, SYN_REPORT, 0);
                            emitted = eff;
                        }
                    }
                }
            }

            if (pfds[0].revents & POLLIN) {
                struct input_event ev;
                ssize_t n;
                while ((n = read(src, &ev, sizeof ev)) == (ssize_t)sizeof ev) {
                    if (ev.type == EV_KEY && (ev.code == BTN_TOOL_PEN || ev.code == BTN_TOUCH)) {
                        int now_in = ev.value ? 1 : 0;
                        if (in_range && !now_in && emitted) {
                            emit(ufd, EV_KEY, BTN_STYLUS, 0);
                            emitted = 0;
                        }
                        in_range = now_in;
                    }
                    if (ev.type == EV_KEY && ev.code == BTN_STYLUS) {
                        local_btn = ev.value ? 1 : 0;
                        int eff = local_btn || remote;
                        if (eff != emitted) { emit(ufd, EV_KEY, BTN_STYLUS, eff); emitted = eff; }
                        continue;
                    }
                    if (ev.type == EV_MSC) continue;
                    if (write(ufd, &ev, sizeof ev) < 0) {
                        if (errno == ENODEV || errno == EINVAL) {
                            LOG("uinput write failed: %s", strerror(errno));
                            break;
                        }
                    }
                    if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                        int eff = local_btn || remote;
                        if (eff != emitted) {
                            emit(ufd, EV_KEY, BTN_STYLUS, eff);
                            emitted = eff;
                            emit(ufd, EV_SYN, SYN_REPORT, 0);
                        }
                    }
                }
                if (n < 0 && errno != EAGAIN && errno != EINTR) {
                    LOG("source device lost (%s), rescanning", strerror(errno));
                    break;
                }
            }

            if (remote && time(NULL) - last_packet > 30) {
                remote = 0;
                LOG("remote timeout: releasing button");
                if (emitted) { emit(ufd, EV_KEY, BTN_STYLUS, 0); emit(ufd, EV_SYN, SYN_REPORT, 0); emitted = 0; }
            }
        }

        emit(ufd, EV_KEY, BTN_STYLUS, 0);
        emit(ufd, EV_SYN, SYN_REPORT, 0);
        ioctl(ufd, UI_DEV_DESTROY);
        close(ufd);
        ioctl(src, EVIOCGRAB, 0);
        close(src);
        if (g_stop) break;
        sleep(1);
    }
    close(sock);
    return 0;
}

static int do_list(void) {
    DIR *d = opendir("/dev/input");
    if (!d) { perror("opendir"); return 1; }
    struct dirent *de;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;
        char path[128];
        snprintf(path, sizeof path, "/dev/input/%s", de->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        char name[128];
        struct input_id id;
        dev_name(fd, name, sizeof name);
        ioctl(fd, EVIOCGID, &id);
        printf("%-20s name=\"%s\" bus=%04x vid=%04x pid=%04x stylus=%d\n",
               path, name, id.bustype, id.vendor, id.product, dev_is_stylus(fd));
        close(fd);
    }
    closedir(d);
    return 0;
}

int main(int argc, char **argv) {
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc < 2) {
        fprintf(stderr, "usage: %s solo <kbd-substr> <stylus-substr> [clone-name]\n"
                        "       %s tx <peer-ip> <port> | rx <port> [device-name] | list\n", argv[0], argv[0]);
        return 2;
    }
    if (!strcmp(argv[1], "list")) return do_list();
    if (!strcmp(argv[1], "solo") && argc >= 4)
        return run_solo(argv[2], argv[3], argc >= 5 ? argv[4] : NULL);
    if (!strcmp(argv[1], "tx") && argc >= 4) return run_tx(argv[2], atoi(argv[3]));
    if (!strcmp(argv[1], "rx") && argc >= 3) return run_rx(argc >= 4 ? argv[3] : "sec_e-pen", atoi(argv[2]));
    fprintf(stderr, "bad arguments\n");
    return 2;
}
