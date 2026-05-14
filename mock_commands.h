#pragma once

class MockCommands
{
public:
	static void Layer(unsigned int docId, int argc, const char* const* argv);
	static void Line(unsigned int docId, int argc, const char* const* argv);
	static void Insert(unsigned int docId, int argc, const char* const* argv);
	static void Color(unsigned int docId, int argc, const char* const* argv);
	static void Offset(unsigned int docId, int argc, const char* const* argv);
	static void Trim(unsigned int docId, int argc, const char* const* argv);
	static void Dist(int argc, const char* const* argv);

	// PLINE / 3DPOLY support an incremental session: the first
	// (command "pline" pt) starts it, subsequent (command pt) calls
	// add points, (command "") or "Close" flushes. Pline begins;
	// PlineContinue feeds into the active session.
	static void Pline(unsigned int docId, int argc, const char* const* argv, bool is_3d);
	static void PlineContinue(unsigned int docId, int argc, const char* const* argv);

	// Set by cmdRhinoLisp before evaluating a script - the directory
	// containing the .lsp file. Used by Insert as a fallback search
	// location for missing block-definition .dwg files.
	static void SetScriptDirectory(const wchar_t* dir);
};

// fsubCOMMAND consults this to decide whether to require a string
// first argument. Inside a pline session, (command pt) is valid -
// the point continues the polyline rather than naming a command.
extern "C" int MockCommands_IsPlineSessionActive(void);
extern "C" void MockCommands_PlineContinue(unsigned int docId, int argc, const char* const* argv);