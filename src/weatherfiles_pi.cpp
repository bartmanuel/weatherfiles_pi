/******************************************************************************
 * WeatherFiles OpenCPN plugin implementation. See weatherfiles_pi.h.
 * GPL-3.0 (see LICENSE).
 ******************************************************************************/

#include "wx/wxprec.h"
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "weatherfiles_pi.h"
#include "version.h"
#include "wxWTranslateCatalog.h"
#include "tpicons.h"
#include "wf_prefs_dialog.h"
#include "wf_models_panel.h"

#ifndef DECL_EXP
#ifdef __WXMSW__
#define DECL_EXP __declspec(dllexport)
#else
#define DECL_EXP
#endif
#endif

// Globals. tpicons sets/uses g_SData_Locn (its icon data path) and references
// the plugin instance; g_GLMinSymbolLineWidth is required by the vendored
// ocpndc/dc_utils to link.
weatherfiles_pi *g_weatherfiles_pi = nullptr;
wxString        *g_SData_Locn = nullptr;
float            g_GLMinSymbolLineWidth = 1.0;

// Class factories used by OpenCPN to create/destroy the plugin instance.
extern "C" DECL_EXP opencpn_plugin* create_pi(void *ppimgr)
{
    return new weatherfiles_pi(ppimgr);
}

extern "C" DECL_EXP void destroy_pi(opencpn_plugin* p)
{
    delete p;
}

//----------------------------------------------------------------------------
//    WeatherFiles plugin implementation
//----------------------------------------------------------------------------

weatherfiles_pi::weatherfiles_pi(void *ppimgr)
    : opencpn_plugin_118(ppimgr)
{
    g_weatherfiles_pi = this;
    m_parent_window = nullptr;
    m_pTPConfig = nullptr;
    m_weatherfiles_button_id = -1;
    m_ptpicons = new tpicons();   // loads the toolbar/plugin icons
}

weatherfiles_pi::~weatherfiles_pi()
{
    delete m_ptpicons;            // also frees g_SData_Locn
    m_ptpicons = nullptr;
}

int weatherfiles_pi::Init(void)
{
    AddLocaleCatalog(PLUGIN_CATALOG_NAME);

    m_parent_window = GetOCPNCanvasWindow();
    m_pTPConfig = GetOCPNConfigObject();
    LoadConfig();

#ifdef PLUGIN_USE_SVG
    m_weatherfiles_button_id = InsertPlugInToolSVG(
        _("WeatherFiles"), m_ptpicons->m_s_weatherfiles_grey_pi,
        m_ptpicons->m_s_weatherfiles_pi, m_ptpicons->m_s_weatherfiles_toggled_pi,
        wxITEM_CHECK, _("WeatherFiles"), wxS(""), NULL, weatherfiles_POSITION, 0,
        this);
#else
    m_weatherfiles_button_id = InsertPlugInTool(
        _("WeatherFiles"), &m_ptpicons->m_bm_weatherfiles_grey_pi,
        &m_ptpicons->m_bm_weatherfiles_pi, wxITEM_CHECK, _("WeatherFiles"),
        wxS(""), NULL, weatherfiles_POSITION, 0, this);
#endif

    return (INSTALLS_TOOLBAR_TOOL | WANTS_TOOLBAR_CALLBACK | WANTS_PREFERENCES);
}

bool weatherfiles_pi::DeInit(void)
{
    if (m_pTPConfig) SaveConfig();
    return true;
}

int weatherfiles_pi::GetAPIVersionMajor()   { return OCPN_API_VERSION_MAJOR; }
int weatherfiles_pi::GetAPIVersionMinor()   { return OCPN_API_VERSION_MINOR; }
int weatherfiles_pi::GetPlugInVersionMajor() { return PLUGIN_VERSION_MAJOR; }
int weatherfiles_pi::GetPlugInVersionMinor() { return PLUGIN_VERSION_MINOR; }
int weatherfiles_pi::GetPlugInVersionPatch() { return PLUGIN_VERSION_PATCH; }
int weatherfiles_pi::GetPlugInVersionPost()  { return PLUGIN_VERSION_TWEAK; }

wxBitmap *weatherfiles_pi::GetPlugInBitmap()
{
    return &m_ptpicons->m_bm_weatherfiles_pi;
}

wxString weatherfiles_pi::GetCommonName()       { return _T(PLUGIN_COMMON_NAME); }
wxString weatherfiles_pi::GetShortDescription() { return _(PLUGIN_SHORT_DESCRIPTION); }
wxString weatherfiles_pi::GetLongDescription()  { return _(PLUGIN_LONG_DESCRIPTION); }

int weatherfiles_pi::GetToolbarToolCount(void) { return 1; }

void weatherfiles_pi::OnToolbarToolCallback(int id)
{
    // Open the WeatherFiles model browser. (Token is set via Preferences; the
    // panel prompts if none is configured.)
    WfModelsPanel dlg(m_parent_window, m_token);
    dlg.ShowModal();
    SetToolbarItemState(m_weatherfiles_button_id, false);
}

void weatherfiles_pi::ShowPreferencesDialog(wxWindow* parent)
{
    WfPrefsDialog dlg(parent, m_token);
    if (dlg.ShowModal() == wxID_OK) {
        m_token = dlg.GetToken();
        SaveConfig();
    }
}

void weatherfiles_pi::SaveConfig()
{
    wxFileConfig *pConf = m_pTPConfig;
    if (!pConf) return;
    pConf->SetPath(wxS("/Settings/weatherfiles_pi"));
    pConf->Write(wxS("ApiToken"), m_token);
}

void weatherfiles_pi::LoadConfig()
{
    wxFileConfig *pConf = m_pTPConfig;
    if (!pConf) return;
    pConf->SetPath(wxS("/Settings/weatherfiles_pi"));
    pConf->Read(wxS("ApiToken"), &m_token, wxEmptyString);
}
