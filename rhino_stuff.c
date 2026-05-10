/* rhino_stuff.c - Rhino plug-in I/O and OS-bridge for XLISP-PLUS
 *
 * This file replaces the upstream OS-specific stub files
 * (winstuff.c / win32stu.c / dosstuff.c / unixstuf.c). It routes
 * XLISP's character-level I/O through a small pair of in-memory
 * buffers managed by rhino_xlisp_driver.c, so the interpreter can
 * be driven from C++ inside a Rhino command without touching stdin
 * or stdout.
 *
 * Note: this file is compiled as C, not C++.
 */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <float.h>
#include <math.h>

#include "XLISP-PLUS/sources/xlisp.h"

/* --------------------------------------------------------------------- */
/* Externally-visible buffers shared with rhino_xlisp_driver.c.          */
/* The driver fills g_rhino_in_buf with the lisp expression text plus a  */
/* trailing newline, sets g_rhino_in_pos = 0, then calls into XLISP.     */
/* All output produced by xlprint / dbgputstr / etc. is appended to      */
/* g_rhino_out_buf (truncated at g_rhino_out_cap-1).                     */
/* --------------------------------------------------------------------- */

extern const char *g_rhino_in_buf;
extern int         g_rhino_in_len;
extern int         g_rhino_in_pos;
extern char       *g_rhino_out_buf;
extern int         g_rhino_out_cap;
extern int         g_rhino_out_len;

/* --------------------------------------------------------------------- */
/* Stack base used by STACKREPORT (xlisp.h, MSC + WIN32 path).           */
/* --------------------------------------------------------------------- */
char *stackbase;

/* --------------------------------------------------------------------- */
/* Helper - append a NUL-terminated string to the output buffer.         */
/* --------------------------------------------------------------------- */

static void rxl_append_out(const char *s)
{
  if (!s || !g_rhino_out_buf || g_rhino_out_cap <= 0)
    return;

  size_t slen = strlen(s);
  if (g_rhino_out_len >= g_rhino_out_cap - 1)
    return;

  size_t room = (size_t)g_rhino_out_cap - 1 - (size_t)g_rhino_out_len;
  if (room > slen)
    room = slen;
  memcpy(g_rhino_out_buf + g_rhino_out_len, s, room);
  g_rhino_out_len += (int)room;
  g_rhino_out_buf[g_rhino_out_len] = '\0';
}


void osinit(char *banner)
{
#ifdef STSZ
  stackbase = (char *)&banner;    /* approximate stack base */
#endif

  lposition = 0;

  /* Allow FP overflow / domain through silently (mirror win32stu.c). */
#ifdef _MSC_VER
  _control87(_EM_OVERFLOW | _EM_INVALID, _EM_OVERFLOW | _EM_INVALID);
#endif
}

// nothing to do for an embedded interpreter
void osfinish() {}

void xoserror(char* msg)
{
  if (msg)
  {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "error: %s\n", msg);
    rxl_append_out(tmp);
  }
}

// osrand - per the algorithm used by every other XLISP-PLUS port
long osrand(long rseed)
{
  if (rseed == 0L) rseed = 1L;

  long k1 = rseed / 127773L;
  if ((rseed = 16807L * (rseed - k1 * 127773L) - k1 * 2836L) < 0L)
    rseed += 2147483647L;

  return rseed;
}

// Terminal I/O - read from g_rhino_in_buf, write to g_rhino_out_buf
int ostgetc(void)
{
  if (!g_rhino_in_buf) return EOF;
  if (g_rhino_in_pos >= g_rhino_in_len) return EOF;
  return (unsigned char)g_rhino_in_buf[g_rhino_in_pos++];
}

void ostputc(int ch)
{
  if (ch == '\n')
    lposition = 0;
  else if (ch == '\b')
  {
    if (lposition > 0)
      --lposition;
  }
  else if (ch >= 0x20 && ch < 0x7f)
    lposition++;

  if (g_rhino_out_buf && g_rhino_out_len < g_rhino_out_cap - 1)
  {
    g_rhino_out_buf[g_rhino_out_len++] = (char)ch;
    g_rhino_out_buf[g_rhino_out_len]   = '\0';
  }

  if (tfp != CLOSED)
    OSPUTC(ch, tfp);
}

