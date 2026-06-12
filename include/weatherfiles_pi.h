/******************************************************************************
 * WeatherFiles OpenCPN plugin.
 *
 * Originally seeded from the OpenCPN plugin template (jongough/testplugin_pi);
 * the Object-Draw demo, control dialog, JSON messaging and related cruft have
 * been removed. What remains is the WeatherFiles client: a toolbar button that
 * opens the model browser, and a preferences dialog for the API token.
 *
 * GPL-3.0 (see LICENSE).
 ******************************************************************************/
#ifndef _WEATHERFILESPI_H_
#define _WEATHERFILESPI_H_

#include <functional>
#include <cstdint>

#include <wx/string.h>
#include <wx/fileconf.h>
#include <wx/gdicmn.h>   // wxPoint

#include "ocpn_plugin.h"

#include "globals.h"
#include "wf_api.h"      // WfModel, WfBBox

class tpicons;
class WfMultiSliceDialog;

//----------------------------------------------------------------------------
//    The PlugIn Class Definition
//----------------------------------------------------------------------------
class weatherfiles_pi : public opencpn_plugin_118
{
public:
    weatherfiles_pi(void *ppimgr);
    ~weatherfiles_pi();

    //    Required PlugIn methods
    int Init(void);
    bool DeInit(void);

    int GetAPIVersionMajor();
    int GetAPIVersionMinor();
    int GetPlugInVersionMajor();
    int GetPlugInVersionMinor();
    int GetPlugInVersionPatch();
    int GetPlugInVersionPost();

    wxBitmap *GetPlugInBitmap();
    wxString GetCommonName();
    wxString GetShortDescription();
    wxString GetLongDescription();

    //    Toolbar + preferences
    int  GetToolbarToolCount(void);
    void OnToolbarToolCallback(int id);
    void ShowPreferencesDialog(wxWindow* parent);

    // Track the current chart view so the download dialog can default its area
    // to what the user is looking at. RenderOverlay is called every redraw with
    // the current viewport (needs WANTS_OVERLAY_CALLBACK|WANTS_ONPAINT_VIEWPORT);
    // SetCurrentViewPort is a secondary hook. Falls back to the model domain if
    // neither fires.
    // API 1.18 calls the MultiCanvas overlay variants (the legacy
    // RenderOverlay/RenderGLOverlay are NOT called for 1.18 plugins). We use
    // them to capture the current viewport and draw the area-pick rectangle.
    bool RenderOverlayMultiCanvas(wxDC& dc, PlugIn_ViewPort* vp, int canvas_ix,
                                  int priority);
    bool RenderGLOverlayMultiCanvas(wxGLContext* pcontext, PlugIn_ViewPort* vp,
                                    int canvas_ix, int priority);
    void SetCurrentViewPort(PlugIn_ViewPort& vp);
    bool MouseEventHook(wxMouseEvent& event);

    // Enter "pick area on chart" mode for `model`: the next left-drag on the
    // chart draws a box; on release the download dialog opens pre-filled with
    // it. Called by the model browser, which closes itself first so the chart
    // is interactive.
    void StartAreaPick(const WfModel& model);

    // Generic on-chart area pick: rubber-band a box and invoke `cb` with it on
    // release. The multi-slice dialog uses this to refill its bbox without
    // closing. `cb` is invoked on the GUI thread.
    void StartAreaPickMulti(std::function<void(const WfBBox&)> cb);

    // Called by WfMultiSliceDialog in its destructor so the plugin can clear
    // its held pointer.
    void ClearMultiDialog(WfMultiSliceDialog* dlg);

    // The currently-open multi-slice dialog or null. Pick callbacks query
    // this rather than capturing the dialog directly, so a dialog destroyed
    // between mouse-release and CallAfter dispatch is a safe no-op.
    WfMultiSliceDialog* MultiDialog() const { return m_multi_dialog; }

    // WeatherFiles API personal access token. Public so the dialogs can read it
    // (set via ShowPreferencesDialog, persisted in the OpenCPN config).
    wxString m_token;

private:
    void SaveConfig();
    void LoadConfig();

    wxWindow     *m_parent_window;
    wxFileConfig *m_pTPConfig;
    tpicons      *m_ptpicons;
    int           m_weatherfiles_button_id;       // area-first multi-slice
    PlugIn_ViewPort m_last_vp;   // most recent chart view (bValid until set)

    // On-chart area-pick state.
    bool     m_picking = false;   // in pick mode (armed by Start*Pick)
    bool     m_dragging = false;  // a left-drag is in progress
    wxPoint  m_pick_start;        // drag anchor (canvas pixels)
    wxPoint  m_pick_cur;          // current drag point (canvas pixels)
    // What to do with the box on release. Set by StartAreaPick (single-model
    // download dialog) or StartAreaPickMulti (callback into the multi-slice
    // dialog). Cleared after firing.
    std::function<void(const WfBBox&)> m_pick_cb;

    // The modeless multi-slice dialog (null when not open).
    WfMultiSliceDialog* m_multi_dialog = nullptr;
};

#endif  // _WEATHERFILESPI_H_
