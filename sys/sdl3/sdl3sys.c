/* NetHack 5.0  sdl3sys.c */
/* Copyright (c) NetHack SDL3 fork, 2026. */
/* NetHack may be freely redistributed.  See license for details. */

/* OS hooks for the SDL3 host platform: everything the core expects a
 * sys/ port to provide, for both Windows (WIN32) and POSIX (UNIX)
 * builds.  Lock handling is adapted from sys/windows/windmain.c and
 * shared between the two. */

#ifdef _WIN32
#include "win32api.h" /* windows.h wrapper; must precede hack.h */
#include <bcrypt.h>
#include <io.h>
#endif

#include "hack.h"
#include "dlb.h"

#include <SDL3/SDL.h>
#include <errno.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

extern char nh_basedir[FQN_MAX_FILENAME]; /* sdl3main.c */
extern char nh_prefdir[FQN_MAX_FILENAME];

static int do_getlock(void);
static int self_recover_prompt(void);
static int eraseoldlocks(void);

/* -------------------------------------------------------------------
 * Fatal error reporting
 */

ATTRNORETURN void
error(const char *fmt, ...)
{
    va_list ap;
    char buf[BUFSZ];

    va_start(ap, fmt);
    (void) vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    (void) fprintf(stderr, "%s\n", buf);
    SDL_Log("%s", buf); /* reaches the debugger log in windowed builds */
#ifdef WIN32
    /* also reach players without a console (windowed builds) */
    (void) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NetHack", buf,
                                    (SDL_Window *) 0);
#endif
    exit(EXIT_FAILURE);
}

/* -------------------------------------------------------------------
 * Random seed and uuid
 */

