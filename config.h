/* See LICENSE file for copyright and license details. */
/* Default settings; can be overriden by command line. */

/* -fn option overrides fonts[0]; default X11 font or font set */
static const char *fonts[] = {
	"monospace:size=10"
};
static const char *colors[SchemeLast][3] = {
	/*     fg         bg       */
        [SchemeSel] = { "#eeeeee", "#005577" },
        [SchemeNorm] = { "#eeeeee", "#222222" },
	    [SchemeMuted] = { "#eeeeee", "#777777" },
};

static int width = 400;
static int height = 150;

static int interval = 33;
static int lifetime = 2500;

static int minVol = 9;
static int maxVol = 100;
static int step = 3;
static char varStep = 1;
