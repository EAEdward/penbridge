// penbridge-replay: turn the pen's "pinch once" into "press = toggle, release = toggle back"
// by replaying a synthetic KEY_F19 tap pair on a virtual keyboard.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_MSC 0x04
#define SYN_REPORT 0
#define KEY_F19 189
#define EV_MAX 0x1f
#define KEY_MAX 0x2ff

struct pb_timeval { long tv_sec; long tv_usec; };
struct input_event { struct pb_timeval time; uint16_t type; uint16_t code; int32_t value; };
struct input_id { uint16_t bustype, vendor, product, version; };
struct uinput_setup { struct input_id id; char name[80]; uint32_t ff; };

#define EVIOCGNAME(len) _IOC(_IOC_READ, 'E', 0x06, len)
#define EVIOCGBIT(ev, len) _IOC(_IOC_READ, 'E', 0x20 + (ev), len)
#define EVIOCGRAB _IOW('E', 0x90, int)
#define UI_DEV_CREATE _IO('U', 1)
#define UI_DEV_DESTROY _IO('U', 2)
#define UI_DEV_SETUP _IOW('U', 3, struct uinput_setup)
#define UI_SET_EVBIT _IOW('U', 100, int)
#define UI_SET_KEYBIT _IOW('U', 101, int)
#define UI_SET_PHYS _IOW('U', 108, char *)

#define LOG(...) do{printf("[penbridge] " __VA_ARGS__);printf("\n");fflush(stdout);}while(0)

/* Synthetic key-press width, runtime-tunable (see TAP_MS_FILE). */
#define TAP_MS_DEFAULT 45
static int tap_ms = TAP_MS_DEFAULT;

static volatile sig_atomic_t stop;
static void onsig(int s){ (void)s; stop=1; }

static int test_bit(int b,const unsigned char*p){return (p[b>>3]>>(b&7))&1;}

static int find_pen(char *path,size_t n){
    DIR *d=opendir("/dev/input"); if(!d) return -1;
    struct dirent *de; int found=-1;
    while((de=readdir(d))){
        if(strncmp(de->d_name,"event",5)) continue;
        char p[128]; snprintf(p,sizeof p,"/dev/input/%s",de->d_name);
        int fd=open(p,O_RDONLY|O_NONBLOCK|O_CLOEXEC); if(fd<0) continue;
        char name[128]={0};
        if(ioctl(fd,EVIOCGNAME(sizeof name-1),name)<=0){close(fd);continue;}
        unsigned char kb[(KEY_MAX+8)/8]; memset(kb,0,sizeof kb);
        if(ioctl(fd,EVIOCGBIT(EV_KEY,sizeof kb),kb)<0){close(fd);continue;}
        /* Never match our own virtual keyboard: it carries the same name and would
           otherwise be grabbed instead of the real pen. */
        char sp[160]; snprintf(sp,sizeof sp,"/sys/class/input/%s/device/phys",de->d_name);
        int pf=open(sp,O_RDONLY|O_CLOEXEC); char pb[64]={0};
        if(pf>=0){ ssize_t pn=read(pf,pb,sizeof pb-1); (void)pn; close(pf); }
        if(strstr(pb,"penbridge-virtual")){ close(fd); continue; }
        if(strstr(name,"Focus Pen")&&test_bit(KEY_F19,kb)){
            LOG("pen keyboard: %s (%s)",name,p);
            snprintf(path,n,"%s",p); found=fd; break;
        }
        close(fd);
    }
    closedir(d); return found;
}

