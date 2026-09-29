#ifndef NFSMW_SETTINGS_H
#define NFSMW_SETTINGS_H
#define SETTINGS_COUNT 11
typedef struct {unsigned value[SETTINGS_COUNT];} GameSettings;
enum {OPT_CAMERA,OPT_MIRROR,OPT_FPS,OPT_HELP,OPT_VIEW,OPT_DEADZONE,OPT_STEER,OPT_INVERT,OPT_MUSIC,OPT_ENGINE,OPT_VOICE};
void settings_defaults(GameSettings*s);
int settings_valid(const GameSettings*s);
int settings_load(const char*root,GameSettings*s);
int settings_save(const char*root,const GameSettings*s);
void settings_ui_category(unsigned category);
void settings_ui_run(void);
void settings_apply_audio(const GameSettings*s);
#define SETTINGS_ROOT "ux0:data/nfsmw/runtime/save"
#endif
