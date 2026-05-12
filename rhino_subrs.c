// rhino_subrs.c - Custom XLISP SUBRs registered at interpreter startup.
//
// Each SUBR is a plain C function `LVAL fn(void)`. It gets its arguments
// off XLISP's argument stack with xlga<type>(), extracts C values with
// get<type>(...), and returns an LVAL constructed with cv<type>(...).
//
// Calls into the Rhino SDK go through extern "C" wrappers in
// rhino_sdk_glue.cpp - that file is the only one that pulls in the C++
// Rhino headers, keeping the lisp glue layer pure C.
//
// To add a new function:
//   1. Write `LVAL xmything(void)` below.
//   2. Add one xlsubr() call inside rhino_xlisp_register_subrs().
//   3. Done. (Recompile.)

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "XLISP-PLUS/sources/xlisp.h"
#include "rhino_subrs.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


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

// Pull (x y [z]) out of a list of numbers; 1 on success.
// AutoLISP point lists may be 2D - z defaults to 0 when omitted.
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

  // Z is optional. (x y) is a valid AutoLISP point, z defaults to 0.
  *z = 0.0;
  lst = cdr(lst);
  if (consp(lst)) {
    e = car(lst);
    if (fixp(e))        *z = (double)getfixnum(e);
    else if (floatp(e)) *z = (double)getflonum(e);
    else return FALSE;
  }
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

// ---------------------------------------------------------------------
// AutoLISP-tolerant = .
//
// XLISP's built-in = is strictly numeric; (= n nil) raises "bad
// argument type". AutoLISP scripts often spell defaults this way:
// (if (= x nil) (setq x DEFAULT))
// So we override = so that:
// - All-numeric args: numeric equality (XLISP semantics).
// - Otherwise: eql semantics; mismatched types just return NIL.
// ---------------------------------------------------------------------
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
        // drain remaining args so the call doesn't trip xllastarg
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

// ---------------------------------------------------------------------
// AutoLISP-tolerant EQ.
//
// Stock XLISP eq is pointer identity, which works for symbols, numbers,
// and same-source-position strings but not for strings that compare
// content-equal but come from different sources (e.g. a literal "LINE"
// in the script vs. the same string built up by entget). AutoLISP eq
// treats those as equal, and scripts rely on it:
//     (eq (cdr (assoc 0 ent)) "LINE")
// We override eq so that string args fall back to content comparison,
// while every other case keeps strict pointer-equality semantics.
// ---------------------------------------------------------------------
LVAL subEQ(void)
{
  LVAL a = xlgetarg();
  LVAL b = xlgetarg();
  xllastarg();

  if (a == b) return s_true;

  if (stringp(a) && stringp(b))
  {
    const char* sa = (const char*)getstring(a);
    const char* sb = (const char*)getstring(b);
    return strcmp(sa, sb) == 0 ? s_true : NIL;
  }

  return NIL;
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
  int rc = helperGETDIST(prompt, has_base, bx, by, bz, &distance);
  if (rc < 0) xlfail("cancelled");
  if (rc == 0) return NIL;

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

  int rc = helperGETPOINT(prompt, has_base, bx, by, bz, &rx, &ry, &rz);
  if (rc < 0) xlfail("cancelled");
  if (rc == 0) return NIL;

  return MakePointList(rx, ry, rz);
}

// ---------------------------------------------------------------------
// (getcorner [pt] [msg]) -> point-or-NIL
//
// In strict AutoLISP, getcorner takes a REQUIRED base point and an
// optional message; the difference from getpoint is the rubber-band
// rectangle UX. In practice, scripts in the wild routinely call
// (getcorner "prompt") with just a string, expecting getpoint-style
// behavior. We accept either - point and message in any order, both
// optional - and degrade to a line rubber-band when no base point is
// supplied. (A true rectangle preview would require subclassing
// CRhinoGetPoint with a custom DynamicDraw.)
// ---------------------------------------------------------------------
LVAL subGETCORNER(void)
{
  int    has_base = 0;
  double bx = 0, by = 0, bz = 0;
  const char* prompt = NULL;
  char   prompt_buf[256];
  double rx, ry, rz;

  ParseOptionalPointAndMessage(&has_base, &bx, &by, &bz,
    &prompt, prompt_buf, (int)sizeof(prompt_buf));

  int rc = helperGETPOINT(prompt, has_base, bx, by, bz, &rx, &ry, &rz);
  if (rc < 0) xlfail("cancelled");
  if (rc == 0) return NIL;

  return MakePointList(rx, ry, rz);
}

// ---------------------------------------------------------------------
// Entity-list helpers used by ENTSEL/ENTGET.
//
// XLISP's GC may run inside any allocator (cons, cvstring, cvflonum,
// newnode, ...) and only inspects LVALs that are on the protect stack.
// The build functions below keep result/pair/key/val protected across
// every allocation, then xlpopn() once before returning.
// ---------------------------------------------------------------------
static LVAL EntPushStringPair(LVAL alist, int code, const char* s)
{
  LVAL pair, key, val;
  xlstkcheck(3);
  xlprotect(pair); xlprotect(key); xlprotect(val);
  val  = cvstring((char FAR*)(s ? s : ""));
  key  = cvfixnum((FIXTYPE)code);
  pair = cons(key, val);
  alist = cons(pair, alist);
  xlpopn(3);
  return alist;
}

static LVAL EntPushFixnumPair(LVAL alist, int code, long n)
{
  LVAL pair, key, val;
  xlstkcheck(3);
  xlprotect(pair); xlprotect(key); xlprotect(val);
  val  = cvfixnum((FIXTYPE)n);
  key  = cvfixnum((FIXTYPE)code);
  pair = cons(key, val);
  alist = cons(pair, alist);
  xlpopn(3);
  return alist;
}

static LVAL EntPushFlonumPair(LVAL alist, int code, double f)
{
  LVAL pair, key, val;
  xlstkcheck(3);
  xlprotect(pair); xlprotect(key); xlprotect(val);
  val  = cvflonum((FLOTYPE)f);
  key  = cvfixnum((FIXTYPE)code);
  pair = cons(key, val);
  alist = cons(pair, alist);
  xlpopn(3);
  return alist;
}