static int make_keyboard(int src){
    int fd=open("/dev/uinput",O_WRONLY|O_NONBLOCK|O_CLOEXEC);
    if(fd<0){perror("uinput");return -1;}
    ioctl(fd,UI_SET_EVBIT,EV_KEY);
    ioctl(fd,UI_SET_EVBIT,EV_SYN);
    ioctl(fd,UI_SET_EVBIT,EV_MSC);
    // Mirror every key the real pen has: F19 is hijacked, everything else (body double-tap,
    // slides, shortcuts) must keep reaching the system untouched.
    unsigned char kb[(KEY_MAX+8)/8]; memset(kb,0,sizeof kb);
    if(ioctl(src,EVIOCGBIT(EV_KEY,sizeof kb),kb)>=0)
        for(int k=0;k<=KEY_MAX;k++) if(test_bit(k,kb)) ioctl(fd,UI_SET_KEYBIT,k);
    ioctl(fd,UI_SET_KEYBIT,KEY_F19);
    struct uinput_setup us; memset(&us,0,sizeof us);
    snprintf(us.name,sizeof us.name,"Xiaomi Focus Pen Pro Keyboard");
    ioctl(fd,UI_SET_PHYS,"penbridge-virtual");
    us.id.bustype=0x0005; us.id.vendor=0x0022; us.id.product=0x5081; us.id.version=0x0111;
    if(ioctl(fd,UI_DEV_SETUP,&us)<0){perror("UI_DEV_SETUP");close(fd);return -1;}
    if(ioctl(fd,UI_DEV_CREATE)<0){perror("UI_DEV_CREATE");close(fd);return -1;}
    LOG("virtual keyboard ready: \"%s\" bus=0005 vid=0022 pid=5081, tap %d ms",us.name,tap_ms);
    return fd;
}

static void emit(int fd,int code,int val){
    struct input_event e; memset(&e,0,sizeof e);
    e.type=EV_KEY; e.code=(uint16_t)code; e.value=val;
    if(write(fd,&e,sizeof e)<0) return;
    memset(&e,0,sizeof e); e.type=EV_SYN; e.code=SYN_REPORT;
    if(write(fd,&e,sizeof e)<0) return;
}

/* How long the synthetic key stays down. Too short and the framework/app can
   swallow it as a stray glitch; too long and it risks reading as a long press
   (500 ms) or tripping the kernel's key autorepeat. 45 ms sits safely between. */
#define TAP_MS_FILE "/data/adb/penbridge.tap_ms"

/* The width can be dialled in without a rebuild:
   echo 60 > /data/adb/penbridge.tap_ms   then restart the daemon. */
static void tap(int ufd){
    emit(ufd,KEY_F19,1);
    usleep((useconds_t)tap_ms*1000);
    emit(ufd,KEY_F19,0);
}

static void load_tap_ms(void){
    int f=open(TAP_MS_FILE,O_RDONLY|O_CLOEXEC);
    if(f<0) return;
    char b[32]={0}; ssize_t n=read(f,b,sizeof b-1); close(f);
    if(n<=0) return;
    int v=atoi(b);
    if(v<5||v>200) return;
    tap_ms=v;
}


/* The framework may restart (soft reboot) without killing us: the uinput device then
   disappears from /sys/class/input and every injected key goes nowhere. Detect that by
   the unique phys string we registered and rebuild the device. */
static int vkbd_present(void){
    DIR *d=opendir("/sys/class/input"); if(!d) return 1;
    struct dirent *de; int found=0;
    while((de=readdir(d))){
        if(strncmp(de->d_name,"event",5)) continue;
        char p[160]; snprintf(p,sizeof p,"/sys/class/input/%s/device/phys",de->d_name);
        int f=open(p,O_RDONLY|O_CLOEXEC); if(f<0) continue;
        char buf[128]={0}; ssize_t n=read(f,buf,sizeof buf-1); close(f);
        if(n>0&&strstr(buf,"penbridge-virtual")){found=1;break;}
    }
    closedir(d); return found;
}


/* A framework-only reboot keeps our process and the uinput node in /sys alive, but
   InputReader comes back with an empty device table: injected keys then go nowhere.
   The reliable signal is system_server itself. */
