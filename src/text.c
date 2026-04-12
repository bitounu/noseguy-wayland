#define _POSIX_C_SOURCE 200809L
#include "text.h"
#include "quotes.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>

struct TextProvider {
    TextConfig       cfg;
    int              builtin_idx;

    /* TEXT_FILE / TEXT_STDIN lines */
    char           **lines;
    int              line_count;
    int              line_idx;

    /* TEXT_COMMAND background thread */
    pthread_t        thread;
    pthread_mutex_t  lock;
    char            *slot;           /* last fetched result, owned    */
    bool             thread_active;
    bool             stop;
};

/* ── helpers ─────────────────────────────────────────────────────── */

static char *strdup_trim(const char *s) {
    while (*s == ' ' || *s == '\n' || *s == '\r') s++;
    char *d = strdup(s);
    if (!d) return NULL;
    size_t n = strlen(d);
    while (n > 0 && (d[n-1]==' '||d[n-1]=='\n'||d[n-1]=='\r')) d[--n] = '\0';
    return d;
}

static void load_lines_from_file(TextProvider *tp, FILE *fh) {
    char   buf[4096];
    char   chunk[65536];
    int    cap = 16;
    chunk[0] = '\0';
    tp->lines      = calloc(cap, sizeof(char *));
    tp->line_count = 0;

    while (fgets(buf, sizeof(buf), fh)) {
        if (buf[0] == '\n' || buf[0] == '\r') {
            if (chunk[0]) {
                if (tp->line_count >= cap - 1) {
                    cap *= 2;
                    tp->lines = realloc(tp->lines, cap * sizeof(char *));
                }
                tp->lines[tp->line_count++] = strdup_trim(chunk);
                chunk[0] = '\0';
            }
        } else {
            strncat(chunk, buf, sizeof(chunk) - strlen(chunk) - 1);
        }
    }
    if (chunk[0])
        tp->lines[tp->line_count++] = strdup_trim(chunk);
}

/* ── background thread (TEXT_COMMAND) ────────────────────────────── */

static void *command_thread(void *arg) {
    TextProvider *tp = arg;
    while (1) {
        pthread_mutex_lock(&tp->lock);
        bool quit = tp->stop;
        pthread_mutex_unlock(&tp->lock);
        if (quit) break;

        FILE *p = popen(tp->cfg.arg, "r");
        if (p) {
            char buf[4096] = {0};
            while (fgets(buf + strlen(buf),
                         (int)(sizeof(buf) - strlen(buf) - 1), p))
                ;
            pclose(p);
            char *result = strdup_trim(buf);
            pthread_mutex_lock(&tp->lock);
            free(tp->slot);
            tp->slot = result;
            pthread_mutex_unlock(&tp->lock);
        }

        struct timespec ts = { .tv_sec = (time_t)tp->cfg.interval, .tv_nsec = 0 };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* ── public API ──────────────────────────────────────────────────── */

TextProvider *text_provider_create(const TextConfig *cfg) {
    TextProvider *tp = calloc(1, sizeof(*tp));
    if (!tp) return NULL;
    tp->cfg = *cfg;
    pthread_mutex_init(&tp->lock, NULL);
    srand((unsigned)time(NULL));

    switch (cfg->source) {
    case TEXT_BUILTIN:
        break;

    case TEXT_FILE: {
        FILE *f = fopen(cfg->arg, "r");
        if (f) { load_lines_from_file(tp, f); fclose(f); }
        break;
    }

    case TEXT_STDIN:
        load_lines_from_file(tp, stdin);
        break;

    case TEXT_COMMAND:
        tp->thread_active = true;
        pthread_create(&tp->thread, NULL, command_thread, tp);
        break;
    }

    return tp;
}

const char *text_get_next(TextProvider *tp) {
    switch (tp->cfg.source) {
    case TEXT_BUILTIN: {
        int count = 0;
        while (builtin_quotes[count]) count++;
        tp->builtin_idx = rand() % count;
        return builtin_quotes[tp->builtin_idx];
    }

    case TEXT_FILE:
    case TEXT_STDIN:
        if (!tp->lines || tp->line_count == 0) return "...";
        if (tp->line_idx >= tp->line_count) tp->line_idx = 0;
        return tp->lines[tp->line_idx++];

    case TEXT_COMMAND: {
        pthread_mutex_lock(&tp->lock);
        const char *s = tp->slot ? tp->slot : "(loading...)";
        pthread_mutex_unlock(&tp->lock);
        return s;
    }
    }
    return "...";
}

void text_provider_destroy(TextProvider *tp) {
    if (!tp) return;
    if (tp->thread_active) {
        pthread_mutex_lock(&tp->lock);
        tp->stop = true;
        pthread_mutex_unlock(&tp->lock);
        pthread_join(tp->thread, NULL);
    }
    if (tp->lines) {
        for (int i = 0; i < tp->line_count; i++) free(tp->lines[i]);
        free(tp->lines);
    }
    free(tp->slot);
    pthread_mutex_destroy(&tp->lock);
    free(tp);
}