// (code X Y Z)  i.e.  (code . (X Y Z))  -- the AutoLISP shape for a
// DXF point entry. Reading is symmetric: (cdr (assoc 10 e)) -> (X Y Z).
static LVAL EntPushPointPair(LVAL alist, int code, const double* xyz)
{
  LVAL pair, key, pt;
  xlstkcheck(3);
  xlprotect(pair); xlprotect(key); xlprotect(pt);
  pt   = MakePointList(xyz[0], xyz[1], xyz[2]);
  key  = cvfixnum((FIXTYPE)code);
  pair = cons(key, pt);
  alist = cons(pair, alist);
  xlpopn(3);
  return alist;
}

// ---------------------------------------------------------------------
// (entsel [prompt]) -> (ename pick-point) or NIL
//
// `ename` is the object's runtime serial number wrapped as a fixnum.
// The lisp side treats it as opaque - passing it to entget is the
// only documented use.
// ---------------------------------------------------------------------
LVAL subENTSEL(void)
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
  }

  unsigned int sn = 0;
  double px = 0, py = 0, pz = 0;
  int rc = helperENTSEL(prompt, &sn, &px, &py, &pz);
  if (rc < 0)
    xlfail("cancelled");
  if (rc == 0)
    return NIL;

  LVAL result, pt, ename;
  xlstkcheck(3);
  xlprotect(result); xlprotect(pt); xlprotect(ename);
  ename  = cvfixnum((FIXTYPE)sn);
  pt     = MakePointList(px, py, pz);
  result = cons(pt, NIL);
  result = cons(ename, result);
  xlpopn(3);
  return result;
}

// ---------------------------------------------------------------------
// (entget ename) -> association list of (group-code . value) pairs.
//
// Group codes we synthesize from Rhino state:
//   0  type     ("LINE", "CIRCLE", "ARC", "LWPOLYLINE", "POINT",
//                "SPLINE", "BREP", "SURFACE", "MESH", "ENTITY")
//   5  handle   UUID string
//   8  layer    layer name
//   10 primary  point (start / center / insertion)
//   11 endpoint (LINE only)
//   40 radius   (CIRCLE / ARC)
//   50 angle    (ARC start angle, radians)
//   62 color    Color index; 256 = BYLAYER
//   70 flags    bit 1 = closed polyline
//
// Returns NIL if the runtime serial number doesn't resolve to a live
// object in the active document.
// ---------------------------------------------------------------------
// ---------------------------------------------------------------------
// (entlast) -> ename of the most-recently-added object, or NIL if the
// document is empty. AutoLISP convention: scripts call this right
// after (command "<verb>" ...) to grab whatever the command just
// produced, so the runtime serial number returned must be valid for
// subsequent (entget ...) calls.
// ---------------------------------------------------------------------
LVAL subENTLAST(void)
{
  xllastarg();
  unsigned int sn = 0;
  if (!helperEntLast(&sn) || sn == 0)
    return NIL;
  return cvfixnum((FIXTYPE)sn);
}

// ---------------------------------------------------------------------
// (textscr) / (graphscr) - legacy screen-mode toggles for the
// pre-graphical-IDE era. Both no-op silently in Rhino, returning NIL,
// so scripts that call them at startup/shutdown don't abort.
// ---------------------------------------------------------------------
LVAL subTEXTSCR(void)  { xllastarg(); return NIL; }
LVAL subGRAPHSCR(void) { xllastarg(); return NIL; }

// ---------------------------------------------------------------------
// Selection sets (ssadd, sslength, ssname, ssmemb).
//
// Real AutoLISP exposes selection sets as an opaque mutable type.
// Scripts rely on the mutation: (ssadd ename set) updates `set` in
// place without needing to wrap the call in setq. Our representation
// is a cons whose head is a sentinel symbol *ssset* and whose cdr is
// the list of entity names. The sentinel cell gives ssadd a stable
// target for rplacd so callers see their set grow even when it
// started empty.
// ---------------------------------------------------------------------
static LVAL SSSentinelSym(void)
{
  // xlenter interns the symbol on first call, then returns the same
  // LVAL on every subsequent call - so identity comparison works.
  return xlenter("*SSSET*");
}

static int SSIsSelectionSet(LVAL v)
{
  return consp(v) && (car(v) == SSSentinelSym());
}

// (ssadd)                  -> fresh empty set
// (ssadd ename)            -> fresh set containing ename
// (ssadd ename set)        -> mutate set to include ename; return set
LVAL subSSADD(void)
{
  if (!moreargs())
  {
    // Empty set: just the sentinel cell.
    LVAL r;
    xlsave1(r);
    r = cons(SSSentinelSym(), NIL);
    xlpop();
    return r;
  }

  LVAL ename = xlgetarg();

  if (!moreargs())
  {
    // New singleton set.
    LVAL r, c;
    xlstkcheck(2);
    xlprotect(r); xlprotect(c);
    c = cons(ename, NIL);
    r = cons(SSSentinelSym(), c);
    xlpopn(2);
    return r;
  }

  // Mutating form: (ssadd ename set)
  LVAL set = xlgetarg();
  xllastarg();

  if (!SSIsSelectionSet(set))
    xlerror("ssadd: not a selection set", set);

  // Prepend ename onto the set's payload. The sentinel cell stays
  // identity-stable, so the caller's binding still points at the
  // updated set.
  LVAL new_cell;
  xlsave1(new_cell);
  new_cell = cons(ename, cdr(set));
  rplacd(set, new_cell);
  xlpop();
  return set;
}

// (sslength set) -> count of entities in the set
LVAL subSSLENGTH(void)
{
  LVAL set = xlgetarg();
  xllastarg();
  if (!SSIsSelectionSet(set))
    return cvfixnum((FIXTYPE)0);
  long n = 0;
  for (LVAL p = cdr(set); consp(p); p = cdr(p))
    ++n;
  return cvfixnum((FIXTYPE)n);
}

// (ssname set index) -> ename at position `index`, NIL out of range
LVAL subSSNAME(void)
{
  LVAL set = xlgetarg();
  LVAL idx = xlgetarg();
  xllastarg();
  if (!SSIsSelectionSet(set) || !fixp(idx))
    return NIL;
  long i = (long)getfixnum(idx);
  if (i < 0) return NIL;
  for (LVAL p = cdr(set); consp(p); p = cdr(p))
  {
    if (i == 0) return car(p);
    --i;
  }
  return NIL;
}

