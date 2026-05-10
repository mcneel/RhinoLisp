// C entry points for embedding XLISP-PLUS
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "XLISP-PLUS/sources/xlisp.h"
#include "rhino_subrs.h"


const char *g_rhino_in_buf  = NULL;
int         g_rhino_in_len  = 0;
int         g_rhino_in_pos  = 0;

char *g_rhino_out_buf = NULL;
int   g_rhino_out_cap = 0;
int   g_rhino_out_len = 0;

jmp_buf exit_xlisp;

VOID xlrdsave(LVAL expr)
{
    setvalue(s_3plus, getvalue(s_2plus));
    setvalue(s_2plus, getvalue(s_1plus));
    setvalue(s_1plus, getvalue(s_minus));
    setvalue(s_minus, expr);
}

VOID xlevsave(LVAL expr)
{
    setvalue(s_3star, getvalue(s_2star));
    setvalue(s_2star, getvalue(s_1star));
    setvalue(s_1star, expr);
}

VOID xlfatal(char *msg)
{
    xoserror(msg);
    if (tfp != CLOSED) OSCLOSE(tfp);
    osfinish();
    longjmp(exit_xlisp, 1);
}

VOID wrapup(void)
{
    if (tfp != CLOSED) OSCLOSE(tfp);
    osfinish();
    longjmp(exit_xlisp, 1);
}

LVAL xtoplevelloop(void)
{
    xllastarg();
    return NIL;
}

LVAL xresetsystem(void)
{
    xllastarg();
    xlflush();
    return NIL;
}


static CONTXT rxl_cntxt;
static int    rxl_silent = 0;   /* when nonzero, skip per-form result printing */

static int InitializeXlipPlus()
{
  static int initialized = -1;
  if (initialized >= 0)
    return initialized;

  if (setjmp(exit_xlisp) != 0)
  {
    initialized = 0;
    return initialized;
  }

#ifdef FILETABLE
  filetab[0].fp = stdin;
  filetab[0].tname = "(stdin)";
  filetab[1].fp = stdout;
  filetab[1].tname = "(stdout)";
  filetab[2].fp = stderr;
  filetab[2].tname = "(console)";
  for (int i = 3; i < FTABSIZE; i++)
  {
    filetab[i].fp = NULL;
    filetab[i].tname = NULL;
  }
#endif

  char banner[128];
  sprintf(banner, "XLISP-PLUS version %d.%02d\n", MAJOR_VERSION, MINOR_VERSION);
  osinit(banner);

  xlbegin(&rxl_cntxt, CF_TOPLEVEL | CF_CLEANUP | CF_BRKLEVEL, (LVAL)1);
  if (setjmp(rxl_cntxt.c_jmpbuf))
  {
    xoserror("fatal initialization error");
    initialized = 0;
    return initialized;
  }
#ifdef SAVERESTORE
  if (setjmp(top_level))
  {
    xoserror("RESTORE not allowed during initialization");
    initialized = 0;
    return initialized;
  }
#endif

  xlinit("");

  // Register our custom functions that are to be made available
  // in lisp scripts
  RegisterCustomLispFunctions();

  xlend(&rxl_cntxt);
  xlbegin(&rxl_cntxt, CF_TOPLEVEL | CF_CLEANUP | CF_BRKLEVEL, s_true);

  initialized = 1;
  return initialized;
}

static int RhinoXlispPlusEvalHelper(const char *src, char *outbuf, int cap)
{
  int initialized = InitializeXlipPlus();
  if (initialized != 1)
  {
    if (outbuf && cap > 0) outbuf[0] = '\0';
    return -1;
  }

    LVAL expr;
    int  status = 0;

    g_rhino_in_buf  = src ? src : "";
    g_rhino_in_len  = src ? (int)strlen(src) : 0;
    g_rhino_in_pos  = 0;

    g_rhino_out_buf = outbuf;
    g_rhino_out_cap = cap;
    g_rhino_out_len = 0;
    if (outbuf && cap > 0) outbuf[0] = '\0';

    if (setjmp(rxl_cntxt.c_jmpbuf)) {
        setvalue(s_evalhook, NIL);
        setvalue(s_applyhook, NIL);
        xltrcindent = 0;
        xldebug    = 0;
        xlflush();
        status = 1;
        goto done;
    }

    if (setjmp(exit_xlisp) != 0) {
        status = -1;
        goto done;
    }

#ifdef STSZ
    stackwarn = FALSE;
#endif

    while (g_rhino_in_pos < g_rhino_in_len) {
        if (!xlread(getvalue(s_stdin), &expr, FALSE, FALSE))
            break;
        xlrdsave(expr);
        expr = xleval(expr);
        xlevsave(expr);
        if (!rxl_silent) {
            xlfreshline(getvalue(s_stdout));
            stdprint(expr);
        }
    }

done:
    g_rhino_in_buf  = NULL;
    g_rhino_in_len  = 0;
    g_rhino_in_pos  = 0;
    g_rhino_out_buf = NULL;
    g_rhino_out_cap = 0;
    g_rhino_out_len = 0;
    return status;
}

/* --------------------------------------------------------------------- */
/* Additional globals normally provided by xlisp.c / win32stu.c.         */
/* --------------------------------------------------------------------- */

/* SAVERESTORE longjmp target. xldmem.c branches to top_level via
   longjmp() if a workspace restore succeeds. Since we never call
   xlirestore() from the embedded driver, we just have to provide
   storage for the symbol. */
jmp_buf top_level;

/* myldexp - referenced by BIGNUMS code via the LDEXP macro (xlisp.h
   defines LDEXP=myldexp on MSC + WIN32 so the runtime can substitute
   a saturating implementation; the stock ldexp on old Borland would
   misbehave for out-of-range exponents). On modern MSVC ldexp is
   correct, so we just delegate. */
#include <math.h>
#include <float.h>
double myldexp(double val, int exp_)
{
    if (exp_ > DBL_MAX_EXP) return val > 0 ? HUGE_VAL : -HUGE_VAL;
    if (exp_ < DBL_MIN_EXP) return 0.0;
    return ldexp(val, exp_);
}

/* rhino_xlisp_load - same as rhino_xlisp_eval, but suppresses the
   per-form printing of return values. Use this for loading a file. */
int RhinoXlispPlusEval(unsigned int docId, const char* src, char* outbuf, int cap)
{
  rxl_silent = 1;
  SetRunningScriptDocument(docId);
  int rc = RhinoXlispPlusEvalHelper(src, outbuf, cap);
  SetRunningScriptDocument(0);
  rxl_silent = 0;
  return rc;
}
