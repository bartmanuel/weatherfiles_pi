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

#include "wxWTranslateCatalog.h"

#include <cstdint>
#include "ocpn_plugin.h"

#include <wx/string.h>
#include <wx/fileconf.h>

#include "globals.h"

class tpicons;

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
    bool RenderOverlay(wxDC& dc, PlugIn_ViewPort* vp);
    void SetCurrentViewPort(PlugIn_ViewPort& vp);

    // WeatherFiles API personal access token. Public so the dialogs can read it
    // (set via ShowPreferencesDialog, persisted in the OpenCPN config).
    wxString m_token;

private:
    void SaveConfig();
    void LoadConfig();

    wxWindow     *m_parent_window;
    wxFileConfig *m_pTPConfig;
    tpicons      *m_ptpicons;
    int           m_weatherfiles_button_id;
    PlugIn_ViewPort m_last_vp;   // most recent chart view (bValid until set)
};

#endif  // _WEATHERFILESPI_H_
