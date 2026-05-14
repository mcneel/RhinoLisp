#include "stdafx.h"
#include "mock_commands.h"
#include "rhino_subrs.h"
#include <cstdio>
#include <cstring>
#include <io.h>     // _waccess

// Directory of the .lsp file currently being evaluated. Set by
// cmdRhinoLisp before each script run. MockCommands::Insert consults
// this when a block name doesn't resolve in the active doc - it looks
// for a .dwg of the same name in this directory.
static ON_wString g_script_directory;

void MockCommands::SetScriptDirectory(const wchar_t* dir)
{
  g_script_directory = dir ? dir : L"";
}

static bool EqIgnoreCase(const char* a, const char* b)
{
  ON_String strA = a;
  return strA.EqualOrdinal(b, true);
}

static void CopyToken(char* dst, int cap, const char* src)
{
  if (!dst || cap <= 0) return;
  int n = 0;
  if (src) { for (; n < cap - 1 && src[n]; ++n) dst[n] = src[n]; }
  dst[n] = '\0';
}

// Attempt to import "<block_name>.dwg" from the script's directory as a
// new instance definition. Uses Rhino's command-line -Insert with the
// _File option, which is the most-portable cross-SDK way to load a
// .dwg as a block. _-Insert also drops an instance at the origin as a
// side effect; we capture the doc's highest runtime serial number
// before the call and delete whatever's newer afterwards.
//
// Returns true if the definition is in the table at function exit.
static bool TryLoadBlockFromDisk(CRhinoDoc* doc, unsigned int docId,
                                 const ON_wString& block_name)
{
  if (!doc || block_name.IsEmpty()) return false;
  if (g_script_directory.IsEmpty()) return false;

  // Build "<dir>\<name>.dwg".
  ON_wString file_path = g_script_directory;
  if (file_path.Length() > 0)
  {
    wchar_t last = file_path[file_path.Length() - 1];
    if (last != L'\\' && last != L'/')
      file_path += L"\\";
  }
  file_path += block_name;
  file_path += L".dwg";

  if (_waccess(file_path.Array(), 0) != 0)
    return false;   // no such file

  // Snapshot the doc's highest runtime serial number so we can find
  // and delete the spurious instance that -Insert will place at origin.
  unsigned int sn_before = 0;
  {
    CRhinoObjectIterator it(*doc, CRhinoObjectIterator::normal_objects);
    for (const CRhinoObject* o = it.First(); o; o = it.Next())
    {
      unsigned int s = o->RuntimeSerialNumber();
      if (s > sn_before) sn_before = s;
    }
  }

  // Drive Rhino's -Insert via the file path. The trailing tokens accept
  // defaults at each prompt (0,0,0 / scale 1 / rotation 0).
  ON_wString cmd;
  cmd.Format(L"_-Insert _File \"%s\" Block Enter 0,0,0 1 1 ", file_path.Array());
  RhinoApp().RunScript(docId, cmd.Array(), 1);

  // Delete the placeholder instance(s) introduced above.
  ON_SimpleArray<const CRhinoObject*> to_delete;
  {
    CRhinoObjectIterator it(*doc, CRhinoObjectIterator::normal_objects);
    for (const CRhinoObject* o = it.First(); o; o = it.Next())
    {
      if (o->RuntimeSerialNumber() > sn_before)
        to_delete.Append(o);
    }
  }
  for (int i = 0; i < to_delete.Count(); ++i)
  {
    doc->DeleteObject(CRhinoObjRef(to_delete[i]));
  }

  // Verify the definition is now resolvable.
  return doc->m_instance_definition_table.FindInstanceDefinition(block_name) >= 0;
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
  if (nullptr == text)
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

// (command "LAYER" "M" name "C" color "" "")
//   M / MAKE  -> next non-empty token is layer name
//   C / COLOR -> next non-empty token is color, then optionally a
//                target-layer name (used as layer_name if we don't
//                have one yet)
//   S / SET   -> next non-empty token is layer name to make current
//   Empty string -> "Enter" (skip)
//   ON/OFF/F/T/L/U/P/? -> recognized but unimplemented; the following
//                         token (the target name) is consumed-and-ignored
void MockCommands::Layer(unsigned int docId, int argc, const char* const* argv)
{
  char layer_name[256] = "";
  char color[64]       = "";

  for (int i = 0; i < argc; ++i)
  {
    const char* tok = argv[i];
    if (tok[0] == '\0') continue;

    if (EqIgnoreCase(tok, "M") || EqIgnoreCase(tok, "MAKE"))
    {
      if (++i >= argc) break;
      CopyToken(layer_name, sizeof(layer_name), argv[i]);
    }
    else if (EqIgnoreCase(tok, "C") || EqIgnoreCase(tok, "COLOR"))
    {
      if (++i >= argc) break;
      CopyToken(color, sizeof(color), argv[i]);
      // WHICH layer to color; eat the next non-empty token as that target.
      if (i + 1 < argc && argv[i + 1][0] != '\0')
      {
        ++i;
        if (layer_name[0] == '\0')
          CopyToken(layer_name, sizeof(layer_name), argv[i]);
      }
    }
    else if (EqIgnoreCase(tok, "S") || EqIgnoreCase(tok, "SET"))
    {
      if (++i >= argc) break;
      CopyToken(layer_name, sizeof(layer_name), argv[i]);
    }
    else if (EqIgnoreCase(tok, "ON")  || EqIgnoreCase(tok, "OFF") ||
             EqIgnoreCase(tok, "F")   || EqIgnoreCase(tok, "T")   ||
             EqIgnoreCase(tok, "L")   || EqIgnoreCase(tok, "U")   ||
             EqIgnoreCase(tok, "P")   || EqIgnoreCase(tok, "?"))
    {
      // Subcommands we don't implement - skip the next arg.
      if (++i >= argc) break;
    }
    // anything else: ignore (AutoCAD would re-prompt)
  }

  const bool makeActive = true;

  ON_wString wName = layer_name;
  if (wName.IsEmpty())
    return;

  CRhinoDoc* doc = CRhinoDoc::FromRuntimeSerialNumber(docId);
  if (!doc)
    return;

  int idx = doc->m_layer_table.FindLayerFromName(wName, true, true, -1, -2);
  if (idx < 0)
  {
    ON_Layer layer;
    layer.SetName(wName);
    if (color && *color)
    {
      ON_Color c;
      if (ColorFromString(color, c))
        layer.SetColor(c);
    }
    idx = doc->m_layer_table.AddLayer(layer);
    if (idx >= 0 && makeActive)
      doc->m_layer_table.SetCurrentLayerIndex(idx);
  }

  // Existing layer - update the color if one was specified.
  if (color && *color)
  {
    ON_Color c;
    if (ColorFromString(color, c))
    {
      ON_Layer mod = doc->m_layer_table[idx];
      mod.SetColor(c);
      doc->m_layer_table.ModifyLayer(mod, idx);
    }
  }

  if (idx >= 0 && makeActive)
    doc->m_layer_table.SetCurrentLayerIndex(idx);
}


// (command "LINE" pt pt pt ... ["c"|""])
//   "x,y,z" token -> next polyline vertex
//   "C" / "CLOSE" -> close the polyline and stop reading
//   Empty string  -> "Enter": stop reading
void MockCommands::Line(unsigned int docId, int argc, const char* const* argv)
{
  const int kMaxPts = 256;
  double pts[kMaxPts][3];
  int    npts   = 0;
  int    closed = 0;

  for (int i = 0; i < argc; ++i)
  {
    const char* tok = argv[i];

    // A point token always contains commas (we encode them that way
    //        in fsubCOMMAND), so a quick char check avoids confusing
    //        "0,0,0" with the close keyword "C".
    if (strchr(tok, ',') != nullptr)
    {
      double x, y, z;
      if (sscanf(tok, "%lf,%lf,%lf", &x, &y, &z) == 3)
      {
        if (npts >= kMaxPts) break;
        pts[npts][0] = x; pts[npts][1] = y; pts[npts][2] = z;
        ++npts;
        continue;
      }
    }

    if (tok[0] == '\0') break;  // Enter -> stop
    if (EqIgnoreCase(tok, "C") || EqIgnoreCase(tok, "CLOSE"))
    {
      closed = 1;
      break;
    }
    // unknown subcommand: ignore
  }

  CRhinoDoc* doc = CRhinoDoc::FromRuntimeSerialNumber(docId);
  if (npts < 2 || nullptr == doc)
    return;

  for (int i = 0; i + 1 < npts; ++i)
  {
    ON_3dPoint pt0(pts[i][0], pts[i][1], pts[i][2]);
    ON_3dPoint pt1(pts[i+1][0], pts[i+1][1], pts[i+1][2]);
    ON_Line line(pt0, pt1);
    doc->AddCurveObject(line);
  }
  if (closed && npts >= 3)
  {
    ON_3dPoint pt0(pts[npts-1][0], pts[npts-1][1], pts[npts-1][2]);
    ON_3dPoint pt1(pts[0][0], pts[0][1], pts[0][2]);
    ON_Line line(pt0, pt1);
    doc->AddCurveObject(line);
  }
  doc->Redraw();
}

// (command "INSERT" name insertion_point xscale yscale rotation)
//
// "PAUSE" is honored at the insertion-point and rotation slots: it
// suspends parsing and prompts the user via CRhinoGetPoint /
// CRhinoGetAngle, matching what AutoLISP's pause marker normally does
// inside (command ...). At scale slots we treat PAUSE as "use the
// running default" since prompting for a typed scale-factor is rare in
// scripts.
void MockCommands::Insert(unsigned int docId, int argc, const char* const* argv)
{
  char   block_name[256] = "";
  double pt[3]           = { 0.0, 0.0, 0.0 };
  double xscale          = 1.0;
  double yscale          = 1.0;
  double rotation        = 0.0;

  bool have_name  = false;
  bool have_point = false;

  enum { SLOT_NAME, SLOT_POINT, SLOT_XSCALE, SLOT_YSCALE, SLOT_ROTATION, SLOT_DONE };
  int slot = SLOT_NAME;

  for (int i = 0; i < argc && slot != SLOT_DONE; ++i)
  {
    const char* tok = argv[i];
    if (tok[0] == '\0') continue;                  // Enter -> skip

    const bool is_pause = EqIgnoreCase(tok, "PAUSE");

    switch (slot)
    {
    case SLOT_NAME:
      if (!is_pause)
      {
        CopyToken(block_name, sizeof(block_name), tok);
        have_name = true;
      }
      slot = SLOT_POINT;
      break;

    case SLOT_POINT:
      if (is_pause)
      {
        if (helperGETPOINT("Insertion point", 0, 0.0, 0.0, 0.0,
                           &pt[0], &pt[1], &pt[2]))
          have_point = true;
      }
      else if (sscanf(tok, "%lf,%lf,%lf", &pt[0], &pt[1], &pt[2]) == 3)
      {
        have_point = true;
      }
      slot = SLOT_XSCALE;
      break;

    case SLOT_XSCALE:
    {
      if (!is_pause)
      {
        double v;
        if (sscanf(tok, "%lf", &v) == 1) xscale = v;
      }
      yscale = xscale;   // AutoCAD: Y defaults to X
      slot = SLOT_YSCALE;
      break;
    }

    case SLOT_YSCALE:
    {
      if (!is_pause)
      {
        double v;
        if (sscanf(tok, "%lf", &v) == 1) yscale = v;
      }
      slot = SLOT_ROTATION;
      break;
    }

    case SLOT_ROTATION:
      if (is_pause)
      {
        helperGETANGLE("Rotation angle",
                       have_point ? 1 : 0,
                       pt[0], pt[1], pt[2],
                       &rotation);
      }
      else
      {
        double v;
        if (sscanf(tok, "%lf", &v) == 1) rotation = v;
      }
      slot = SLOT_DONE;
      break;
    }
  }

  if (!have_name || !have_point) return;

  CRhinoDoc* doc = CRhinoDoc::FromRuntimeSerialNumber(docId);
  if (!doc) return;

  ON_wString wName = block_name;
  int idef_idx = doc->m_instance_definition_table.FindInstanceDefinition(wName);
  if (idef_idx < 0)
  {
    // Not in the doc's block table - try to load it from a .dwg in the
    // script's directory (AutoCAD's INSERT does the same as a fallback).
    if (TryLoadBlockFromDisk(doc, docId, wName))
      idef_idx = doc->m_instance_definition_table.FindInstanceDefinition(wName);

    if (idef_idx < 0)
    {
      RhinoApp().Print(L"INSERT: block '%S' not found in document or script directory.\n",
                       wName.Array());
      return;
    }
  }

  // Compose the placement transform: translate * rotate-Z * scale.
  //      The order matters - we want to scale the block first (around its
  //      own origin), then rotate, then translate to the insertion point.
  ON_Xform xfScale = ON_Xform::IdentityTransformation;
  xfScale[0][0] = xscale;
  xfScale[1][1] = yscale;
  xfScale[2][2] = 1.0;

  ON_Xform xfRot;
  xfRot.Rotation(rotation, ON_3dVector(0.0, 0.0, 1.0), ON_3dPoint(0.0, 0.0, 0.0));

  ON_Xform xfTrans = ON_Xform::TranslationTransformation(pt[0], pt[1], pt[2]);

  ON_Xform xf = xfTrans * xfRot * xfScale;

  doc->m_instance_definition_table.AddInstanceObject(idef_idx, xf);
  doc->Redraw();
}

// (command "COLOR" value)
//
// AutoCAD's COLOR command sets the current-entity color for newly-
// created objects. AutoLISP scripts pair it with (getvar "CECOLOR"):
//     (setq old (getvar "cecolor"))     ; remember
//     (command "color" 1)               ; set red
//     ... draw stuff ...
//     (command "color" old)             ; restore
//
// We route both halves through the same CECOLOR shadow so the round-
// trip is consistent. Value tokens may be a number ("1".."256"), a
// named color ("RED", "BYLAYER", "BYBLOCK"), or anything else - we
// just store the string. If the script later queries CECOLOR, it gets
// back exactly what was passed in.
//
// Driving Rhino's actual per-object color attribute from this would
// require translating the AutoCAD color index into an ON_Color and
// setting doc->m_default_object_attributes.m_color, plus the matching
// ColorSource flag. Doable as a future extension; for now the shadow
// is enough to unblock scripts that just want the save/restore idiom.
void MockCommands::Color(unsigned int docId, int argc, const char* const* argv)
{
  (void)docId;

  // First non-empty token wins. Empty/Enter tokens are skipped to
  // match AutoCAD's prompt behavior (Enter accepts the running value).
  for (int i = 0; i < argc; ++i)
  {
    const char* tok = argv[i];
    if (tok && tok[0] != '\0')
    {
      helperSetCEColor(tok);
      return;
    }
  }
}

// (command "OFFSET" distance pt_on_source pt_on_side "")
//
// OFFSET prompts for a distance, then "select object" (the
// user clicks ON the source curve), then "side to offset" (clicks the
// side). AutoLISP scripts feed those three slots as a number and two
// point lists.
//
// We translate this to the Rhino SDK directly: find the curve nearest
// pt_on_source, call ON_Curve::Offset() with pt_on_side as the side
// indicator and distance as the magnitude, then add the result to the
// doc. The new curve becomes the doc's most-recent object, so
// subsequent (entlast) in the script picks it up.
//
// Tolerance comes from the document settings. Curves further than a
// few model-tolerances from pt_on_source are ignored (a click in
// empty space shouldn't offset a random distant curve).
void MockCommands::Offset(unsigned int docId, int argc, const char* const* argv)
{
  double     distance = 0.0;
  ON_3dPoint src_pt(0.0, 0.0, 0.0);
  ON_3dPoint side_pt(0.0, 0.0, 0.0);
  bool       have_distance = false;
  bool       have_src      = false;
  bool       have_side     = false;

  // Positional walk. Comma-bearing tokens are points (encoded by
  // fsubCOMMAND as "x,y,z"); the first non-point non-empty token is
  // the distance. Empty tokens are Enter-markers and skipped.
  for (int i = 0; i < argc; ++i)
  {
    const char* tok = argv[i];
    if (!tok || !*tok) continue;

    double x, y, z;
    if (strchr(tok, ',') && sscanf(tok, "%lf,%lf,%lf", &x, &y, &z) == 3)
    {
      if (!have_src)       { src_pt.Set(x, y, z);  have_src  = true; }
      else if (!have_side) { side_pt.Set(x, y, z); have_side = true; }
    }
    else if (!have_distance)
    {
      double d;
      if (sscanf(tok, "%lf", &d) == 1)
      {
        distance      = d;
        have_distance = true;
      }
    }
  }

  if (!have_distance || !have_src || !have_side) return;

  // AutoCAD-style: distance is magnitude, side determines direction.
  if (distance < 0.0) distance = -distance;

  CRhinoDoc* doc = CRhinoDoc::FromRuntimeSerialNumber(docId);
  if (!doc) return;

  // Find the curve nearest src_pt. ON_Curve::ClosestPoint gives us
  // both the parameter and (via PointAt) the foot of the perpendicular;
  // we measure that foot's distance to src_pt to score candidates.
  const ON_Curve* src_curve = nullptr;
  double          best_dist = 1.0e300;
  {
    CRhinoObjectIterator it(*doc, CRhinoObjectIterator::normal_objects);
    for (const CRhinoObject* o = it.First(); o; o = it.Next())
    {
      const CRhinoCurveObject* co = CRhinoCurveObject::Cast(o);
      if (!co) continue;
      const ON_Curve* c = co->Curve();
      if (!c) continue;
      double t;
      if (!c->GetClosestPoint(src_pt, &t)) continue;
      ON_3dPoint foot = c->PointAt(t);
      double     d    = foot.DistanceTo(src_pt);
      if (d < best_dist)
      {
        best_dist = d;
        src_curve = c;
      }
    }
  }
  if (!src_curve)
    return;

  // Skip if the nearest curve is implausibly far. Threshold is a few
  // model-tolerances; the source point in AutoLISP scripts is almost
  // always exactly on the curve (it's typically a midpoint we just
  // computed), so even a tight bound catches the right curve.
  double tol = doc->AbsoluteTolerance();
  if (tol <= 0.0) tol = 0.001;
  if (best_dist > tol * 100.0) return;

  // RhinoOffsetCurve handles a wider set of curve types than the
  // virtual ON_Curve::Offset and can yield multiple disconnected
  // result pieces (e.g. when a polyline's offset self-intersects and
  // needs trimming). corner_style 1 = Sharp, matching AutoCAD's
  // default OFFSET behavior. The caller owns the returned curves and
  // is responsible for freeing them.
  ON_SimpleArray<ON_Curve*> offset_curves;
  bool ok = RhinoOffsetCurve(
      *src_curve,
      distance,
      side_pt,
      ON_3dVector(0.0, 0.0, 1.0),   // XY-plane offset
      1,                             // corner_style: Sharp
      tol,
      offset_curves);

  if (ok)
  {
    for (int i = 0; i < offset_curves.Count(); ++i)
    {
      if (offset_curves[i])
        doc->AddCurveObject(*offset_curves[i]);
    }
    doc->Redraw();
  }

  // Free regardless of success - RhinoOffsetCurve may have allocated
  // partial results even on failure.
  for (int i = 0; i < offset_curves.Count(); ++i)
    delete offset_curves[i];
}

// (command "TRIM" <cutting-set> "" <trim-pt1> <trim-pt2> ... "")
//
// AutoCAD's TRIM flow: select cutting edges (Enter to finish), then
// click each curve segment to trim away (Enter to finish). AutoLISP
// scripts pass cutting edges via a selection set in slot 1; fsubCOMMAND
// expands the set into individual ename argv tokens so we see:
//     argv[0..k-1] = ename strings (cutting edges)
//     argv[k]      = "" (Enter, end cutting selection)
//     argv[k+1..n] = "x,y,z" tokens (trim points)
//     argv[n+1]    = "" (Enter, end command)
//
// Per trim point we:
//   1. Find the doc curve nearest the click.
//   2. Intersect that target curve against every cutting edge.
//   3. Pick the intersection parameter closest to the click's
//      projected parameter, split the target there, and discard the
//      half containing the click. Add the surviving half to the doc.
//
// This is a simplified TRIM. AutoCAD does fancier logic (ExtendLines,
// edge-mode, projection settings) - we don't. For straightforward 2D
// trimming where the click is on the target curve, the behavior
// matches.
void MockCommands::Trim(unsigned int docId, int argc, const char* const* argv)
{
  CRhinoDoc* doc = CRhinoDoc::FromRuntimeSerialNumber(docId);
  if (!doc)
    return;

  // --- Phase 1: cutting edges (numeric ename tokens) ---
  ON_SimpleArray<const ON_Curve*>          cutting_curves;
  ON_SimpleArray<const CRhinoCurveObject*> cutting_objs;
  int i = 0;
  for (; i < argc; ++i)
  {
    const char* tok = argv[i];
    if (!tok || !*tok)
      break;          // "" terminates cutting-edge selection
    long sn = atol(tok);
    if (sn <= 0)
      continue;
    const CRhinoObject* obj = CRhinoObject::FromRuntimeSerialNumber(docId, (unsigned int)sn);
    if (!obj || obj->Document() != doc)
      continue;
    const CRhinoCurveObject* co = CRhinoCurveObject::Cast(obj);
    if (!co || !co->Curve())
      continue;
    cutting_objs.Append(co);
    cutting_curves.Append(co->Curve());
  }
  if (cutting_curves.Count() == 0)
    return;
  if (i < argc)
    i++;                   // skip the empty separator

  // --- Phase 2: trim points (each x,y,z is a click on a target curve) ---
  double tol = doc->AbsoluteTolerance();
  if (tol <= 0.0)
    tol = 0.001;

  for (; i < argc; ++i)
  {
    const char* tok = argv[i];
    if (!tok || !*tok)
      break;

    double tx, ty, tz;
    if (sscanf(tok, "%lf,%lf,%lf", &tx, &ty, &tz) != 3)
      continue;
    ON_3dPoint trim_pt(tx, ty, tz);

    // Locate the target curve: closest doc curve to the click.
    // (Cutting-edge curves can also be trimmed - AutoCAD allows it.)
    const CRhinoCurveObject* target_obj = nullptr;
    double                   target_t   = 0.0;
    double                   best_dist  = 1.0e300;
    {
      CRhinoObjectIterator it(*doc, CRhinoObjectIterator::normal_objects);
      for (const CRhinoObject* o = it.First(); o; o = it.Next())
      {
        const CRhinoCurveObject* co = CRhinoCurveObject::Cast(o);
        const ON_Curve* c = co ? co->Curve() : nullptr;
        if (nullptr == c)
          continue;
        double t;
        if (!c->GetClosestPoint(trim_pt, &t))
          continue;
        double d = c->PointAt(t).DistanceTo(trim_pt);
        if (d < best_dist)
        {
          best_dist  = d;
          target_obj = co;
          target_t   = t;
        }
      }
    }
    if (!target_obj)
      continue;
    const ON_Curve* target = target_obj->Curve();

    // Find every parameter on `target` where it intersects a cutting
    // edge. Skip intersections with itself.
    ON_SimpleArray<double> cut_params;
    for (int j = 0; j < cutting_curves.Count(); ++j)
    {
      const ON_Curve* cut = cutting_curves[j];
      if (cut == target)
        continue;
      ON_SimpleArray<ON_X_EVENT> events;
      if (target->IntersectCurve(cut, events, tol) < 1)
        continue;
      for (int k = 0; k < events.Count(); ++k)
        cut_params.Append(events[k].m_a[0]);
    }
    if (cut_params.Count() == 0)
      continue;

    // Pick the intersection parameter closest to the click.
    double cut_t   = cut_params[0];
    double cut_diff = fabs(cut_t - target_t);
    for (int j = 1; j < cut_params.Count(); ++j)
    {
      double d = fabs(cut_params[j] - target_t);
      if (d < cut_diff)
      {
        cut_diff = d;
        cut_t    = cut_params[j];
      }
    }

    // Split the target at the intersection and discard the side
    // containing the click.
    ON_Curve* part_before = nullptr;
    ON_Curve* part_after  = nullptr;
    if (!target->Split(cut_t, part_before, part_after))
    {
      delete part_before;
      delete part_after;
      continue;
    }

    ON_Curve* keep    = nullptr;
    ON_Curve* discard = nullptr;
    if (target_t < cut_t) { discard = part_before; keep = part_after;  }
    else                  { discard = part_after;  keep = part_before; }

    if (keep) doc->AddCurveObject(*keep);
    delete keep;
    delete discard;

    // Remove the original target.
    doc->DeleteObject(CRhinoObjRef(target_obj));
  }

  doc->Redraw();
}

// (command "DIST" p1 p2) - AutoCAD's distance-measure command.
//
// AutoCAD's DIST reports distance + per-axis deltas to the command
// line. Rhino has no exact equivalent (the Distance command is
// similar but interactive); we just print the same information
// scripts expect to see.
void MockCommands::Dist(int argc, const char* const* argv)
{
  ON_3dPoint p1(0,0,0), p2(0,0,0);
  bool have_p1 = false, have_p2 = false;

  for (int i = 0; i < argc; ++i)
  {
    const char* tok = argv[i];
    if (!tok || !*tok) continue;

    double x, y, z;
    if (sscanf(tok, "%lf,%lf,%lf", &x, &y, &z) != 3) continue;

    if (!have_p1)      { p1.Set(x, y, z); have_p1 = true; }
    else if (!have_p2) { p2.Set(x, y, z); have_p2 = true; break; }
  }

  if (!have_p1 || !have_p2) return;

  double dx = p2.x - p1.x;
  double dy = p2.y - p1.y;
  double dz = p2.z - p1.z;
  double d  = p1.DistanceTo(p2);

  ON_wString msg;
  msg.Format(L"Distance = %g, dx = %g, dy = %g, dz = %g\n",
             d, dx, dy, dz);
  RhinoApp().Print(msg.Array());
}

// Incremental polyline session state.
//
// AutoLISP scripts often build polylines a point at a time:
//     (command "pline" pt1)         ; start session
//     (command pt2)                 ; add point
//     (command pt3)                 ; add point
//     (command "")                  ; Enter -> end session, flush
// Each (command ...) is a separate fsubCOMMAND call, so we need
// cross-call state. The polyline only materializes when the session
// flushes - on empty-string Enter, "Close", or a script-end safety
// flush.
namespace
{
  struct PlineSession {
    bool            active;
    bool            is_3d;
    bool            closed;
    ON_3dPointArray pts;
  };
  static PlineSession g_pline = { false, false, false, ON_3dPointArray() };

  void FlushPlineSession(unsigned int docId)
  {
    if (!g_pline.active) return;

    if (g_pline.pts.Count() >= 2)
    {
      CRhinoDoc* doc = CRhinoDoc::FromRuntimeSerialNumber(docId);
      if (doc)
      {
        ON_Polyline pl;
        for (int i = 0; i < g_pline.pts.Count(); ++i)
          pl.Append(g_pline.pts[i]);
        if (g_pline.closed && g_pline.pts.Count() >= 3)
          pl.Append(g_pline.pts[0]);
        ON_PolylineCurve pcurve(pl);

        // TODO: fix Rhino 8 source. The following can cause a crash
        //ON_PolylineCurve pcurve;
        //for (int i = 0; i < g_pline.pts.Count(); ++i)
        //  pcurve.m_pline.Append(g_pline.pts[i]);
        //if (g_pline.closed && g_pline.pts.Count() >= 3)
        //  pcurve.m_pline.Append(g_pline.pts[0]);
        doc->AddCurveObject(pcurve);
        doc->Redraw();
      }
    }

    g_pline.active = false;
    g_pline.is_3d  = false;
    g_pline.closed = false;
    g_pline.pts.SetCount(0);
  }
}

extern "C" int MockCommands_IsPlineSessionActive(void)
{
  return g_pline.active ? 1 : 0;
}

extern "C" void MockCommands_PlineContinue(unsigned int docId, int argc, const char* const* argv)
{
  MockCommands::PlineContinue(docId, argc, argv);
}

// (command "PLINE" pt ...)  -> opens session, processes initial args
// (command "3DPOLY" pt ...) -> same, with is_3d=true (no functional
//                              difference today; both store full 3D
//                              points)
void MockCommands::Pline(unsigned int docId, int argc, const char* const* argv, bool is_3d)
{
  // If a stale session exists for any reason, flush it first.
  FlushPlineSession(docId);

  g_pline.active = true;
  g_pline.is_3d  = is_3d;
  g_pline.closed = false;
  g_pline.pts.SetCount(0);

  // Process initial argv exactly like a continuation would.
  PlineContinue(docId, argc, argv);
}

// Process arguments against the active session. Handles points,
// the "Close" keyword, and the empty-string Enter terminator.
// Other tokens are accepted-and-ignored to tolerate scripts that
// pass through pline subcommands we don't implement (Width, Arc,
// Halfwidth, etc.). For arc-mode polylines drawn this way, the
// resulting geometry will lose the curvature but the script
// proceeds.
void MockCommands::PlineContinue(unsigned int docId, int argc, const char* const* argv)
{
  if (!g_pline.active) return;

  for (int i = 0; i < argc; ++i)
  {
    const char* tok = argv[i];
    if (!tok) continue;

    if (tok[0] == '\0')
    {
      // Enter -> end of session.
      FlushPlineSession(docId);
      return;
    }

    // Point form.
    if (strchr(tok, ','))
    {
      double x, y, z;
      if (sscanf(tok, "%lf,%lf,%lf", &x, &y, &z) == 3)
      {
        g_pline.pts.Append(ON_3dPoint(x, y, z));
        continue;
      }
    }

    // Close keyword - close the polyline and flush.
    if (EqIgnoreCase(tok, "C") || EqIgnoreCase(tok, "CLOSE"))
    {
      g_pline.closed = true;
      FlushPlineSession(docId);
      return;
    }

    // Unknown subcommand - silently ignore (Width, Arc, Halfwidth,
    // Length, Undo, etc.).
  }
}
