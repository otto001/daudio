/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/file.h>
#include <errno.h>

#include <signal.h>
#include <semaphore.h>


#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif

#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

/* macros */
#define INTERSECT(x, y, w, h, r)  (MAX(0, MIN((x)+(w),(r).x_org+(r).width)  - MAX((x),(r).x_org)) \
                             && MAX(0, MIN((y)+(h),(r).y_org+(r).height) - MAX((y),(r).y_org)))
#define LENGTH(X)             (sizeof X / sizeof X[0])

enum {
    SchemeSel, SchemeNorm, SchemeMuted, SchemeLast
};
static char *embed;
static int bh, mw, mh;
static int mon = -1, screen;
static char interactive;

static Atom clip, utf8;
static Display *dpy;
static Window root, parentwin, win;
static XIC xic;

static Drw *drw;
static Clr *scheme[SchemeLast];

static char muted;
static int volume;
static int notChangedCounter;

static char *cmd;

static char buf[32];
static const char *setVolCmd[] = {"amixer", "set", "Master", buf, NULL};
static const char *toggleVolCmd[] = {"amixer", "set", "Master", "toggle", NULL};

sem_t mutex;

static void executeCommand(void);
#include "config.h"

static void
difftimespec(struct timespec *res, struct timespec *a, struct timespec *b) {
    res->tv_sec = a->tv_sec - b->tv_sec - (a->tv_nsec < b->tv_nsec);
    res->tv_nsec = a->tv_nsec - b->tv_nsec + (a->tv_nsec < b->tv_nsec) * 1E9;
}

static void
cleanup(void) {
    XUngrabKey(dpy, AnyKey, AnyModifier, root);
    for (size_t i = 0; i < SchemeLast; i++) {
        free(scheme[i]);
    }
    drw_free(drw);
    XSync(dpy, False);
    XCloseDisplay(dpy);
}

static int
_readAlsaVolume() {
    int fileDescriptors[2];
    if (pipe(fileDescriptors) == -1) {
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        return -1;
    } else if (pid == 0) {
        while ((dup2(fileDescriptors[1], STDOUT_FILENO) == -1) && (errno == EINTR)) {}
        close(fileDescriptors[0]);
        execlp("amixer",
               "amixer",
               "get",
               "Master", NULL);
        _exit(1);
    }
    close(fileDescriptors[1]);

    char buffer[256];
    memset(buffer, 0, sizeof(buffer));
    while (1) {
        ssize_t count = read(fileDescriptors[0], buffer, sizeof(buffer));
        if (count == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                return -1;
            }
        } else {
            break;
        }
    }
    close(fileDescriptors[0]);


    const char *firstBracket = strchr(buffer, '[');
    const char *percentSign = strchr(firstBracket, '%');

    const char *secondBracket = strchr(firstBracket + 1, '[');

    char isOn = strncmp(secondBracket + 1, "on", 2) == 0;

    char val[4];
    memset(val, 0, 4);
    memcpy(val, firstBracket + 1, MIN(percentSign - firstBracket - 1, 3));
    volume = strtol(val, NULL, 10);

    muted = !isOn;

    waitpid(pid, NULL, 0);
    return 0;
}

static int readAlsaVolume() {
    sem_wait(&mutex);
    int result = _readAlsaVolume();
    sem_post(&mutex);
    return result;
}

static void
executeCommand(void) {
    if (cmd == NULL) {
        return;
    }
    const char **cmdVec;
    readAlsaVolume();

    if (!strcmp(cmd, "toggle")) {
        cmdVec = toggleVolCmd;
        muted = !muted;
    } else {
        int volumeStep;

        if (!strcmp(cmd, "up")) {
            volumeStep = MIN(step, MAX(maxVol - volume, 0));
        } else if (!strcmp(cmd, "down")) {
            volumeStep = -MIN(step, MAX(volume - minVol, 0));
        } else {
            return;
        }
        volume += volumeStep;
        snprintf(buf, sizeof(buf), "%d%%", volume);
        cmdVec = setVolCmd;
    }

    if (cmdVec == NULL) {
        return;
    }

    pid_t pid = fork();
    if (pid == -1) {
        return;
    } else if (pid == 0) {
        int pipes[2];
        pipe(pipes);
        dup2(pipes[1], 1);
        execvp(cmdVec[0], (char *const *) cmdVec);
        _exit(1);
    }
}

