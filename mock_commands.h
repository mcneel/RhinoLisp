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

	// Set by cmdRhinoLisp before evaluating a script - the directory
	// containing the .lsp file. Used by Insert as a fallback search
	// location for missing block-definition .dwg files.
	static void SetScriptDirectory(const wchar_t* dir);
};