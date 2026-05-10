#pragma once

class CRhinoLispPlugIn : public CRhinoUtilityPlugIn
{
public:
  CRhinoLispPlugIn() = default;
  ~CRhinoLispPlugIn() = default;

  // Required overrides
  
  // Plug-in name display string. This name is displayed by Rhino when
  // loading the plug-in, in the plug-in help menu, and in the Rhino
  // interface for managing plug-ins. 
  const wchar_t* PlugInName() const override;
  
  // Plug-in version display string. This name is displayed by Rhino
  // when loading the plug-in and in the Rhino interface for 
  // managing plug-ins.
  const wchar_t* PlugInVersion() const override;
  
  // Plug-in unique identifier. The identifier is used by Rhino for
  // managing plug-ins.
  GUID PlugInID() const override;
};

// Return a reference to the one and only CRhinoLispPlugIn object
CRhinoLispPlugIn& RhinoLispPlugIn();


// Evaluate lisp as string
// Returns:
//   0   - all forms evaluated normally
//   1   - a non-fatal error was caught (the partial output is in outbuf)
//  -1   - a fatal error / interpreter unavailable */
extern "C" int RhinoXlispPlusEval(unsigned int docId, const char* src, char* outbuf, int cap);

