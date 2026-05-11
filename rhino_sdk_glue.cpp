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

#undef max

static void CopyToString(char* dst, int cap, const char* src)
{
  if (!dst || cap <= 0) return;
  memset(dst, 0, cap * sizeof(char));
  if (src)
  {
    int length = std::max(ON_String::Length(src), cap - 1);
    memcpy(dst, src, length);
  }
}

// Map a named color to an ON_Color
static bool ColorFromString(const char* text, ON_Color& out)
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
      { "1",       255,   0,   0 },
      { "2",       255, 255,   0 },
      { "3",         0, 255,   0 },
      { "4",         0, 255, 255 },
      { "5",         0,   0, 255 },
      { "6",       255,   0, 255 },
      { "7",       255, 255, 255 }
  };
  if (nullptr==text)
    return false;

  ON_String colorName = text;
  for (size_t i = 0; i < sizeof(table); i++)
  {
    if (colorName.EqualOrdinal(table[i].name, true))
    {
      out.SetRGB((int)table[i].r, (int)table[i].g, (int)table[i].b);
      return true;
    }
  }

  return false;
}

// Attempt to convert an ON_Color to an acad color index for the standard 7
// color. Anything else falls through to BYLAYER (256).
static int ColorIndexFromON_Color(const ON_Color& c)
{
  int r = c.Red(), g = c.Green(), b = c.Blue();
  if (r == 255 && g == 0 && b == 0)   return 1;
  if (r == 255 && g == 255 && b == 0)   return 2;
  if (r == 0 && g == 255 && b == 0)   return 3;
  if (r == 0 && g == 255 && b == 255) return 4;
  if (r == 0 && g == 0 && b == 255) return 5;
  if (r == 255 && g == 0 && b == 255) return 6;
  if (r == 255 && g == 255 && b == 255) return 7;
  if (r == 0 && g == 0 && b == 0)   return 7;
  return 256;
}


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

