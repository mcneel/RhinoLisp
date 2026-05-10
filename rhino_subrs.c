/* rhino_subrs.c - Custom XLISP SUBRs registered at interpreter startup.
 *
 * Each SUBR is a plain C function `LVAL fn(void)`. It gets its arguments
 * off XLISP's argument stack with xlga<type>(), extracts C values with
 * get<type>(...), and returns an LVAL constructed with cv<type>(...).
 *
 * Calls into the Rhino SDK go through extern "C" wrappers in
 * rhino_sdk_glue.cpp - that file is the only one that pulls in the C++
 * Rhino headers, keeping the lisp glue layer pure C.
 *
 * To add a new function:
 *   1. Write `LVAL xmything(void)` below.
 *   2. Add one xlsubr() call inside rhino_xlisp_register_subrs().
 *   3. Done. (Recompile.)
 */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <math.h>
#include <stdio.h>
#include "XLISP-PLUS/sources/xlisp.h"
#include "rhino_subrs.h"


static double ArgAsDouble()
{
  LVAL a = xlgetarg();
  if (fixp(a))
    return (double)getfixnum(a);
  if (floatp(a))
    return getflonum(a);
  xlerror("expected a number", a);
  return 0.0; // never reached
}

// Pull (x y z) out of a 3-element list of numbers; 1 on success
static int ParsePointList(LVAL lst, double* x, double* y, double* z)
{
  if (NULL == x || NULL == y || NULL == z)
    return FALSE;

  if (!consp(lst))
    return FALSE;

  LVAL e = car(lst);
  if (fixp(e))        *x = (double)getfixnum(e);
  else if (floatp(e)) *x = (double)getflonum(e);
  else return FALSE;

  lst = cdr(lst);
  if (!consp(lst)) return FALSE;
  e = car(lst);
  if (fixp(e))        *y = (double)getfixnum(e);
  else if (floatp(e)) *y = (double)getflonum(e);
  else return FALSE;

  lst = cdr(lst);
  if (!consp(lst)) return FALSE;
  e = car(lst);
  if (fixp(e))        *z = (double)getfixnum(e);
  else if (floatp(e)) *z = (double)getflonum(e);
  else return FALSE;

  return TRUE;
}

// Build a fresh (x y z) flonum list. xlsave1 protects across the
// cvflonum/cons calls, which may trigger GC.
static LVAL MakePointList(double x, double y, double z)
{
  LVAL r;
  xlsave1(r);
  r = cons(cvflonum((FLOTYPE)z), NIL);
  r = cons(cvflonum((FLOTYPE)y), r);
  r = cons(cvflonum((FLOTYPE)x), r);
  xlpop();
  return r;
}

// Parse the optional [point] [msg] arguments, in either order.
static void ParseOptionalPointAndMessage(int* hasPoint,
  double* x, double* y, double* z,
  const char** prompt,
  char* prompt_buf, int prompt_buf_cap)
{
  *hasPoint = FALSE;
  *prompt = NULL;
  if (prompt_buf_cap > 0)
    prompt_buf[0] = '\0';

  while (moreargs())
  {
    LVAL a = xlgetarg();
    if (consp(a))
    {
      if (!ParsePointList(a, x, y, z))
        xlerror("base point must be (x y z)", a);
      *hasPoint = 1;
    }
    else if (stringp(a))
    {
      const char* s = (const char*)getstring(a);
      int n;
      for (n = 0; n < prompt_buf_cap - 1 && s[n]; ++n)
        prompt_buf[n] = s[n];
      prompt_buf[n] = '\0';
      *prompt = prompt_buf;
    }
    else
    {
      xlerror("expected point or string", a);
    }
  }
}

// (alert msg)
LVAL subALERT()
{
  LVAL s = xlgastring();
  xllastarg();
  helperALERT((const char*)getstring(s));
  return NIL;
}