// (ssmemb ename set) -> ename if present in set, else NIL
LVAL subSSMEMB(void)
{
  LVAL ename = xlgetarg();
  LVAL set   = xlgetarg();
  xllastarg();
  if (!SSIsSelectionSet(set)) return NIL;
  for (LVAL p = cdr(set); consp(p); p = cdr(p))
  {
    if (eql(car(p), ename)) return ename;
  }
  return NIL;
}

LVAL subENTGET(void)
{
  LVAL arg = xlgetarg();
  xllastarg();

  // Tolerate NIL so (entget (car (entsel))) doesn't blow up when the
  //      user cancels entsel. Non-fixnum garbage also degrades to NIL.
  if (null(arg) || !fixp(arg))
    return NIL;

  unsigned int sn = (unsigned int)getfixnum(arg);

  RhinoEntityProps p;
  if (!helperGETENT(sn, &p))
    return NIL;

  LVAL result;
  xlsave1(result);
  result = NIL;

  // Build in reverse: last prepend ends up at the head. We want type
  // first because (cdr (assoc 0 e)) is by far the most common probe.
  if (p.has_scale)  result = EntPushFlonumPair(result, 43, p.scale[2]);
  if (p.has_scale)  result = EntPushFlonumPair(result, 42, p.scale[1]);
  if (p.has_scale)  result = EntPushFlonumPair(result, 41, p.scale[0]);
  if (p.has_flag70) result = EntPushFixnumPair(result, 70, (long)p.flag70);
  if (p.has_angle)  result = EntPushFlonumPair(result, 50, p.angle);
  if (p.has_radius) result = EntPushFlonumPair(result, 40, p.radius);
  if (p.has_pt11)   result = EntPushPointPair (result, 11, p.pt11);
  if (p.has_pt10)   result = EntPushPointPair (result, 10, p.pt10);
  if (p.has_color)  result = EntPushFixnumPair(result, 62, (long)p.color_idx);
  if (p.handle[0])  result = EntPushStringPair(result, 5,  p.handle);
  if (p.layer[0])   result = EntPushStringPair(result, 8,  p.layer);
                    result = EntPushStringPair(result, 0,  p.type);

  xlpop();
  return result;
}

// ---------------------------------------------------------------------
// (getint [msg]) -> integer-or-NIL
//   msg : optional prompt string.
// Same shape as GETREAL but returns a fixnum. Cancel -> NIL.
// ---------------------------------------------------------------------
LVAL subGETINT(void)
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
    // anything else: silently ignore (matches GETSTRING tolerance)
  }

  int value = 0;
  int rc = helperGETINT(prompt, &value);
  if (rc < 0) xlfail("cancelled");
  if (rc == 0) return NIL;
  return cvfixnum((FIXTYPE)value);
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
    // anything else: silently ignore (matches GETSTRING tolerance)
  }

  double value = 0.0;
  int rc = helperGETREAL(prompt, &value);
  if (rc < 0) xlfail("cancelled");
  if (rc == 0) return NIL;
  return cvflonum((FLOTYPE)value);
}

// ---------------------------------------------------------------------
// (getangle [pt] [msg]) -> radians-or-NIL
//
// Like GETDIST, accepts the AutoLISP-style optional point + message in
// either order. With a base point, CRhinoGetAngle draws a rubber-band
// reference so the user can either click a direction or type a number.
// Result is in radians regardless of the current AUNITS setting -
// that matches AutoLISP, which uses radians internally and only honors
// AUNITS when formatting via ANGTOS.
// ---------------------------------------------------------------------
LVAL subGETANGLE(void)
{
  int    has_base = 0;
  double bx = 0, by = 0, bz = 0;
  const char* prompt = NULL;
  char   prompt_buf[256];

  ParseOptionalPointAndMessage(&has_base, &bx, &by, &bz,
    &prompt, prompt_buf, (int)sizeof(prompt_buf));

  double angle = 0.0;
  int rc = helperGETANGLE(prompt, has_base, bx, by, bz, &angle);
  if (rc < 0) xlfail("cancelled");
  if (rc == 0) return NIL;
  return cvflonum((FLOTYPE)angle);
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
//               4=architectural, 5=fractional. Modes 3-5 are imperial
//               conventions; we fall back to decimal for them.
//   precision : digits after the decimal point. Default 4.
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
    // Scientific: e.g. 1.2345E+02
    snprintf(fmt, sizeof(fmt), "%%.%dE", precision);
  }
  else
  {
    // Decimal (and fall-through for unsupported imperial modes 3-5).
    snprintf(fmt, sizeof(fmt), "%%.%df", precision);
  }
  snprintf(buf, sizeof(buf), fmt, n);
  return cvstring(buf);
}

