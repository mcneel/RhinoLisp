// rhino_sdk_glue.cpp - C++ shim between the lisp SUBRs in rhino_subrs.c
// and the Rhino C++ SDK. Each function in here has C linkage so the C
// side can call it directly.
//
// Keep this file thin: the goal is to translate POD types in/out of
// the SDK and keep all the Rhino-SDK includes and types out of the
// XLISP layer.
#include "stdafx.h"
#include "rhino_subrs.h"
#include <string>

static unsigned int g_running_script_document = 0;
extern "C" void SetRunningScriptDocument(unsigned int docId)
{
  g_running_script_document = docId;
}

extern "C" void RhinoAppRunScript(const char* command, const char* args)
{
  ON_wString s = command;
  if (args && args[0] != 0)
  {
    s += L" ";
    s += args;
  }
  RhinoApp().RunScript(g_running_script_document, s.Array());
}


extern "C" void RhinoAppPrint(const char* msg)
{
  ON_wString s = msg;
  RhinoApp().Print(s.Array());

  // Throw a newline in if the input was empty or didn't end in a newline
  if (s.Length() == 0 || s[s.Length() - 1] != L'\n')
    RhinoApp().Print(L"\n");
}

extern "C" int helperGETREAL(const char* prompt, double* value)
{
  if (nullptr == value)
    return FALSE;
  *value = 0.0;

  CRhinoGetNumber gn;
  if (prompt && *prompt)
  {
    ON_wString wPrompt = prompt;
    wPrompt.TrimLeftAndRight();
    gn.SetCommandPrompt(wPrompt);
  }

  if (gn.GetNumber() != CRhinoGet::number)
    return FALSE;

  *value = gn.Number();
  return TRUE;
}

extern "C" int helperGETDIST(const char* prompt, int has_base, double bx, double by, double bz, double* distance)
{
  if (nullptr == distance)
    return FALSE;

  CRhinoGetDistance gd;
  if (prompt && *prompt)
  {
    ON_wString wPrompt = prompt;
    wPrompt.TrimLeftAndRight();
    gd.SetCommandPrompt(wPrompt);
  }

  if (has_base)
  {
    ON_3dPoint base(bx, by, bz);
    gd.SetBasePoint(base);
    // Drawing a rubber-band line is the AutoLISP-default UX.
    gd.DrawLineFromPoint(base, TRUE);
  }

  gd.GetDistance();
  if (gd.CommandResult() != CRhinoCommand::success)
    return FALSE;

  *distance = gd.Distance();
  return TRUE;
}

// Prompt the user to pick a point. If has_base is non-zero, draws a
// rubber-band line from (bx,by,bz). Writes the picked point to the
// out parameters and returns 1 on success, 0 on cancel/escape.
extern "C" int helperGETPOINT(const char* prompt,
  int has_base, double bx, double by, double bz,
  double* out_x, double* out_y, double* out_z)
{
  if (nullptr == out_x || nullptr == out_y || nullptr == out_z)
    return FALSE;

  *out_x = 0.0;
  *out_y = 0.0;
  *out_z = 0.0;

  CRhinoGetPoint gp;
  if (prompt && *prompt)
  {
    ON_wString wPrompt = prompt;
    wPrompt.TrimLeftAndRight();
    gp.SetCommandPrompt(wPrompt);
  }

  if (has_base)
  {
    ON_3dPoint base(bx, by, bz);
    gp.SetBasePoint(base);
    // Drawing a rubber-band line is the AutoLISP-default UX.
    gp.DrawLineFromPoint(base, TRUE);
  }

  if (gp.GetPoint() != CRhinoGet::point)
    return 0;

  ON_3dPoint pt = gp.Point();
  *out_x = pt.x;
  *out_y = pt.y;
  *out_z = pt.z;
  return 1;
}


