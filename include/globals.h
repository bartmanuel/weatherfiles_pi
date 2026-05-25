/******************************************************************************
 * WeatherFiles OpenCPN plugin — shared constants/globals.
 * GPL-3.0 (see LICENSE).
 ******************************************************************************/
#ifndef _TPGLOBALS_H_
#define _TPGLOBALS_H_

class tpicons;

#define weatherfiles_POSITION -1
#define ID_NONE -1

// Needed for the vendored ocpndc/dc_utils to link (normally lives in
// glChartCanvas.cpp in the OpenCPN core).
extern float g_GLMinSymbolLineWidth;

#endif  // _TPGLOBALS_H_
