/* PDCursesMod SDL3 backend -- see pdcsdl3.h */

#include "pdcsdl3.h"

int
PDC_curs_set(int visibility)
{
    int ret_vis = SP->visibility;

    SP->visibility = visibility;
    PDC_gotoyx(SP->cursrow, SP->curscol);
    return ret_vis;
}

void
PDC_set_title(const char *title)
{
    (void) SDL_SetWindowTitle(pdc_window, title);
}

int
PDC_set_blink(bool blinkon)
{
    if (!SP)
        return ERR;

    if (SP->color_started) /* see PDCursesMod common/pdccolor.txt */
        COLORS = 256 + (256 * 256 * 256);

    if (blinkon)
        SP->termattrs |= A_BLINK;
    else
        SP->termattrs &= ~A_BLINK;
    return OK;
}

int
PDC_set_bold(bool boldon)
{
    if (!SP)
        return ERR;
    /* no bold face in the bitmap font; A_BOLD brightens instead */
    return boldon ? ERR : OK;
}
