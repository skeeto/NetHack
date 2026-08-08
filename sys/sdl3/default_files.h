/* NetHack 5.0  default_files.h */
/* Copyright (c) NetHack SDL3 fork, 2026. */
/* NetHack may be freely redistributed.  See license for details. */

/* Initial contents for runtime files seeded on first launch. */

#ifndef DEFAULT_FILES_H
#define DEFAULT_FILES_H

/* System configuration: single-user machine, everything allowed. */
static const char sdl3_default_sysconf[] =
    "# NetHack-SDL3 system configuration (seeded on first run)\n"
    "WIZARDS=*\n"
    "EXPLORERS=*\n"
    "MAXPLAYERS=10\n";

/* Run-time options: the bordered curses layout -- map on top, tall\n
 * message pane under it, 3-line status, persistent inventory pane on
 * the right, menu coloring for blessed/cursed states. */
static const char sdl3_default_nethackrc[] =
    "# NetHack-SDL3 options (seeded on first run; edit freely)\n"
    "OPTIONS=windowtype:curses\n"
    "OPTIONS=perm_invent\n"
    "OPTIONS=align_message:bottom\n"
    "OPTIONS=align_status:bottom\n"
    "OPTIONS=statuslines:3\n"
    "OPTIONS=windowborders:1\n"
    "OPTIONS=color,hilite_pet,hilite_pile,hitpointbar\n"
    "OPTIONS=menucolors\n"
    "MENUCOLOR=\" blessed \"=green\n"
    "MENUCOLOR=\" holy \"=green\n"
    "MENUCOLOR=\" cursed \"=red\n"
    "MENUCOLOR=\" unholy \"=red\n"
    "MENUCOLOR=\" cursed .* (being worn)\"=orange&underline\n";

#endif /* DEFAULT_FILES_H */
