#include "stdafx.h"
#include "mock_commands.h"
#include "rhino_subrs.h"
#include <cstdio>
#include <cstring>

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
      // AutoCAD then asks WHICH layer to color; eat the next
      // non-empty token as that target.
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
      /* Subcommands we don't implement - skip the next arg. */
      if (++i >= argc) break;
    }
    /* anything else: ignore (AutoCAD would re-prompt) */
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

    /* A point token always contains commas (we encode them that way
       in fsubCOMMAND), so a quick char check avoids confusing
       "0,0,0" with the close keyword "C". */
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

    if (tok[0] == '\0') break;  /* Enter -> stop */
    if (EqIgnoreCase(tok, "C") || EqIgnoreCase(tok, "CLOSE"))
    {
      closed = 1;
      break;
    }
    /* unknown subcommand: ignore */
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
