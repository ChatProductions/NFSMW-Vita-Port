#ifndef WORLD_TEXTURES_H
#define WORLD_TEXTURES_H
#include <vita2d.h>
int world_textures_open(const char*root);
void world_textures_close(void);
void world_textures_begin(void);
void world_textures_request(unsigned id,float distance);
void world_textures_prepare(void);
void world_textures_prepare_budget(unsigned budget);
int world_textures_collidable(unsigned id);
int world_textures_event_barrier(unsigned id);
int world_textures_opaque(unsigned id);
vita2d_texture*world_textures_get(unsigned id);
#endif