// ---------------------------------------------------------------------
// (angtos radians [mode [precision]]) -> string
//
// Format an angle (in radians) as a human-readable string. The mode
// values are the AUNITS conventions:
//   0  decimal degrees   (e.g. "45.0000")
//   1  degrees / minutes / seconds (e.g. "45d0'0\"")
//   2  gradians          (e.g. "50.0000g")
//   3  radians           (e.g. "0.7854r")
//   4  surveyor units    (not fully implemented - falls through to
//                         decimal degrees, with a placeholder format)
//
// When mode or precision is omitted, the corresponding sysvar (AUNITS
// or AUPREC) is consulted. This lets scripts that set those globals
// at the top of the file get consistent formatting from every ANGTOS
// call further down, just like real AutoLISP.
// ---------------------------------------------------------------------
LVAL subANGTOS(void)
{
  double radians   = ArgAsDouble();
  int    mode      = -1;     // sentinel: read AUNITS
  int    precision = -1;     // sentinel: read AUPREC

  if (moreargs())
  {
    LVAL m = xlgetarg();
    if (fixp(m))        mode = (int)getfixnum(m);
    else if (floatp(m)) mode = (int)getflonum(m);
    else xlerror("angtos: mode must be a number", m);
  }
  if (moreargs())
  {
    LVAL p = xlgetarg();
    if (fixp(p))        precision = (int)getfixnum(p);
    else if (floatp(p)) precision = (int)getflonum(p);
    else xlerror("angtos: precision must be a number", p);
  }
  xllastarg();

  if (mode < 0)      { int v = 0; helperGetAUnits(&v);  mode      = v; }
  if (precision < 0) { int v = 4; helperGetAUPrec(&v);  precision = v; }
  if (precision < 0)  precision = 0;
  if (precision > 16) precision = 16;

  char buf[128];
  char fmt[16];

  if (mode == 1)
  {
    // Degrees/minutes/seconds. Compute on the absolute value, then
    //        re-attach the sign at the front. AutoCAD prints negative angles
    //        without a leading minus (it normalizes to 0..360), but scripts
    //        sometimes test against negative values, so we keep the sign.
    double deg_total = radians * 180.0 / M_PI;
    int    sign      = (deg_total < 0) ? -1 : 1;
    deg_total *= (double)sign;
    int    d = (int)deg_total;
    double rem_min = (deg_total - (double)d) * 60.0;
    int    m = (int)rem_min;
    double s = (rem_min - (double)m) * 60.0;

    char sec_fmt[16], sec_buf[32];
    snprintf(sec_fmt, sizeof(sec_fmt), "%%.%df", precision);
    snprintf(sec_buf, sizeof(sec_buf), sec_fmt, s);

    snprintf(buf, sizeof(buf), "%s%dd%d'%s\"",
             sign < 0 ? "-" : "", d, m, sec_buf);
  }
  else if (mode == 2)
  {
    double grads = radians * 200.0 / M_PI;
    snprintf(fmt, sizeof(fmt), "%%.%dfg", precision);
    snprintf(buf, sizeof(buf), fmt, grads);
  }
  else if (mode == 3)
  {
    snprintf(fmt, sizeof(fmt), "%%.%dfr", precision);
    snprintf(buf, sizeof(buf), fmt, radians);
  }
  else
  {
    // Mode 0 (decimal degrees) and the fall-through for the unsupported
    //        surveyor mode 4.
    double degrees = radians * 180.0 / M_PI;
    snprintf(fmt, sizeof(fmt), "%%.%df", precision);
    snprintf(buf, sizeof(buf), fmt, degrees);
  }

  return cvstring(buf);
}

// =====================================================================
// AutoLISP spelling aliases for stock XLISP functionality.
//
// These functions exist in XLISP-PLUS under different names (truncate,
// char-code, subseq, string-upcase, ...) or as small compositions of
// primitives. We wrap them with their AutoLISP spellings so scripts
// can call them as written.
// =====================================================================

// (fix n) - truncate a number toward zero. AutoLISP returns a single
// value; XLISP's TRUNCATE is multiple-value-returning, so we just
// synthesize the integer ourselves.
LVAL subFIX(void)
{
  LVAL a = xlgetarg();
  xllastarg();
  if (fixp(a))   return a;
  if (floatp(a)) return cvfixnum((FIXTYPE)getflonum(a));
  xlerror("fix: expected a number", a);
  return NIL;
}

// (ascii "X") -> integer character code of the first byte. NIL if the
// string is empty.
LVAL subASCII(void)
{
  LVAL s = xlgastring();
  xllastarg();
  const char* p = (const char*)getstring(s);
  if (p[0] == '\0') return NIL;
  return cvfixnum((FIXTYPE)(unsigned char)p[0]);
}

// (chr 65) -> "A". One-byte character; for codes outside 0..255 we
// mask down to the low byte (matches AutoLISP's behavior).
LVAL subCHR(void)
{
  LVAL a = xlgafixnum();
  xllastarg();
  char buf[2];
  buf[0] = (char)(((long)getfixnum(a)) & 0xFF);
  buf[1] = '\0';
  return cvstring(buf);
}

// (strlen [s1 s2 ...]) -> total character count across all string args.
// (strlen) is 0; AutoLISP allows zero or more args.
LVAL subSTRLEN(void)
{
  long total = 0;
  while (moreargs())
  {
    LVAL s = xlgastring();
    total += (long)getslength(s);
  }
  return cvfixnum((FIXTYPE)total);
}

// (strcase str [downcase-p]) -> upper-cased copy; lower-cased when the
// optional 2nd arg is non-nil. ASCII-only (matches AutoLISP).
LVAL subSTRCASE(void)
{
  LVAL s = xlgastring();
  int down = 0;
  if (moreargs())
  {
    LVAL flag = xlgetarg();
    if (!null(flag)) down = 1;
  }
  xllastarg();

  unsigned len = getslength(s);
  LVAL result = newstring(len);
  char FAR* dst = (char FAR*)getstring(result);
  const char* src = (const char*)getstring(s);
  for (unsigned i = 0; i < len; i++)
  {
    char c = src[i];
    if (down) { if (c >= 'A' && c <= 'Z') c = (char)(c + 32); }
    else      { if (c >= 'a' && c <= 'z') c = (char)(c - 32); }
    dst[i] = c;
  }
  dst[len] = '\0';
  return result;
}

// (substr str start [length]) -> substring.
// AutoLISP indexing is 1-based; we translate to XLISP's 0-based slice.
// Out-of-range start returns "". Omitted length means "to end of str".
LVAL subSUBSTR(void)
{
  LVAL s     = xlgastring();
  LVAL start = xlgafixnum();
  long count = -1;
  if (moreargs())
  {
    LVAL n = xlgafixnum();
    count = (long)getfixnum(n);
  }
  xllastarg();

  long start1 = (long)getfixnum(start);
  if (start1 < 1) start1 = 1;
  long start0 = start1 - 1;

  unsigned slen = getslength(s);
  if ((unsigned long)start0 >= slen) return cvstring("");

  long available = (long)slen - start0;
  if (count < 0 || count > available) count = available;
  if (count < 0) count = 0;

  LVAL result = newstring((unsigned)count);
  char FAR* dst = (char FAR*)getstring(result);
  const char* src = (const char*)getstring(s) + start0;
  for (long i = 0; i < count; i++) dst[i] = src[i];
  dst[count] = '\0';
  return result;
}

// (atoi "42") -> 42. Unparseable strings produce 0 (AutoLISP semantics).
LVAL subATOI(void)
{
  LVAL s = xlgastring();
  xllastarg();
  const char* p = (const char*)getstring(s);
  long n = strtol(p, NULL, 10);
  return cvfixnum((FIXTYPE)n);
}

// (atof "3.14") -> 3.14. Unparseable -> 0.0.
LVAL subATOF(void)
{
  LVAL s = xlgastring();
  xllastarg();
  const char* p = (const char*)getstring(s);
  double d = strtod(p, NULL);
  return cvflonum((FLOTYPE)d);
}

