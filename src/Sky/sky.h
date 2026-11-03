#pragma once
#ifndef SKY_H
#define SKY_H

#include "main.h"

#define SKY_TICKS_PER_DAY     24000
#define SKY_TICKS_PER_SECOND  20.0
#define SKY_CLOUD_HEIGHT      192.0f
#define SKY_HORIZON_HEIGHT    63.0f

#ifndef SKY_SMOOTH_TIME
#define SKY_SMOOTH_TIME 1  // smooth between ticks, 0 = vanilla steppy
#endif

int  sky_init(const char* texture_dir);
void sky_shutdown(void);

void sky_update(double dt);

void sky_get_fog_color(const float* look, float out_rgb[3]);
void sky_get_sky_color(float out_rgb[3]);

void sky_draw(const float* vp, const float* cam, const float* look);
void sky_draw_clouds(const float* vp, const float* cam);

void   sky_set_time(double ticks);
double sky_get_time(void);
double sky_get_day_time(void);
long   sky_get_day(void);
void   sky_set_day_cycle(int on);
int    sky_get_moon_phase(void);

void sky_set_weather(float rain, float thunder);
void sky_set_render_distance(int chunks);
void sky_set_clouds(int on);
void sky_set_biome_colors(const float sky_rgb[3], const float fog_rgb[3]);

float sky_get_darken(void);
float sky_get_brightness(void);
float sky_get_time_of_day(void);
float sky_get_sun_angle(void);

#endif