static void
setPlaybackSink(int number) {
    char *list = run_command("pacmd list-sinks | sed -n -E 's/(index:)//p' | sed 's/^[\\* \\t]*//' | sed -z 's/\\n/;/g'");
    int index = 1;
    char *ptr = strtok(list, ";");

    while(ptr != NULL) {
        if (index == number) {
            char bigbuf[512];
            snprintf(bigbuf, sizeof(bigbuf), "/bin/bash -c \"inputs=\\$(pacmd list-sink-inputs | awk '/index/ {print \\$2}'); pacmd set-default-sink %s &> /dev/null; for i in \\$inputs; do pacmd move-sink-input \\$i %s &> /dev/null; done\"", ptr, ptr);
            run_command(bigbuf);
            break;
        }
        ptr = strtok(NULL, ";");
        index++;
    }
    readAlsaVolume();

}

static void
draw(void) {
    sem_wait(&mutex);

    char *list = run_command("pacmd list-sinks | sed -n -E 's/(index:|device\\.description = )//p' | sed 's/^[ \\t]*//' | sed -z 's/\\n/;/g'");
    int occurrences = 0;

    for (const char* s=list; s[occurrences]; s[occurrences] == ';' ? occurrences++ : *s++);

    int barHeight = (int) 1.5f*bh;
    int newMh = barHeight + bh + bh*((occurrences+1)/2);


    if (mh != newMh) {
        mh = newMh;
        XResizeWindow(dpy, win, mw, mh);
        drw_resize(drw, mw, mh);
    }
    drw_setscheme(drw, scheme[SchemeNorm]);
    drw_rect(drw, 0, 0, mw, mh, 1, 1);

    if (volume >= 0 && volume <= 100) {
        double volumeRatio = ((double) (volume - minVol)) / ((double) (maxVol - minVol));
        unsigned int w = mw * volumeRatio;
        if (muted) {
            drw_setscheme(drw, scheme[SchemeMuted]);
        } else {
            drw_setscheme(drw, scheme[SchemeSel]);
        }

        drw_rect(drw, 0, 0, w, barHeight, 1, 1);

    }
    drw_setscheme(drw, scheme[SchemeNorm]);

    unsigned int y = barHeight + bh;
    int counter = 1;
    if (list) {
        char *ptr = strtok(list, ";");
        char *index = NULL;

        while(ptr != NULL) {
            if (index != NULL) {

                if (index[0] == '*') {
                    drw_setscheme(drw, scheme[SchemeSel]);
                    drw_rect(drw, 0, y, mw, bh, 1, 0);
                } else {
                    drw_setscheme(drw, scheme[SchemeNorm]);
                }
                ptr++;
                ptr[strlen(ptr) - 1] = ' ';
                snprintf(buf, sizeof(buf), "%d", counter);
                drw_text(drw, 0, y, mw, bh, 0, buf, 0);
                drw_text(drw, bh, y, mw, bh, 0, ptr, 0);
                y += bh;
                counter++;
                index = NULL;
            } else {
                index = ptr;
            }
            ptr = strtok(NULL, ";");
        }

    }
    drw_map(drw, win, 0, 0, mw, mh);
    sem_post(&mutex);
}

static void
keypress(XKeyEvent *ev)
{
    char buf[32];
    KeySym ksym;
    Status status;

    XmbLookupString(xic, ev, buf, sizeof buf, &ksym, &status);
    switch (status) {
        default: /* XLookupNone, XBufferOverflow */
            return;
        case XLookupKeySym:
        case XLookupBoth:
            break;
    }
    int _notChangedCounter = notChangedCounter;
    notChangedCounter = 0;

    switch(ksym) {
        default:
            notChangedCounter = _notChangedCounter;
            break;
        case XK_Up:
        case 0x1008ff13:
            cmd = "up";
            executeCommand();
            break;
        case XK_Down:
        case 0x1008ff11:
            cmd = "down";
            executeCommand();
            break;
        case XK_1:
            setPlaybackSink(1);
            break;
        case XK_2:
            setPlaybackSink(2);
            break;
        case XK_3:
            setPlaybackSink(3);
            break;
        case XK_4:
            setPlaybackSink(4);
            break;
        case XK_5:
            setPlaybackSink(5);
            break;
        case XK_6:
            setPlaybackSink(6);
            break;
        case XK_7:
            setPlaybackSink(7);
            break;
        case XK_8:
            setPlaybackSink(8);
            break;
        case XK_9:
            setPlaybackSink(9);
            break;
        case XK_m:
        case 0x1008ff12:
            cmd = "toggle";
            executeCommand();
            break;
        case XK_KP_Enter:
        case XK_Return:
        case XK_Escape:
        case XK_q:
            cleanup();
            exit(0);
    }
    draw();
}

