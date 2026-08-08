/* PDCursesMod SDL3 backend -- see pdcsdl3.h */

#include "pdcsdl3.h"

#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "common/pdccolor.h"
#include "common/pdccolor.c"

SDL_Window *pdc_window = NULL;
SDL_Surface *pdc_screen = NULL;
int pdc_sheight = 0, pdc_swidth = 0;
int pdc_fheight = 16, pdc_fwidth = 16;
int pdc_fthick = 2;
int pdc_scale = 2;

/* Default 16-color palette: muted pastels on dark slate, tuned for a
 * game screen rather than a terminal.  Entries are PACK_RGB(r,g,b);
 * indices follow curses COLOR_* (PDC_RGB order). */
static const PACKED_RGB pastel16[16] = {
    PACK_RGB(0x1b, 0x27, 0x33), /* 0 black: window background */
    PACK_RGB(0xc6, 0x6b, 0x62), /* 1 red */
    PACK_RGB(0x8a, 0xa8, 0x72), /* 2 green */
    PACK_RGB(0xb0, 0x8d, 0x57), /* 3 yellow (nethack brown) */
    PACK_RGB(0x6b, 0x93, 0xb8), /* 4 blue */
    PACK_RGB(0xb4, 0x8e, 0xad), /* 5 magenta */
    PACK_RGB(0x7f, 0xb4, 0xb0), /* 6 cyan */
    PACK_RGB(0xaa, 0xb4, 0xbe), /* 7 white (nethack gray) */
    PACK_RGB(0x55, 0x60, 0x6c), /* 8 bright black (dark gray) */
    PACK_RGB(0xe0, 0x8d, 0x84), /* 9 bright red (orange-ish) */
    PACK_RGB(0xa9, 0xc9, 0x8a), /* 10 bright green */
    PACK_RGB(0xe5, 0xc8, 0x90), /* 11 bright yellow */
    PACK_RGB(0x8c, 0xb4, 0xdd), /* 12 bright blue */
    PACK_RGB(0xd0, 0xa9, 0xd6), /* 13 bright magenta */
    PACK_RGB(0x99, 0xd1, 0xcd), /* 14 bright cyan */
    PACK_RGB(0xdd, 0xe4, 0xec), /* 15 bright white */
};

/* preferred size in cells (clamped to the display): 80-column map +
 * borders + a ~56-column inventory pane.  get_approx_display_cols()
 * in sdl3sys.c returns the same width so the win/curses startup
 * resize_term() is a no-op and the window stays centered. */
#define PDC_DEF_COLS 140
#define PDC_DEF_ROWS 66
#define PDC_MIN_COLS 80
#define PDC_MIN_ROWS 28

static void
_clean(void)
{
    if (pdc_window) {
        SDL_DestroyWindow(pdc_window);
        pdc_window = NULL;
        pdc_screen = NULL;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    pdc_sheight = pdc_swidth = 0;
}

/* re-fetch the window surface and pixel geometry (after any resize) */
void
PDC_refresh_pixel_geometry(void)
{
    pdc_screen = SDL_GetWindowSurface(pdc_window);
    if (pdc_screen) {
        pdc_swidth = pdc_screen->w;
        pdc_sheight = pdc_screen->h;
    }
}

/* window points per surface pixel (1.0 unless high-dpi scaled) */
static float
_points_per_pixel(void)
{
    int pw = 0, px = 0, dummy;

    SDL_GetWindowSize(pdc_window, &pw, &dummy);
    SDL_GetWindowSizeInPixels(pdc_window, &px, &dummy);
    return (px > 0 && pw > 0) ? (float) pw / (float) px : 1.0f;
}

void
PDC_scr_close(void)
{
}

void
PDC_scr_free(void)
{
    PDC_free_palette();
    _clean();
}

int
PDC_scr_open(void)
{
    SDL_DisplayID display;
    SDL_Rect usable;
    float content, ppp;
    int i, cols, rows, maxw, maxh;

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        fprintf(stderr, "Could not start SDL video: %s\n", SDL_GetError());
        return ERR;
    }
    atexit(_clean);

    display = SDL_GetPrimaryDisplay();
    content = SDL_GetDisplayContentScale(display);
    if (content < 1.0f)
        content = 1.0f;
    if (!SDL_GetDisplayUsableBounds(display, &usable)) {
        usable.w = 1280;
        usable.h = 720;
    }
    /* usable bounds, window sizes, and (on Windows) surface pixels all
       share one coordinate space; no DPI conversion here */
    maxw = usable.w * 95 / 100;
    maxh = usable.h * 90 / 100;

    /* cell size: text notably larger than a terminal's, but the full
       layout (80-column map + inventory pane + borders = ~110 cols)
       must fit without map scrollbars.  NETHACK_SDL3_SCALE overrides. */
    {
        const char *env = SDL_getenv("NETHACK_SDL3_SCALE");

        pdc_scale = env ? SDL_atoi(env) : 0;
        if (pdc_scale < 2 || pdc_scale > 16) {
            pdc_scale = (int) (2.0f * content + 0.5f);
            if (pdc_scale < 2)
                pdc_scale = 2;
            while (pdc_scale > 2
                   && (110 * 8 * pdc_scale > maxw
                       || PDC_MIN_ROWS * 8 * pdc_scale > maxh))
                pdc_scale--;
        }
    }
    pdc_fwidth = pdc_fheight = 8 * pdc_scale;
    pdc_fthick = pdc_scale;

    cols = maxw / pdc_fwidth;
    rows = maxh / pdc_fheight;
    if (cols > PDC_DEF_COLS)
        cols = PDC_DEF_COLS;
    if (rows > PDC_DEF_ROWS)
        rows = PDC_DEF_ROWS;
    if (cols < PDC_MIN_COLS)
        cols = PDC_MIN_COLS;
    if (rows < PDC_MIN_ROWS)
        rows = PDC_MIN_ROWS;

    /* No SDL_WINDOW_HIGH_PIXEL_DENSITY: on Windows it decouples window
       points from surface pixels, and this backend sizes its cells in
       pixels from the display content scale instead.  Not resizable
       for now: live resizing breaks too much of the curses layout. */
    pdc_window = SDL_CreateWindow("NetHack", cols * pdc_fwidth,
                                  rows * pdc_fheight, SDL_WINDOW_HIDDEN);
    if (!pdc_window) {
        fprintf(stderr, "Could not open SDL window: %s\n", SDL_GetError());
        return ERR;
    }
    /* position while hidden so the window appears centered */
    SDL_SetWindowPosition(pdc_window, SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED);
#ifdef _WIN32
    /* use the executable's icon resource for the title bar/taskbar */
    {
        void *hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(
                                                pdc_window),
                                            SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                            NULL);
        HICON icon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(1));

        if (hwnd && icon) {
            SendMessage((HWND) hwnd, WM_SETICON, ICON_BIG, (LPARAM) icon);
            SendMessage((HWND) hwnd, WM_SETICON, ICON_SMALL, (LPARAM) icon);
        }
    }
