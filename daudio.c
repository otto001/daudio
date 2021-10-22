/* See LICENSE file for copyright and license details. */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <math.h>


#include <sys/file.h>
#include <errno.h>

#include <signal.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef XINERAMA

#include <X11/extensions/Xinerama.h>

#endif
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"
#include "pulseaudio.h"

/* macros */
#define INTERSECT(x, y, w, h, r)  (MAX(0, MIN((x)+(w),(r).x_org+(r).width)  - MAX((x),(r).x_org)) \
                             && MAX(0, MIN((y)+(h),(r).y_org+(r).height) - MAX((y),(r).y_org)))
#define LENGTH(X)             (sizeof (X) / sizeof (X)[0])

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

static struct timespec last_draw;


static uint32_t selected_sink;

static char *cmd;

static char buf[32];


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
    free_pulse();
}

static float
get_volume_ratio(void) {
    const PulseSink *sink = get_default_sink();
    float result = ((float) (sink->volume - min_volume_factor * PA_VOLUME_NORM)) / ((float) PA_VOLUME_NORM * (max_volume_factor - min_volume_factor));
    result = MAX(result, 0);
    result = MIN(result, 1);
    return result;
}

static void toggle_mute() {
    pulse_lock();
    const PulseSink* sink = get_default_sink();
    set_mute(sink, !sink->mute);
    pulse_unlock();
}

static void change_volume(float direction) {
    pulse_lock();
    const PulseSink* sink = get_default_sink();
    if (!sink) {
        die("change_volume failed: no default sink");
        return;
    }
    uint32_t volume = sink->volume;
    int max_volume = (int) roundf(max_volume_factor * PA_VOLUME_NORM);
    int min_volume = (int) roundf(min_volume_factor * PA_VOLUME_NORM);


    float volume_step = (min_volume_step + (max_volume_step - min_volume_step) * get_volume_ratio()) *
                         ((float) (max_volume - min_volume));
    volume_step = MAX(1.0, volume_step);
    volume_step *= direction;

    // Ensure that previous volume is within limits.
    // Without this, edge case can occur where the first/last step needs to be fired twice
    if (volume <= min_volume_factor * PA_VOLUME_NORM * 1.001) {
        volume = MAX(min_volume, volume);
        set_mute(sink, 0);
    } else {
        volume = MIN(max_volume, volume);
    }


    volume += (int) roundf(volume_step);

    // Ensure that new volume is within limits.
    if (volume <= min_volume) {
        volume = min_volume;
        set_mute(sink, 1);
    } else if (volume >= max_volume * 0.995) {
        volume = max_volume;
    }
    set_volume(sink, volume);
    pulse_unlock();
}

static void wait_for_default_sink() {
    struct timespec ts = {.tv_sec = 0, .tv_nsec = 100000};
    for (int i = 0; i < 100; ++i) {
        if (get_default_sink()) {
            return;
        }
        nanosleep(&ts, NULL);
    }
    die("execute_cli_command failed: no default sink");

}

static void execute_cli_command(void) {
    if (cmd == NULL) {
        return;
    }

    wait_for_default_sink();

    if (strcmp(cmd, "toggle") == 0) {
        toggle_mute();
    } else if (strcmp(cmd, "inc") == 0){
        change_volume(1);
    } else if (strcmp(cmd, "dec") == 0) {
        change_volume(-1);
    }
}

static void set_selected_to_default_sink() {
    pulse_lock();
    if (selected_sink > get_sinks_count()) {
        pulse_unlock();
        return;
    };
    set_default_sink(&get_sinks()[selected_sink]);
    pulse_unlock();
}

static void draw(void) {
    pulse_lock();

    int sinks_count = get_sinks_count();
    const PulseSink *default_sink = get_default_sink();

    int bar_height = (int) 1.5f * bh;
    int newMh = (int) (bar_height + (interactive ? bh + bh * sinks_count : 0));

    if (mh != newMh) {
        mh = newMh;
        XResizeWindow(dpy, win, mw, mh);
        drw_resize(drw, mw, mh);
    }

    drw_setscheme(drw, scheme[SchemeNorm]);
    drw_rect(drw, 0, 0, mw, mh, 1, 1);

    float volume_ratio = get_volume_ratio();
    int w = (int) ((float) mw * volume_ratio);
    if (default_sink->mute) {
        drw_setscheme(drw, scheme[SchemeMuted]);
    } else {
        drw_setscheme(drw, scheme[SchemeSel]);
    }

    drw_rect(drw, 0, 0, w, bar_height, 1, 1);

    if (interactive) {
        drw_setscheme(drw, scheme[SchemeNorm]);

        int y = bar_height + bh;

        const PulseSink *pulse_sinks = get_sinks();

        for (size_t index = 0; index < sinks_count; index++) {
            const PulseSink *sink = &pulse_sinks[index];

            if (index == selected_sink) {
                drw_setscheme(drw, scheme[SchemeSel]);
                drw_rect(drw, 0, y, mw, bh, 1, 1);
            } else {
                drw_setscheme(drw, scheme[SchemeNorm]);
            }
            if (sink == default_sink) {
                drw_text(drw, 0, y, mw, bh, lrpad / 2, "*", 0);
            }
            drw_text(drw, bh, y, mw - bh, bh, lrpad / 2, sink->description, 0);
            y += bh;
        }
    }
    drw_map(drw, win, 0, 0, mw, mh);

    if (clock_gettime(CLOCK_MONOTONIC, &last_draw) < 0) {
        die("clock_gettime:");
    }
    pulse_unlock();
}

