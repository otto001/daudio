/* See LICENSE file for copyright and license details. */
/* Default settings; can be overriden by command line. */

/* -fn option overrides fonts[0]; default X11 font or font set */
static const char *fonts[] = {
        "monospace:size=10"
};
static const char *colors[SchemeLast][2] = {
        /*     fg         bg       */
        [SchemeNorm] = { "#eeeeee", "#222222" },
        [SchemeMuted] = { "#777777", "#222222" },
};

static int width = 400;
static int height = 50;

static int interval = 33;
