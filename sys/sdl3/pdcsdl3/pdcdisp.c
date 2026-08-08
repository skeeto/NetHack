/* PDCursesMod SDL3 backend -- see pdcsdl3.h */

#include "pdcsdl3.h"

#include <stdlib.h>
#include <string.h>

#define USE_UNICODE_ACS_CHARS 1
#include "common/acs_defs.h" /* defines acs_map[] */
#include "common/pdccolor.h"
#include "common/blink.c" /* PDC_gotoyx, PDC_check_for_blinking */

#define MAXRECT 200 /* dirty rects to queue before forcing an update */

static SDL_Rect uprect[MAXRECT];
static int rectcount = 0;

/* debug aid: NETHACK_SDL3_SHOT=<path.bmp> snapshots the window
   surface (at most once per second) whenever it is presented */
static void
_debug_shot(void)
{
    static const char *path;
    static bool inited;
    static Uint64 last;

    if (!inited) {
        inited = TRUE;
        path = SDL_getenv("NETHACK_SDL3_SHOT");
    }
    if (path && pdc_screen && SDL_GetTicks() - last > 1000) {
        last = SDL_GetTicks();
        (void) SDL_SaveBMP(pdc_screen, path);
    }
}

void
PDC_update_rects(void)
{
    if (rectcount) {
        if (rectcount == MAXRECT)
            SDL_UpdateWindowSurface(pdc_window);
        else
            SDL_UpdateWindowSurfaceRects(pdc_window, uprect, rectcount);
        rectcount = 0;
    }
}

static Uint32
_mapped_color(const int color_idx)
{
    const PACKED_RGB rgb =
        PDC_get_palette_entry(color_idx > 0 ? color_idx : 0);
    const SDL_PixelFormatDetails *det =
        SDL_GetPixelFormatDetails(pdc_screen->format);

    return SDL_MapRGB(det, NULL, (Uint8) Get_RValue(rgb),
                      (Uint8) Get_GValue(rgb), (Uint8) Get_BValue(rgb));
}