/* --------------------------------------------------------------------- */
/* AutoLISP-tolerant = .                                                 */
/*                                                                       */
/* XLISP's built-in = is strictly numeric; (= n nil) raises "bad         */
/* argument type". AutoLISP scripts often spell defaults this way:       */
/*   (if (= x nil) (setq x DEFAULT))                                     */
/* So we override = so that:                                             */
/*   - All-numeric args: numeric equality (XLISP semantics).             */
/*   - Otherwise: eql semantics; mismatched types just return NIL.       */
/* --------------------------------------------------------------------- */
LVAL subEQUAL(void)
{
  if (!moreargs())
    return s_true;

  LVAL prev = xlgetarg();
  while (moreargs())
  {
    LVAL next = xlgetarg();
    if ((fixp(prev) || floatp(prev)) && (fixp(next) || floatp(next)))
    {
      double a = fixp(prev) ? (double)getfixnum(prev) : (double)getflonum(prev);
      double b = fixp(next) ? (double)getfixnum(next) : (double)getflonum(next);
      if (a != b)
      {
        /* drain remaining args so the call doesn't trip xllastarg */
        while (moreargs()) (void)xlgetarg();
        return NIL;
      }
    }
    else
    {
      if (!eql(prev, next))
      {
        while (moreargs()) (void)xlgetarg();
        return NIL;
      }
    }
    prev = next;
  }
  return s_true;
}


// (getdist [pt] [msg])
LVAL subGETDIST(void)
{
  int    has_base = 0;
  double bx = 0, by = 0, bz = 0;
  const char* prompt = NULL;
  char   prompt_buf[256];

  ParseOptionalPointAndMessage(&has_base, &bx, &by, &bz,
    &prompt, prompt_buf, (int)sizeof(prompt_buf));

  double distance = 0;
  if (!helperGETDIST(prompt, has_base, bx, by, bz, &distance))
    return NIL;

  return cvflonum((FLOTYPE)distance);
}

LVAL subGETPOINT(void)
{
  int    has_base = 0;
  double bx = 0, by = 0, bz = 0;
  const char* prompt = NULL;
  char   prompt_buf[256];
  double rx, ry, rz;

  ParseOptionalPointAndMessage(&has_base, &bx, &by, &bz,
    &prompt, prompt_buf, (int)sizeof(prompt_buf));

  if (!helperGETPOINT(prompt, has_base, bx, by, bz, &rx, &ry, &rz))
    return NIL;

  return MakePointList(rx, ry, rz);
}

// ---------------------------------------------------------------------
// (getreal [msg]) -> real-or-NIL
//   msg : optional prompt string.
// AutoLISP returns NIL when the user cancels with ESC. We mirror that
// rather than aborting the script with an XLISP error.
// ---------------------------------------------------------------------
LVAL subGETREAL(void)
{
  const char* prompt = NULL;
  char prompt_buf[256];
  prompt_buf[0] = '\0';

  while (moreargs())
  {
    LVAL a = xlgetarg();
    if (stringp(a))
    {
      const char* s = (const char*)getstring(a);
      int n;
      for (n = 0; n < (int)sizeof(prompt_buf) - 1 && s[n]; ++n)
        prompt_buf[n] = s[n];
      prompt_buf[n] = '\0';
      prompt = prompt_buf;
    }
    /* anything else: silently ignore (matches GETSTRING tolerance) */
  }

  double value = 0.0;
  if (!helperGETREAL(prompt, &value))
    return NIL;
  return cvflonum((FLOTYPE)value);
}

// ---------------------------------------------------------------------
// (strcat [s1 s2 ...]) -> string
//
// XLISP's CONCATENATE works on sequences in general but requires a
// type tag as its first argument:  (concatenate 'string a b). AutoLISP
// scripts spell it more concisely as (strcat a b ...), so we provide
// a thin SUBR with that name.
// ---------------------------------------------------------------------
LVAL subSTRCAT(void)
{
  if (!moreargs())
    return cvstring("");

  // We need to walk the argument list twice (size, then copy). xlargv
  // and xlargc are global lexer-style cursors; save/restore them so
  // the second pass starts at the first arg again.
  LVAL FAR *saveargv = xlargv;
  int       saveargc = xlargc;

  // First pass: validate types and accumulate total length.
  unsigned total = 0;
  while (moreargs())
  {
    LVAL a = xlgetarg();
    if (!stringp(a))
      xlerror("strcat: expected string", a);
    total += getslength(a);
  }

  xlargv = saveargv;
  xlargc = saveargc;

  // Allocate first - newstring may trigger GC. After this point we
  // make no more allocations, so writing into the buffer is safe.
  LVAL val = newstring(total);
  char FAR *dst = (char FAR*)getstring(val);

  while (moreargs())
  {
    LVAL a = xlgetarg();
    unsigned len = getslength(a);
    if (len) MEMCPY(dst, getstring(a), len);
    dst += len;
  }
  *dst = '\0';
  return val;
}