extern "C" void helperALERT(const char* msg)
{
  ON_wString alert = msg;
  alert.TrimLeftAndRight();
  RhinoMessageBox(alert.Array(), L"Alert", MB_OK | MB_ICONINFORMATION);
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

extern "C" int helperGETINT(const char* prompt, int* value)
{
  if (nullptr == value)
    return FALSE;
  *value = 0;

  CRhinoGetInteger gi;
  if (prompt && *prompt)
  {
    ON_wString wPrompt = prompt;
    wPrompt.TrimLeftAndRight();
    gi.SetCommandPrompt(wPrompt);
  }

  if (gi.GetInteger() != CRhinoGet::number)
    return FALSE;

  *value = gi.Number();
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

// (getvar "CLAYER") - write the current layer's name to `out` (ASCII).
extern "C" int rhino_glue_getvar_clayer(char* out, int out_cap)
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
extern "C" int rhino_glue_setvar_clayer(const char* name)
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

// ---------------------------------------------------------------------
// OSMODE / CMDECHO - shadowed system variables.
//
// AutoLISP scripts typically use these in the save-modify-restore
// idiom:  (setq old (getvar "OSMODE")) ... (setvar "OSMODE" 0) ...
// (setvar "OSMODE" old). For that to work we only have to round-trip
// the value; we do NOT have to actually flip Rhino's running osnaps
// or mute the command echo to unblock the scripts. Today these are
// pure shadow variables - their values persist for the Rhino session
// but don't drive any real Rhino state.
//
// Upgrade paths:
//   - OSMODE: bits 1..16384 map to Rhino's CRhinoAppSettings osnap
//     toggles. A future revision can apply the bits on each setvar.
//   - CMDECHO: when 0, RhinoAppRunScript() above could call the
//     echo-mode-suppressed overload of RhinoApp().RunScript() in
//     Rhino 6+ SDKs.
// ---------------------------------------------------------------------
static int g_osmode  = 0;   /* AutoCAD default: no running snaps */
static int g_cmdecho = 1;   /* AutoCAD default: echo commands */

extern "C" int helperGetOSnapMode(int* out_value)
{
  if (!out_value) return 0;
  *out_value = g_osmode;
  return 1;
}

extern "C" int helperSetOSnapMode(int value)
{
  g_osmode = value;
  return 1;
}

extern "C" int helpGetEcho(int* out_value)
{
  if (!out_value) return 0;
  *out_value = g_cmdecho;
  return 1;
}

extern "C" int helperSetEcho(int value)
{
  g_cmdecho = value;
  return 1;
}

// ---------------------------------------------------------------------
// Additional shadow system variables.
//
// These follow the same shadow pattern as OSMODE/CMDECHO above - the
// values round-trip through getvar/setvar but don't currently drive
// any real Rhino state. They unblock the save-modify-restore idiom
// that AutoLISP scripts use pervasively.
//
// SNAPANG and VIEWTWIST are angle quantities in radians, so they're
// doubles. AUPREC defaults to 4 (matching the AutoCAD default precision
// for angle formatting in ANGTOS). AUNITS defaults to 0 (decimal
// degrees).
// ---------------------------------------------------------------------
static int    g_osnapcoord = 1;     /* 0=use osnaps, 1=use typed coords */
static int    g_orthomode  = 0;     /* 0=ortho off, 1=on               */
static double g_snapang    = 0.0;   /* snap rotation, radians          */
static double g_viewtwist  = 0.0;   /* view twist, radians (read-ish)  */
static int    g_aunits     = 0;     /* 0=decimal deg, 1=DMS, 2=grad...  */
static int    g_auprec     = 4;     /* angle precision (digits)         */

extern "C" int helperGetOSnapCoord(int* out_value) {
  if (!out_value) return 0;
  *out_value = g_osnapcoord;
  return 1;
}
extern "C" int helperSetOSnapCoord(int value) {
  g_osnapcoord = value;
  return 1;
}

extern "C" int helperGetOrthoMode(int* out_value) {
  if (!out_value) return 0;
  *out_value = g_orthomode;
  return 1;
}
extern "C" int helperSetOrthoMode(int value) {
  g_orthomode = value;
  return 1;
}

extern "C" int helperGetSnapAng(double* out_value) {
  if (!out_value) return 0;
  *out_value = g_snapang;
  return 1;
}
extern "C" int helperSetSnapAng(double value) {
  g_snapang = value;
  return 1;
}

extern "C" int helperGetViewTwist(double* out_value) {
  if (!out_value) return 0;
  *out_value = g_viewtwist;
  return 1;
}
extern "C" int helperSetViewTwist(double value) {
  g_viewtwist = value;
  return 1;
}

extern "C" int helperGetAUnits(int* out_value) {
  if (!out_value) return 0;
  *out_value = g_aunits;
  return 1;
}
extern "C" int helperSetAUnits(int value) {
  g_aunits = value;
  return 1;
}

extern "C" int helperGetAUPrec(int* out_value) {
  if (!out_value) return 0;
  *out_value = g_auprec;
  return 1;
}
extern "C" int helperSetAUPrec(int value) {
  g_auprec = value;
  return 1;
}

// ---------------------------------------------------------------------
// (getangle [pt] [msg]) -> angle in radians, or NIL on cancel.
//
// CRhinoGetAngle accepts either a typed number (interpreted per the
// document's angle-input mode) or two clicked points; in both cases
// it returns the angle in radians. The optional base point feeds the
// "pick a second point relative to this base" UX, matching how
// AutoLISP's getangle works when given the optional pt argument.
// ---------------------------------------------------------------------
extern "C" int helperGETANGLE(const char* prompt,
                              int has_base, double bx, double by, double bz,
                              double* angle)
{
  if (nullptr == angle) return FALSE;
  *angle = 0.0;

  CRhinoGetAngle ga;
  if (prompt && *prompt)
  {
    ON_wString p = prompt;
    p.TrimLeftAndRight();
    ga.SetCommandPrompt(p);
  }
  if (has_base)
  {
    ga.SetBase(ON_3dPoint(bx, by, bz));
  }

  if (ga.GetAngle() != CRhinoGet::angle)
    return FALSE;

  *angle = ga.Angle();
  return TRUE;
}

extern "C" int helperIntersectLineLine(double* points, int bounded, double* outX, double* outY, double* outZ)
{
  if (nullptr == points || nullptr == outX || nullptr == outY || nullptr == outZ)
    return FALSE;
  ON_Line line1(ON_3dPoint(points[0], points[1], points[2]), ON_3dPoint(points[3], points[4], points[5]));
  ON_Line line2(ON_3dPoint(points[6], points[7], points[8]), ON_3dPoint(points[9], points[10], points[11]));
  double a = 0;
  double b = 0;
  if (!ON_Intersect(line1, line2, &a, &b))
    return FALSE;

  if (bounded && (a < 0 || a>1))
    return FALSE;

  ON_3dPoint rc = line1.PointAt(a);
  *outX = rc.x;
  *outY = rc.y;
  *outZ = rc.z;
  return TRUE;
}


// Helper used by (command "LAYER" "M" name "C" color ...).
// Creates the layer (or finds it), optionally sets its color.
extern "C" int rhino_glue_make_layer(const char* name, const char* color)
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
      if (ColorFromString(color, c))
        layer.SetColor(c);
    }
    idx = doc->m_layer_table.AddLayer(layer);
    return idx >= 0 ? 1 : 0;
  }

  // Existing layer - update the color if one was specified.
  if (color && *color) {
    ON_Color c;
    if (ColorFromString(color, c)) {
      ON_Layer mod = doc->m_layer_table[idx];
      mod.SetColor(c);
      doc->m_layer_table.ModifyLayer(mod, idx);
    }
  }
  return 1;
}

