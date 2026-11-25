// Settings... in the application menu. See mac_menu.h for why this is its own
// file: the Objective-C runtime's BOOL and the Win32 compatibility layer's
// cannot both be in one translation unit.

#if defined(__APPLE__)

#include "mac_menu.h"

#include <objc/message.h>
#include <objc/runtime.h>

namespace {

using menu_id = id;
using menu_sel = SEL;

void (*g_action)() = nullptr;
void (*g_quit_action)() = nullptr;

menu_id Send(menu_id self, const char* name)
{
	return ((menu_id(*)(menu_id, menu_sel))objc_msgSend)(self, sel_getUid(name));
}

menu_id Str(const char* utf8)
{
	Class cls = objc_getClass("NSString");
	return ((menu_id(*)(Class, menu_sel, const char*))objc_msgSend)(
	    cls, sel_getUid("stringWithUTF8String:"), utf8);
}

// What the menu item calls; the shape the runtime expects of a method.
void Invoke(menu_id, menu_sel, menu_id)
{
	if (g_action) g_action();
}

void InvokeQuit(menu_id, menu_sel, menu_id)
{
	if (g_quit_action) g_quit_action();
}

} // namespace

namespace pob_mac {

void AddSettingsMenuItem(const char* title, void (*action)())
{
	g_action = action;

	Class appClass = objc_getClass("NSApplication");
	if (!appClass) return;
	menu_id app = ((menu_id(*)(Class, menu_sel))objc_msgSend)(appClass, sel_getUid("sharedApplication"));
	if (!app) return;

	menu_id mainMenu = Send(app, "mainMenu");
	if (!mainMenu) return;
	menu_id appItem = ((menu_id(*)(menu_id, menu_sel, long))objc_msgSend)(
	    mainMenu, sel_getUid("itemAtIndex:"), 0L);
	if (!appItem) return;
	menu_id appMenu = Send(appItem, "submenu");
	if (!appMenu) return;

	// A class of one method, to be what the item calls.
	static Class targetClass = nullptr;
	static menu_id target = nullptr;
	if (!targetClass) {
		targetClass = objc_allocateClassPair(objc_getClass("NSObject"), "PobSettingsTarget", 0);
		if (!targetClass) return;
		class_addMethod(targetClass, sel_getUid("pobOpenSettings:"), (IMP)Invoke, "v@:@");
		objc_registerClassPair(targetClass);
		target = Send(((menu_id(*)(Class, menu_sel))objc_msgSend)(targetClass, sel_getUid("alloc")), "init");
	}
	if (!target) return;

	Class itemClass = objc_getClass("NSMenuItem");
	if (!itemClass) return;
	menu_id item = ((menu_id(*)(Class, menu_sel))objc_msgSend)(itemClass, sel_getUid("alloc"));
	item = ((menu_id(*)(menu_id, menu_sel, menu_id, menu_sel, menu_id))objc_msgSend)(
	    item, sel_getUid("initWithTitle:action:keyEquivalent:"),
	    Str(title), sel_getUid("pobOpenSettings:"), Str(","));
	if (!item) return;
	((void(*)(menu_id, menu_sel, menu_id))objc_msgSend)(item, sel_getUid("setTarget:"), target);

	// Under About, where a Mac program keeps it, with a separator after.
	((void(*)(menu_id, menu_sel, menu_id, long))objc_msgSend)(
	    appMenu, sel_getUid("insertItem:atIndex:"), item, 1L);
	menu_id sep = ((menu_id(*)(Class, menu_sel))objc_msgSend)(itemClass, sel_getUid("separatorItem"));
	if (sep) {
		((void(*)(menu_id, menu_sel, menu_id, long))objc_msgSend)(
		    appMenu, sel_getUid("insertItem:atIndex:"), sep, 2L);
	}
}

void TakeOverQuitMenuItem(void (*action)())
{
	g_quit_action = action;

	Class appClass = objc_getClass("NSApplication");
	if (!appClass) return;
	menu_id app = ((menu_id(*)(Class, menu_sel))objc_msgSend)(appClass, sel_getUid("sharedApplication"));
	if (!app) return;
	menu_id mainMenu = Send(app, "mainMenu");
	if (!mainMenu) return;
	menu_id appItem = ((menu_id(*)(menu_id, menu_sel, long))objc_msgSend)(
	    mainMenu, sel_getUid("itemAtIndex:"), 0L);
	if (!appItem) return;
	menu_id appMenu = Send(appItem, "submenu");
	if (!appMenu) return;

	static Class quitClass = nullptr;
	static menu_id quitTarget = nullptr;
	if (!quitClass) {
		quitClass = objc_allocateClassPair(objc_getClass("NSObject"), "PobQuitTarget", 0);
		if (!quitClass) return;
		class_addMethod(quitClass, sel_getUid("pobQuit:"), (IMP)InvokeQuit, "v@:@");
		objc_registerClassPair(quitClass);
		quitTarget = Send(((menu_id(*)(Class, menu_sel))objc_msgSend)(quitClass, sel_getUid("alloc")), "init");
	}
	if (!quitTarget) return;

	// The item that ends the process, wherever the window library put it.
	const long n = (long)((long(*)(menu_id, menu_sel))objc_msgSend)(appMenu, sel_getUid("numberOfItems"));
	for (long i = 0; i < n; i++) {
		menu_id it = ((menu_id(*)(menu_id, menu_sel, long))objc_msgSend)(
		    appMenu, sel_getUid("itemAtIndex:"), i);
		if (!it) continue;
		menu_sel act = ((menu_sel(*)(menu_id, menu_sel))objc_msgSend)(it, sel_getUid("action"));
		if (act != sel_getUid("terminate:")) continue;
		((void(*)(menu_id, menu_sel, menu_id))objc_msgSend)(it, sel_getUid("setTarget:"), quitTarget);
		((void(*)(menu_id, menu_sel, menu_sel))objc_msgSend)(
		    it, sel_getUid("setAction:"), sel_getUid("pobQuit:"));
		break;
	}
}

} // namespace pob_mac

#endif