// No buffered input to discard for the embedded driver
void osflush(void) {}

// oscheck - called periodically to give the host a chance to interrupt.
// We have no async signal source here, so this is a no-op.
void oscheck(void) {}

// ossymbols - port-specific symbol setup. We have nothing extra to add.
void ossymbols(void) {}

// flushbuf - referenced by xtime when TIMES is enabled.
void flushbuf(void) {}


/* --------------------------------------------------------------------- */
/* File-table support (FILETABLE is auto-defined for MSC + WIN32).       */
/* --------------------------------------------------------------------- */
#ifdef FILETABLE

extern void gc(void);

int truename(char *name, char *rname)
{
  // _fullpath is the MSVC CRT equivalent of realpath/GetFullPathName,
  // and it doesn't drag in <windows.h>.
#ifdef _MSC_VER
  if (_fullpath(rname, name, FNAMEMAX) == NULL)
  {
    if (strlen(name) >= FNAMEMAX)
      return FALSE;
    strcpy(rname, name);
  }
#else
  if (strlen(name) >= FNAMEMAX)
    return FALSE;
  strcpy(rname, name);
#endif
  // normalize to lowercase like win32stu.c does
  {
    unsigned char *cp = (unsigned char *)rname;
    for (; *cp; ++cp)
      if (*cp >= 'A' && *cp <= 'Z')
        *cp = (unsigned char)(*cp + ('a' - 'A'));
  }
  return TRUE;
}

int getslot(void)
{
  for (int i = 0; i < FTABSIZE; i++)
  {
    if (filetab[i].fp == NULL)
      return i;
  }

  gc();

  for (int i = 0; i < FTABSIZE; i++)
  {
    if (filetab[i].fp == NULL)
      return i;
  }

  xlfail("too many open files");
  return 0;
}

FILEP osaopen(const char *name, const char *mode)
{
  int   i = getslot();
  char  namebuf[FNAMEMAX + 1];
  FILE *fp;

  if (!truename((char *)name, namebuf))
    strcpy(namebuf, name);

  if ((filetab[i].tname = (char *)malloc(strlen(namebuf) + 1)) == NULL)
    xlfail("insufficient memory");

  if ((fp = fopen(name, mode)) == NULL)
  {
    free(filetab[i].tname);
    filetab[i].tname = NULL;
    return CLOSED;
  }

  filetab[i].fp = fp;
  strcpy(filetab[i].tname, namebuf);

  // mode used to reopen across save/restore (win32 path)
  if (mode[0] == 'w')
  {
    strcpy(filetab[i].reopenmode, "r+");
    if (mode[strlen(mode) - 1] == 'b')
      strcat(filetab[i].reopenmode, "b");
  }
  else
  {
    strncpy(filetab[i].reopenmode, mode, 3);
    filetab[i].reopenmode[3] = '\0';
  }

  return i;
}

FILEP osbopen(const char *name, const char *mode)
{
  char bmode[8];
  strcpy(bmode, mode);
  strcat(bmode, "b");
  return osaopen(name, bmode);
}

void osclose(FILEP f)
{
  if (filetab[f].fp != NULL)
    fclose(filetab[f].fp);
  if (f > 2 && filetab[f].tname != NULL)
    free(filetab[f].tname);
  filetab[f].tname = NULL;
  filetab[f].fp    = NULL;
}

