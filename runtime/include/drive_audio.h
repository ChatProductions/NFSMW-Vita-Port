#ifndef DRIVE_AUDIO_H
#define DRIVE_AUDIO_H
void drive_audio_volume(unsigned music,unsigned engine,unsigned voice);
int drive_audio_start(const char *root);
void drive_audio_stop(void);
void drive_audio_update(float rpm,float throttle,int paused,int engine_on);
void drive_audio_call(void);
void drive_audio_reset_call(void);
int drive_audio_call_done(void);
void drive_radio_change(int direction);
void drive_radio_toggle(void);
const char*drive_radio_label(void);
int drive_radio_enabled(void);
#endif
