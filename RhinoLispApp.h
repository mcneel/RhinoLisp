#pragma once

class CRhinoLispApp : public CWinApp
{
public:
  // CRITICAL: DO NOT CALL RHINO SDK FUNCTIONS HERE!
  // All significant initialization should take place in
  // CRhinoLispPlugIn::OnLoadPlugIn().
  CRhinoLispApp() = default;

  DECLARE_MESSAGE_MAP()
};
