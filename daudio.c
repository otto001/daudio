/* See LICENSE file for copyright and license details. */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>

#include <sys/wait.h>
#include <sys/file.h>
#include <errno.h>

#include <signal.h>
#include <semaphore.h>


#include <X11/Xlib.h>
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
static int bh, mw, mh, lrpad;
static int mon = -1, screen;
static char interactive;

static Display *dpy;
static Window root, parentWin, win;
static XIC xic;

static Drw *drw;
static Clr *scheme[SchemeLast];

static char muted;
static int volume;
static struct timespec lastDraw;


typedef struct Sink {
    int index;
    char name[128];
} Sink;

static Sink *sinks;
static size_t sinksNum;
static size_t defaultSink;
static size_t selectedSink;

static char *cmd;

static char buf[32];
static const char *setVolCmd[] = {"amixer", "set", "Master", buf, NULL};
static const char *toggleVolCmd[] = {"amixer", "set", "Master", "toggle", NULL};

sem_t mutex;

static void executeCommand(void);

#include "config.h"


static void
cleanup(void) {
	if (dpy) {
		XUngrabKey(dpy, AnyKey, AnyModifier, root);
    	drw_free(drw);
    	XSync(dpy, False);
    	XCloseDisplay(dpy);
	}
    for (size_t i = 0; i < SchemeLast; i++) {
        free(scheme[i]);
    }
    if (sinks) {
        free(sinks);
    }
    sem_destroy(&mutex);

}

static void
readAlsaVolume() {
    const char* readVec[] = {"amixer", "get", "Master", NULL};
    const char* readOutput = execRead(readVec[0], readVec);

    if (!readOutput) {
        die("unable to read volume");
    }

    const char *firstBracket = strchr(readOutput, '[');
    const char *secondBracket = strchr(firstBracket + 1, '[');

    if (!firstBracket || !secondBracket) {
        die("unable to read volume");
    }

    sem_wait(&mutex);
    volume = (int) strtol(firstBracket + 1, NULL, 10);
    muted = (char) (strncmp(secondBracket + 1, "on", 2) != 0);
    sem_post(&mutex);

}

static int readSinks() {
    char *sinksRaw = runCommand(
            "pacmd list-sinks | sed -n -E 's/(index:|device\\.description = )//p'");
    if (sinksRaw) {
        int occurrences = 0;
        for (const char *s = sinksRaw; s[occurrences]; s[occurrences] == '\n' ? occurrences++ : *s++);
        sinksNum = ((occurrences + 1) / 2);

        int index = 0;

        if (sinks) {
            free(sinks);
        }
        sinks = malloc(sizeof(Sink) * sinksNum);

        char *ptr = strtok(sinksRaw, "\n");
        char *indexPtr = NULL;

        while (ptr != NULL) {
            if (indexPtr != NULL) {
                while (!isdigit(*indexPtr)) {
                	if (*indexPtr == '*') {
     	                defaultSink = selectedSink = index;
     	                break;
                	}
                    indexPtr++;
                }
                sinks[index].index = (int) strtol(indexPtr, NULL, 10);
                char *name = sinks[index].name;
                ptr = strchr(ptr, '"') + 1;
                snprintf(name, sizeof(sinks[0].name), "%s", ptr);
                if (name[strlen(name) - 1] == '"') {
                    name[strlen(name) - 1] = '\0';
                }
                index++;
                indexPtr = NULL;
            } else {
                indexPtr = ptr;
            }
            ptr = strtok(NULL, "\n");
        }

    }
    return 0;
}

static void
executeCommand(void) {
    if (cmd == NULL) {
        return;
    }
    const char **cmdVec;

    if (!strcmp(cmd, "toggle")) {
        cmdVec = toggleVolCmd;
        muted = (char) !muted;
    } else {
        int volumeStep;

        if (!strcmp(cmd, "inc")) {
            volumeStep = step;
        } else if (!strcmp(cmd, "dec")) {
            volumeStep = -step;
        } else {
            return;
        }
        volume += volumeStep;
        volume = MAX(minVol, MIN(maxVol, volume));
        snprintf(buf, sizeof(buf), "%d%%", volume);
        cmdVec = setVolCmd;
    }

    pid_t child = fork();
    if (child == -1) {
        die("fork: %d", errno);
    } else if (child == 0) {
        int fd = open("/dev/null", O_WRONLY);
        dup2(fd, 1);
        dup2(fd, 2);
        execvp(cmdVec[0], (char *const *) cmdVec);
        _exit(1);
    }
    waitpid(child, NULL, 0);
}