#ifdef PATHNAMES
// ospopen - search the PATHNAMES env-var (defaults to "XLPATH" in xlisp.h).
FILEP ospopen(char *name, int ascii)
{
  FILEP fp;
  char *path = getenv(PATHNAMES);
  char *newnamep;
  char  ch;
  char  newname[512];

  // explicit path: open as-is
  if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL || path == NULL)
    return (ascii ? osaopen : osbopen)(name, "r");

  do
  {
    if (*path == '\0') /* no more entries to try, fall back to cwd */
      return (ascii ? osaopen : osbopen)(name, "r");

    newnamep = newname;
    while ((ch = *path++) != '\0' && ch != ';')
      *newnamep++ = ch;

    if (ch == '\0') --path;

    if (newnamep != newname && *(newnamep - 1) != '/' && *(newnamep - 1) != '\\')
      *newnamep++ = '\\';

    *newnamep = '\0';

    strcat(newname, name);
    fp = (ascii ? osaopen : osbopen)(newname, "r");
  }
  while (fp == CLOSED);

  return fp;
}
#endif // PATHNAMES

int renamebackup(char *filename)
{
  char *bufp;
  char  ch = 0;

  strcpy(buf, filename);
  bufp = &buf[strlen(buf)];
  while (bufp > buf && (ch = *--bufp) != '.' && ch != '/' && ch != '\\')
    ;

  if (ch == '.') strcpy(bufp, ".bak");
  else strcat(buf, ".bak");

  remove(buf);
  return !rename(filename, buf);
}

#endif // FILETABLE

/* --------------------------------------------------------------------- */
/* xsystem and xgetkey - referenced unconditionally by osptrs.h.         */
/* xgetkey is included for non-Unix builds.                              */
/* --------------------------------------------------------------------- */

LVAL xsystem(void)
{
  /* Refuse rather than silently disabling, per Rhino plug-in policy. */
  xllastarg();
  xlfail("system call not available in embedded XLISP");
  return NIL;
}

LVAL xgetkey(void)
{
  xllastarg();
  /* No interactive console here - just return NIL. */
  return NIL;
}

/* --------------------------------------------------------------------- */
/* TIMES support - simple wall-clock based.                              */
/* --------------------------------------------------------------------- */

#ifdef TIMES

#define OURTICKS 1000

unsigned long ticks_per_second(void) { return (unsigned long)OURTICKS; }

unsigned long real_tick_count(void)
{
  return (unsigned long)((OURTICKS / (double)CLOCKS_PER_SEC) * (double)clock());
}

unsigned long run_tick_count(void)
{
  return real_tick_count();
}

LVAL xtime(void)
{
  LVAL expr = xlgetarg();
  xllastarg();

  unsigned long tm = run_tick_count();
  LVAL result = xleval(expr);
  tm = run_tick_count() - tm;

  sprintf(buf, "The evaluation took %.2f seconds.\n",
            ((double)tm) / (double)ticks_per_second());
  trcputstr(buf);
  return result;
}

LVAL xruntime(void)
{
  xllastarg();
  return cvfixnum((FIXTYPE)run_tick_count());
}

LVAL xrealtime(void)
{
  xllastarg();
  return cvfixnum((FIXTYPE)real_tick_count());
}

#endif /* TIMES */

/* --------------------------------------------------------------------- */
/* GRAPHICS stubs.                                                       */
/* xlisp.h auto-defines GRAPHICS for our platform, and osptrs.h then     */
/* expects xmode/xcolor/xcls/etc. to exist. The native console-graphics  */
/* implementation lives in winstuff.c / win32stu.c which we don't        */
/* include - so provide harmless stubs that all return NIL.              */
/* --------------------------------------------------------------------- */

#ifdef GRAPHICS
LVAL xmode(void)    { while (moreargs()) (void)xlgetarg(); return NIL; }
LVAL xcolor(void)   { while (moreargs()) (void)xlgetarg(); return NIL; }
LVAL xmove(void)    { while (moreargs()) (void)xlgetarg(); return NIL; }
LVAL xdraw(void)    { while (moreargs()) (void)xlgetarg(); return NIL; }
LVAL xmoverel(void) { while (moreargs()) (void)xlgetarg(); return NIL; }
LVAL xdrawrel(void) { while (moreargs()) (void)xlgetarg(); return NIL; }
LVAL xcls(void)     { xllastarg(); return NIL; }
LVAL xcleol(void)   { xllastarg(); return NIL; }
LVAL xgotoxy(void)  { while (moreargs()) (void)xlgetarg(); return NIL; }
#endif
