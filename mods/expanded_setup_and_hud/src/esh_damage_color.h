#ifndef ESH_DAMAGE_COLOR_H
#define ESH_DAMAGE_COLOR_H

#include <stdint.h>

#define ESH_COLOR_BLACK  UINT32_C(0xFF000000)
#define ESH_COLOR_GREEN  UINT32_C(0xFF00FF00)
#define ESH_COLOR_YELLOW UINT32_C(0xFFFFFF00)
#define ESH_COLOR_RED    UINT32_C(0xFFFF0000)

#define ESH_DAMAGE_EPSILON 1e-6
#define ESH_PI 3.14159265358979323846

static inline double esh_deg_to_rad(double deg)
{
    return deg * (ESH_PI / 180.0);
}

/* Normalizes a damage accumulator delta (current - baseline) against its max
 * delta cap into [0.0, 1.0]. Any NaN or sub-epsilon delta returns 0.0. */
static inline double esh_normalize_damage(double current, double baseline, double max_delta)
{
    double delta;
    if (!(max_delta > 1e-9))
        return 0.0;
    delta = current - baseline;
    /* !(delta > eps) also catches NaN safely. */
    if (!(delta > ESH_DAMAGE_EPSILON))
        return 0.0;
    if (delta >= max_delta)
        return 1.0;
    return delta / max_delta;
}

/* Returns the worst (maximum) normalized damage across a drag/lift pair. */
static inline double esh_pair_damage_norm(
    double cur_drag, double base_drag, double max_drag_delta,
    double cur_lift, double base_lift, double max_lift_delta)
{
    double d = esh_normalize_damage(cur_drag, base_drag, max_drag_delta);
    double l = esh_normalize_damage(cur_lift, base_lift, max_lift_delta);
    return (d >= l) ? d : l;
}

/* Normalizes cumulative suspension/wheel bend (radians, baseline 0.0) against
 * the configured maximum angle in degrees. */
static inline double esh_suspension_damage_norm(double bend_rad, double max_bend_deg)
{
    if (!(max_bend_deg > 0.0))
        return 0.0;
    return esh_normalize_damage(bend_rad, 0.0, esh_deg_to_rad(max_bend_deg));
}

/* Maps normalized damage [0.0, 1.0] to ARGB:
 *   - 0.0 (undamaged) -> Green (0xFF00FF00)
 *   - > 0.0           -> Yellow (0xFFFFFF00) -> Orange (0xFFFF8000) -> Red (0xFFFF0000)
 */
static inline uint32_t esh_damage_to_argb(double norm_damage)
{
    double t;
    uint32_t g;
    if (!(norm_damage > 0.0))
        return ESH_COLOR_GREEN;
    t = (norm_damage > 1.0) ? 1.0 : norm_damage;
    g = (uint32_t)(255.0 * (1.0 - t) + 0.5);
    if (g > 255u)
        g = 255u;
    return UINT32_C(0xFFFF0000) | (g << 8);
}

#endif
