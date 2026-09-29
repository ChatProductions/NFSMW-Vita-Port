#ifndef NFSMW_RENDERER_H
#define NFSMW_RENDERER_H
#include "asset_store.h"
int renderer_init(void);
void renderer_status(AssetResult result, unsigned checks, int video_result);
void renderer_loading_end(void);
void renderer_loading(const char *stage, unsigned step, unsigned total);
void renderer_video_wait(void);
void renderer_overlay_transition(const char *const *lines, unsigned count, float alpha, float offset);
void renderer_form(const char*title,const char*const*labels,const char*const*values,unsigned count,unsigned selected,const char*hint);
void renderer_hint(const char*message);
void renderer_overlay(const char *const *lines, unsigned count);
void renderer_shutdown(void);
#endif
