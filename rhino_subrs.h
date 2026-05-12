#pragma once

#ifdef __cplusplus
extern "C" {
#endif
  void RegisterCustomLispFunctions(void);
  void RhinoAppPrint(const char* msg);
  /* Like RhinoAppPrint but does NOT auto-append a trailing newline.
     Used by subPRINC so (princ "a") (princ "b") prints "ab" on one
     line, matching AutoLISP semantics. */
  void RhinoAppPrintRaw(const char* msg);
  void SetRunningScriptDocument(unsigned int docId);
  void RhinoAppRunScript(const char* command, int argc, const char* const* argv);

  int helperGETDIST(const char* prompt, int has_base, double bx, double by, double bz,
    double* distance);
  int helperGETINT(const char* prompt, int* value);
  int helperGETPOINT(const char* prompt, int has_base, double bx, double by, double bz,
    double* out_x, double* out_y, double* out_z);
  int helperGETREAL(const char* prompt, double* value);
  int helperGETSTRING(const char* prompt, int allow_spaces, char* out, int out_cap);
  void helperALERT(const char* msg);

  int rhino_glue_getvar_clayer(char* out, int out_cap);
  int rhino_glue_setvar_clayer(const char* name);

  int helperGetOSnapMode(int* out_value);
  int helperSetOSnapMode(int value);

  int helpGetEcho(int* out_value);
  int helperSetEcho(int value);

  int helperGetOSnapCoord(int* out_value);
  int helperSetOSnapCoord(int value);

  int helperGetOrthoMode(int* out_value);
  int helperSetOrthoMode(int value);

  int helperGetSnapAng(double* out_value);
  int helperSetSnapAng(double value);

  int helperGetViewTwist(double* out_value);
  int helperSetViewTwist(double value);

  int helperGetAUnits(int* out_value);
  int helperSetAUnits(int value);

  int helperGetAUPrec(int* out_value);
  int helperSetAUPrec(int value);

  int helperGetCEColor(char* out, int out_cap);
  int helperSetCEColor(const char* value);

  /* Returns the runtime serial number of the most-recently-added
     object in the active document. Result is 0 (and the function
     returns 0) when the doc is empty. */
  int helperEntLast(unsigned int* out_serial);

  int helperGETANGLE(const char* prompt,
                     int has_base, double bx, double by, double bz,
                     double* angle);

  int helperIntersectLineLine(double* points, int bounded, double* outX, double* outY, double* outZ);


  /* ---- entity selection / inspection ---------------------------------- */

  /* Snapshot of a Rhino object, mapped to AutoLISP DXF-style data. Lives
     on the lisp side just long enough for subENTGET to weave it into an
     association list. POD-only, so this header is safe to include from
     both C (rhino_subrs.c) and C++ (rhino_sdk_glue.cpp). */
  typedef struct RhinoEntityProps_
  {
    char type[32];         /* DXF type string: "LINE", "CIRCLE", etc. */
    char layer[256];
    char handle[40];       /* object UUID, formatted as a hex string */

    int  has_color;
    int  color_idx;        /* Color index; 256 = BYLAYER */

    int  has_pt10;
    double pt10[3];        /* primary point: start / center / insertion */

    int  has_pt11;
    double pt11[3];        /* secondary point: end (LINE) */

    int  has_radius;
    double radius;         /* CIRCLE/ARC radius (group 40) */

    int  has_angle;
    double angle;          /* rotation in radians (group 50) */

    int  has_flag70;
    int  flag70;           /* misc flags; bit 1 = closed polyline */

    /* INSERT-specific scale factors (group codes 41, 42, 43). */
    int  has_scale;
    double scale[3];
  } RhinoEntityProps;

  int helperENTSEL(const char* prompt,
                   unsigned int* out_serial,
                   double* out_px, double* out_py, double* out_pz);

  int helperGETENT(unsigned int runtime_sn, RhinoEntityProps* out);

#ifdef __cplusplus
}
#endif
