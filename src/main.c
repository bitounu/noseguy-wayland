#define _POSIX_C_SOURCE 200809L
#include "wayland.h"
#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "  --text-command <cmd>    shell command for text (default: built-in)\n"
        "  --text-file <path>      read text from file\n"
        "  --stdin                 read text from stdin\n"
        "  --fps <n>               target frame rate (default: 30)\n"
        "  --font <name>           pango font name (default: Sans)\n"
        "  --font-size <px>        font size in pixels (default: 18)\n"
        "  --interval <seconds>    seconds between text fetches (default: 30)\n"
        "  --bg-color <#rrggbb>    background color (default: #000000)\n",
        prog);
}

static bool parse_color(const char *s, double *r, double *g, double *b) {
    if (*s == '#') s++;
    unsigned ri, gi, bi;
    if (sscanf(s, "%02x%02x%02x", &ri, &gi, &bi) != 3) return false;
    *r = ri / 255.0; *g = gi / 255.0; *b = bi / 255.0;
    return true;
}

int main(int argc, char **argv) {
    TextConfig tcfg = { .source = TEXT_BUILTIN, .arg = NULL, .interval = 30.0 };
    App app = {
        .fps       = 30,
        .font_name = "Sans",
        .font_size = 18,
        .bg_r = 0.0, .bg_g = 0.0, .bg_b = 0.0,
    };

    static const struct option long_opts[] = {
        { "text-command", required_argument, NULL, 'c' },
        { "text-file",    required_argument, NULL, 'f' },
        { "stdin",        no_argument,       NULL, 's' },
        { "fps",          required_argument, NULL, 'F' },
        { "font",         required_argument, NULL, 'n' },
        { "font-size",    required_argument, NULL, 'z' },
        { "interval",     required_argument, NULL, 'i' },
        { "bg-color",     required_argument, NULL, 'b' },
        { "help",         no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'c': tcfg.source = TEXT_COMMAND; tcfg.arg = optarg; break;
        case 'f': tcfg.source = TEXT_FILE;    tcfg.arg = optarg; break;
        case 's': tcfg.source = TEXT_STDIN;                      break;
        case 'F': app.fps       = atoi(optarg);                  break;
        case 'n': app.font_name = optarg;                        break;
        case 'z': app.font_size = atoi(optarg);                  break;
        case 'i': tcfg.interval = atof(optarg);                  break;
        case 'b':
            if (!parse_color(optarg, &app.bg_r, &app.bg_g, &app.bg_b)) {
                fprintf(stderr, "Invalid color: %s\n", optarg);
                return 1;
            }
            break;
        default: usage(argv[0]); return opt == 'h' ? 0 : 1;
        }
    }

    app.text = text_provider_create(&tcfg);
    if (!app.text) { fprintf(stderr, "Failed to create text provider\n"); return 1; }

    if (!wayland_init(&app)) return 1;
    wayland_run(&app);
    wayland_destroy(&app);
    text_provider_destroy(app.text);
    return 0;
}