// ---------------------------------------------------------------------
// (rtos number [mode [precision]]) -> string
//   mode      : 1=scientific, 2=decimal (default), 3=engineering,
//               4=architectural, 5=fractional. Modes 3-5 are AutoCAD
//               imperial conventions; we fall back to decimal for them.
//   precision : digits after the decimal point. Default 4.
// ---------------------------------------------------------------------
LVAL subRTOS(void)
{
  double n         = ArgAsDouble();
  int    mode      = 2;
  int    precision = 4;

  if (moreargs())
  {
    LVAL m = xlgetarg();
    if (fixp(m))        mode = (int)getfixnum(m);
    else if (floatp(m)) mode = (int)getflonum(m);
    else xlerror("rtos: mode must be a number", m);
  }
  if (moreargs())
  {
    LVAL p = xlgetarg();
    if (fixp(p))        precision = (int)getfixnum(p);
    else if (floatp(p)) precision = (int)getflonum(p);
    else xlerror("rtos: precision must be a number", p);
  }
  xllastarg();

  if (precision < 0)  precision = 0;
  if (precision > 16) precision = 16;

  char fmt[16];
  char buf[64];

  if (mode == 1)
  {
    /* Scientific: e.g. 1.2345E+02 */
    snprintf(fmt, sizeof(fmt), "%%.%dE", precision);
  }
  else
  {
    /* Decimal (and fall-through for unsupported imperial modes 3-5). */
    snprintf(fmt, sizeof(fmt), "%%.%df", precision);
  }
  snprintf(buf, sizeof(buf), fmt, n);
  return cvstring(buf);
}

LVAL subPOLAR(void)
{
  LVAL pt_lv;
  double px, py, pz;
  double ang, dist;

  pt_lv = xlgetarg();
  if (!consp(pt_lv) || !ParsePointList(pt_lv, &px, &py, &pz))
    xlerror("first arg to polar must be (x y z)", pt_lv);

  ang = ArgAsDouble();
  dist = ArgAsDouble();
  xllastarg();

  return MakePointList(px + dist * cos(ang),
    py + dist * sin(ang),
    pz);
}

LVAL subPOW(void)
{
  double base = ArgAsDouble();
  double exponent = ArgAsDouble();
  xllastarg();
  return cvflonum((FLOTYPE)pow(base, exponent));
}

/* --------------------------------------------------------------------- */
/* (getvar "NAME") / (setvar "NAME" value)                               */
/* For now we only honor "CLAYER" (the active doc's current layer name). */
/* Anything else fails so it surfaces during script porting.             */
/* --------------------------------------------------------------------- */

static int EqualInsensitive(const char* a, const char* b)
{
  while (*a && *b)
  {
    char ca = *a, cb = *b;
    if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
    if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);

    if (ca != cb)
      return 0;
    ++a; ++b;
  }
  return *a == 0 && *b == 0;
}

LVAL subGETVAR()
{
  LVAL nm = xlgastring();
  xllastarg();
  {
    const char* name = (const char*)getstring(nm);
    if (EqualInsensitive(name, "CLAYER")) {
      char buf[256];
      if (rhino_glue_getvar_clayer(buf, (int)sizeof(buf)))
        return cvstring(buf);
      return NIL;
    }
    xlerror("getvar: unsupported system variable", nm);
    return NIL;
  }
}

LVAL subSETVAR()
{
  LVAL nm = xlgastring();
  LVAL val = xlgetarg();
  xllastarg();
  {
    const char* name = (const char*)getstring(nm);
    if (EqualInsensitive(name, "CLAYER")) {
      if (!stringp(val))
        xlerror("setvar CLAYER: value must be a string", val);
      (void)rhino_glue_setvar_clayer((const char*)getstring(val));
      return val;
    }
    xlerror("setvar: unsupported system variable", nm);
    return NIL;
  }
}

