#include "stdafx.h"
#include "RhinoLispPlugIn.h"
#include <fstream>
#include <sstream>
#include <string>


class CCommandRhinoLisp : public CRhinoScriptCommand
{
public:
  CCommandRhinoLisp() = default;
  ~CCommandRhinoLisp() = default;
  UUID CommandUUID() override
  {
    // {C06E712C-8F53-45B2-AA9B-CA745A55620C}
    static const GUID id = 
    {0xc06e712c,0x8f53,0x45b2,{0xaa,0x9b,0xca,0x74,0x5a,0x55,0x62,0x0c}};
    return id;
  }

  const wchar_t* EnglishCommandName() override { return L"RhinoLisp"; }
  CRhinoCommand::result RunCommand(const CRhinoCommandContext& context) override;
};

static CCommandRhinoLisp theRhinoLispCommand;


// Preprocess an AutoLISP source string for XLISP-PLUS:
//   - Strip a leading "c:" or "C:" from any symbol that starts at a
//     fresh symbol-position. (XLISP-PLUS treats colon as a package
//     separator; AutoLISP uses c:NAME to mark command-callable defuns.)
//   - Leaves string literals and ;... line comments untouched.
//
// We track three lexer states (normal, inside-string, inside-comment)
// and only apply the stripping in the normal state when the previous
// significant character is a symbol-terminating one.
static std::string PreprocessAutoLisp(const std::string& src, ON_ClassArray<ON_String>& commands)
{
  std::string out;
  out.reserve(src.size());

  enum { NORMAL, IN_STRING, IN_COMMENT, DEFINING_COMMAND } state = NORMAL;

  auto isSymStartFollow = [](char c)
  {
    // After "c:", we want to see a letter or a symbol-character to
    // be confident this was a c:NAME defun token. Bail otherwise.
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            c == '_' || c == '*' || c == '+' || c == '-' || c == '/' ||
            c == '?' || c == '!' || c == '$' || c == '%' || c == '&';
  };

  char prev = ' ';  // pretend BOF is preceded by whitespace
  size_t i = 0;
  size_t n = src.size();
  ON_String command;
  while (i < n)
  {
    char c = src[i];

    if (state == IN_STRING)
    {
      out.push_back(c);
      if (c == '\\' && i + 1 < n)
      {
        // skip escaped char
        out.push_back(src[i + 1]);
        i += 2;
        continue;
      }
      if (c == '"')
        state = NORMAL;
      prev = c;
      i++;
      continue;
    }

    if (state == IN_COMMENT)
    {
      out.push_back(c);
      if (c == '\n')
        state = NORMAL;
      prev = c;
      i++;
      continue;
    }

    // NORMAL state.
    if (c == '"')
    {
      state = IN_STRING;
      out.push_back(c);
      prev = c;
      i++;
      continue;
    }
    if (c == ';')
    {
      state = IN_COMMENT;
      out.push_back(c);
      prev = c;
      i++;
      continue;
    }

    // Are we at a fresh symbol-start? Yes if `prev` is whitespace,
    // an opening paren, a quote/backquote, or comma. (',' is the
    // unquote in lisp; we'd rather not eat it.)
    bool atSymStart = (prev == ' ' || prev == '\t' || prev == '\n' ||
                       prev == '\r' || prev == '(' || prev == '\'' ||
                       prev == '`' || prev == ',');

    if (atSymStart && (c == 'c' || c == 'C') &&
            i + 2 < n && src[i + 1] == ':' &&
            isSymStartFollow(src[i + 2]))
    {
      // Skip the "c:" - the rest of the symbol falls through.
      i += 2;
      // `prev` deliberately stays as it was; the next char now
      // becomes the first char of the symbol.
      state = DEFINING_COMMAND;
      command.Empty();
      continue;
    }

    if (DEFINING_COMMAND == state)
    {
      if ((c == ' ' && command.IsNotEmpty()) || c == '(' || c == '\n')
      {
        state = NORMAL;
        command.TrimLeftAndRight();
        if (command.IsNotEmpty())
        {
          commands.Append(command);
          command.Empty();
        }
      }
      else
      {
        command.Append(&c, 1);
      }
    }

    out.push_back(c);
    prev = c;
    i++;
  }

  return out;
}


CRhinoCommand::result CCommandRhinoLisp::RunCommand(const CRhinoCommandContext& context)
{
  // Use the Win32 file dialog directly so we can filter to *.LSP. The
  // filter string is a sequence of NUL-terminated pairs ending in a
  // double NUL: "label\0pattern\0label\0pattern\0\0".
  wchar_t filePath[MAX_PATH] = L"";
  OPENFILENAMEW ofn = {0};
  ofn.lStructSize  = sizeof(ofn);
  // GetSafeHwnd() returns NULL if the CWnd is null - safer than
  // dereferencing AfxGetMainWnd() directly.
  ofn.hwndOwner    = AfxGetMainWnd() ? AfxGetMainWnd()->GetSafeHwnd() : NULL;
  ofn.lpstrFilter  =
      L"Lisp files (*.lsp;*.lisp;*.cl)\0*.lsp;*.lisp;*.cl\0"
      L"All files (*.*)\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.lpstrFile    = filePath;
  ofn.nMaxFile     = MAX_PATH;
  ofn.lpstrTitle   = L"Open LISP File";
  ofn.lpstrDefExt  = L"lsp";
  ofn.Flags        = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                     OFN_EXPLORER     | OFN_HIDEREADONLY;

  if (!::GetOpenFileNameW(&ofn))
    return CRhinoCommand::cancel;

  std::ifstream file(filePath);
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string content = buffer.str();

  // Strip AutoLISP's c:NAME prefix so (defun c:opg ...) parses cleanly.
  // XLISP-PLUS has packages enabled and would otherwise treat c: as a
  // package-name lookup and fail.
  ON_ClassArray<ON_String> commands;
  content = PreprocessAutoLisp(content, commands);

  if (commands.Count() == 1)
  {
    content.push_back('\n');
    content.push_back('(');
    ON_String command = commands[0];
    content.append(command.Array());
    content.push_back(')');
    content.push_back('\n');
  }

  ON_String expr = content.c_str();
  if (expr.IsEmpty())
    return CRhinoCommand::success;

  // Output buffer is generous; XLISP can produce a lot of text from
  // pretty-printing big lists.
  const int kOutCap = 64 * 1024;
  ON_SimpleArray<char> outBuffer(kOutCap);
  char* out = outBuffer.Array();
  out[0] = '\0';

  // Use the silent loader so the per-form return values from defun /
  // setq / etc. don't get echoed back. Side-effect output (princ, our
  // command/draw calls) still flows through the output buffer.
  int rc = RhinoXlispPlusEval(context.m_rhino_doc_sn, expr, out, kOutCap);

  // Forward whatever XLISP printed to the Rhino command line, line by line.
  if (out[0] != '\0')
  {
    ON_wString wout = out;
    RhinoApp().Print(wout.Array());
    if (wout.Length() == 0 || wout[wout.Length() - 1] != L'\n')
      RhinoApp().Print(L"\n");
  }

  if (rc < 0)
  {
    RhinoApp().Print(L"RhinoLisp: interpreter unavailable.\n");
    return CRhinoCommand::failure;
  }
  return CRhinoCommand::success;
}

