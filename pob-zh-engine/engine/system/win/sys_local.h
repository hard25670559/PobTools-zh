// SimpleGraphic Engine
// (c) David Gowor, 2014
//
// System Local Header
// Platform: Windows
//

#include <functional>
#include <mutex>
#include "common.h"

#include "system.h"

#ifdef _WIN32
#define _WIN32_WINNT _WIN32_WINNT_WIN7
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shellapi.h>
#include <mmsystem.h>
#endif

#include <chrono>
#include <vector>

// =======
// Classes
// =======

// System Main: sys_main.cpp
class sys_main_c: public sys_IMain {
public:
	// Interface
	bool	PacesItsOwnFrames() { return pollsOwnEvents; }
	int		GetTime();
	void	Sleep(int msec);
	bool	IsKeyDown(byte key);
	void	ClipboardCopy(const char* str);
	char*	ClipboardPaste();
	bool	SetWorkDir(std::filesystem::path const& newCwd = {});
	void	SpawnProcess(std::filesystem::path cmdName, const char* argList);
	std::optional<std::string> OpenURL(const char* url); // return value has failure reason
	void	Error(const char* fmt, ...);
	void	Exit(const char* msg = NULL);
	void	Restart();

	// Encapsulated
	sys_main_c();

	bool	Run(int argc, char** argv);

	// Run, taken apart so several engines can be driven from one thread:
	// open one, step each in turn, shut down the ones that are finished.
	// Run itself still calls these in the order it always had.
	bool	RunStart(int argc, char** argv);
	bool	RunOneFrame();
	void	RunCoreShutdown();
	bool	RunFinish();

	byte	GlfwKeyToKey(int key, int scancode);
	char	GlfwKeyExtraChar(int key);

#ifdef _WIN32
	HINSTANCE hinst = nullptr;
	HICON	icon = nullptr;
#endif

	class core_IMain* core = nullptr;

	bool	initialised = false;
	// This engine takes the window system's events itself. True when Run
	// drives the frame loop; false when something outside steps several
	// engines and takes the events once for all of them.
	bool	pollsOwnEvents = false;

	// Held while this engine runs a frame, and by every window callback that
	// touches it.
	//
	// Events arrive on the main thread -- macOS takes them nowhere else -- so
	// with the frame loop on a thread of its own the two meet in POB's state:
	// a click rewriting what a frame is in the middle of reading. One at a
	// time. Frames are milliseconds and callbacks are shorter still, so the
	// wait is never felt.
	std::recursive_mutex frameLock;

	// Input waits here until this engine's own thread picks it up.
	//
	// macOS delivers events on the main thread and nowhere else, and handling
	// one runs POB's script. With the frame loop on a thread of its own that
	// means two threads in the same script at once -- which no lock makes
	// safe, because the script engine assumes the thread it is on stays the
	// same. So the main thread only writes down what happened; the engine's
	// thread replays it at the top of its next frame, and everything that
	// touches POB or the interface stays on that one thread.
	std::vector<std::function<void()>> pendingInput;
	std::mutex inputLock;

	void	QueueInput(std::function<void()> fn);
	void	DrainInput();

	// The other direction: work that only the main thread may do.
	//
	// macOS refuses window operations -- the title, the size, the position --
	// anywhere but the main thread, and raises rather than ignoring it. POB
	// sets its window title from the script, which runs on this engine's own
	// thread, so the call is handed back instead of made there.
	//
	// One queue for the whole process: there is one main thread, and it runs
	// these where it takes events (PobEnginePollEvents).
	static void	OnMainThread(std::function<void()> fn);
	// The same, but waits for it. For window calls whose effect the caller
	// needs before it goes on -- hiding a window, destroying one.
	static void	OnMainThreadSync(std::function<void()> fn);
	static void	RunMainThreadWork();
	bool	minimized = false;
	// POB_ZH_HEADLESS=1: no GLFW window, frame loop paced by stdin requests,
	// errors go to the failure log and a non-zero exit code instead of a
	// console window that waits for someone to close it.
	bool	headless = false;
	int		exitCode = 0;
	volatile bool	exitFlag = false;
	volatile bool	restartFlag = false;
	char*	exitMsg = nullptr;
	char*	threadError = nullptr;
	bool	errorRaised = false;
	std::chrono::system_clock::time_point baseTime;
	std::vector<uint8_t> heldKeyState;
};