// (itoa 42) -> "42". Integer only - flonums should go through rtos.
LVAL subITOA(void)
{
  LVAL a = xlgafixnum();
  xllastarg();
  char buf[32];
  snprintf(buf, sizeof(buf), "%ld", (long)getfixnum(a));
  return cvstring(buf);
}

// =====================================================================
// Pure-math AutoLISP primitives. No SDK calls, no state.
// =====================================================================

// (distance p1 p2) -> flonum
// Full 3D Euclidean distance. 2D point lists are accepted on either side
// (z defaults to 0), matching AutoLISP.
LVAL subDISTANCE(void)
{
  LVAL p1_arg = xlgetarg();
  LVAL p2_arg = xlgetarg();
  xllastarg();

  double x1, y1, z1, x2, y2, z2;
  if (!ParsePointList(p1_arg, &x1, &y1, &z1))
    xlerror("distance: first arg must be a point", p1_arg);
  if (!ParsePointList(p2_arg, &x2, &y2, &z2))
    xlerror("distance: second arg must be a point", p2_arg);

  double dx = x2 - x1, dy = y2 - y1, dz = z2 - z1;
  return cvflonum((FLOTYPE)sqrt(dx * dx + dy * dy + dz * dz));
}

// (angle p1 p2) -> flonum (radians in [0, 2*pi))
// AutoLISP convention is XY-plane angle from p1 to p2 - the z component
// is ignored. Result is normalized to [0, 2*pi) for compatibility with
// scripts that expect non-negative angles.
LVAL subANGLE(void)
{
  LVAL p1_arg = xlgetarg();
  LVAL p2_arg = xlgetarg();
  xllastarg();

  double x1, y1, z1, x2, y2, z2;
  if (!ParsePointList(p1_arg, &x1, &y1, &z1))
    xlerror("angle: first arg must be a point", p1_arg);
  if (!ParsePointList(p2_arg, &x2, &y2, &z2))
    xlerror("angle: second arg must be a point", p2_arg);

  double a = atan2(y2 - y1, x2 - x1);
  if (a < 0.0) a += 2.0 * M_PI;
  return cvflonum((FLOTYPE)a);
}

