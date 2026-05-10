#include "stdafx.h"
#include "rhinoSdkPlugInDeclare.h"
#include "RhinoLispPlugIn.h"
#include "Resource.h"

// The plug-in object must be constructed before any plug-in classes derived
// from CRhinoCommand. The #pragma init_seg(lib) ensures that this happens.
#pragma warning(push)
#pragma warning(disable : 4073)
#pragma init_seg(lib)
#pragma warning(pop)

// Rhino plug-in declaration
RHINO_PLUG_IN_DECLARE
RHINO_PLUG_IN_NAME(L"RhinoLisp");
RHINO_PLUG_IN_ID(L"AE98F9D0-E138-4CAD-BC25-6A7A3F155CCC");
RHINO_PLUG_IN_VERSION(__DATE__ "  " __TIME__)
RHINO_PLUG_IN_DESCRIPTION(L"Lisp support in Rhino");
RHINO_PLUG_IN_ICON_RESOURCE_ID(IDI_ICON);

// Rhino plug-in developer declarations
RHINO_PLUG_IN_DEVELOPER_ORGANIZATION(L"Robert McNeel & Associates");
RHINO_PLUG_IN_DEVELOPER_ADDRESS(L"146 North Canal Street, Suite 320\015\012Seattle WA 98103");
RHINO_PLUG_IN_DEVELOPER_COUNTRY(L"United States");
RHINO_PLUG_IN_DEVELOPER_PHONE(L"");
RHINO_PLUG_IN_DEVELOPER_FAX(L"");
RHINO_PLUG_IN_DEVELOPER_EMAIL(L"devsupport@mcneel.com");
RHINO_PLUG_IN_DEVELOPER_WEBSITE(L"http://www.rhino3d.com");
RHINO_PLUG_IN_UPDATE_URL(L"");

// The one and only CRhinoLispPlugIn object
static CRhinoLispPlugIn thePlugIn;

CRhinoLispPlugIn& RhinoLispPlugIn()
{
  return thePlugIn;
}

const wchar_t* CRhinoLispPlugIn::PlugInName() const
{
  return RhinoPlugInName();
}

const wchar_t* CRhinoLispPlugIn::PlugInVersion() const
{
  ON_wString version = RhinoPlugInVersion();
  return version;
}

GUID CRhinoLispPlugIn::PlugInID() const
{
  return ON_UuidFromString(RhinoPlugInId());
}