static void
grabfocus(void)
{
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000  };
    Window focuswin;
    int i, revertwin;

    for (i = 0; i < 100; ++i) {
        XGetInputFocus(dpy, &focuswin, &revertwin);
        if (focuswin == win)
            return;
        XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
        nanosleep(&ts, NULL);
    }
    die("cannot grab focus");
}

static void
grabkeyboard(void)
{
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000  };
    int i;

    if (embed)
        return;
    /* try to grab keyboard, we may have to wait for another process to ungrab */
    for (i = 0; i < 1000; i++) {
        if (XGrabKeyboard(dpy, DefaultRootWindow(dpy), True, GrabModeAsync,
                          GrabModeAsync, CurrentTime) == GrabSuccess)
            return;
        nanosleep(&ts, NULL);
    }
    die("cannot grab keyboard");
}

static void
handleEvents(void) {
    XEvent ev;

    while (XPending(dpy) && !XNextEvent(dpy, &ev)) {

        if (XFilterEvent(&ev, win))
            continue;
        switch (ev.type) {
            case FocusIn:
                /* regrab focus from parent window */
                if (ev.xfocus.window != win)
                    grabfocus();
                break;
            case KeyPress:
                keypress(&ev.xkey);
                break;
            case DestroyNotify:
                if (ev.xdestroywindow.window != win)
                    break;
                cleanup();
                exit(1);
            case Expose:
                if (ev.xexpose.count == 0)
                    drw_map(drw, win, 0, 0, mw, mh);
                break;
            case VisibilityNotify:
                if (ev.xvisibility.state != VisibilityUnobscured)
                    XRaiseWindow(dpy, win);
                break;
        }
    }
}

static void
run(void) {
    struct timespec start, current, diff, intspec, wait;


    while (1) {


        handleEvents();

        if (clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
            die("clock_gettime:");
        }

		notChangedCounter++;
        if (notChangedCounter >= lifetime / interval) {
            cleanup();
            exit(0);
        }

        if (clock_gettime(CLOCK_MONOTONIC, &current) < 0) {
            die("clock_gettime:");
        }
        difftimespec(&diff, &current, &start);

        intspec.tv_sec = interval / 1000;
        intspec.tv_nsec = (interval % 1000) * 1E6;
        difftimespec(&wait, &intspec, &diff);

        if (wait.tv_sec >= 0) {
            if (nanosleep(&wait, NULL) < 0 && errno != EINTR) {
                die("nanosleep:");
            }
        }


    }

}


static void
checkSingleton(void) {
    while (1) {
        int pid_file = open("/tmp/dwm_vol.pid", O_CREAT | O_RDWR, 0666);
        int rc = lockf(pid_file, F_TLOCK, 0);
        if (rc) {
            // EACCES == errno if dwm_vol is already running
            if (errno != EINTR) {
                run_command("killall -s USR1 dwm_vol");
                exit(0);
            }
        } else {
            // singleton, can continue
            return;
        }
    }

}

void
signal_callback_handler(int signum) {
    notChangedCounter = 0;

    struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000  };
    nanosleep(&ts, NULL);

    readAlsaVolume();
    draw();
}