/* --------------------------------------------------------------------- */
/* (command ...) - minimal AutoLISP emulator (LAYER and LINE only).      */
/*                                                                       */
/* This is an FSUBR: arguments arrive UNEVALUATED so we can let the      */
/* user's keyboard-style strings come through as bare symbols. We        */
/* explicitly evaluate each arg before consuming it.                     */
/*                                                                       */
/*   (command "LAYER" "M" name "C" color "" "")                          */
/*       Make and switch to layer `name`; set its color to `color`.     */
/*       Trailing empty strings are AutoCAD's way of accepting the       */
/*       LAYER prompt. We just stop reading once we see them.            */
/*                                                                       */
/*   (command "LINE" pt pt pt ... ["c"|""])                              */
/*       Draw connected line segments through the points. "c" closes    */
/*       the figure; empty string just terminates input.                 */
/* --------------------------------------------------------------------- */

#define MAX_LINE_POINTS 256

static int eval_string_arg(LVAL form, char* buf, int buf_cap)
{
  LVAL v = xleval(form);
  if (!stringp(v)) return 0;
  {
    const char* s = (const char*)getstring(v);
    int n;
    for (n = 0; n < buf_cap - 1 && s[n]; ++n) buf[n] = s[n];
    buf[n] = '\0';
  }
  return 1;
}

LVAL fsubCOMMAND(void)
{
  /* xlargv/xlargc point at the unevaluated arg list for an FSUBR. */
  char first[64];

  if (xlargc < 1)
  {
    xllastarg();
    return NIL;
  }

  if (!eval_string_arg(xlargv[0], first, (int)sizeof(first)))
    xlerror("command: first argument must evaluate to a string",
      xlargv[0]);

  /* ---- LAYER ----------------------------------------------------- */
  if (EqualInsensitive(first, "LAYER"))
  {
    char layer_name[256] = "";
    char color[64] = "";
    char tok[256];

    for (int i = 1; i < xlargc; ++i)
    {
      if (!eval_string_arg(xlargv[i], tok, (int)sizeof(tok))) {
        xlerror("command LAYER: expected string arg", xlargv[i]);
      }
      if (tok[0] == '\0') continue;            /* empty = "Enter" */
      if (EqualInsensitive(tok, "M") || EqualInsensitive(tok, "MAKE")) {
        if (++i >= xlargc) break;
        eval_string_arg(xlargv[i], layer_name, (int)sizeof(layer_name));
      }
      else if (EqualInsensitive(tok, "C") || EqualInsensitive(tok, "COLOR")) {
        if (++i >= xlargc) break;
        eval_string_arg(xlargv[i], color, (int)sizeof(color));
        /* AutoCAD then prompts for which layers; eat the next
           non-empty token (the layer name to apply color to).  */
        if (i + 1 < xlargc)
        {
          char tgt[256];
          if (eval_string_arg(xlargv[i + 1], tgt, (int)sizeof(tgt)) && tgt[0] != '\0')
          {
            i++;
            if (layer_name[0] == '\0')
            {
              int k;
              for (k = 0; k < (int)sizeof(layer_name) - 1 && tgt[k]; ++k)
                layer_name[k] = tgt[k];
              layer_name[k] = '\0';
            }
          }
        }
      }
      else if (EqualInsensitive(tok, "S") || EqualInsensitive(tok, "SET"))
      {
        if (++i >= xlargc)
          break;
        eval_string_arg(xlargv[i], layer_name, (int)sizeof(layer_name));
      }
      else if (EqualInsensitive(tok, "ON") || EqualInsensitive(tok, "OFF") ||
        EqualInsensitive(tok, "F") || EqualInsensitive(tok, "T") ||
        EqualInsensitive(tok, "L") || EqualInsensitive(tok, "U") ||
        EqualInsensitive(tok, "P") || EqualInsensitive(tok, "?"))
      {
        /* Subcommands we don't implement - skip the next arg. */
        if (++i >= xlargc)
          break;
      }
      /* anything else: ignore (AutoCAD would re-prompt)         */
    }

    if (layer_name[0] == '\0')
      return NIL;

    if (!rhino_glue_make_layer(layer_name, color))
      return NIL;
    if (!rhino_glue_set_current_layer(layer_name))
      return NIL;
    return s_true;
  }

  /* ---- LINE ------------------------------------------------------ */
  if (EqualInsensitive(first, "LINE"))
  {
    double pts[MAX_LINE_POINTS][3];
    int    npts = 0;
    int    closed = 0;

    for (int i = 1; i < xlargc; ++i)
    {
      LVAL v = xleval(xlargv[i]);

      if (consp(v))
      {
        double x, y, z;
        if (npts >= MAX_LINE_POINTS)
          xlfail("command LINE: too many points");
        if (!ParsePointList(v, &x, &y, &z))
          xlerror("command LINE: malformed point", v);
        pts[npts][0] = x; pts[npts][1] = y; pts[npts][2] = z;
        ++npts;
      }
      else if (stringp(v))
      {
        const char* s = (const char*)getstring(v);
        if (s[0] == '\0') {
          /* "Enter" - end the LINE command */
          break;
        }
        if (EqualInsensitive(s, "C") || EqualInsensitive(s, "CLOSE")) {
          closed = 1;
          break;
        }
        /* unknown subcommand: ignore */
      }
      else
      {
        xlerror("command LINE: expected point or string", v);
      }
    }

    if (npts < 2)
      return NIL;

    for (int i = 0; i + 1 < npts; ++i)
    {
      rhino_glue_add_line(pts[i][0], pts[i][1], pts[i][2], pts[i + 1][0], pts[i + 1][1], pts[i + 1][2]);
    }

    if (closed && npts >= 3)
    {
      rhino_glue_add_line(pts[npts - 1][0], pts[npts - 1][1], pts[npts - 1][2], pts[0][0], pts[0][1], pts[0][2]);
    }
    return s_true;
  }

  // Just pass along as a scripted command and hope it works
  {
    char tok[256];
    char args[512];
    args[0] = 0;
    int argsIndex = 0;
    for (int i = 1; i < xlargc; i++)
    {
      if (!eval_string_arg(xlargv[i], tok, (int)sizeof(tok)) || tok[0] == '\0')
      {
        continue;
      }
      for (int j = 0; j < (int)sizeof(tok) && argsIndex < ((int)sizeof(args)-1); j++)
      {
        if (tok[j] == 0 || tok[j] == '\0')
        {
          break;
        }
        args[argsIndex++] = tok[j];
      }
      if (argsIndex < ((int)sizeof(args) - 1))
        args[argsIndex++] = ' ';
      if (argsIndex < ((int)sizeof(args) - 1))
        args[argsIndex] = 0;
    }
    RhinoAppRunScript(first, args);
  }
  return s_true;
}