#endif
    SDL_ShowWindow(pdc_window);

    SDL_PumpEvents();
    PDC_refresh_pixel_geometry();
    if (!pdc_screen) {
        fprintf(stderr, "Could not get SDL window surface: %s\n",
                SDL_GetError());
        return ERR;
    }

    /* win/curses panics below 15 rows x 40 cols; don't allow it */
    ppp = _points_per_pixel();
    SDL_SetWindowMinimumSize(pdc_window,
                             (int) (40.0f * pdc_fwidth * ppp) + 1,
                             (int) (15.0f * pdc_fheight * ppp) + 1);

    PDC_init_palette();
    for (i = 0; i < 16; i++)
        PDC_set_palette_entry(i, pastel16[i]);

    SP->mono = FALSE;
    SP->orig_attr = FALSE;
    SP->mouse_wait = PDC_CLICK_PERIOD;
    SP->audible = FALSE;
    SP->termattrs = A_COLOR | WA_UNDERLINE | WA_LEFT | WA_RIGHT | WA_TOP
                    | WA_REVERSE | WA_STRIKEOUT | WA_BLINK;

    SDL_StartTextInput(pdc_window);
    PDC_mouse_set();
    PDC_reset_prog_mode();

    return OK;
}

/* the core of resize_term() */
int
PDC_resize_screen(int nlines, int ncols)
{
    if (!stdscr) { /* specifying initial size before initscr() */
        if (nlines && ncols) {
            pdc_sheight = nlines * pdc_fheight;
            pdc_swidth = ncols * pdc_fwidth;
        }
        return OK;
    }

    if (nlines && ncols) {
        SDL_Rect max;
        float ppp = _points_per_pixel();
        int top, left, bottom, right;

        if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(pdc_window),
                                       &max)) {
            if (!SDL_GetWindowBordersSize(pdc_window, &top, &left, &bottom,
                                          &right))
                top = left = bottom = right = 0;
            max.h -= top + bottom;
            max.w -= left + right;

            /* max is in points; compare in points */
            while (nlines > 15
                   && (float) (nlines * pdc_fheight) * ppp > (float) max.h)
                nlines--;
            while (ncols > 40
                   && (float) (ncols * pdc_fwidth) * ppp > (float) max.w)
                ncols--;
        }

        SDL_SetWindowSize(pdc_window,
                          (int) ((float) (ncols * pdc_fwidth) * ppp),
                          (int) ((float) (nlines * pdc_fheight) * ppp));
        /* growing/shrinking anchors the top-left corner; re-center */
        SDL_SetWindowPosition(pdc_window, SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED);
    }
    PDC_refresh_pixel_geometry();

    return OK;
}

void
PDC_reset_prog_mode(void)
{
    PDC_flushinp();
}

void
PDC_reset_shell_mode(void)
{
    PDC_flushinp();
}

void
PDC_restore_screen_mode(int i)
{
    INTENTIONALLY_UNUSED_PARAMETER(i);
}

void
PDC_save_screen_mode(int i)
{
    INTENTIONALLY_UNUSED_PARAMETER(i);
}

bool
PDC_can_change_color(void)
{
    return TRUE;
}

int
PDC_color_content(int color, int *red, int *green, int *blue)
{
    const PACKED_RGB col = PDC_get_palette_entry(color);

    *red = DIVROUND(Get_RValue(col) * 1000, 255);
    *green = DIVROUND(Get_GValue(col) * 1000, 255);
    *blue = DIVROUND(Get_BValue(col) * 1000, 255);

    return OK;
}

int
PDC_init_color(int color, int red, int green, int blue)
{
    const PACKED_RGB new_rgb = PACK_RGB(DIVROUND(red * 255, 1000),
                                        DIVROUND(green * 255, 1000),
                                        DIVROUND(blue * 255, 1000));

    if (!PDC_set_palette_entry(color, new_rgb))
        curscr->_clear = TRUE;
    return OK;
}

void
PDC_set_resize_limits(const int new_min_lines, const int new_max_lines,
                      const int new_min_cols, const int new_max_cols)
{
    INTENTIONALLY_UNUSED_PARAMETER(new_min_lines);
    INTENTIONALLY_UNUSED_PARAMETER(new_max_lines);
    INTENTIONALLY_UNUSED_PARAMETER(new_min_cols);
    INTENTIONALLY_UNUSED_PARAMETER(new_max_cols);
}
