// DyLua: SimpleGraphic
// (c) David Gowor, 2014
//
// Entry Point
// Platform: Windows
//

#include "system/win/sys_local.h"
#include "startup_trace.h"

#include <GLFW/glfw3.h>
#if 0
#pragma comment(lib, "winmm")
#pragma comment(lib, "opengl32")
#include "_lib/lua.h"
#if LUA_VERSION_NUM >= 503
#if defined(_WIN64) && defined(_DEBUG)
#pragma comment(lib, "_lib\\lua53_64MT_D")
#elif defined(_WIN64)
#pragma comment(lib, "_lib\\lua53_64MT")
#elif defined(_DEBUG)
#pragma comment(lib, "_lib\\lua53_32MT_D")
#else
#pragma comment(lib, "_lib\\lua53_32MT")
#endif
#else
#if defined(_WIN64) 
#pragma comment(lib, "_lib\\lua51_64")
#else
#pragma comment(lib, "_lib\\lua51_32")
#endif
#endif
#if defined(_WIN64) && defined(_DEBUG)
#pragma comment(lib, "_lib\\zlib64MT_D")
#pragma comment(lib, "_lib\\jpeg64MT_D")
#pragma comment(lib, "_lib\\png64MT_D")
#pragma comment(lib, "_lib\\gif64MT_D")
#elif defined(_WIN64)
#pragma comment(lib, "_lib\\zlib64MT")
#pragma comment(lib, "_lib\\jpeg64MT")
#pragma comment(lib, "_lib\\png64MT")
#pragma comment(lib, "_lib\\gif64MT")
#elif defined(_DEBUG)
#pragma comment(lib, "_lib\\zlib32MT_D")
#pragma comment(lib, "_lib\\jpeg32MT_D")
#pragma comment(lib, "_lib\\png32MT_D")
#pragma comment(lib, "_lib\\gif32MT_D")
#else
#pragma comment(lib, "_lib\\zlib32MT")
#pragma comment(lib, "_lib\\jpeg32MT")
#pragma comment(lib, "_lib\\png32MT")
#pragma comment(lib, "_lib\\gif32MT")
#endif
#endif

#if defined _WIN32 || defined __CYGWIN__
  #ifdef SIMPLEGRAPHIC_EXPORTS
    #ifdef __GNUC__
      #define SIMPLEGRAPHIC_DLL_PUBLIC __attribute__ ((dllexport))
    #else
      #define SIMPLEGRAPHIC_DLL_PUBLIC __declspec(dllexport) // Note: actually gcc seems to also supports this syntax.
    #endif
  #else
    #ifdef __GNUC__
      #define SIMPLEGRAPHIC_DLL_PUBLIC __attribute__ ((dllimport))
    #else
      #define SIMPLEGRAPHIC_DLL_PUBLIC __declspec(dllimport) // Note: actually gcc seems to also supports this syntax.
    #endif
  #endif
  #define SIMPLEGRAPHIC_DLL_LOCAL
#else
  #if __GNUC__ >= 4
    #define SIMPLEGRAPHIC_DLL_PUBLIC __attribute__ ((visibility ("default")))
    #define SIMPLEGRAPHIC_DLL_LOCAL  __attribute__ ((visibility ("hidden")))
  #else
    #define SIMPLEGRAPHIC_DLL_PUBLIC
    #define SIMPLEGRAPHIC_DLL_LOCAL
  #endif
#endif

// Driving several POBs from one thread.
//
// RunLuaFileAsWin owns the whole program until its window closes, so a second
// POB cannot be started while the first is up. These three take the same
// engine apart into the steps a caller can interleave: open one, step each of
// them in turn, close the ones that are finished.
//
// Why it has to be this way on macOS: windows may only be made and serviced on
// the main thread, so "one thread per POB" is not available. One thread taking
// turns is. That in turn is what lets POB's windows belong to the launcher's
// own process -- one icon in the Dock, with every open build listed under it,
// which is what a Mac application looks like.
//
// Nothing here changes RunLuaFileAsWin, which still opens one POB and runs it
// to the end.

extern "C" SIMPLEGRAPHIC_DLL_PUBLIC void* PobEngineOpen(int argc, char** argv)
{
	startup_trace_begin("engine");
	startup_trace_mark("PobEngineOpen entered");
	auto* sys = new sys_main_c;
	if (!sys->RunStart(argc, argv)) {
		sys->RunFinish();
		delete sys;
		return nullptr;
	}
	// Let go of the link to the graphics card before handing this engine over.
	//
	// A link belongs to one thread at a time. Opening the window takes it on
	// whichever thread called in -- the main one -- and a frame loop on a
	// thread of its own then cannot have it: its drawing is refused and the
	// window stays black. Released here, each engine's own thread claims it at
	// the top of every frame (sys_main_c::RunOneFrame) and keeps it.
	glfwMakeContextCurrent(nullptr);
	return sys;
}

// False once this POB is finished; the caller then closes it.
// The caller takes the window system's events itself, once per round, before
// stepping any engine -- see the comment in sys_main.cpp's frame loop.
// Window work the engines' threads handed back, without taking events.
//
// For a host that runs its own event loop -- the launcher, which has a window
// of its own -- and only needs the engines' window calls to happen on this
// thread.
extern "C" SIMPLEGRAPHIC_DLL_PUBLIC void PobEngineRunMainWork(void)
{
	sys_main_c::RunMainThreadWork();
}

extern "C" SIMPLEGRAPHIC_DLL_PUBLIC void PobEnginePollEvents(void)
{
	glfwPollEvents();
	// Window work the engines' own threads handed back (sys_local.h).
	sys_main_c::RunMainThreadWork();
}

extern "C" SIMPLEGRAPHIC_DLL_PUBLIC int PobEngineStep(void* handle)
{
	if (!handle) return 0;
	return static_cast<sys_main_c*>(handle)->RunOneFrame() ? 1 : 0;
}

extern "C" SIMPLEGRAPHIC_DLL_PUBLIC int PobEngineClose(void* handle)
{
	if (!handle) return 0;
	auto* sys = static_cast<sys_main_c*>(handle);
	sys->RunCoreShutdown();
	sys->RunFinish();
	const int rc = sys->exitCode;
	delete sys;
	return rc;
}

extern "C" SIMPLEGRAPHIC_DLL_PUBLIC int RunLuaFileAsWin(int argc, char** argv)
{
#ifdef _MEMTRAK_H
	strcpy_s(_memTrak_reportName, 512, "SimpleGraphic/memtrak.log");
#endif

#ifdef _WIN32
	timeBeginPeriod(1);
#endif

	// Startup timeline for the engine child (PobTools\startup_engine.txt).
	startup_trace_begin("engine");
	startup_trace_mark("RunLuaFileAsWin entered");

	sys_main_c* sys = new sys_main_c;

	while (sys->Run(argc, argv));

	// Non-zero only in headless mode (sys_main.cpp): the classic window reports
	// its errors on screen and the host never looked at this value.
	const int rc = sys->exitCode;
	delete sys;

#ifdef _WIN32
	timeEndPeriod(1);
#endif
	return rc;
}
