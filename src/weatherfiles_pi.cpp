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
#include "wf_download_dialog.h"

#ifdef __WXOSX__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

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
    m_last_vp.bValid = false;
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

    return (INSTALLS_TOOLBAR_TOOL | WANTS_TOOLBAR_CALLBACK | WANTS_PREFERENCES |
            WANTS_OVERLAY_CALLBACK | WANTS_ONPAINT_VIEWPORT | WANTS_MOUSE_EVENTS);
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

bool weatherfiles_pi::RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp)
{
    if (vp) m_last_vp = *vp;   // keep the current view for the download default

    // While dragging in pick mode, draw the selection rectangle.
    if (m_picking && m_dragging) {
        dc.SetPen(wxPen(wxColour(0x00, 0x54, 0xd6), 2));   // brand blue
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        const int x = wxMin(m_pick_start.x, m_pick_cur.x);
        const int y = wxMin(m_pick_start.y, m_pick_cur.y);
        const int w = abs(m_pick_cur.x - m_pick_start.x);
        const int h = abs(m_pick_cur.y - m_pick_start.y);
        dc.DrawRectangle(x, y, w, h);
        return true;
    }
    return false;
}

bool weatherfiles_pi::RenderGLOverlay(wxGLContext* pcontext, PlugIn_ViewPort* vp)
{
    // OpenCPN renders in OpenGL mode by default, so this (not the wxDC overlay)
    // is what's normally called. Capture the view + draw the rubber-band here.
    if (vp) m_last_vp = *vp;
    if (m_picking && m_dragging) {
        glColor3ub(0x00, 0x54, 0xd6);   // brand blue
        glLineWidth(2);
        glBegin(GL_LINE_LOOP);
        glVertex2i(m_pick_start.x, m_pick_start.y);
        glVertex2i(m_pick_cur.x, m_pick_start.y);
        glVertex2i(m_pick_cur.x, m_pick_cur.y);
        glVertex2i(m_pick_start.x, m_pick_cur.y);
        glEnd();
        return true;
    }
    return false;
}

void weatherfiles_pi::SetCurrentViewPort(PlugIn_ViewPort& vp)
{
    m_last_vp = vp;
}

void weatherfiles_pi::StartAreaPick(const WfModel& model)
{
    m_pending_model = model;
    m_picking = true;
    m_dragging = false;
}

bool weatherfiles_pi::MouseEventHook(wxMouseEvent& event)
{
    if (!m_picking) return false;   // let OpenCPN handle the mouse normally

    if (event.RightDown()) {        // cancel the pick
        m_picking = false;
        m_dragging = false;
        RequestRefresh(m_parent_window);
        return true;
    }
    if (event.LeftDown()) {
        m_pick_start = event.GetPosition();
        m_pick_cur = m_pick_start;
        m_dragging = true;
        return true;
    }
    if (event.Dragging() && m_dragging) {
        m_pick_cur = event.GetPosition();
        RequestRefresh(m_parent_window);   // redraw the rubber-band
        return true;
    }
    if (event.LeftUp() && m_dragging) {
        m_dragging = false;
        m_picking = false;
        const wxPoint a = m_pick_start, b = event.GetPosition();
        RequestRefresh(m_parent_window);   // clear the rectangle

        // Convert the two corners to lat/lon using the current viewport.
        double lat1, lon1, lat2, lon2;
        GetCanvasLLPix(&m_last_vp, a, &lat1, &lon1);
        GetCanvasLLPix(&m_last_vp, b, &lat2, &lon2);
        WfBBox box;
        box.south = wxMin(lat1, lat2);
        box.north = wxMax(lat1, lat2);
        box.west = wxMin(lon1, lon2);
        box.east = wxMax(lon1, lon2);
        box.valid = true;

        // Tiny boxes are almost certainly a misclick - ignore.
        if (box.north - box.south < 0.01 || box.east - box.west < 0.01)
            return true;

        // Open the download dialog after this event finishes dispatching.
        const WfModel model = m_pending_model;
        const wxString token = m_token;
        wxWindow* parent = m_parent_window;
        // Open the dialog after this mouse event finishes dispatching.
        wxTheApp->CallAfter([parent, model, box, token]() {
            WfDownloadDialog dlg(parent, model, box, token);
            dlg.ShowModal();
        });
        return true;
    }
    return false;
}

void weatherfiles_pi::OnToolbarToolCallback(int id)
{
    // Open the WeatherFiles model browser, defaulting the download area to the
    // current chart view if we have one. (Token is set via Preferences; the
    // panel prompts if none is configured.)
    WfBBox view;
    if (m_last_vp.bValid) {
        view.south = m_last_vp.lat_min;
        view.north = m_last_vp.lat_max;
        view.west = m_last_vp.lon_min;
        view.east = m_last_vp.lon_max;
        view.valid = true;
    }
    WfModelsPanel dlg(m_parent_window, m_token, view, this);
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