extern "C" int rhino_glue_set_current_layer(const char* name)
{
  return rhino_glue_setvar_clayer(name);
}

// Add a single line segment to the doc on the current layer.
extern "C" int rhino_glue_add_line(double x1, double y1, double z1, double x2, double y2, double z2)
{
  CRhinoDoc* doc = CRhinoDoc::FromRuntimeSerialNumber(g_running_script_document);
  if (!doc)
    return 0;

  ON_Line line(ON_3dPoint(x1, y1, z1), ON_3dPoint(x2, y2, z2));
  CRhinoCurveObject* obj = doc->AddCurveObject(line);
  if (!obj)
    return 0;

  doc->Redraw();
  return 1;
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

// ---------------------------------------------------------------------
// Entity-name / DXF mapping.
//
// AutoLISP scripts treat the document as a flat collection of typed
// "entities" exposed through (entsel) + (entget). entget returns an
// association list whose elements are (group-code . value), and the
// group-code numbers come from the AutoCAD DXF reference.
//
// Rhino has no DXF model internally, so we synthesize the assoc list
// from CRhinoObject + ON_Geometry. The mapping is intentionally narrow
// - just the codes most scripts actually look up: 0 (type), 5 (handle),
// 8 (layer), 10/11 (points), 40 (radius), 50 (angle), 62 (color),
// 70 (flags). Scripts that walk obscure group codes will see them as
// absent and will need extension here.
// ---------------------------------------------------------------------

// Fill type-string + per-type fields from an ON_Geometry. Returns
// quietly with type="ENTITY" if we don't recognize the geometry.
static void fill_geometry_props(const ON_Geometry* geom, RhinoEntityProps* out)
{
  if (!geom)
  {
    CopyToString(out->type, sizeof(out->type), "ENTITY");
    return;
  }

  // Curves: line, circle/arc, polyline, generic NURBS.
  if (const ON_Curve* curve = ON_Curve::Cast(geom))
  {
    if (const ON_LineCurve* lc = ON_LineCurve::Cast(curve))
    {
      CopyToString(out->type, sizeof(out->type), "LINE");
      out->has_pt10 = 1;
      out->pt10[0] = lc->m_line.from.x;
      out->pt10[1] = lc->m_line.from.y;
      out->pt10[2] = lc->m_line.from.z;
      out->has_pt11 = 1;
      out->pt11[0] = lc->m_line.to.x;
      out->pt11[1] = lc->m_line.to.y;
      out->pt11[2] = lc->m_line.to.z;
      return;
    }
    if (const ON_ArcCurve* ac = ON_ArcCurve::Cast(curve))
    {
      const bool is_circle = ac->IsCircle();
      CopyToString(out->type, sizeof(out->type), is_circle ? "CIRCLE" : "ARC");
      ON_3dPoint c = ac->m_arc.Center();
      out->has_pt10  = 1;
      out->pt10[0]   = c.x;
      out->pt10[1]   = c.y;
      out->pt10[2]   = c.z;
      out->has_radius = 1;
      out->radius     = ac->m_arc.Radius();
      if (!is_circle) {
        // AutoCAD ARC group 50 is the start angle in radians.
        out->has_angle = 1;
        out->angle     = ac->m_arc.AngleRadians();
      }
      return;
    }
    if (const ON_PolylineCurve* pl = ON_PolylineCurve::Cast(curve))
    {
      CopyToString(out->type, sizeof(out->type), "LWPOLYLINE");
      if (pl->PointCount() > 0) {
        ON_3dPoint p0 = pl->PointAt(0.0);
        out->has_pt10 = 1;
        out->pt10[0] = p0.x;
        out->pt10[1] = p0.y;
        out->pt10[2] = p0.z;
      }
      out->has_flag70 = 1;
      out->flag70     = pl->IsClosed() ? 1 : 0;
      return;
    }
    // Generic curve - report it as a spline so scripts at least see
    // a curve-shaped entity.
    CopyToString(out->type, sizeof(out->type), "SPLINE");
    ON_3dPoint p0 = curve->PointAtStart();
    out->has_pt10 = 1;
    out->pt10[0] = p0.x;
    out->pt10[1] = p0.y;
    out->pt10[2] = p0.z;
    return;
  }

  // Points.
  if (const ON_Point* pt = ON_Point::Cast(geom))
  {
    CopyToString(out->type, sizeof(out->type), "POINT");
    out->has_pt10 = 1;
    out->pt10[0]  = pt->point.x;
    out->pt10[1]  = pt->point.y;
    out->pt10[2]  = pt->point.z;
    return;
  }

  // Higher-level types we recognize by family but don't decompose.
  if (ON_Brep::Cast(geom))       { CopyToString(out->type, sizeof(out->type), "BREP");    return; }
  if (ON_Surface::Cast(geom))    { CopyToString(out->type, sizeof(out->type), "SURFACE"); return; }
  if (ON_Mesh::Cast(geom))       { CopyToString(out->type, sizeof(out->type), "MESH");    return; }

  CopyToString(out->type, sizeof(out->type), "ENTITY");
}

extern "C" int helperENTSEL(const char* prompt,
                            unsigned int* out_sn,
                            double* out_px, double* out_py, double* out_pz)
{
  if (!out_sn || !out_px || !out_py || !out_pz)
    return FALSE;
  *out_sn = 0;
  *out_px = *out_py = *out_pz = 0.0;

  CRhinoGetObject go;
  if (prompt && *prompt)
  {
    ON_wString wPrompt = prompt;
    wPrompt.TrimLeftAndRight();
    go.SetCommandPrompt(wPrompt);
  }

  go.EnableSubObjectSelect(FALSE);
  go.EnableDeselectAllBeforePostSelect(FALSE);

  if (go.GetObjects(1, 1) != CRhinoGet::object)
    return FALSE;

  const CRhinoObjRef& oref = go.Object(0);
  const CRhinoObject* obj  = oref.Object();
  if (!obj)
    return FALSE;

  *out_sn = (unsigned int)obj->RuntimeSerialNumber();

  ON_3dPoint pt;
  if (oref.SelectionPoint(pt) && pt.IsValid())
  {
    *out_px = pt.x;
    *out_py = pt.y;
    *out_pz = pt.z;
  }
  return TRUE;
}

extern "C" int helperGETENT(unsigned int sn, RhinoEntityProps* out)
{
  if (!out) return FALSE;
  memset(out, 0, sizeof(*out));

  CRhinoDoc* doc = CRhinoDoc::FromRuntimeSerialNumber(g_running_script_document);
  const CRhinoObject* obj = CRhinoObject::FromRuntimeSerialNumber(g_running_script_document, sn);
  if (nullptr==doc || nullptr==obj)
    return FALSE;

  // Layer (DXF group 8).
  {
    const CRhinoObjectAttributes& attrs = obj->Attributes();
    int layer_idx = attrs.m_layer_index;
    if (layer_idx >= 0 && layer_idx < doc->m_layer_table.LayerCount())
    {
      const CRhinoLayer& layer = doc->m_layer_table[layer_idx];
      ON_String aName(layer.Name());
      CopyToString(out->layer, sizeof(out->layer), static_cast<const char*>(aName));
    }
  }

  // Handle (DXF group 5) - object UUID as a compact hex string.
  {
    ON_UUID id = obj->Id();
    ON_String s;
    ON_UuidToString(id, s);
    CopyToString(out->handle, sizeof(out->handle), static_cast<const char*>(s));
  }

  // Color (DXF group 62). BYLAYER if the object inherits its color.
  {
    const CRhinoObjectAttributes& attrs = obj->Attributes();
    if (attrs.ColorSource() == ON::color_from_layer)
    {
      out->has_color = 1;
      out->color_idx = 256;  // BYLAYER
    }
    else
    {
      out->has_color = 1;
      out->color_idx = ColorIndexFromON_Color(attrs.m_color);
    }
  }

  // Type-specific geometry data (0, 10, 11, 40, 50, 70).
  fill_geometry_props(obj->Geometry(), out);

  return TRUE;
}