static void
setPlaybackSink(int number) {
    int index = sinks[number].index;

    char bigBuf[256];
    snprintf(bigBuf, sizeof(bigBuf),
             "/bin/bash -c \"inputs=\\$(pacmd list-sink-inputs | awk '/index/ {print \\$2}'); pacmd set-default-sink %d &> /dev/null; for i in \\$inputs; do pacmd move-sink-input \\$i %d &> /dev/null; done\"",
             index, index);
    runCommand(bigBuf);
    readAlsaVolume();
    readSinks();
}

static void
draw(void) {
    sem_wait(&mutex);

    int barHeight = (int) 1.5f * bh;
    int newMh = (int) (barHeight + (interactive ? bh + bh * sinksNum : 0));

    if (mh != newMh) {
        mh = newMh;
        XResizeWindow(dpy, win, mw, mh);
        drw_resize(drw, mw, mh);
    }
    drw_setscheme(drw, scheme[SchemeNorm]);
    drw_rect(drw, 0, 0, mw, mh, 1, 1);

    if (volume >= minVol && volume <= 100) {
        double volumeRatio = ((double) (volume - minVol)) / ((double) (maxVol - minVol));
        int w = (int) (mw * volumeRatio);
        w = MIN(w, mw);
        if (muted) {
            drw_setscheme(drw, scheme[SchemeMuted]);
        } else {
            drw_setscheme(drw, scheme[SchemeSel]);
        }

        drw_rect(drw, 0, 0, w, barHeight, 1, 1);

    }
    if (interactive) {
        drw_setscheme(drw, scheme[SchemeNorm]);

        int y = barHeight + bh;

        for (size_t index = 0; index < sinksNum; index++) {

            if (index == selectedSink) {
                drw_setscheme(drw, scheme[SchemeSel]);
                drw_rect(drw, 0, y, mw, bh, 1, 1);
            } else {
                drw_setscheme(drw, scheme[SchemeNorm]);
            }
            if (index == defaultSink) {
                drw_text(drw, 0, y, mw, bh, lrpad / 2, "*", 0);
            }
            drw_text(drw, bh, y, mw - bh, bh, lrpad / 2, sinks[index].name, 0);
            y += bh;
        }
    }
    drw_map(drw, win, 0, 0, mw, mh);

    if (clock_gettime(CLOCK_MONOTONIC, &lastDraw) < 0) {
        die("clock_gettime:");
    }
    sem_post(&mutex);
}

static void
keypress(XKeyEvent *ev) {
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
    switch (ksym) {
        default:
            return;
        case XK_Right:
        case 0x1008ff13:
            cmd = "inc";
            executeCommand();
            break;
        case XK_Left:
        case 0x1008ff11:
            cmd = "dec";
            executeCommand();
            break;
        case XK_Up:
            if (selectedSink > 0) {
                selectedSink--;
            }
            break;
        case XK_Down:
            if (selectedSink < sinksNum - 1) {
                selectedSink++;
            }
            break;
        case XK_Return:
        case XK_KP_Enter:
            setPlaybackSink(selectedSink);
            break;
        case XK_m:
        case 0x1008ff12:
            cmd = "toggle";
            executeCommand();
            break;
        case XK_Escape:
        case XK_q:
            exit(0);
    }
    draw();
}

static void
grabfocus(void) {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 10000000};
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
grabkeyboard(void) {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000};
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
    struct timespec start, current, diff, intervalSpec, wait;
    timespecSetMs(&intervalSpec, interval);

    for (;;) {

        if (clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
            die("clock_gettime:");
        }

        timespecDiff(&diff, &start, &lastDraw);
        if (timespecToMs(&diff) > lifetime) {
            exit(0);
        }

        handleEvents();

        if (clock_gettime(CLOCK_MONOTONIC, &current) < 0) {
            die("clock_gettime:");
        }

        timespecDiff(&diff, &current, &start);
        timespecDiff(&wait, &intervalSpec, &diff);

        if (wait.tv_sec >= 0 && wait.tv_nsec >= 0) {
            if (nanosleep(&wait, NULL) < 0 && errno != EINTR) {
                die("nanosleep:");
            }
        }
    }
}

static void
checkSingleton(void) {
    int pidFile = open("/tmp/daudio.pid", O_CREAT | O_RDWR, 0666);
    if (pidFile < 0) {
        die("error open '/tmp/daudio.pid' errno: %d", errno);
    }
    while (lockf(pidFile, F_TLOCK, 0) == -1) {
        if (errno != EINTR) {
            ssize_t n = read(pidFile, buf, sizeof(buf));
            buf[n] = '\0';
            pid_t singletonPid = strtol(buf, NULL, 10);
            kill(singletonPid, SIGUSR1);
            exit(0);
        }
    }
    snprintf(buf, sizeof(buf), "%d", getpid());
    write(pidFile, buf, strlen(buf)+1);
}

