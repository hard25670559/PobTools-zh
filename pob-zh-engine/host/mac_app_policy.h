#pragma once

#if defined(__APPLE__)

// Keep a process out of the Dock and the app switcher while still letting it
// open and focus windows -- what AppKit calls an accessory application.
//
// Declared by hand rather than through AppKit's headers: this is included from
// files that also reach the Win32 compatibility layer, and <objc/objc.h> and
// Foundation both typedef BOOL the way that layer already spells it.
namespace pob_mac {

using mac_id = struct objc_object*;
using mac_sel = struct objc_selector*;
using mac_class = struct objc_class*;

extern "C" mac_sel sel_getUid(const char*);
extern "C" mac_class objc_getClass(const char*);
extern "C" void objc_msgSend(void);

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

} // namespace pob_mac

#endif