/* binary search of the embedded glyph table */
static const unsigned char *
_glyph_bits(unsigned int cp)
{
    int lo = 0, hi = nh_font8_count - 1;

    while (lo <= hi) {
        int mid = (lo + hi) / 2;

        if (nh_font8[mid].cp == cp)
            return nh_font8[mid].rows;
        if (nh_font8[mid].cp < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

/* fill a pixel rectangle with a mapped color; rect in pixel coords */
static void
_fill(int x, int y, int w, int h, Uint32 color)
{
    SDL_Rect r;

    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    (void) SDL_FillSurfaceRect(pdc_screen, &r, color);
}

/* draw one cell */
static void
_render_cell(int row, int col, chtype ch)
{
    unsigned int cp;
    int fg, bg, tmp;
    const unsigned char *bits;
    const int px = col * pdc_fwidth, py = row * pdc_fheight;
    const attr_t sysattrs = SP->termattrs;
    const bool blinked_off = SP->blink_state && (ch & A_BLINK)
                             && (sysattrs & A_BLINK);
    Uint32 mfg, mbg;

    cp = (unsigned int) (ch & A_CHARTEXT);
    if (_is_altcharset(ch))
        cp = (unsigned int) (acs_map[ch & 0x7f] & A_CHARTEXT);
    if (cp == DUMMY_CHAR_NEXT_TO_FULLWIDTH)
        cp = ' ';

    extended_pair_content((int) PAIR_NUMBER(ch), &fg, &bg);
    if (fg < 0)
        fg = SP->default_foreground_idx;
    if (bg < 0)
        bg = SP->default_background_idx;
    if ((ch & A_BOLD) && !(sysattrs & A_BOLD) && fg < 8)
        fg += 8;
    if ((ch & A_BLINK) && !(sysattrs & A_BLINK) && bg < 8)
        bg += 8;
    if (ch & A_REVERSE) {
        tmp = fg;
        fg = bg;
        bg = tmp;
    }
    mfg = _mapped_color(fg);
    mbg = _mapped_color(bg);

    _fill(px, py, pdc_fwidth, pdc_fheight, mbg);

    if (!blinked_off) {
        bits = _glyph_bits(cp);
        if (!bits && cp > 0x7f)
            bits = _glyph_bits(0xfffd); /* replacement character */
        if (!bits && cp != ' ')
            bits = _glyph_bits('?');
        if (bits && cp != ' ') {
            int fy, fx;

            for (fy = 0; fy < 8; fy++) {
                unsigned rowbits = bits[fy];

                if (!rowbits)
                    continue;
                for (fx = 0; fx < 8; fx++)
                    if (rowbits & (0x80u >> fx))
                        _fill(px + fx * pdc_scale, py + fy * pdc_scale,
                              pdc_scale, pdc_scale, mfg);
            }
        }
    }

    /* line attributes; drawn even when blinking text is blanked */
    if (ch & (WA_UNDERLINE | WA_TOP | WA_STRIKEOUT | WA_LEFT | WA_RIGHT)) {
        Uint32 mline = (SP->line_color >= 0) ? _mapped_color(SP->line_color)
                                             : mfg;

        if (ch & WA_UNDERLINE)
            _fill(px, py + pdc_fheight - pdc_fthick, pdc_fwidth, pdc_fthick,
                  mline);
        if (ch & WA_TOP)
            _fill(px, py, pdc_fwidth, pdc_fthick, mline);
        if (ch & WA_STRIKEOUT)
            _fill(px, py + (pdc_fheight - pdc_fthick) / 2, pdc_fwidth,
                  pdc_fthick, mline);
        if (ch & WA_LEFT)
            _fill(px, py, pdc_fthick, pdc_fheight, mline);
        if (ch & WA_RIGHT)
            _fill(px + pdc_fwidth - pdc_fthick, py, pdc_fthick, pdc_fheight,
                  mline);
    }
}

/* update the given physical line to look like the one in curscr */
void
PDC_transform_line(int lineno, int x, int len, const chtype *srcp)
{
    int i;
    chtype tch;

    if (!pdc_screen)
        return;

    if (SP->drawing_cursor) {
        tch = *srcp;
        if (SP->drawing_cursor == 2) /* full-cell cursor */
            tch ^= A_REVERSE;
        if (SP->drawing_cursor == 3) /* hollow-box cursor */
            tch ^= A_UNDERLINE | A_TOP | A_LEFT | A_RIGHT;
        srcp = &tch;
        len = 1;
    }

    for (i = 0; i < len; i++)
        _render_cell(lineno, x + i, srcp[i]);

    /* cursor styles 1 (normal), 4, 5: partial block over the cell,
       matching the reference sdl2 backend */
    if (SP->drawing_cursor == 1 || SP->drawing_cursor == 4
        || SP->drawing_cursor == 5) {
        int h = pdc_fheight / ((SP->drawing_cursor == 5) ? 2 : 5);
        int fg, bg;

        if (h < pdc_fthick)
            h = pdc_fthick;
        extended_pair_content((int) PAIR_NUMBER(*srcp), &fg, &bg);
        if (fg < 0)
            fg = SP->default_foreground_idx;
        if (*srcp & A_REVERSE) {
            fg = bg;
            if (fg < 0)
                fg = SP->default_background_idx;
        }
        _fill(x * pdc_fwidth, lineno * pdc_fheight + pdc_fheight - h,
              pdc_fwidth, h, _mapped_color(fg));
    }

    if (rectcount == MAXRECT)
        PDC_update_rects();
    uprect[rectcount].x = x * pdc_fwidth;
    uprect[rectcount].y = lineno * pdc_fheight;
    uprect[rectcount].w = len * pdc_fwidth;
    uprect[rectcount].h = pdc_fheight;
    rectcount++;
}

void
PDC_doupdate(void)
{
    PDC_update_rects();
    _debug_shot();
}

/* Keep the display fresh on expose/restore during naps, WITHOUT
   dequeuing input events -- pulling keystrokes here and pushing them
   back rotates SDL's queue and scrambles typing.  Only window refresh
   events are extracted (via SDL_PeepEvents), leaving input untouched
   for the pdckbd lookahead to consume in order. */
void
PDC_pump_and_peep(void)
{
    static const Uint32 refresh[] = { SDL_EVENT_WINDOW_EXPOSED,
                                      SDL_EVENT_WINDOW_RESTORED,
                                      SDL_EVENT_WINDOW_SHOWN };
    SDL_Event ev;
    size_t i;

    SDL_PumpEvents();
    for (i = 0; i < sizeof refresh / sizeof refresh[0]; i++)
        while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, refresh[i], refresh[i])
               > 0) {
            if (pdc_window)
                SDL_UpdateWindowSurface(pdc_window);
            rectcount = 0;
        }
}
