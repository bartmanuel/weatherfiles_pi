/******************************************************************************
 * WeatherFiles OpenCPN plugin implementation. See weatherfiles_pi.h.
 * GPL-3.0 (see LICENSE).
 ******************************************************************************/

#include "wx/wxprec.h"
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "config.h"

#include "weatherfiles_pi.h"
#include "tpicons.h"
#include "wf_prefs_dialog.h"
#include "wf_download_dialog.h"
#include "wf_multi_slice_dialog.h"

#if defined(__WXOSX__)
#include <OpenGL/gl.h>
#elif defined(__WXMSW__)
#include <windows.h>   // must precede GL/gl.h on MSVC (WINGDIAPI/APIENTRY)
#include <GL/gl.h>
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
    AddLocaleCatalog("opencpn-weatherfiles");

    m_parent_window = GetOCPNCanvasWindow();
    m_pTPConfig = GetOCPNConfigObject();
    LoadConfig();

#ifdef PLUGIN_USE_SVG
    m_weatherfiles_button_id = InsertPlugInToolSVG(
        _("WeatherFiles"), m_ptpicons->m_s_weatherfiles_grey_pi,
        m_ptpicons->m_s_weatherfiles_pi, m_ptpicons->m_s_weatherfiles_toggled_pi,
        wxITEM_CHECK, _("WeatherFiles: area-first multi-slice download"),
        wxS(""), NULL, weatherfiles_POSITION, 0, this);
#else
    m_weatherfiles_button_id = InsertPlugInTool(
        _("WeatherFiles"), &m_ptpicons->m_bm_weatherfiles_grey_pi,
        &m_ptpicons->m_bm_weatherfiles_pi, wxITEM_CHECK,
        _("WeatherFiles: area-first multi-slice download"), wxS(""), NULL,
        weatherfiles_POSITION, 0, this);
#endif

    return (INSTALLS_TOOLBAR_TOOL | WANTS_TOOLBAR_CALLBACK | WANTS_PREFERENCES |
            WANTS_OVERLAY_CALLBACK | WANTS_OPENGL_OVERLAY_CALLBACK |
            WANTS_ONPAINT_VIEWPORT | WANTS_MOUSE_EVENTS);
}

bool weatherfiles_pi::DeInit(void)
{
    if (m_pTPConfig) SaveConfig();
    // Tear down the modeless multi-slice dialog before the plugin (and its
    // captured callback state) goes away. Destroy() runs the dialog's dtor,
    // which clears m_multi_dialog via ClearMultiDialog().
    if (m_multi_dialog) {
        m_multi_dialog->Destroy();
        m_multi_dialog = nullptr;
    }
    // Release any process-wide HTTP-client state acquired by WfApi during
    // the plugin's lifetime (e.g. libcurl's global init on macOS/Linux).
    WfApi::GlobalCleanup();
    return true;
}

int weatherfiles_pi::GetAPIVersionMajor() { return atoi(API_VERSION); }
int weatherfiles_pi::GetAPIVersionMinor() {
  std::string v(API_VERSION);
  size_t dotpos = v.find('.');
  return atoi(v.substr(dotpos + 1).c_str());
}

int weatherfiles_pi::GetPlugInVersionMajor() { return PLUGIN_VERSION_MAJOR; }
int weatherfiles_pi::GetPlugInVersionMinor() { return PLUGIN_VERSION_MINOR; }
int weatherfiles_pi::GetPlugInVersionPatch() { return PLUGIN_VERSION_PATCH; }
int weatherfiles_pi::GetPlugInVersionPost()  { return PLUGIN_VERSION_TWEAK; }

wxBitmap *weatherfiles_pi::GetPlugInBitmap()
{
    return &m_ptpicons->m_bm_weatherfiles_pi;
}

wxString weatherfiles_pi::GetCommonName()       { return PKG_NAME; }
wxString weatherfiles_pi::GetShortDescription() { return _(PKG_SUMMARY); }
wxString weatherfiles_pi::GetLongDescription()  { return _(PKG_DESCRIPTION); }

int weatherfiles_pi::GetToolbarToolCount(void) { return 1; }

