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

static int width = 800;
static int height = 150;

static int interval = 33;
static int lifetime = 2500;

static double minVol = 0.09;
static double maxVol = 1.0;
static double maxVolStep = 0.03;
static double minVolStep = 0.01;
