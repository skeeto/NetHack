/* NetHack 5.0	sdl3conf.h */
/* Copyright (c) NetHack SDL3 fork, 2026. */
/* NetHack may be freely redistributed.  See license for details. */

/*
 * Configuration deltas for the SDL3 host platform (sys/sdl3).
 *
 * This header is included from global.h *after* unixconf.h/windconf.h,
 * so the native OS macro (UNIX or WIN32) remains in effect -- files.c,
 * fnamesiz.h, dlb.h and friends all branch on it with no default case.
 * NETHACK_SDL3 is an additive flavor; this header only applies the
 * differences from a stock unix/windows build.
 */

#ifndef SDL3CONF_H
#define SDL3CONF_H

#define PORT_SUB_ID "sdl3"

/* All runtime files live under one SDL-provided directory; every
   fqn_prefix[] slot is set explicitly by sys/sdl3/sdl3main.c. */
#ifndef NOCWD_ASSUMPTIONS
#define NOCWD_ASSUMPTIONS
#endif

/* The game recovers its own aborted games; there is no external
   recover utility in this distribution. */
#ifndef SELF_RECOVER
#define SELF_RECOVER
#endif

/* nhdat is embedded (DLBMEM) or sits beside the executable; never
   append version digits to its name. */
#ifdef VERSION_IN_DLB_FILENAME
#undef VERSION_IN_DLB_FILENAME
#endif

/* No chdir(): prefixes are absolute.  The only core consumer is the
   --showpaths flow, which is guarded. */
#ifdef CHDIR
#undef CHDIR
#endif

/* The run-time options file lives in the port's data directory on
   every platform (windconf.h defines this for WIN32 already). */
#ifndef CONFIG_FILE
#define CONFIG_FILE ".nethackrc"
#endif

/* No external pager/compressor/mail daemon assumptions. */
#ifdef COMPRESS
#undef COMPRESS
#endif
#ifdef COMPRESS_EXTENSION
#undef COMPRESS_EXTENSION
#endif
#ifdef MAIL
#undef MAIL
#endif
#ifdef DEF_PAGER
#undef DEF_PAGER
#endif
#ifdef SHELL
#undef SHELL
#endif

#ifdef WIN32
/* --- deltas from windconf.h ------------------------------------ */

/* Features whose implementations live in sys/windows files that this
   port does not compile. */
#ifdef LAN_FEATURES
#undef LAN_FEATURES
#endif
#ifdef RUNTIME_PORT_ID
#undef RUNTIME_PORT_ID
#endif
#ifdef PORT_DEBUG
#undef PORT_DEBUG
#endif
#ifdef PORT_HELP
#undef PORT_HELP /* no porthelp file in the embedded archive */
#endif
#ifdef nethack_enter
#undef nethack_enter
#endif
#define nethack_enter(argc, argv) ((void) 0)

/* windconf turns this on for gcc, but UCRT's tmpfile_s fails in
   practice (it wants the volume root); makedefs then can't write its
   temp files.  Plain temp files in the staged dat directory work. */
#ifdef MD_USE_TMPFILE_S
#undef MD_USE_TMPFILE_S
#endif

#else /* UNIX */
/* --- deltas from unixconf.h ------------------------------------ */

/* Job control makes no sense for a windowed app, and its dosuspend()
   implementation lives in sys/share/ioctl.c which we don't compile. */
#ifdef SUSPEND
#undef SUSPEND
#endif

#endif /* WIN32 */

#endif /* SDL3CONF_H */
