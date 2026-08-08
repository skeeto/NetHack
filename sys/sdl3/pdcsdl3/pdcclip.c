/* PDCursesMod SDL3 backend -- see pdcsdl3.h */

#include "pdcsdl3.h"

#include <stdlib.h>
#include <string.h>

int
PDC_getclipboard(char **contents, long *length)
{
    char *text = SDL_GetClipboardText();
    size_t len;

    if (!text)
        return PDC_CLIP_ACCESS_ERROR;
    len = strlen(text);
    if (!len) {
        SDL_free(text);
        return PDC_CLIP_EMPTY;
    }
    *contents = malloc(len + 1);
    if (!*contents) {
        SDL_free(text);
        return PDC_CLIP_MEMORY_ERROR;
    }
    strcpy(*contents, text);
    *length = (long) len;
    SDL_free(text);
    return PDC_CLIP_SUCCESS;
}

int
PDC_setclipboard(const char *contents, long length)
{
    char *tmp = malloc(length + 1);

    if (!tmp)
        return PDC_CLIP_MEMORY_ERROR;
    memcpy(tmp, contents, length);
    tmp[length] = '\0';
    if (!SDL_SetClipboardText(tmp)) {
        free(tmp);
        return PDC_CLIP_ACCESS_ERROR;
    }
    free(tmp);
    return PDC_CLIP_SUCCESS;
}

int
PDC_freeclipboard(char *contents)
{
    free(contents);
    return PDC_CLIP_SUCCESS;
}

int
PDC_clearclipboard(void)
{
    return SDL_SetClipboardText("") ? PDC_CLIP_SUCCESS
                                    : PDC_CLIP_ACCESS_ERROR;
}
