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
        "  --text-command <cmd>       shell command for text (default: built-in)\n"
        "  --text-file <path>         read text from file\n"
        "  --stdin                    read text from stdin\n"
        "  --fps <n>                  target frame rate (default: 30)\n"
        "  --font <name>              pango font name (default: Sans)\n"
        "  --font-size <px>           font size in pixels (default: 18)\n"
        "  --font-color <#rrggbb>     speech bubble font color (default: #0d0d0d)\n"
        "  --interval <seconds>       seconds between text fetches (default: 30)\n"
        "  --reading-wpm <n>          reading speed in words/min (default: 200)\n"
        "  --bg-color <#rrggbb>       background color (default: #000000)\n"
        "  --sprites-dir <path>       directory containing nose-*.png sprites\n"
        "  --bubble-color <#rrggbbaa> bubble fill color + alpha (default: #ffffffee)\n"
        "  --bubble-border-color <#rrggbbaa>  border color + alpha (default: #2e2e2ed9)\n"
        "  --bubble-border-width <px> border line width (default: 1.5)\n",
        prog);
}

static bool parse_rgb(const char *s, double *r, double *g, double *b) {
    if (*s == '#') s++;
    unsigned ri, gi, bi;
    if (sscanf(s, "%02x%02x%02x", &ri, &gi, &bi) != 3) return false;
    *r = ri / 255.0; *g = gi / 255.0; *b = bi / 255.0;
    return true;
}

static bool parse_rgba(const char *s, double *r, double *g, double *b, double *a) {
    if (*s == '#') s++;
    unsigned ri, gi, bi, ai = 255;
    int n = sscanf(s, "%02x%02x%02x%02x", &ri, &gi, &bi, &ai);
    if (n < 3) return false;
    *r = ri / 255.0; *g = gi / 255.0; *b = bi / 255.0; *a = ai / 255.0;
    return true;
}

int main(int argc, char **argv) {
    TextConfig tcfg = { .source = TEXT_BUILTIN, .arg = NULL, .interval = 30.0 };
    const char *sprites_dir = NULL;

    App app = {
        .fps       = 30,
        .font_name = "Sans",
        .font_size = 18,
        .bg_r = 0.0, .bg_g = 0.0, .bg_b = 0.0,
        .bubble = {
            .fill_r = 1.0, .fill_g = 1.0, .fill_b = 1.0, .fill_a = 0.93,
            .border_r = 0.18, .border_g = 0.18, .border_b = 0.18, .border_a = 0.85,
            .border_width = 1.5,
            .font_r = 0.05, .font_g = 0.05, .font_b = 0.05,
        },
    };

    static const struct option long_opts[] = {
        { "text-command",        required_argument, NULL, 'c' },
        { "text-file",           required_argument, NULL, 'f' },
        { "stdin",               no_argument,       NULL, 's' },
        { "fps",                 required_argument, NULL, 'F' },
        { "font",                required_argument, NULL, 'n' },
        { "font-size",           required_argument, NULL, 'z' },
        { "font-color",          required_argument, NULL, 'C' },
        { "interval",            required_argument, NULL, 'i' },
        { "bg-color",            required_argument, NULL, 'b' },
        { "sprites-dir",         required_argument, NULL, 'S' },
        { "bubble-color",        required_argument, NULL, 'B' },
        { "bubble-border-color", required_argument, NULL, 'D' },
        { "bubble-border-width", required_argument, NULL, 'W' },
        { "reading-wpm",         required_argument, NULL, 'R' },
        { "help",                no_argument,       NULL, 'h' },
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
        case 'S': sprites_dir   = optarg;                        break;
        case 'W': app.bubble.border_width = atof(optarg);        break;
        case 'R': app.reading_wpm = atoi(optarg);                break;
        case 'b':
            if (!parse_rgb(optarg, &app.bg_r, &app.bg_g, &app.bg_b)) {
                fprintf(stderr, "Invalid color: %s\n", optarg); return 1;
            }
            break;
        case 'C':
            if (!parse_rgb(optarg, &app.bubble.font_r,
                                   &app.bubble.font_g, &app.bubble.font_b)) {
                fprintf(stderr, "Invalid color: %s\n", optarg); return 1;
            }
            break;
        case 'B':
            if (!parse_rgba(optarg, &app.bubble.fill_r, &app.bubble.fill_g,
                                    &app.bubble.fill_b, &app.bubble.fill_a)) {
                fprintf(stderr, "Invalid color: %s\n", optarg); return 1;
            }
            break;
        case 'D':
            if (!parse_rgba(optarg, &app.bubble.border_r, &app.bubble.border_g,
                                    &app.bubble.border_b, &app.bubble.border_a)) {
                fprintf(stderr, "Invalid color: %s\n", optarg); return 1;
            }
            break;
        default: usage(argv[0]); return opt == 'h' ? 0 : 1;
        }
    }

    app.text = text_provider_create(&tcfg);
    if (!app.text) { fprintf(stderr, "Failed to create text provider\n"); return 1; }

    if (!wayland_init(&app)) return 1;
    wayland_load_sprites(&app, sprites_dir);
    wayland_run(&app);
    wayland_destroy(&app);
    text_provider_destroy(app.text);
    return 0;
}