static pid_t system_server_pid(void){
    DIR *d=opendir("/proc"); if(!d) return -1;
    struct dirent *de; pid_t found=-1;
    while((de=readdir(d))){
        if(de->d_name[0]<'0'||de->d_name[0]>'9') continue;
        char p[64]; snprintf(p,sizeof p,"/proc/%s/comm",de->d_name);
        int f=open(p,O_RDONLY|O_CLOEXEC); if(f<0) continue;
        char b[64]={0}; ssize_t n=read(f,b,sizeof b-1); close(f);
        if(n>0&&strstr(b,"system_server")){ found=(pid_t)atoi(de->d_name); break; }
    }
    closedir(d); return found;
}

int main(void){
    /* Single instance: a framework soft-reboot can leave an old supervisor alive while a new
       one starts. flock makes the loser exit immediately instead of fighting over the device. */
    int lock=open("/data/adb/penbridge.lock",O_RDWR|O_CREAT|O_CLOEXEC,0600);
    if(lock>=0 && flock(lock,LOCK_EX|LOCK_NB)<0){ fprintf(stderr,"[penbridge] already running\n"); return 0; }

    signal(SIGINT,onsig); signal(SIGTERM,onsig);
    load_tap_ms();
    setvbuf(stdout,NULL,_IOLBF,0);
    int ufd=-1, pressed=0, restored=1;
    pid_t ss=system_server_pid();
    while(!stop){
        char path[128];
        int src=find_pen(path,sizeof path);
        if(src<0){ if(!stop) sleep(2); continue; }
        if(ioctl(src,EVIOCGRAB,1)<0){
            /* Another instance owns the device: never run without the grab, it would
               double-deliver every key. Hand back to the supervisor. */
            LOG("grab failed: %s, retrying",strerror(errno));
            close(src);
            sleep(2);
            continue;
        }
        LOG("grabbed %s",path);
        if(ufd<0){ ufd=make_keyboard(src); if(ufd<0){ ioctl(src,EVIOCGRAB,0); close(src); return 1; } }
        struct pollfd p={.fd=src,.events=POLLIN};
        int tick=0;
        while(!stop){
            int pr=poll(&p,1,500);
            if(pr<0&&errno==EINTR) continue;
            if(pr<=0){
                if(++tick%10==0){
                    pid_t now=system_server_pid();
                    int rebooted=(now>0&&ss>0&&now!=ss);
                    ss=now;
                    if(rebooted||!vkbd_present()){
                        LOG("%s", rebooted?"framework restarted, rebuilding keyboard"
                                          :"virtual keyboard gone, rebuilding");
                        ioctl(ufd,UI_DEV_DESTROY); close(ufd);
                        ufd=make_keyboard(src);
                        if(ufd<0){ LOG("rebuild failed, exiting"); break; }
                        ioctl(src,EVIOCGRAB,0); ioctl(src,EVIOCGRAB,1);
                    }
                }
                continue;
            }
            struct input_event ev; ssize_t n;
            while((n=read(src,&ev,sizeof ev))==(ssize_t)sizeof ev){
                if(ev.type!=EV_KEY||ev.code!=KEY_F19){
                    if(write(ufd,&ev,sizeof ev)<0) goto lost;
                    continue;
                }
                if(ev.value==2) continue;
                if(ev.value==1){
                    /* A repeat DOWN while we are already pressed means the press is still
                       in progress (or its UP was lost). The tool is already toggled, so
                       stay silent: tapping twice here would look like a double pinch and
                       the framework would treat it as a different gesture. */
                    if(!pressed){ pressed=1; restored=0; tap(ufd); LOG("down -> toggle"); }
                } else if(ev.value==0 && pressed){
                    pressed=0;
                    if(!restored){ tap(ufd); restored=1; LOG("up -> toggle back"); }
                }
            }
            if(n<0&&errno!=EAGAIN&&errno!=EINTR) break;
        }
    lost:
        if(pressed&&!restored){ tap(ufd); restored=1; pressed=0; }
        ioctl(src,EVIOCGRAB,0); close(src);
        if(!stop){ LOG("pen disconnected, waiting for it to come back"); sleep(2); }
    }
    if(ufd>=0){ ioctl(ufd,UI_DEV_DESTROY); close(ufd); }
    LOG("stopped");
    return 0;
}
