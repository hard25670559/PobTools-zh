#pragma once

#if defined(__APPLE__)

#include <GLFW/glfw3.h>

#include "mac_objc.h"

// Hold the layer a window is drawn through at the density of the display it is
// on.
//
// The layer carries a density of its own -- the one the display it was made on
// wanted -- and keeps it when the window is carried to a display of another.
// Where that density and the drawing surface disagree, the surface is shown at
// a fraction of the window and the interface sits in a corner of it. Nothing
// the window system or the graphics library reports says which of them is
// right, so the layer is set rather than measured.
//
// Called once a frame: a window can be carried between displays at any time
// and GLFW raises no event for it. Cheap -- two property reads, and a write
// only when they differ.
//
// Shared by the launcher's own window and by the engine, where the whole of
// POB is drawn.

namespace pob_apple {

using layer_id = pob_objc_id;
using layer_sel = pob_objc_sel;

// Not through glfw3native.h: that header typedefs BOOL the way the Win32
// compatibility layer already spells it.
extern "C" void* glfwGetCocoaWindow(GLFWwindow*);

inline layer_id Send(layer_id self, const char* name)
{
	return ((layer_id(*)(layer_id, layer_sel))objc_msgSend)(self, sel_getUid(name));
}

inline double SendDouble(layer_id self, const char* name)
{
	return ((double(*)(layer_id, layer_sel))objc_msgSend)(self, sel_getUid(name));
}

inline void SendSetDouble(layer_id self, const char* name, double value)
{
	((void(*)(layer_id, layer_sel, double))objc_msgSend)(self, sel_getUid(name), value);
}

// Returns the density now in force, or 0 if the window could not be reached.
inline double HoldLayerScale(GLFWwindow* window)
{
	if (!window) return 0.0;
	layer_id nswindow = (layer_id)glfwGetCocoaWindow(window);
	if (!nswindow) return 0.0;
	layer_id view = Send(nswindow, "contentView");
	if (!view) return 0.0;
	layer_id layer = Send(view, "layer");
	if (!layer) return 0.0;
	const double want = SendDouble(nswindow, "backingScaleFactor");
	const double have = SendDouble(layer, "contentsScale");
	if (!(want > 0.125 && want < 8.0)) return have;
	if (have < want - 0.01 || have > want + 0.01) {
		SendSetDouble(layer, "setContentsScale:", want);
		layer_id subs = Send(layer, "sublayers");
		const long count = subs ? ((long(*)(layer_id, layer_sel))objc_msgSend)(subs, sel_getUid("count")) : 0;
		for (long i = 0; i < count; ++i) {
			layer_id sub = ((layer_id(*)(layer_id, layer_sel, long))objc_msgSend)(subs, sel_getUid("objectAtIndex:"), i);
			if (sub) SendSetDouble(sub, "setContentsScale:", want);
		}
	}
	return want;
}

} // namespace pob_apple

#endif
