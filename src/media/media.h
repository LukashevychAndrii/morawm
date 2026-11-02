#pragma once

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MORA_MEDIA_TEXT_CAPACITY 128

typedef struct {
    BOOL active;
    BOOL playing;
    wchar_t title[MORA_MEDIA_TEXT_CAPACITY];
    wchar_t artist[MORA_MEDIA_TEXT_CAPACITY];
} mora_media_t;

/* Starts listening on a background thread. `message` is posted to `notify`
 * whenever what is playing changes; read it with mora_media_get. */
BOOL mora_media_start(HWND notify, UINT message);
void mora_media_get(mora_media_t* media);

typedef enum {
    MORA_MEDIA_PREVIOUS,
    MORA_MEDIA_TOGGLE,
    MORA_MEDIA_NEXT,
    MORA_MEDIA_COMMAND_COUNT,
} mora_media_command_t;

void mora_media_command(mora_media_command_t command);
void mora_media_stop(void);

#ifdef __cplusplus
}
#endif