void
onSigUsr1(int _unused) {
    (void)_unused;

    struct timespec ts = {.tv_sec = 0, .tv_nsec = 10000000};
    nanosleep(&ts, NULL);

    readAlsaVolume();
    if (interactive) {
        readSinks();
    }
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
    XClassHint ch = {"daudio", "daudio"};
#ifdef XINERAMA
    XineramaScreenInfo *info;
    Window pw;
    int a, di, n, area = 0;
#endif

    /* init appearance */
    for (j = 0; j < SchemeLast; j++) {
        scheme[j] = drw_scm_create(drw, colors[j], 2);
    }

    /* calculate menu geometry */
    bh = (int) drw->fonts->h + 2;
    lrpad = (int) drw->fonts->h;
    mh = height;
#ifdef XINERAMA
    i = 0;
    if (parentWin == root && (info = XineramaQueryScreens(dpy, &n))) {
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
        x = info[i].x_org + ((info[i].width - mw) / 2);
        y = info[i].y_org + ((info[i].height - mh) / 2);
        XFree(info);
    } else
#endif
    {
        if (!XGetWindowAttributes(dpy, parentWin, &wa))
            die("could not get embedding window attributes: 0x%lx",
                parentWin);
        mw = MIN(MAX(width, 100), wa.width);
        x = (wa.width - mw) / 2;
        y = (wa.height - mh) / 2;
    }

    /* create menu window */
    swa.override_redirect = True;
    swa.background_pixel = scheme[SchemeNorm][ColBg].pixel;
    swa.event_mask = ExposureMask | KeyPressMask | VisibilityChangeMask;
    win = XCreateWindow(dpy, parentWin, x, y, mw, mh, 0,
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
        XSelectInput(dpy, parentWin, FocusChangeMask | SubstructureNotifyMask);
        if (XQueryTree(dpy, parentWin, &dw, &w, &dws, &du) && dws) {
            for (i = 0; i < du && dws[i] != win; ++i)
                XSelectInput(dpy, dws[i], FocusChangeMask);
            XFree(dws);
        }
    }

    drw_resize(drw, mw, mh);
    if (interactive) {
        grabkeyboard();
    }

    draw();
}

static void
usage(void) {
    fputs("usage:  daudio [-iv] [-cmd inc|dec|toggle] [-m monitor] [-fn font] ["
          "-nb color] [-nf color] [-sb color] [-sf color] [-mb color] [-mf color] [-w windowid]\n", stderr);
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
            puts("daudio-"
                 VERSION);
            exit(0);
        } else if (!strcmp(argv[i], "-i"))
            interactive = 1;
        else if (i + 1 == argc)
            usage();
            /* these options take one argument */
        else if (!strcmp(argv[i], "-m"))
            mon = (int) strtol(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "-fn"))  /* font or font set */
            fonts[0] = argv[++i];
        else if (!strcmp(argv[i], "-cmd"))
            cmd = argv[++i];
        else if (!strcmp(argv[i], "-nf"))  /* normal foreground color */
            colors[SchemeNorm][ColFg] = argv[++i];
        else if (!strcmp(argv[i], "-nb"))  /* normal background color */
            colors[SchemeNorm][ColBg] = argv[++i];
        else if (!strcmp(argv[i], "-sf"))  /* selected foreground color */
            colors[SchemeSel][ColFg] = argv[++i];
        else if (!strcmp(argv[i], "-sb"))  /* selected background color */
            colors[SchemeSel][ColBg] = argv[++i];
        else if (!strcmp(argv[i], "-mf"))  /* muted foreground color */
            colors[SchemeMuted][ColFg] = argv[++i];
        else if (!strcmp(argv[i], "-mb"))  /* muted background color */
            colors[SchemeMuted][ColBg] = argv[++i];
        else if (!strcmp(argv[i], "-w"))   /* embedding window id */
            embed = argv[++i];

        else
            usage();
    }
    sem_init(&mutex, 0, 1);

    int e = atexit(cleanup);
    if (e != 0) {
        die("cannot set exit function");
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = onSigUsr1;
    sigaction(SIGUSR1, &sa, NULL);
    sa.sa_handler = exit;
    sigaction(SIGINT, &sa, NULL);

    readAlsaVolume();
    executeCommand();
    checkSingleton();

    if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
        fputs("warning: no locale support\n", stderr);
    if (!(dpy = XOpenDisplay(NULL)))
        die("cannot open display");
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    if (!embed || !(parentWin = strtol(embed, NULL, 0)))
        parentWin = root;
    if (!XGetWindowAttributes(dpy, parentWin, &wa))
        die("could not get embedding window attributes: 0x%lx",
            parentWin);
    drw = drw_create(dpy, screen, root, wa.width, wa.height);
    if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
        die("no fonts could be loaded.");

    if (interactive) {
        readSinks();
    }
    setup();
    run();

    return 1; /* unreachable */
}
