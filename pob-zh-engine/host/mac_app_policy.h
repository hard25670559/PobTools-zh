#pragma once

#if defined(__APPLE__)

// Keep a process out of the Dock and the app switcher while still letting it
// open and focus windows -- what AppKit calls an accessory application.
//
// Declared by hand rather than through AppKit's headers: this is included from
// files that also reach the Win32 compatibility layer, and <objc/objc.h> and
// Foundation both typedef BOOL the way that layer already spells it.
#include "mac_objc.h"

namespace pob_mac {

using mac_id = pob_objc_id;
using mac_sel = pob_objc_sel;
using mac_class = pob_objc_class;

inline void MakeAccessoryApp()
{
	mac_class appClass = objc_getClass("NSApplication");
	if (!appClass) return;
	// +[NSApplication sharedApplication] creates it if the window system has
	// not been touched yet, which is the case this early.
	mac_id app = ((mac_id(*)(mac_class, mac_sel))objc_msgSend)(appClass, sel_getUid("sharedApplication"));
	if (!app) return;
	// NSApplicationActivationPolicyAccessory == 1
	((bool(*)(mac_id, mac_sel, long))objc_msgSend)(app, sel_getUid("setActivationPolicy:"), 1L);
}

// The other way: a program with a Dock tile and a place in Cmd+Tab.
//
// For a launcher that gave its tile up while POB ran and is about to show its
// own window again (the return-after-exit mode).
inline void MakeRegularApp()
{
	mac_class appClass = objc_getClass("NSApplication");
	if (!appClass) return;
	mac_id app = ((mac_id(*)(mac_class, mac_sel))objc_msgSend)(appClass, sel_getUid("sharedApplication"));
	if (!app) return;
	// NSApplicationActivationPolicyRegular == 0
	((bool(*)(mac_id, mac_sel, long))objc_msgSend)(app, sel_getUid("setActivationPolicy:"), 0L);
}

} // namespace pob_mac

#endif