extern "C" {

// Map an AutoCAD-style color word ("YELLOW", "RED", "1", "2", ...) to
// a 0xRRGGBB ON_Color. Returns 1 on success, 0 if unrecognized.
  static int color_word_to_oncolor(const char* word, ON_Color* out)
  {
    struct { const char* name; unsigned r, g, b; } table[] = {
        { "RED",     255,   0,   0 },
        { "YELLOW",  255, 255,   0 },
        { "GREEN",     0, 255,   0 },
        { "CYAN",      0, 255, 255 },
        { "BLUE",      0,   0, 255 },
        { "MAGENTA", 255,   0, 255 },
        { "WHITE",   255, 255, 255 },
        { "BLACK",     0,   0,   0 },
        { "GRAY",    128, 128, 128 },
        { "GREY",    128, 128, 128 },
    };
    if (!word || !*word) return 0;

    // Uppercase compare.
    char up[32]; int i;
    for (i = 0; i < 31 && word[i]; ++i) {
      char c = word[i];
      up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    up[i] = 0;

    for (size_t k = 0; k < sizeof(table) / sizeof(table[0]); ++k) {
      if (strcmp(up, table[k].name) == 0) {
        out->SetRGB((int)table[k].r, (int)table[k].g, (int)table[k].b);
        return 1;
      }
    }

    // Numeric AutoCAD color index (1..7 mostly): treat 1=red,2=yellow,etc.
    if (word[0] >= '0' && word[0] <= '9') {
      int n = atoi(word);
      switch (n) {
      case 1: out->SetRGB(255, 0, 0); return 1;
      case 2: out->SetRGB(255, 255, 0); return 1;
      case 3: out->SetRGB(0, 255, 0); return 1;
      case 4: out->SetRGB(0, 255, 255); return 1;
      case 5: out->SetRGB(0, 0, 255); return 1;
      case 6: out->SetRGB(255, 0, 255); return 1;
      case 7: out->SetRGB(255, 255, 255); return 1;
      }
    }
    return 0;
  }

// (getvar "CLAYER") - write the current layer's name to `out` (ASCII).
int rhino_glue_getvar_clayer(char* out, int out_cap)
{
  if (!out || out_cap <= 0) return 0;
  out[0] = '\0';

  CRhinoDoc* doc = CRhinoDoc::FromRuntimeSerialNumber(g_running_script_document);
  if (!doc) return 0;

  int idx = doc->m_layer_table.CurrentLayerIndex();
  const CRhinoLayer& layer = doc->m_layer_table[idx];

  ON_String aName(layer.Name());
  int n = aName.Length();
  if (n >= out_cap) n = out_cap - 1;
  if (n > 0) memcpy(out, static_cast<const char*>(aName), n);
  out[n] = '\0';
  return 1;
}

// (setvar "CLAYER" "name") - switch the doc to the named layer
// (creating it if it doesn't exist).
int rhino_glue_setvar_clayer(const char* name)
{
  if (!name || !*name) return 0;

  CRhinoDoc* doc = CRhinoDoc::FromRuntimeSerialNumber(g_running_script_document);
  if (!doc) return 0;

  ON_String aName(name);
  ON_wString wName(aName);

  int idx = doc->m_layer_table.FindLayerFromName(wName, true, true, -1, -2);
  if (idx < 0) {
    // Create it on the fly.
    ON_Layer layer;
    layer.SetName(wName);
    idx = doc->m_layer_table.AddLayer(layer);
    if (idx < 0) return 0;
  }
  return doc->m_layer_table.SetCurrentLayerIndex(idx) ? 1 : 0;
}

// Helper used by (command "LAYER" "M" name "C" color ...).
// Creates the layer (or finds it), optionally sets its color.
int rhino_glue_make_layer(const char* name, const char* color)
{
  if (!name || !*name) return 0;

  CRhinoDoc* doc = CRhinoDoc::FromRuntimeSerialNumber(g_running_script_document);
  if (!doc) return 0;

  ON_String aName(name);
  ON_wString wName(aName);

  int idx = doc->m_layer_table.FindLayerFromName(wName, true, true, -1, -2);
  if (idx < 0) {
    ON_Layer layer;
    layer.SetName(wName);
    if (color && *color) {
      ON_Color c;
      if (color_word_to_oncolor(color, &c))
        layer.SetColor(c);
    }
    idx = doc->m_layer_table.AddLayer(layer);
    return idx >= 0 ? 1 : 0;
  }

  // Existing layer - update the color if one was specified.
  if (color && *color) {
    ON_Color c;
    if (color_word_to_oncolor(color, &c)) {
      ON_Layer mod = doc->m_layer_table[idx];
      mod.SetColor(c);
      doc->m_layer_table.ModifyLayer(mod, idx);
    }
  }
  return 1;
}

int rhino_glue_set_current_layer(const char* name)
{
  return rhino_glue_setvar_clayer(name);
}

// Add a single line segment to the doc on the current layer.
int rhino_glue_add_line(double x1, double y1, double z1,
  double x2, double y2, double z2)
{
  CRhinoDoc* doc = CRhinoDoc::FromRuntimeSerialNumber(g_running_script_document);
  if (!doc) return 0;

  ON_LineCurve lc(ON_3dPoint(x1, y1, z1), ON_3dPoint(x2, y2, z2));
  CRhinoCurveObject* obj = doc->AddCurveObject(lc);
  if (!obj) return 0;

  doc->Redraw();
  return 1;
}

} // extern "C"

// (alert msg) - modal message box
extern "C" void helperALERT(const char* msg)
{
  ON_wString alert = msg;
  alert.TrimLeftAndRight();
  RhinoMessageBox(alert.Array(), L"Alert", MB_OK | MB_ICONINFORMATION);
}

extern "C" int helperGETSTRING(const char* prompt, int allow_spaces, char* out, int out_cap)
{
  if (!out || out_cap <= 0)
    return FALSE;
  out[0] = '\0';

  CRhinoGetString gs;
  ON_wString commandPrompt = prompt;
  commandPrompt.TrimLeftAndRight();
  if (commandPrompt.IsNotEmpty())
  {
    gs.SetCommandPrompt(commandPrompt.Array());
  }

  if (allow_spaces)
    gs.GetLiteralString();
  else
    gs.GetString();

  if (gs.CommandResult() != CRhinoCommand::success)
    return FALSE;

  ON_String aResult = gs.String();
  int n = aResult.Length();
  if (n >= out_cap) n = out_cap - 1;
  if (n > 0) memcpy(out, aResult.Array(), n);
  out[n] = '\0';
  return TRUE;
}
