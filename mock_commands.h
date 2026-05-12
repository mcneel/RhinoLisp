#pragma once

class MockCommands
{
public:
	static void Layer(unsigned int docId, int argc, const char* const* argv);
	static void Line(unsigned int docId, int argc, const char* const* argv);
	static void Insert(unsigned int docId, int argc, const char* const* argv);
};