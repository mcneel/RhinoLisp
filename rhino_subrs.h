#pragma once

#ifdef __cplusplus
extern "C" {
#endif
  void RegisterCustomLispFunctions(void);
  void RhinoAppPrint(const char* msg);
  void SetRunningScriptDocument(unsigned int docId);
  void RhinoAppRunScript(const char* command, const char* args);

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


  int rhino_glue_make_layer(const char* name, const char* color);
  int rhino_glue_set_current_layer(const char* name);
  int rhino_glue_add_line(double x1, double y1, double z1, double x2, double y2, double z2);

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
    int  color_idx;        /* AutoCAD color index; 256 = BYLAYER */

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
  } RhinoEntityProps;

  int helperENTSEL(const char* prompt,
                   unsigned int* out_serial,
                   double* out_px, double* out_py, double* out_pz);

  int helperGETENT(unsigned int runtime_sn, RhinoEntityProps* out);

#ifdef __cplusplus
}
#endif
