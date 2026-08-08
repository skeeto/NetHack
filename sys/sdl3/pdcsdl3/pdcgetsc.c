/* PDCursesMod SDL3 backend -- see pdcsdl3.h */

#include "pdcsdl3.h"

/* get the cursor size/shape */
int
PDC_get_cursor_mode(void)
{
    return 0;
}

/* return number of screen rows */
int
PDC_get_rows(void)
{
    return pdc_sheight / pdc_fheight;
}

/* return width of screen/viewport */
int
PDC_get_columns(void)
{
    return pdc_swidth / pdc_fwidth;
}
