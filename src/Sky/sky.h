#pragma once
#ifndef SKY_H
#define SKY_H

#include "main.h"

#define SKY_TICKS_PER_DAY     24000
#define SKY_TICKS_PER_SECOND  20.0
#define SKY_CLOUD_HEIGHT      192.0f   /* OverworldEffects cloud height (1.18+) */
#define SKY_HORIZON_HEIGHT    63.0f    /* ClientLevelData.getHorizonHeight (non-flat) */

 /* Vanilla only changes the time once per tick (20 Hz). 1 = interpolate between ticks
  * (smooth, visually identical), 0 = step in whole ticks exactly like vanilla. */
#ifndef SKY_SMOOTH_TIME
#define SKY_SMOOTH_TIME 1
#endif

  /* ---- setup ---- */
  /* texture_dir must contain: sun.png clouds.png and the 8 moon phase images
   *   full_moon waning_gibbous third_quarter waning_crescent new_moon waxing_crescent
   *   first_quarter waxing_gibbous (.png)
   * Needs a current GL 3.3 context. Returns 1 if the shader built, 0 otherwise.
   * A missing texture only disables that element (error goes to stderr). */
int  sky_init(const char* texture_dir);
void sky_shutdown(void);

/* ---- per frame ---- */
/* dt in seconds. Advances time of day (if the cycle is on) and the cloud drift. */
void sky_update(double dt);

/* Fog / clear colour. look = camera view direction (unit vector). It tints the horizon
 * towards the sunrise/sunset colour when you face the sun, exactly like vanilla.
 * Use it for glClearColor each frame (vanilla clears to the fog colour) and for terrain fog. */
void sky_get_fog_color(const float* look, float out_rgb[3]);
/* Zenith colour of the dome (no fog, no sunrise tint). */
void sky_get_sky_color(float out_rgb[3]);

/* Right after glClear, before terrain. vp = projection * view, cam = eye position,
 * look = view direction (unit). */
void sky_draw(const float* vp, const float* cam, const float* look);
/* After opaque + translucent terrain (clouds are depth tested). Uses the fog colour
 * computed by the last sky_draw. */
void sky_draw_clouds(const float* vp, const float* cam);

/* ---- time ---- */
void   sky_set_time(double ticks);        /* total ticks. 0 = sunrise, 6000 noon, 12000 sunset, 18000 midnight */
double sky_get_time(void);                /* total ticks since time 0 */
double sky_get_day_time(void);            /* 0 .. 24000 */
long   sky_get_day(void);                 /* whole days elapsed */
void   sky_set_day_cycle(int on);         /* 0 = freeze time (clouds keep drifting) */
int    sky_get_moon_phase(void);          /* 0 full, 1 waning gibbous, 2 third q., 3 waning crescent,
                                             4 new, 5 waxing crescent, 6 first q., 7 waxing gibbous */

                                             /* ---- environment ---- */
void sky_set_weather(float rain, float thunder);   /* 0..1 each; thunder is already scaled by rain like vanilla */
void sky_set_render_distance(int chunks);          /* fog distance for the sky / clouds (default 12) */
void sky_set_clouds(int on);                       /* default on */
/* Biome colours (vanilla plains: sky 0x78A7FF, fog 0xC0D8FF = the defaults). */
void sky_set_biome_colors(const float sky_rgb[3], const float fog_rgb[3]);

/* ---- for the lighting module ---- */
float sky_get_darken(void);       /* 0 (day) .. 11 (night), includes rain/thunder (Level.skyDarken, unrounded) */
float sky_get_brightness(void);   /* 1 day .. 0 night: clamp(cos(a)*2+0.5, 0, 1) */
float sky_get_time_of_day(void);  /* 0..1, 0 = noon (Level.getTimeOfDay) */
float sky_get_sun_angle(void);    /* radians, getTimeOfDay * 2pi */

#endif