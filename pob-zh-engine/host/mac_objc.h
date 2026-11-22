#pragma once

#if defined(__APPLE__)

// The few Objective-C runtime entry points the macOS-only helpers need,
// declared by hand.
//
// Not <objc/runtime.h>: these headers are included from files that also reach
// the Win32 compatibility layer, and objc/objc.h typedefs BOOL the way that
// layer already spells it. The declarations below are the ones in that header,
// written out, and must stay in the global namespace -- a struct declared
// inside a namespace is a different type to the compiler, which is what makes
// two copies of these conflict.

struct objc_object;
struct objc_selector;
struct objc_class;

using pob_objc_id = objc_object*;
using pob_objc_sel = objc_selector*;
using pob_objc_class = objc_class*;

extern "C" pob_objc_sel sel_getUid(const char*);
extern "C" pob_objc_class objc_getClass(const char*);
extern "C" void objc_msgSend(void);

#endif
