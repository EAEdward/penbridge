// penbridge-replay: turn the pen's "pinch once" into "press = toggle, release = toggle back"
// by replaying a synthetic KEY_F19 tap pair on a virtual keyboard.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
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

#define LOG(...) do{printf("[penbridge] " __VA_ARGS__);printf("\n");fflush(stdout);}while(0)

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
    us.id.bustype=0x0005; us.id.vendor=0x0022; us.id.product=0x5081; us.id.version=0x0111;
    if(ioctl(fd,UI_DEV_SETUP,&us)<0){perror("UI_DEV_SETUP");close(fd);return -1;}
    if(ioctl(fd,UI_DEV_CREATE)<0){perror("UI_DEV_CREATE");close(fd);return -1;}
    LOG("virtual keyboard ready: \"%s\" bus=0005 vid=0022 pid=5081",us.name);
    return fd;
}

static void emit(int fd,int code,int val){
    struct input_event e; memset(&e,0,sizeof e);
    e.type=EV_KEY; e.code=(uint16_t)code; e.value=val;
    if(write(fd,&e,sizeof e)<0) return;
    memset(&e,0,sizeof e); e.type=EV_SYN; e.code=SYN_REPORT;
    if(write(fd,&e,sizeof e)<0) return;
}

static void tap(int ufd){
    emit(ufd,KEY_F19,1);
    usleep(20000);
    emit(ufd,KEY_F19,0);
}

int main(void){
    signal(SIGINT,onsig); signal(SIGTERM,onsig);
    setvbuf(stdout,NULL,_IOLBF,0);
    int ufd=-1, pressed=0, restored=1;
    while(!stop){
        char path[128];
        int src=find_pen(path,sizeof path);
        if(src<0){ if(!stop) sleep(2); continue; }
        if(ioctl(src,EVIOCGRAB,1)<0) LOG("grab failed: %s",strerror(errno));
        else LOG("grabbed %s",path);
        if(ufd<0){ ufd=make_keyboard(src); if(ufd<0){ ioctl(src,EVIOCGRAB,0); close(src); return 1; } }
        struct pollfd p={.fd=src,.events=POLLIN};
        while(!stop){
            int pr=poll(&p,1,500);
            if(pr<0&&errno==EINTR) continue;
            if(pr<=0) continue;
            struct input_event ev; ssize_t n;
            while((n=read(src,&ev,sizeof ev))==(ssize_t)sizeof ev){
                if(ev.type!=EV_KEY||ev.code!=KEY_F19){
                    if(write(ufd,&ev,sizeof ev)<0) goto lost;
                    continue;
                }
                if(ev.value==2) continue;
                if(ev.value==1){
                    if(pressed){ tap(ufd); restored=1; }
                    pressed=1; restored=0; tap(ufd); LOG("down -> toggle");
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