// ---------------------------------------------------------------------
// AutoLISP-tolerant PRINC.
//
// AutoLISP scripts often end with a bare (princ) to suppress the
// trailing return value. XLISP's stock princ requires at least one
// argument, which surfaces as "error: too few arguments". We override
// it with a wrapper that returns NIL on zero args and otherwise calls
// xlprint (no escaping, no terpri) - same semantics as XLISP's princ.
// ---------------------------------------------------------------------
LVAL subPRINC(void)
{
  if (!moreargs())
    return NIL;

  LVAL val  = xlgetarg();
  LVAL fptr = (moreargs() ? xlgetfile(TRUE) : getvalue(s_stdout));
  xllastarg();

  xlprint(fptr, val, FALSE);
  return val;
}

// ---------------------------------------------------------------------
// AutoLISP-tolerant DEFUN.
//
// AutoLISP allows local-variable declarations in the parameter list
// using a / separator:
//
//   (defun NAME (param1 param2 / local1 local2) BODY...)
//
// XLISP's built-in defun would treat / and the locals as additional
// parameters. We override DEFUN as an FSUBR: split the lambda list at
// /, and if locals are present wrap BODY in (let (locals) BODY...).
// ---------------------------------------------------------------------
LVAL fsubDEFUN(void)
{
  if (xlargc < 2) xlfail("DEFUN: too few arguments");

  LVAL name = xlargv[0];
  if (!symbolp(name)) xlerror("DEFUN: name must be a symbol", name);

  LVAL lam_list = xlargv[1];

  LVAL slash_sym    = xlenter("/");
  LVAL let_sym      = xlenter("LET");
  LVAL lambda_sym   = xlenter("LAMBDA");
  LVAL function_sym = xlenter("FUNCTION");

  LVAL params_first = NIL, params_last = NIL;
  LVAL locals_first = NIL, locals_last = NIL;
  LVAL body_first   = NIL, body_last   = NIL;
  LVAL form         = NIL;
  LVAL fn           = NIL;
  int  has_locals   = 0;

  // Protect everything we'll grow across cons() calls.
  xlstkcheck(8);
  xlprotect(params_first); xlprotect(params_last);
  xlprotect(locals_first); xlprotect(locals_last);
  xlprotect(body_first);   xlprotect(body_last);
  xlprotect(form);         xlprotect(fn);

  // Walk the lambda list, splitting at /.
  for (LVAL p = lam_list; consp(p); p = cdr(p))
  {
    LVAL elem = car(p);
    if (!has_locals && elem == slash_sym)
    {
      has_locals = 1;
      continue;
    }
    LVAL cell = cons(elem, NIL);
    if (has_locals)
    {
      if (null(locals_first)) { locals_first = cell; locals_last = cell; }
      else                    { rplacd(locals_last, cell); locals_last = cell; }
    }
    else
    {
      if (null(params_first)) { params_first = cell; params_last = cell; }
      else                    { rplacd(params_last, cell); params_last = cell; }
    }
  }

  // Copy body forms into a fresh list (xlargv's cells belong to caller).
  for (int i = 2; i < xlargc; i++)
  {
    LVAL cell = cons(xlargv[i], NIL);
    if (null(body_first)) { body_first = cell; body_last = cell; }
    else                  { rplacd(body_last, cell); body_last = cell; }
  }

  // If we collected locals, wrap body in (let (locals) body...).
  if (has_locals)
  {
    form = cons(let_sym, cons(locals_first, body_first));
    body_first = cons(form, NIL);
  }

  // Build (function (lambda params body...)) and evaluate to closure.
  form = cons(lambda_sym,   cons(params_first, body_first));
  form = cons(function_sym, cons(form, NIL));
  fn   = xleval(form);

  setfunction(name, fn);

  xlpopn(8);
  return name;
}

