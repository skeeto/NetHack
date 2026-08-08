/* PDCursesMod SDL3 backend -- see pdcsdl3.h */

#include "pdcsdl3.h"

void
PDC_beep(void)
{
    /* quiet; an SDL3 soundlib may take this over later */
}

void
PDC_napms(int ms)
{
    while (ms > 50) {
        PDC_pump_and_peep();
        SDL_Delay(50);
        ms -= 50;
        PDC_check_for_blinking();
        PDC_doupdate();
    }
    PDC_pump_and_peep();
    SDL_Delay(ms);
    PDC_check_for_blinking();
    PDC_doupdate();
}

const char *
PDC_sysname(void)
{
    return "SDL3";
}

enum PDC_port PDC_port_val = PDC_PORT_SDL2; /* closest existing id */
