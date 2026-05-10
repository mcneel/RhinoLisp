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
  int helperGETPOINT(const char* prompt, int has_base, double bx, double by, double bz,
    double* out_x, double* out_y, double* out_z);
  int helperGETSTRING(const char* prompt, int allow_spaces, char* out, int out_cap);
  void helperALERT(const char* msg);

  int rhino_glue_getvar_clayer(char* out, int out_cap);
  int rhino_glue_setvar_clayer(const char* name);


  int rhino_glue_make_layer(const char* name, const char* color);
  int rhino_glue_set_current_layer(const char* name);
  int rhino_glue_add_line(double x1, double y1, double z1, double x2, double y2, double z2);

#ifdef __cplusplus
}
#endif
