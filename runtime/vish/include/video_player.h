#ifndef NFSMW_VIDEO_PLAYER_H
#define NFSMW_VIDEO_PLAYER_H
/* 0 completed, 1 skipped, negative failure; timeouts apply to the playback polling loop. */
int video_play(const char *path);
#endif
