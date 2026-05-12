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
  cmd.Format(L"_-Insert _File \"%s\" 0,0,0 1 1 0 ", file_path.Array());
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