// ---------------------------------------------------------------------
// (getstring [cr] [prompt]) -> string-or-NIL
//   cr     : optional. T (or any non-nil) means "accept whitespace".
//   prompt : optional string.
// Args are positional but type-driven (matches AutoLISP's flexibility).
// ---------------------------------------------------------------------
LVAL subGETSTRING(void)
{
  int   allow_spaces = 0;
  char  prompt_buf[256];
  const char *prompt = NULL;
  char  out_buf[1024];

  prompt_buf[0] = '\0';
  out_buf[0]    = '\0';

  while (moreargs())
  {
    LVAL a = xlgetarg();
    if (stringp(a))
    {
      const char *s = (const char *)getstring(a);
      int n;
      for (n = 0; n < (int)sizeof(prompt_buf) - 1 && s[n]; ++n)
        prompt_buf[n] = s[n];
      prompt_buf[n] = '\0';
      prompt = prompt_buf;
    }
    else if (a == NIL)
    {
      allow_spaces = 0;
    }
    else
    {
      // T, a number, a symbol other than NIL - treat as "allow spaces".
      allow_spaces = 1;
    }
  }

  if (!helperGETSTRING(prompt, allow_spaces, out_buf, (int)sizeof(out_buf)))
    return NIL;

  return cvstring(out_buf);
}

// ---------------------------------------------------------------------
// Registration. Called once from rhino_xlisp_init() in the driver,
// after xlinit() has built the interpreter image.
// ---------------------------------------------------------------------
void RegisterCustomLispFunctions(void)
{
  xlsubr("=",         SUBR,  subEQUAL,    0);
  xlsubr("ALERT",     SUBR,  subALERT,    0);
  xlsubr("COMMAND",   FSUBR, fsubCOMMAND, 0);
  xlsubr("DEFUN",     FSUBR, fsubDEFUN,   0);
  xlsubr("GETDIST",   SUBR,  subGETDIST,  0);
  xlsubr("GETPOINT",  SUBR,  subGETPOINT, 0);
  xlsubr("GETREAL",   SUBR,  subGETREAL,  0);
  xlsubr("GETSTRING", SUBR,  subGETSTRING,0);
  xlsubr("GETVAR",    SUBR,  subGETVAR,   0);
  xlsubr("POW",       SUBR,  subPOW,      0);
  xlsubr("POLAR",     SUBR,  subPOLAR,    0);
  xlsubr("PRINC",     SUBR,  subPRINC,    0);
  xlsubr("RTOS",      SUBR,  subRTOS,     0);
  xlsubr("SETVAR",    SUBR,  subSETVAR,   0);
  xlsubr("STRCAT",    SUBR,  subSTRCAT,   0);
}