static void
setup(void) {
    int x, y, i, j;
    unsigned int du;
    XSetWindowAttributes swa;
    XIM xim;
    Window w, dw, *dws;
    XWindowAttributes wa;
    XClassHint ch = {"dwm_vol", "dwm_vol"};
#ifdef XINERAMA
    XineramaScreenInfo *info;
    Window pw;
    int a, di, n, area = 0;
#endif
    notChangedCounter = 0;
    volume = 0;
    muted = 0;
    /* init appearance */
    for (j = 0; j < SchemeLast; j++) {
        scheme[j] = drw_scm_create(drw, colors[j], 2);
    }

    clip = XInternAtom(dpy, "CLIPBOARD", False);
    utf8 = XInternAtom(dpy, "UTF8_STRING", False);

    /* calculate menu geometry */
    bh = drw->fonts->h + 2;
    mh = height;
#ifdef XINERAMA
    i = 0;
    if (parentwin == root && (info = XineramaQueryScreens(dpy, &n))) {
        XGetInputFocus(dpy, &w, &di);
        if (mon >= 0 && mon < n)
            i = mon;
        else if (w != root && w != PointerRoot && w != None) {
            /* find top-level window containing current input focus */
            do {
                if (XQueryTree(dpy, (pw = w), &dw, &w, &dws, &du) && dws)
                    XFree(dws);
            } while (w != root && w != pw);
            /* find xinerama screen with which the window intersects most */
            if (XGetWindowAttributes(dpy, pw, &wa))
                for (j = 0; j < n; j++)
                    if ((a = INTERSECT(wa.x, wa.y, wa.width, wa.height, info[j])) > area) {
                        area = a;
                        i = j;
                    }
        }
        /* no focused window is on screen, so use pointer location instead */
        if (mon < 0 && !area && XQueryPointer(dpy, root, &dw, &dw, &x, &y, &di, &di, &du))
            for (i = 0; i < n; i++)
                if (INTERSECT(x, y, 1, 1, info[i]))
                    break;

        mw = MIN(MAX(width, 100), info[i].width);
        x = info[i].x_org + ((info[i].width  - mw) / 2);
        y = info[i].y_org + ((info[i].height - mh) / 2);
        XFree(info);
    } else
#endif
    {
        if (!XGetWindowAttributes(dpy, parentwin, &wa))
            die("could not get embedding window attributes: 0x%lx",
                parentwin);
        mw = MIN(MAX(width, 100), wa.width);
        x = (wa.width - mw) / 2;
        y = (wa.height - mh) / 2;
    }

    /* create menu window */
    swa.override_redirect = True;
    swa.background_pixel = scheme[SchemeNorm][ColBg].pixel;
    swa.event_mask = ExposureMask | KeyPressMask | VisibilityChangeMask;
    win = XCreateWindow(dpy, parentwin, x, y, mw, mh, 0,
                        CopyFromParent, CopyFromParent, CopyFromParent,
                        CWOverrideRedirect | CWBackPixel | CWEventMask, &swa);
    XSetClassHint(dpy, win, &ch);


    /* input methods */
    if ((xim = XOpenIM(dpy, NULL, NULL, NULL)) == NULL)
        die("XOpenIM failed: could not open input device");

    xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                    XNClientWindow, win, XNFocusWindow, win, NULL);

    XMapRaised(dpy, win);
    if (embed) {
        XSelectInput(dpy, parentwin, FocusChangeMask | SubstructureNotifyMask);
        if (XQueryTree(dpy, parentwin, &dw, &w, &dws, &du) && dws) {
            for (i = 0; i < du && dws[i] != win; ++i)
                XSelectInput(dpy, dws[i], FocusChangeMask);
            XFree(dws);
        }
    }

    drw_resize(drw, mw, mh);
    if (interactive) {
        grabkeyboard();
    }
    readAlsaVolume();
    draw();
    signal(SIGUSR1, signal_callback_handler);

}

static void
usage(void) {
    fputs("usage: dwm_vol [-bfiv] [-cmd toggle|up|down] [-i interactive] [-fn font] [-m monitor]\n"
          "             [-b color] [-f color] [-w windowid]\n", stderr);
    exit(1);
}

int
main(int argc, char *argv[]) {
    XWindowAttributes wa;
    int i;
    interactive = 0;
    for (i = 1; i < argc; i++) {

        /* these options take no arguments */
        if (!strcmp(argv[i], "-v")) {      /* prints version information */
            puts("dwm_vol-"
            VERSION);
            exit(0);
        }
        else if (!strcmp(argv[i], "-i"))
            interactive = 1;
        else if (i + 1 == argc)
            usage();
            /* these options take one argument */
        else if (!strcmp(argv[i], "-m"))
            mon = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-fn"))  /* font or font set */
            fonts[0] = argv[++i];
        else if (!strcmp(argv[i], "-cmd"))  /* font or font set */
            cmd = argv[++i];
        else if (!strcmp(argv[i], "-b"))  /* normal background color */
            colors[SchemeNorm][ColBg] = argv[++i];
        else if (!strcmp(argv[i], "-sb"))  /* normal foreground color */
            colors[SchemeSel][ColBg] = argv[++i];
        else if (!strcmp(argv[i], "-w"))   /* embedding window id */
            embed = argv[++i];

        else
            usage();
    }
    sem_init(&mutex, 0, 1);

    executeCommand();
    checkSingleton();

    if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
        fputs("warning: no locale support\n", stderr);
    if (!(dpy = XOpenDisplay(NULL)))
        die("cannot open display");
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    if (!embed || !(parentwin = strtol(embed, NULL, 0)))
        parentwin = root;
    if (!XGetWindowAttributes(dpy, parentwin, &wa))
        die("could not get embedding window attributes: 0x%lx",
            parentwin);
    drw = drw_create(dpy, screen, root, wa.width, wa.height);
    if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
        die("no fonts could be loaded.");

#ifdef __OpenBSD__
    if (pledge("stdio rpath", NULL) == -1)
        die("pledge");
#endif
    setup();
    run();

    return 1; /* unreachable */
}