unsigned long
sys_random_seed(void)
{
    unsigned long seed = 0L;
    boolean no_seed = TRUE;
#ifdef WIN32
    if (BCryptGenRandom((BCRYPT_ALG_HANDLE) 0, (PUCHAR) &seed, sizeof seed,
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0) {
        has_strong_rngseed = TRUE;
        no_seed = FALSE;
    }
#else
    FILE *fptr = fopen("/dev/urandom", "r");

    if (fptr) {
        if (fread(&seed, sizeof seed, 1, fptr) == 1) {
            has_strong_rngseed = TRUE;
            no_seed = FALSE;
        }
        (void) fclose(fptr);
    }
#endif
    if (no_seed) {
        unsigned long pid = (unsigned long) getpid();

        seed = (unsigned long) getnow(); /* time((TIME_type) 0) */
        if (pid) {
            if (!(pid & 3L))
                pid -= 1L;
            seed *= pid;
        }
    }
    return seed;
}

void
get_nhuuid(void)
{
    unsigned char raw[16];
    int i;

    if (svn.nhuuid[0])
        return;

#ifdef WIN32
    if (BCryptGenRandom((BCRYPT_ALG_HANDLE) 0, (PUCHAR) raw, sizeof raw,
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
#else
    FILE *fptr = fopen("/dev/urandom", "r");
    boolean ok = fptr && fread(raw, sizeof raw, 1, fptr) == 1;

    if (fptr)
        (void) fclose(fptr);
    if (!ok)
#endif
        (void) memset(raw, 0, sizeof raw);

    /* RFC 4122 version 4 layout */
    raw[6] = (raw[6] & 0x0f) | 0x40;
    raw[8] = (raw[8] & 0x3f) | 0x80;
    for (i = 0; i < 16; i++) {
        const char *dash = (i == 4 || i == 6 || i == 8 || i == 10) ? "-" : "";

        Snprintf(eos(svn.nhuuid), sizeof svn.nhuuid - strlen(svn.nhuuid),
                 "%s%02x", dash, raw[i]);
    }
}

void
free_nhuuid(void)
{
    (void) memset(svn.nhuuid, 0, sizeof svn.nhuuid);
}

/* -------------------------------------------------------------------
 * Filename handling
 */

/* replace filesystem-hostile characters in a filename fragment */
#ifdef WIN32
void
nt_regularize(char *s)
{
    unsigned char *lp;

    for (lp = (unsigned char *) s; *lp; lp++)
        if (*lp == '?' || *lp == '"' || *lp == '\\' || *lp == '/'
            || *lp == '>' || *lp == '<' || *lp == '*' || *lp == '|'
            || *lp == ':' || *lp > 127 || *lp < 32)
            *lp = '_';
}
#else
void
regularize(char *s)
{
    char *lp;

    while ((lp = strchr(s, '.')) != 0 || (lp = strchr(s, '/')) != 0
           || (lp = strchr(s, ' ')) != 0)
        *lp = '_';
}
#endif

/* add a trailing path separator if there is not one already */
void
append_slash(char *name)
{
    char *ptr;

    if (!*name)
        return;
    ptr = name + (strlen(name) - 1);
#ifdef WIN32
    if (*ptr != '\\' && *ptr != '/' && *ptr != ':') {
        *++ptr = '\\';
        *++ptr = '\0';
    }
#else
    if (*ptr != '/') {
        *++ptr = '/';
        *++ptr = '\0';
    }
#endif
}

boolean
file_exists(const char *path)
{
    struct stat sb;

    return (boolean) (stat(path, &sb) == 0);
}

boolean
file_newer(const char *a_path, const char *b_path)
{
    struct stat a_sb, b_sb;

    if (stat(a_path, &a_sb))
        return FALSE;
    if (stat(b_path, &b_sb))
        return TRUE;
    return (boolean) (difftime(a_sb.st_mtime, b_sb.st_mtime) > 0);
}

/* -------------------------------------------------------------------
 * User identity and mode authorization
 */

char *
get_login_name(void)
{
    static char buf[BUFSZ];
    const char *s;

    s = nh_getenv("USER");
    if (!s || !*s)
        s = nh_getenv("LOGNAME");
    if (!s || !*s)
        s = nh_getenv("USERNAME"); /* Windows */
    buf[0] = '\0';
    if (s && *s)
        (void) strncpy(buf, s, sizeof buf - 1);
    return buf;
}

boolean
check_user_string(const char *optstr)
{
    int pwlen;
    const char *eop, *w;
    char *pwname = 0;

    if (optstr[0] == '*')
        return TRUE; /* allow any user */
    if (sysopt.check_plname)
        pwname = svp.plname;
    else
        pwname = get_login_name();
    if (!pwname || !*pwname)
        return FALSE;
    pwlen = (int) strlen(pwname);
    eop = eos((char *) optstr);
    w = optstr;
    while (w + pwlen <= eop) {
        if (!*w)
            break;
        if (isspace((uchar) *w)) {
            w++;
            continue;
        }
        if (!strncmpi(w, pwname, pwlen)) {
            if (!w[pwlen] || isspace((uchar) w[pwlen]))
                return TRUE;
        }
        while (*w && !isspace((uchar) *w))
            w++;
    }
    return FALSE;
}

boolean
authorize_wizard_mode(void)
{
    if (sysopt.wizards && sysopt.wizards[0]) {
        if (check_user_string(sysopt.wizards))
            return TRUE;
    }
    iflags.wiz_error_flag = TRUE; /* not being allowed into wizard mode */
    return FALSE;
}

boolean
authorize_explore_mode(void)
{
#ifdef SYSCF
    if (sysopt.explorers && sysopt.explorers[0]) {
        if (check_user_string(sysopt.explorers))
            return TRUE;
    }
    iflags.explore_error_flag = TRUE; /* not allowed into explore mode */
    return FALSE;
#else
    return TRUE;
#endif
}

/* -------------------------------------------------------------------
 * Game locking (adapted from sys/windows/windmain.c getlock)
 */

static int
eraseoldlocks(void)
{
    int i;

    /* cannot use maxledgerno() here: the lock name is needed before
       dungeon initialization sets astral_level */
    for (i = 1; i <= MAXDUNGEON * MAXLEVEL + 1; i++) {
        set_levelfile_name(gl.lock, i);
        (void) unlink(fqname(gl.lock, LEVELPREFIX, 0));
    }
    set_levelfile_name(gl.lock, 0);
    if (unlink(fqname(gl.lock, LEVELPREFIX, 0)))
        return 0; /* cannot remove it */
    return 1;     /* success! */
}

/* 1 = recover the old game, -1 = destroy it, 0 = don't start a new game */
static int
self_recover_prompt(void)
{
    int c, retval;
    int save_popupdialog = iflags.wc_popup_dialog;

    if (WINDOWPORT(curses))
        iflags.wc_popup_dialog = TRUE;
    c = y_n("There are files from a game in progress under your name. "
            "Recover?");
    if (c == 'y' || c == 'Y') {
        retval = 1;
    } else {
        c = y_n("Are you sure you wish to destroy the old game, "
                "rather than try to recover it?");
        retval = (c == 'y' || c == 'Y') ? -1 : 0;
    }
    if (WINDOWPORT(curses))
        iflags.wc_popup_dialog = save_popupdialog;
    return retval;
}

static int
do_getlock(void)
{
    int fd, ern = 0, prompt_result = 1;
    const char *fq_lock;
    char oops[BUFSZ];

    /* derive the lock basename from the character name */
#ifdef WIN32
    {
        char fnamebuf[BUFSZ], encodedfnamebuf[BUFSZ];

        Sprintf(fnamebuf, "%s", svp.plname);
        (void) fname_encode(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_-.", '%',
            fnamebuf, encodedfnamebuf, BUFSZ);
        Snprintf(gl.lock, sizeof gl.lock, "%s", encodedfnamebuf);
    }
#else
    Sprintf(gl.lock, "%u%s", (unsigned) getuid(), svp.plname);
    regularize(gl.lock);
#endif

    /* we ignore QUIT and INT at this point */
    if (!lock_file(HLOCK, LOCKPREFIX, 10)) {
        wait_synch();
        error("Quitting.");
    }

    set_levelfile_name(gl.lock, 0);
    fq_lock = fqname(gl.lock, LEVELPREFIX, 1);
    if ((fd = open(fq_lock, 0)) == -1) {
        if (errno == ENOENT)
            goto gotlock; /* no such file */
        unlock_file(HLOCK);
        error("Bad directory or name: %s\n%s\n", fq_lock, strerror(errno));
    }
    (void) nhclose(fd);

    /* there is an old game in progress */
    prompt_result = self_recover_prompt();
    if (prompt_result == 1) { /* recover */
        /* on failure fall through anyway and replace the stale lock:
           a new game beats exiting (matches windmain.c) */
        if (!recover_savefile())
            raw_print("Couldn't recover the old game; starting a new one.");
        goto gotlock;
    } else if (prompt_result < 0) { /* destroy old game */
        if (eraseoldlocks()) {
            goto gotlock;
        } else {
            unlock_file(HLOCK);
            raw_print("Couldn't destroy the old game.");
            return 0;
        }
    } else {
        unlock_file(HLOCK);
        return 0;
    }

 gotlock:
    fd = creat(fq_lock, FCMASK);
    if (fd == -1)
        ern = errno;
    unlock_file(HLOCK);
    if (fd == -1) {
        Sprintf(oops, "cannot creat lock file (%s.)\n%s\n", fq_lock,
                strerror(ern));
        raw_print(oops);
    } else {
        if (write(fd, (char *) &svh.hackpid, sizeof svh.hackpid)
            != sizeof svh.hackpid)
            error("cannot write lock (%s)", fq_lock);
        if (nhclose(fd) == -1)
            error("cannot close lock (%s)", fq_lock);
    }
    return prompt_result;
}

#ifdef WIN32
int
getlock(void)
{
    return do_getlock();
}
#else
void
getlock(void)
{
    if (do_getlock() == 0)
        nethack_exit(EXIT_SUCCESS);
}
#endif

/* -------------------------------------------------------------------
 * Signals (POSIX only; Windows builds define NO_SIGNAL)
 */

#ifndef WIN32
/* src/pager.c brackets its interrupt help around these (they live in
   sys/share/unixtty.c, which this port doesn't compile) */
void
intron(void)
{
}

void
introff(void)
{
}
#endif

#ifndef NO_SIGNAL
void
sethanguphandler(void (*handler)(int))
{
#ifdef SA_RESTART
    /* don't want reads to restart */
    struct sigaction sact;

    (void) memset((genericptr_t) &sact, 0, sizeof sact);
    sact.sa_handler = (SIG_RET_TYPE) handler;
    (void) sigaction(SIGHUP, &sact, (struct sigaction *) 0);
#ifdef SIGXCPU
    (void) sigaction(SIGXCPU, &sact, (struct sigaction *) 0);
#endif
#else
    (void) signal(SIGHUP, (SIG_RET_TYPE) handler);
#endif
}
#endif /* !NO_SIGNAL */

/* -------------------------------------------------------------------
 * Windows-only support routines
 */

#ifdef WIN32

ATTRNORETURN void
nethack_exit(int status)
{
    exit(status);
}

void
Delay(int ms)
{
    SDL_Delay((Uint32) ms);
}

void
win32_abort(void)
{
    abort();
}

void
nt_assert_failed(const char *expression, const char *filepath, int line)
{
    raw_printf("nhassert(%s) failed in '%s' at line %d", expression,
               filepath, line);
    abort();
}

void
interject_assistance(int num, int interjection_type, genericptr_t ptr1,
                     genericptr_t ptr2)
{
    if (num == 1 && interjection_type == INTERJECT_PANIC) {
        raw_printf("%s", (char *) ptr1);
        if (ptr2)
            raw_printf("(data directory: %s)", (char *) ptr2);
    }
}

void
interject(int interjection_type)
{
    (void) interjection_type;
}

char *
windows_exepath(void)
{
    return nh_basedir;
}

int GUILaunched = 0; /* read by src/mdlib.c's runtime options text */

/* "--windows:xxx" early options; none are supported by this port */
int
windows_early_options(const char *window_opt)
{
    raw_printf("-%s: no windows:xxx options are supported by sdl3.\n",
               window_opt);
    return 0;
}

/* win/curses grows its layout toward these via resize_term(); the SDL3
   backend clamps to the display, so these are the preferred window
   size in cells (kept equal to PDC_DEF_COLS/ROWS in pdcsdl3/pdcscrn.c
   so the startup resize is a no-op) */
int
get_approx_display_cols(void)
{
    return 140;
}

int
get_approx_display_rows(void)
{
    return 66;
}

const char *
get_portable_device(void)
{
    return "portable device";
}

/* expand %VARIABLE% references; result goes into dest (PATHLEN) */
char *
translate_path_variables(const char *str, char *dest)
{
    char varname[BUFSZ];
    const char *src = str;
    char *dp = dest, *vp;
    const char *val;
    size_t left = PATHLEN - 1;

    while (*src && left) {
        if (*src == '%') {
            const char *end = strchr(src + 1, '%');

            if (end && end > src + 1 && (size_t) (end - src) < sizeof varname) {
                vp = varname;
                for (src++; src < end; src++)
                    *vp++ = *src;
                *vp = '\0';
                src = end + 1;
                val = nh_getenv(varname);
                if (val) {
                    while (*val && left) {
                        *dp++ = *val++;
                        left--;
                    }
                }
                continue;
            }
        }
        *dp++ = *src++;
        left--;
    }
    *dp = '\0';
    return dest;
}

boolean
get_user_home_folder(char *buf, size_t buflen)
{
    const char *s = nh_getenv("USERPROFILE");

    if (!s || !*s || strlen(s) >= buflen)
        return FALSE;
    Strcpy(buf, s);
    return TRUE;
}

/* saved-game enumeration for get_saved_games() */
static intptr_t ff_handle = -1;
static struct _finddata_t ff_data;
static char ff_buffer[_MAX_PATH];

int
findfirst(char *path)
{
    if (ff_handle != -1) {
        (void) _findclose(ff_handle);
        ff_handle = -1;
    }
    ff_handle = _findfirst(path, &ff_data);
    if (ff_handle == -1)
        return 0;
    (void) strncpy(ff_buffer, ff_data.name, sizeof ff_buffer - 1);
    return 1;
}

int
findnext(void)
{
    if (ff_handle == -1 || _findnext(ff_handle, &ff_data) != 0) {
        if (ff_handle != -1) {
            (void) _findclose(ff_handle);
            ff_handle = -1;
        }
        return 0;
    }
    (void) strncpy(ff_buffer, ff_data.name, sizeof ff_buffer - 1);
    return 1;
}

char *
foundfile_buffer(void)
{
    return ff_buffer;
}

#endif /* WIN32 */

/* -------------------------------------------------------------------
 * Clipboard
 */

#ifdef RUNTIME_PASTEBUF_SUPPORT
void
port_insert_pastebuf(char *buf)
{
    if (buf)
        (void) SDL_SetClipboardText(buf);
}
#endif

/*sdl3sys.c*/