bool weatherfiles_pi::RenderOverlayMultiCanvas(wxDC& dc, PlugIn_ViewPort* vp,
                                               int canvas_ix, int priority)
{
    if (canvas_ix != 0) return false;        // primary canvas only
    if (vp) m_last_vp = *vp;                  // keep the current view

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

bool weatherfiles_pi::RenderGLOverlayMultiCanvas(wxGLContext* pcontext,
                                                 PlugIn_ViewPort* vp,
                                                 int canvas_ix, int priority)
{
    // OpenCPN renders in OpenGL mode by default, so this is normally what's
    // called. Capture the view + draw the rubber-band here.
    if (canvas_ix != 0) return false;
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
    // Build a callback that opens the single-model download dialog with the
    // box. Captures by value so the model survives this scope and the user's
    // chart-interaction time.
    const WfModel m = model;
    const wxString token = m_token;
    wxWindow* parent = m_parent_window;
    m_pick_cb = [parent, m, token](const WfBBox& box) {
        WfDownloadDialog dlg(parent, m, box, token);
        dlg.ShowModal();
    };
    m_picking = true;
    m_dragging = false;
}

void weatherfiles_pi::StartAreaPickMulti(std::function<void(const WfBBox&)> cb)
{
    m_pick_cb = std::move(cb);
    m_picking = true;
    m_dragging = false;
}

void weatherfiles_pi::ClearMultiDialog(WfMultiSliceDialog* dlg)
{
    if (m_multi_dialog != dlg) return;
    m_multi_dialog = nullptr;
    // Don't leave a pick armed if its callback was rooted in this dialog.
    // Single-model picks capture by value and are unaffected if reinstated
    // later; the worst case is the user has to re-arm. Cheap and safe.
    m_pick_cb = nullptr;
    m_picking = false;
    m_dragging = false;
}

// wxMouseEvent positions are logical points; the GL overlay + viewport work in
// physical pixels. On a retina/HiDPI display they differ by the content-scale
// factor, so scale mouse coords up to physical pixels for drawing + conversion.
static wxPoint wfPhysPos(const wxMouseEvent& e)
{
    double sf = OCPN_GetDisplayContentScaleFactor();
    if (sf <= 0.0) sf = 1.0;
    const wxPoint p = e.GetPosition();
    return wxPoint(static_cast<int>(p.x * sf), static_cast<int>(p.y * sf));
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
        m_pick_start = wfPhysPos(event);
        m_pick_cur = m_pick_start;
        m_dragging = true;
        return true;
    }
    if (event.Dragging() && m_dragging) {
        m_pick_cur = wfPhysPos(event);
        RequestRefresh(m_parent_window);   // redraw the rubber-band
        return true;
    }
    if (event.LeftUp() && m_dragging) {
        m_dragging = false;
        m_picking = false;
        const wxPoint a = m_pick_start, b = wfPhysPos(event);
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

        // Fire the pick callback after this event finishes dispatching, so
        // any UI work it does (opening dialogs etc.) runs cleanly.
        auto cb = m_pick_cb;
        m_pick_cb = nullptr;
        if (cb) wxTheApp->CallAfter([cb, box]() { cb(box); });
        return true;
    }
    return false;
}

void weatherfiles_pi::OnToolbarToolCallback(int id)
{
    // First run with no token yet: take the user straight to the token-entry
    // (Preferences) dialog. If they don't set one, stop here.
    if (m_token.IsEmpty()) {
        ShowPreferencesDialog(m_parent_window);
        if (m_token.IsEmpty()) {
            SetToolbarItemState(id, false);
            return;
        }
    }

    // Default the area to the current chart view if we have one.
    WfBBox view;
    if (m_last_vp.bValid) {
        view.south = m_last_vp.lat_min;
        view.north = m_last_vp.lat_max;
        view.west = m_last_vp.lon_min;
        view.east = m_last_vp.lon_max;
        view.valid = true;
    }

    // Open the area-first multi-slice wizard (modeless). Re-pressing while
    // it's open raises it to the front.
    if (m_multi_dialog) {
        m_multi_dialog->Raise();
    } else {
        m_multi_dialog =
            new WfMultiSliceDialog(m_parent_window, m_token, view, this);
        m_multi_dialog->Show();
    }
    SetToolbarItemState(id, false);
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
