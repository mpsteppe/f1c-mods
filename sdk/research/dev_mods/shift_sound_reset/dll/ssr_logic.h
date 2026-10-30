/* ssr_logic.h - Shift Sound Reset decision logic.
 *
 * Pure functions (no Windows, no game memory) so ssr_test.c can check them
 * headless. Included by shift_sound_reset.c.
 */
#ifndef SSR_LOGIC_H
#define SSR_LOGIC_H

#include <ctype.h>
#include <stddef.h>
#include <string.h>

/* ---- shift detection ----------------------------------------------------
 * Live probe 2026-10-26: every shift is old gear -> 0 -> new gear within one
 * frame. A shift is therefore "landed in a non-neutral gear that differs from
 * the last non-neutral gear". Holding neutral and re-selecting the same gear
 * is not a shift. The first gear selected after (re)start only arms. */
typedef struct SsrShift {
    int last_gear;   /* last non-neutral gear */
    int armed;
} SsrShift;

#define SSR_GEAR_MIN (-1)
#define SSR_GEAR_MAX 9

/* Returns +1 for an upshift, -1 for a downshift, 0 otherwise. */
static int ssr_shift_step(SsrShift *s, int gear)
{
    int dir;
    if (gear < SSR_GEAR_MIN || gear > SSR_GEAR_MAX) {
        s->armed = 0;
        return 0;
    }
    if (gear == 0) return 0;
    if (!s->armed) {
        s->armed = 1;
        s->last_gear = gear;
        return 0;
    }
    if (gear == s->last_gear) return 0;
    dir = gear > s->last_gear ? 1 : -1;
    s->last_gear = gear;
    return dir;
}

/* ---- fade-in -------------------------------------------------------------
 * Linear gain from 0 to 1 over fade_ms after a restart. */
static float ssr_fade_gain(double elapsed_ms, double fade_ms)
{
    if (fade_ms <= 0.0 || elapsed_ms >= fade_ms) return 1.0f;
    if (elapsed_ms <= 0.0) return 0.0f;
    return (float)(elapsed_ms / fade_ms);
}

/* ---- .sfx engine layers --------------------------------------------------
 * Parses one .sfx line. On VS_INSIDE_ENGINE_<n>=file or
 * VS_OUTSIDE_ENGINE_<n>=file writes the file, lower-cased with '\' separators,
 * and returns 1 (inside) or 2 (outside). Returns 0 for anything else,
 * including commented-out lines. */
#define SSR_KIND_INSIDE 1
#define SSR_KIND_OUTSIDE 2

static int ssr_prefix_ci(const char *s, const char *prefix)
{
    while (*prefix) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return 0;
        s++;
        prefix++;
    }
    return 1;
}

static int ssr_parse_engine_line(const char *line, char *out, size_t cap)
{
    int kind;
    size_t n = 0;
    const char *end;
    if (!line || !out || cap < 2) return 0;
    while (*line == ' ' || *line == '\t') line++;
    if (ssr_prefix_ci(line, "VS_INSIDE_ENGINE_")) {
        kind = SSR_KIND_INSIDE;
        line += strlen("VS_INSIDE_ENGINE_");
    } else if (ssr_prefix_ci(line, "VS_OUTSIDE_ENGINE_")) {
        kind = SSR_KIND_OUTSIDE;
        line += strlen("VS_OUTSIDE_ENGINE_");
    } else {
        return 0;
    }
    if (!isdigit((unsigned char)*line)) return 0;
    while (isdigit((unsigned char)*line)) line++;
    while (*line == ' ' || *line == '\t') line++;
    if (*line != '=') return 0;
    line++;
    while (*line == ' ' || *line == '\t' || *line == '"') line++;
    end = line;
    while (*end && *end != '\r' && *end != '\n' && !(end[0] == '/' && end[1] == '/')) end++;
    while (end > line && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '"')) end--;
    if (end == line) return 0;
    for (; line < end && n + 1 < cap; line++)
        out[n++] = *line == '/' ? '\\' : (char)tolower((unsigned char)*line);
    if (line < end) return 0;   /* too long: refuse rather than truncate */
    out[n] = '\0';
    return kind;
}

/* 1 when full_path ends with rel (case-insensitive, '/' == '\') at a path
 * boundary: "AUDIO\VEHICLE\INT\FERRARI\MAX4.WAV" matches "int\ferrari\max4.wav"
 * but not "ferrari\max44.wav" or "nt\ferrari\max4.wav". */
static int ssr_path_matches(const char *full_path, const char *rel)
{
    size_t lf = strlen(full_path), lr = strlen(rel), i;
    if (!lr || lr > lf) return 0;
    for (i = 0; i < lr; i++) {
        char a = full_path[lf - lr + i], b = rel[i];
        if (a == '/') a = '\\';
        if (b == '/') b = '\\';
        if (tolower((unsigned char)a) != tolower((unsigned char)b)) return 0;
    }
    return lf == lr || full_path[lf - lr - 1] == '\\' || full_path[lf - lr - 1] == '/';
}

/* ---- ownership ------------------------------------------------------------
 * Live evidence (README rules 16-18): all of a car's sound objects share one
 * position, but that point is NOT the car's WorldPosition: it sits 1.0-1.3 m
 * away, by a car-dependent amount. So the focus car's sound point is found
 * from the sounds themselves: the closest engine-layer sound within search_m
 * of the car, kept as an offset relative to the car and refreshed every
 * window_ms. A sound belongs to the focus car when it is within match_m of
 * that point. Another car's engine cannot be within match_m of it. */
static float ssr_dist_sq(const float a[3], const float b[3])
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

typedef struct SsrAnchor {
    float rel[3];          /* the focus car's sound point, relative to the car */
    int valid;
    float best_rel[3];     /* closest candidate in the current window */
    float best_d2;
    int have_best;
    double window_start, last_best_at;
} SsrAnchor;

#define SSR_ANCHOR_STALE_MS 500.0

/* Feeds one engine-layer sound (rel = sound position - car position).
 * Returns 1 when it belongs to the focus car; writes its distance from the
 * car (squared) for diagnostics. */
static int ssr_anchor_classify(SsrAnchor *a, const float rel[3], double now, float search_m,
                               float match_m, double window_ms, float *d2_out)
{
    static const float origin[3] = {0.0f, 0.0f, 0.0f};
    float d2 = ssr_dist_sq(rel, origin);
    if (d2_out) *d2_out = d2;
    if (!(d2 == d2)) return 0;                 /* NaN position */
    if (now - a->window_start >= window_ms) {
        if (a->have_best) {
            memcpy(a->rel, a->best_rel, sizeof a->rel);
            a->valid = 1;
        } else if (now - a->last_best_at > SSR_ANCHOR_STALE_MS) {
            a->valid = 0;                      /* the car's sounds stopped */
        }
        a->have_best = 0;
        a->window_start = now;
    }
    if (d2 <= search_m * search_m && (!a->have_best || d2 < a->best_d2)) {
        memcpy(a->best_rel, rel, sizeof a->best_rel);
        a->best_d2 = d2;
        a->have_best = 1;
        a->last_best_at = now;
    }
    /* Only a completed window sets the point, so a car alongside that happens
     * to be processed first can never be adopted; ownership starts one
     * window (50 ms) after a reset or a change of focus car. */
    return a->valid && ssr_dist_sq(rel, a->rel) <= match_m * match_m;
}

#endif