// (inters p1 p2 p3 p4 [bounded]) -> point or NIL
//
// Computes the intersection of two lines in the XY plane.
//   - The line P1-P2 and the line P3-P4.
//   - When `bounded` is omitted or non-nil (default), both must be
//     bounded segments; the intersection has to fall inside both.
//   - When `bounded` is nil, the lines are treated as infinite.
//
// Returns the intersection point with z linearly interpolated along the
// first line. NIL if the lines are parallel, or (in bounded mode) if
// the crossing point falls outside either segment.
//
// This is a 2D-only implementation - matches what nearly all AutoLISP
// scripts assume in practice. True 3D skew-line "closest approach"
// is a separate, more involved exercise.
LVAL subINTERS(void)
{
  LVAL p1_arg = xlgetarg();
  LVAL p2_arg = xlgetarg();
  LVAL p3_arg = xlgetarg();
  LVAL p4_arg = xlgetarg();

  int bounded = 1;
  if (moreargs())
  {
    LVAL flag = xlgetarg();
    if (null(flag)) bounded = 0;
  }
  xllastarg();

  double pts[12];
  if (!ParsePointList(p1_arg, pts, pts+1, pts+2))
    xlerror("inters: arg 1 must be a point", p1_arg);
  if (!ParsePointList(p2_arg, pts+3, pts+4, pts+5))
    xlerror("inters: arg 2 must be a point", p2_arg);
  if (!ParsePointList(p3_arg, pts+6, pts+7, pts+8))
    xlerror("inters: arg 3 must be a point", p3_arg);
  if (!ParsePointList(p4_arg, pts+9, pts+10, pts+11))
    xlerror("inters: arg 4 must be a point", p4_arg);

  double x=0, y=0, z=0;
  int rc = helperIntersectLineLine(pts, bounded, &x, &y, &z);
  if (0 == rc)
    return NIL;

  return MakePointList(x, y, z);
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

// ---------------------------------------------------------------------
// (boole func int1 int2 ...) -> integer
//
// AutoLISP's general bitwise primitive. `func` is a 4-bit integer that
// encodes a truth table over the two input bits. For each bit position
// of the operands, bit k of `func` (k=0..3) selects the result when
// the inputs match the pattern:
//
//   k=0: a=1, b=1   ->  &
//   k=1: a=1, b=0   ->  a & ~b
//   k=2: a=0, b=1   ->  ~a & b
//   k=3: a=0, b=0   ->  ~a & ~b
//
// So func=1 is AND, func=6 (0110) is XOR, func=7 (0111) is OR, func=8
// is NOR, func=14 is NAND, func=0 is clear-all, func=15 is set-all.
// Additional operands are folded left-to-right (boole op (boole op a b) c).
// ---------------------------------------------------------------------
LVAL subBOOLE(void)
{
  LVAL op_arg = xlgafixnum();
  long op = (long)getfixnum(op_arg);
  op &= 0x0F;  // truth-table is 4 bits; mask off junk

  if (!moreargs())
    xlfail("BOOLE: needs at least one integer operand");

  LVAL first = xlgafixnum();
  long acc = (long)getfixnum(first);

  while (moreargs())
  {
    LVAL b_arg = xlgafixnum();
    long b = (long)getfixnum(b_arg);

    long result = 0;
    if (op & 1) result |= (acc & b);
    if (op & 2) result |= (acc & ~b);
    if (op & 4) result |= (~acc & b);
    if (op & 8) result |= (~acc & ~b);
    acc = result;
  }

  return cvfixnum((FIXTYPE)acc);
}

// (getstring [cr] [msg])
LVAL subGETSTRING(void)
{
  int   allow_spaces = 0;
  char  prompt_buf[256];
  const char* prompt = NULL;
  char  out_buf[1024];

  prompt_buf[0] = '\0';
  out_buf[0] = '\0';

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

  int rc = helperGETSTRING(prompt, allow_spaces, out_buf, (int)sizeof(out_buf));
  if (rc < 0) xlfail("cancelled");
  if (rc == 0) return NIL;

  return cvstring(out_buf);
}

// ---------------------------------------------------------------------
// (getvar "NAME") / (setvar "NAME" value)
// For now we only honor "CLAYER" (the active doc's current layer name).
// Anything else fails so it surfaces during script porting.
// ---------------------------------------------------------------------

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

// AutoLISP accepts either spelling for a system-variable name:
//    (getvar "CLAYER")    ;; string literal
//    (getvar 'CLAYER)     ;; quoted symbol
// Strict XLISP-PLUS evaluates the quoted form to the symbol CLAYER,
// which isn't a string. This helper accepts both: returns the symbol's
// print-name when given a symbol, the raw string when given a string,
// and errors out cleanly on anything else.
static const char* SystemVarName(LVAL arg)
{
  if (stringp(arg))
    return (const char*)getstring(arg);
  if (symbolp(arg))
    return (const char*)getstring(getpname(arg));
  xlerror("system variable name must be a string or symbol", arg);
  return NULL; // unreachable - xlerror longjmps
}

LVAL subGETVAR()
{
  LVAL nm = xlgetarg();
  xllastarg();
  {
    const char* name = SystemVarName(nm);

    if (EqualInsensitive(name, "CLAYER")) {
      char buf[256];
      if (rhino_glue_getvar_clayer(buf, (int)sizeof(buf)))
        return cvstring(buf);
      return NIL;
    }

    if (EqualInsensitive(name, "OSMODE")) {
      int v = 0;
      if (helperGetOSnapMode(&v))
        return cvfixnum((FIXTYPE)v);
      return NIL;
    }

    if (EqualInsensitive(name, "CMDECHO")) {
      int v = 1;
      if (helpGetEcho(&v))
        return cvfixnum((FIXTYPE)v);
      return NIL;
    }

    if (EqualInsensitive(name, "OSNAPCOORD")) {
      int v = 1;
      if (helperGetOSnapCoord(&v))
        return cvfixnum((FIXTYPE)v);
      return NIL;
    }

    if (EqualInsensitive(name, "ORTHOMODE")) {
      int v = 0;
      if (helperGetOrthoMode(&v))
        return cvfixnum((FIXTYPE)v);
      return NIL;
    }

    if (EqualInsensitive(name, "SNAPANG")) {
      double v = 0.0;
      if (helperGetSnapAng(&v))
        return cvflonum((FLOTYPE)v);
      return NIL;
    }

    if (EqualInsensitive(name, "VIEWTWIST")) {
      double v = 0.0;
      if (helperGetViewTwist(&v))
        return cvflonum((FLOTYPE)v);
      return NIL;
    }

    if (EqualInsensitive(name, "AUNITS")) {
      int v = 0;
      if (helperGetAUnits(&v))
        return cvfixnum((FIXTYPE)v);
      return NIL;
    }

    if (EqualInsensitive(name, "AUPREC")) {
      int v = 4;
      if (helperGetAUPrec(&v))
        return cvfixnum((FIXTYPE)v);
      return NIL;
    }

    if (EqualInsensitive(name, "CECOLOR")) {
      char buf[64];
      if (helperGetCEColor(buf, (int)sizeof(buf)))
        return cvstring(buf);
      return NIL;
    }

    xlerror("getvar: unsupported system variable", nm);
    return NIL;
  }
}

// Coerce an LVAL to int for setvar of an integer-valued variable.
//    AutoLISP is forgiving: floats get truncated, integers pass through.
//    `var_name` is accepted for future richer diagnostics but currently
//    unused - we report a generic message and let the irritant LVAL show
//    the offending value.
static int SetvarIntArg(LVAL val, const char* var_name)
{
  (void)var_name;
  if (fixp(val))   return (int)getfixnum(val);
  if (floatp(val)) return (int)getflonum(val);
  xlerror("setvar: integer-valued variable requires a number", val);
  return 0; // unreachable
}

// Coerce to double for real-valued sysvars (SNAPANG, VIEWTWIST).
static double SetvarRealArg(LVAL val, const char* var_name)
{
  (void)var_name;
  if (fixp(val))   return (double)getfixnum(val);
  if (floatp(val)) return (double)getflonum(val);
  xlerror("setvar: real-valued variable requires a number", val);
  return 0.0; // unreachable
}

LVAL subSETVAR()
{
  LVAL nm = xlgetarg();
  LVAL val = xlgetarg();
  xllastarg();
  {
    const char* name = SystemVarName(nm);

    if (EqualInsensitive(name, "CLAYER")) {
      if (!stringp(val))
        xlerror("setvar CLAYER: value must be a string", val);
      (void)rhino_glue_setvar_clayer((const char*)getstring(val));
      return val;
    }

    if (EqualInsensitive(name, "OSMODE")) {
      int v = SetvarIntArg(val, "OSMODE");
      (void)helperSetOSnapMode(v);
      return val;
    }

    if (EqualInsensitive(name, "CMDECHO")) {
      int v = SetvarIntArg(val, "CMDECHO");
      (void)helperSetEcho(v);
      return val;
    }

    if (EqualInsensitive(name, "OSNAPCOORD")) {
      int v = SetvarIntArg(val, "OSNAPCOORD");
      (void)helperSetOSnapCoord(v);
      return val;
    }

    if (EqualInsensitive(name, "ORTHOMODE")) {
      int v = SetvarIntArg(val, "ORTHOMODE");
      (void)helperSetOrthoMode(v);
      return val;
    }

    if (EqualInsensitive(name, "SNAPANG")) {
      double v = SetvarRealArg(val, "SNAPANG");
      (void)helperSetSnapAng(v);
      return val;
    }

    if (EqualInsensitive(name, "VIEWTWIST")) {
      double v = SetvarRealArg(val, "VIEWTWIST");
      (void)helperSetViewTwist(v);
      return val;
    }

    if (EqualInsensitive(name, "AUNITS")) {
      int v = SetvarIntArg(val, "AUNITS");
      (void)helperSetAUnits(v);
      return val;
    }

    if (EqualInsensitive(name, "AUPREC")) {
      int v = SetvarIntArg(val, "AUPREC");
      (void)helperSetAUPrec(v);
      return val;
    }

    if (EqualInsensitive(name, "CECOLOR")) {
      if (!stringp(val))
        xlerror("setvar CECOLOR: value must be a string", val);
      (void)helperSetCEColor((const char*)getstring(val));
      return val;
    }

    xlerror("setvar: unsupported system variable", nm);
    return NIL;
  }
}

// ---------------------------------------------------------------------
// (command ...) - dispatch a Rhino command from AutoLISP.
//
// This is an FSUBR: arguments arrive UNEVALUATED so the COMMAND form
// can accept keyboard-shaped input (bare symbols, strings, numbers,
// point lists). We explicitly evaluate each arg, stringify it according
// to its type, then hand the command name and an argv-shaped array to
// RhinoAppRunScript. The Rhino-side glue is responsible for deciding
// whether a given command (LAYER, LINE, ...) gets special handling or
// falls through to RhinoApp().RunScript() as a typed-input script.
//
// Type-to-string conventions:
// string      -> as-is (empty string survives, meaning "Enter")
// symbol      -> print-name
// fixnum      -> %ld
// flonum      -> %.17g (round-trip precision)
// point list  -> "x,y,z" with %.17g per coord
// NIL / other -> skipped
// ---------------------------------------------------------------------

LVAL fsubCOMMAND(void)
{
  if (xlargc < 1)
  {
    xllastarg();
    return NIL;
  }

  // Evaluate the command name. Must be a string.
  LVAL cmd_val = xleval(xlargv[0]);
  if (!stringp(cmd_val))
    xlerror("command: first argument must evaluate to a string", xlargv[0]);
  const char* cmd = (const char*)getstring(cmd_val);

  // Build a flat argv from the remaining args. Sized for typical
  //      AutoLISP COMMAND usage (rarely more than ~20 args, ~50 bytes each
  //      since point strings dominate).
  enum { kMaxArgs = 64, kArgBufSize = 256 };
  static char arg_buf[kMaxArgs][kArgBufSize];
  const char* argv[kMaxArgs];
  int argc = 0;

  for (int i = 1; i < xlargc && argc < kMaxArgs; ++i)
  {
    LVAL form = xlargv[i];
    char* dst = arg_buf[argc];
    dst[0] = '\0';

    // AutoLISP COMMAND is a typing macro - bare unbound symbols come
    //        through as if typed at the prompt, not as variable lookups. A
    //        stray (command ... C) on the end of a LINE call is meant to be
    //        the "close" keyword, not a reference to a variable named C.
    //        Catch that before xleval blows up on the missing binding.
    //
    //        IMPORTANT: boundp() only inspects the symbol's GLOBAL value
    //        cell. Locals introduced by defun's "/" lambda list live in
    //        xlenv, not in the global cell, so boundp would falsely report
    //        them as unbound and we'd wrongly stringify the *symbol name*
    //        ("PL", "P3") instead of evaluating to the point value. Use
    //        xlxgetvalue instead - it walks xlenv first, then the global
    //        cell, and returns s_unbound only when the symbol has no value
    //        in any reachable scope.
    LVAL v;
    if (symbolp(form))
    {
      v = xlxgetvalue(form);
      if (v == s_unbound)
      {
        const char* s = (const char*)getstring(getpname(form));
        int n;
        for (n = 0; n < kArgBufSize - 1 && s[n]; ++n) dst[n] = s[n];
        dst[n] = '\0';
        argv[argc++] = dst;
        continue;
      }
    }
    else
    {
      v = xleval(form);
    }

    if (null(v))
    {
      // skip NIL - real AutoLISP errors here; we degrade quietly.
      continue;
    }
    else if (consp(v))
    {
      // Selection sets are tagged cons cells: (*SSSET* ename1 ename2 ...).
      // AutoLISP's (command "...") naturally expands a selection set
      // into its members so the receiving command sees each entity as
      // a separate input. Mirror that: emit one argv token per ename
      // and skip the standard single-arg dispatch below.
      if (car(v) == SSSentinelSym())
      {
        for (LVAL p = cdr(v); consp(p) && argc < kMaxArgs; p = cdr(p))
        {
          LVAL e = car(p);
          if (!fixp(e)) continue;
          char* sd = arg_buf[argc];
          snprintf(sd, kArgBufSize, "%ld", (long)getfixnum(e));
          argv[argc++] = sd;
        }
        continue;   // don't fall through; the set has been unrolled
      }

      double x, y, z;
      if (!ParsePointList(v, &x, &y, &z))
        continue;
      snprintf(dst, kArgBufSize, "%.17g,%.17g,%.17g", x, y, z);
    }
    else if (stringp(v))
    {
      const char* s = (const char*)getstring(v);
      int n;
      for (n = 0; n < kArgBufSize - 1 && s[n]; ++n) dst[n] = s[n];
      dst[n] = '\0';
    }
    else if (symbolp(v))
    {
      const char* s = (const char*)getstring(getpname(v));
      int n;
      for (n = 0; n < kArgBufSize - 1 && s[n]; ++n) dst[n] = s[n];
      dst[n] = '\0';
    }
    else if (fixp(v))
    {
      snprintf(dst, kArgBufSize, "%ld", (long)getfixnum(v));
    }
    else if (floatp(v))
    {
      snprintf(dst, kArgBufSize, "%.17g", (double)getflonum(v));
    }
    else
    {
      // anything else: skip
      continue;
    }

    argv[argc] = dst;
    ++argc;
  }

  RhinoAppRunScript(cmd, argc, argv);
  return s_true;
}

// ---------------------------------------------------------------------
// AutoLISP-tolerant PRINC.
//
// Three things this needs to do beyond stock XLISP's princ:
//   1. Tolerate zero args - many scripts end with bare (princ) to
//      suppress the trailing return value. XLISP's princ would error.
//   2. Push the formatted value straight to Rhino's command line via
//      RhinoApp().Print() rather than into the deferred eval-buffer
//      that gets flushed at end-of-script. Output appears immediately.
//   3. No auto-appended newlines, so (princ "a") (princ "b") prints
//      "ab" on a single line - this is the AutoLISP convention.
//
// We still honor the optional second argument: if a stream LVAL is
// passed, write to it through normal XLISP channels (matches the
// stock CL/XLISP behavior). Only the no-stream form short-circuits to
// Rhino.
// ---------------------------------------------------------------------
LVAL subPRINC(void)
{
  if (!moreargs())
    return NIL;

  LVAL val  = xlgetarg();
  LVAL fptr = (moreargs() ? xlgetfile(TRUE) : NIL);
  xllastarg();

  if (!null(fptr))
  {
    // Caller specified an explicit stream - write there.
    xlprint(fptr, val, FALSE);
    return val;
  }

  // No stream: format the value into an unnamed string stream, then
  //      hand the captured text to RhinoApp().Print(). This works for
  //      every printable type because xlprint already knows how to format
  //      strings, numbers, lists, symbols, etc.; we just intercept the
  //      output instead of letting it pool in the lisp stdout buffer.
  LVAL stream;
  xlsave1(stream);
  stream = newustream();
  xlprint(stream, val, FALSE);
  LVAL captured = getstroutput(stream);
  xlpop();

  if (stringp(captured))
    RhinoAppPrintRaw((const char*)getstring(captured));

  return val;
}

// ---------------------------------------------------------------------
// (prompt msg) - write a message to the command line, return NIL.
//
// Behaves like princ on a single string argument: no escaping of the
// string's quotes, no trailing newline. AutoLISP scripts conventionally
// embed "\n" at the start of the message themselves (you'll see
// (prompt "\nSelect object: ") throughout the corpus), so we don't
// auto-newline either.
//
// We route output through xlprint to the same stream princ uses, so the
// text flows into the shared XLISP output buffer that cmdRhinoLisp.cpp
// flushes to Rhino's command line at the end of the script run.
// ---------------------------------------------------------------------
LVAL subPROMPT(void)
{
  LVAL s = xlgastring();
  xllastarg();
  xlprint(getvalue(s_stdout), s, FALSE);
  return NIL;
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

// XLISP-PLUS ships with LOOP (iterate forever, exit via RETURN) and a
// number of DO variants, but no WHILE. Many AutoLISP scripts spell
// their loops as (while T ...) or (while (setq x ...) ...), so we add
// a thin FSUBR with the canonical semantics:
//   - Evaluate `test`.
//   - If non-NIL, evaluate each body form in order.
//   - Repeat. Return NIL when test goes NIL (also our return value if
//     the loop never runs).
// ---------------------------------------------------------------------
// (while testexpr [expr ...])
LVAL fsubWHILE(void)
{
  if (xlargc < 1) xlfail("WHILE: missing test form");

  LVAL test_form = xlargv[0];
  // Snapshot the body forms - everything after the test. We re-walk
  //      this slice each iteration.
  LVAL FAR *body_argv = xlargv + 1;
  int       body_argc = xlargc - 1;

  LVAL val = NIL;

  while (1)
  {
    LVAL test_val = xleval(test_form);
    if (null(test_val))
      break;

    // Re-point xlargv/xlargc at the body so nextarg()/moreargs()
    //        walk the body forms cleanly inside this iteration.
    xlargv = body_argv;
    xlargc = body_argc;
    while (moreargs())
      val = xleval(nextarg());

    // Allow Ctrl+Break / OS interrupts to escape an infinite loop -
    //        same hook XLISP's LOOP uses. Without this, (while T ...) is
    //        genuinely uninterruptible.
    if (--xlsample <= 0) {
      xlsample = SAMPLE;
      oscheck();
    }
  }

  return val;
}

// ---------------------------------------------------------------------
// Registration. Called once from rhino_xlisp_init() in the driver,
// after xlinit() has built the interpreter image.
// ---------------------------------------------------------------------
void RegisterCustomLispFunctions(void)
{
  xlsubr("=",         SUBR,  subEQUAL,    0);
  xlsubr("EQ",        SUBR,  subEQ,       0);
  xlsubr("ALERT",     SUBR,  subALERT,    0);
  xlsubr("ANGLE",     SUBR,  subANGLE,    0);
  xlsubr("ANGTOS",    SUBR,  subANGTOS,   0);
  xlsubr("ASCII",     SUBR,  subASCII,    0);
  xlsubr("ATOF",      SUBR,  subATOF,     0);
  xlsubr("ATOI",      SUBR,  subATOI,     0);
  xlsubr("BOOLE",     SUBR,  subBOOLE,    0);
  xlsubr("CHR",       SUBR,  subCHR,      0);
  xlsubr("COMMAND",   FSUBR, fsubCOMMAND, 0);
  xlsubr("DEFUN",     FSUBR, fsubDEFUN,   0);
  xlsubr("DISTANCE",  SUBR,  subDISTANCE, 0);
  xlsubr("ENTGET",    SUBR,  subENTGET,   0);
  xlsubr("ENTLAST",   SUBR,  subENTLAST,  0);
  xlsubr("ENTSEL",    SUBR,  subENTSEL,   0);
  xlsubr("FIX",       SUBR,  subFIX,      0);
  xlsubr("GETANGLE",  SUBR,  subGETANGLE, 0);
  xlsubr("GETCORNER", SUBR,  subGETCORNER,0);
  xlsubr("GETDIST",   SUBR,  subGETDIST,  0);
  xlsubr("GETINT",    SUBR,  subGETINT,   0);
  xlsubr("GETPOINT",  SUBR,  subGETPOINT, 0);
  xlsubr("GETREAL",   SUBR,  subGETREAL,  0);
  xlsubr("GETSTRING", SUBR,  subGETSTRING,0);
  xlsubr("GETVAR",    SUBR,  subGETVAR,   0);
  xlsubr("GRAPHSCR",  SUBR,  subGRAPHSCR, 0);
  xlsubr("INTERS",    SUBR,  subINTERS,   0);
  xlsubr("ITOA",      SUBR,  subITOA,     0);
  xlsubr("POW",       SUBR,  subPOW,      0);
  xlsubr("POLAR",     SUBR,  subPOLAR,    0);
  xlsubr("PRINC",     SUBR,  subPRINC,    0);
  xlsubr("PROMPT",    SUBR,  subPROMPT,   0);
  xlsubr("RTOS",      SUBR,  subRTOS,     0);
  xlsubr("SETVAR",    SUBR,  subSETVAR,   0);
  xlsubr("STRCASE",   SUBR,  subSTRCASE,  0);
  xlsubr("STRCAT",    SUBR,  subSTRCAT,   0);
  xlsubr("SSADD",     SUBR,  subSSADD,    0);
  xlsubr("SSLENGTH",  SUBR,  subSSLENGTH, 0);
  xlsubr("SSMEMB",    SUBR,  subSSMEMB,   0);
  xlsubr("SSNAME",    SUBR,  subSSNAME,   0);
  xlsubr("STRLEN",    SUBR,  subSTRLEN,   0);
  xlsubr("SUBSTR",    SUBR,  subSUBSTR,   0);
  xlsubr("TEXTSCR",   SUBR,  subTEXTSCR,  0);
  xlsubr("WHILE",     FSUBR, fsubWHILE,   0);
}
