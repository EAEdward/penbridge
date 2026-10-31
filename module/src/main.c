#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "penbridge.h"

#define EVIOCGRAB _IOW('E', 0x90, int)
#define WATCHDOG_MS 5000

static volatile sig_atomic_t stop;

static void on_signal(int sig)
{
    (void)sig;
    stop = 1;
}

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* A framework-only restart keeps the uinput node in /sys but leaves InputReader
 * with an empty device table, so the reliable signal is system_server itself. */
static pid_t system_server_pid(void)
{
    DIR *d = opendir("/proc");
    if (!d)
        return -1;
    struct dirent *de;
    pid_t found = -1;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] < '0' || de->d_name[0] > '9')
            continue;
        char p[64], buf[64] = {0};
        snprintf(p, sizeof p, "/proc/%s/comm", de->d_name);
        int f = open(p, O_RDONLY | O_CLOEXEC);
        if (f < 0)
            continue;
        ssize_t n = read(f, buf, sizeof buf - 1);
        close(f);
        if (n > 0 && strstr(buf, "system_server")) {
            found = (pid_t)atoi(de->d_name);
            break;
        }
    }
    closedir(d);
    return found;
}

/* Poll timeout: wake up for the app's next switch deadline, else for the
 * watchdog. */
static int poll_timeout(const struct pb_arbiter *arb)
{
    long long deadline = arbiter_deadline(arb);
    if (deadline < 0)
        return 500;
    long long wait = deadline - now_ms();
    if (wait <= 0)
        return 0;
    return wait > 500 ? 500 : (int)wait;
}

int main(void)
{
    /* A soft reboot can start a second instance while the first is still alive;
     * the loser exits instead of fighting over the device. */
    int lock = open("/data/adb/penbridge.lock", O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) < 0) {
        pb_log("already running");
        return 0;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    int ufd = -1;
    int waiting = 0;
    pid_t ss = system_server_pid();

    while (!stop) {
        char path[128];
        int src = pen_open(path, sizeof path);
        if (src < 0) {
            /* No pen yet (asleep, or not paired): wait, never grab a lookalike. */
            if (!waiting)
                pb_log("waiting for the pen");
            waiting = 1;
            if (!stop)
                sleep(2);
            continue;
        }
        waiting = 0;
        if (ioctl(src, EVIOCGRAB, 1) < 0) {
            /* Never run ungrabbed: the system would see every key as well and the
             * tool would flip twice per edge. */
            pb_log("grab failed: %s", strerror(errno));
            close(src);
            sleep(2);
            continue;
        }
        if (ufd < 0) {
            ufd = replay_open(src);
            if (ufd < 0) {
                ioctl(src, EVIOCGRAB, 0);
                close(src);
                sleep(2);
                continue;
            }
        }
        pb_log("grabbed %s", path);

        struct pb_arbiter arb;
        arbiter_init(&arb);
        int lost = 0;
        long long last_watchdog = now_ms();

        while (!stop && !lost) {
            struct pb_decision due = arbiter_settle(&arb, now_ms());
            if (due.toggle && replay_pair(ufd, PB_KEY_F19) < 0) {
                lost = 1;
                break;
            }

            struct pollfd p = { .fd = src, .events = POLLIN };
            int pr = poll(&p, 1, poll_timeout(&arb));
            if (pr < 0 && errno == EINTR)
                continue;
            if (pr < 0) {
                lost = 1;
                break;
            }
            if (pr > 0) {
                struct pb_event ev;
                ssize_t n;
                int failed = 0;
                while ((n = read(src, &ev, sizeof ev)) == (ssize_t)sizeof ev) {
                    struct pb_decision d = arbiter_feed(&arb, &ev, now_ms());
                    if ((d.forward && replay_forward(ufd, &ev) < 0) ||
                        (d.toggle && replay_pair(ufd, PB_KEY_F19) < 0)) {
                        failed = 1;
                        break;
                    }
                }
                if (failed || (n < 0 && errno != EAGAIN && errno != EINTR)) {
                    /* The sink itself is gone; drop it so the outer loop builds a
                     * new virtual keyboard instead of writing into a dead fd. */
                    replay_close(ufd);
                    ufd = -1;
                    lost = 1;
                }
                continue;
            }

            if (now_ms() - last_watchdog < WATCHDOG_MS)
                continue;
            last_watchdog = now_ms();
            pid_t pid = system_server_pid();
            int restarted = pid > 0 && ss > 0 && pid != ss;
            ss = pid;
            if (!restarted && replay_present())
                continue;
            pb_log("%s, rebuilding virtual keyboard",
                   restarted ? "framework restarted" : "virtual keyboard gone");
            replay_close(ufd);
            ufd = replay_open(src);
            if (ufd < 0)
                lost = 1;
        }

        ioctl(src, EVIOCGRAB, 0);
        close(src);
        if (!stop) {
            pb_log("pen lost, waiting for it to come back");
            sleep(2);
        }
    }

    replay_close(ufd);
    pb_log("stopped");
    return 0;
}