static void keypress(XKeyEvent *ev) {
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
            change_volume(1);
            break;
        case XK_Left:
        case 0x1008ff11:
            change_volume(-1);
            break;
        case XK_Up:
            if (selected_sink > 0) {
                selected_sink--;
            }
            break;
        case XK_Down:
            if (selected_sink < get_sinks_count() - 1) {
                selected_sink++;
            }
            break;
        case XK_Return:
        case XK_KP_Enter:
            set_selected_to_default_sink();
            break;
        case XK_m:
        case 0x1008ff12:
            toggle_mute();
            break;
        case XK_Escape:
        case XK_q:
            exit(0);
    }
    draw();
}

static void grab_focus(void) {
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

static void grab_keyboard(void) {
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

static void handle_events(void) {
    XEvent ev;

    while (XPending(dpy) && !XNextEvent(dpy, &ev)) {
        if (XFilterEvent(&ev, win))
            continue;
        switch (ev.type) {
            case FocusIn:
                /* regrab focus from parent window */
                if (ev.xfocus.window != win)
                    grab_focus();
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

static void handle_pulse_updates() {
    if (get_updates()) {
        pulse_lock();
        const PulseSink *sinks = get_sinks();
        const PulseSink *default_sink = get_default_sink();

        for (size_t i = 0; i < get_sinks_count(); i++) {
            if (&sinks[i] == default_sink) {
                selected_sink = i;
                break;
            }
        }
        pulse_unlock();
        draw();
        updated();
    }
}

static void run(void) {
    struct timespec start, current, diff, intervalSpec, wait;
    timespec_set_ms(&intervalSpec, interval);

    for (;;) {

        if (clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
            die("clock_gettime:");
        }

        timespec_diff(&diff, &start, &last_draw);
        if (timespec_to_ms(&diff) > lifetime) {
            exit(0);
        }

        handle_events();
        handle_pulse_updates();


        if (clock_gettime(CLOCK_MONOTONIC, &current) < 0) {
            die("clock_gettime:");
        }

        timespec_diff(&diff, &current, &start);
        timespec_diff(&wait, &intervalSpec, &diff);

        if (wait.tv_sec >= 0 && wait.tv_nsec >= 0) {
            if (nanosleep(&wait, NULL) < 0 && errno != EINTR) {
                die("nanosleep:");
            }
        }
    }
}

static void check_singleton(void) {
    int pidFile = open("/tmp/daudio.pid", O_CREAT | O_RDWR, 0666);
    if (pidFile < 0) {
        die("error open '/tmp/daudio.pid' errno: %d", errno);
    }
    while (lockf(pidFile, F_TLOCK, 0) == -1) {
        if (errno != EINTR) {
            exit(0);
        }
    }
    snprintf(buf, sizeof(buf), "%d", getpid());
    write(pidFile, buf, strlen(buf)+1);
}


static void setup(void) {
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
        grab_keyboard();
    }

    draw();
}

static void usage(void) {
    fputs("usage:  daudio [-iv] [-cmd inc|dec|toggle] [-m monitor] [-fn font] ["
          "-nb color] [-nf color] [-sb color] [-sf color] [-mb color] [-mf color] [-w windowid]\n", stderr);
    exit(1);
}

int main(int argc, char *argv[]) {
    XWindowAttributes wa;
    int i;
    struct sigaction sa;

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

    int e = atexit(cleanup);
    if (e != 0) {
        die("cannot set exit function");
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = exit;
    sigaction(SIGINT, &sa, NULL);

    setup_pulse();
    execute_cli_command();
    check_singleton();

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

    setup();
    run();

    return 1; /* unreachable */
}
