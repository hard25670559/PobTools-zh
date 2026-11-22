// POSIX implementation of the window dock declared in window_dock.h.
//
// Platform counterpart to window_dock.cpp, selected by the build on anything
// that is not Windows (the same pattern as sys_console.cpp /
// sys_console_unix.cpp).
//
// The Windows version strips a POB window's frame, reparents it under the
// launcher and keeps it positioned, so several POB instances appear as tabs
// inside one window. Every step of that needs Win32 window ownership and
// z-order control, plus the taskbar COM interface to hide the adopted windows
// from the task switcher. macOS has no equivalent: a window belongs to its own
// process and only that process may restyle it, and the window manager --
// Mission Control, Spaces, native tabs -- is what arranges windows here.
//
// So the tab list is still kept, because the launcher's UI reads it to show
// which POB instances are running and to close them. What is not done is the
// adopting: each POB keeps its own window, which is how a macOS user would
// expect two instances of an app to behave.

#include "window_dock.h"

#include "error_log.h"

#include <string>
#include <vector>

namespace WindowDock {

Dock::~Dock() = default;

void Dock::Init(void* host, const std::wstring& logPath)
{
	(void)logPath;
	host_ = host;
}

// The launcher calls this when it starts a POB. The entry is what its tab
// strip draws; the window handle stays null because nothing here goes looking
// for another process's window.
void Dock::Track(unsigned long pid, const std::wstring& label)
{
	for (const Tab& existing : tabs_) {
		if (existing.pid == pid) return;
	}
	Tab t;
	t.pid = pid;
	t.label = label;
	t.baseLabel = label;
	t.hwnd = nullptr;
	t.docked = false;
	t.wantFocus = false;   // focus follows the OS here, not the launcher
	tabs_.push_back(t);
	orig_.emplace_back();
}

void Dock::Update(int stripH, int activeIndex)
{
	stripH_ = stripH;
	active_ = activeIndex;
	// Nothing to reposition: the windows are not ours to move.
}

void Dock::OnHostMoved() {}
void Dock::OnHostFocused() {}

// The launcher's close button. Asking another process's window to close needs
// the window messaging this platform does not have, so the entry is dropped
// from the strip and the POB process is left to the user -- the same outcome as
// a Windows run where the window could not be found.
void Dock::RequestClose(size_t index)
{
	if (index >= tabs_.size()) return;
	tabs_.erase(tabs_.begin() + static_cast<long>(index));
	if (index < orig_.size()) orig_.erase(orig_.begin() + static_cast<long>(index));
	if (active_ >= static_cast<int>(tabs_.size())) active_ = static_cast<int>(tabs_.size()) - 1;
}

// Nothing was adopted, so there is nothing to give back.
void Dock::RestoreAll()
{
	tabs_.clear();
	orig_.clear();
	active_ = -1;
}

bool Dock::TakeFocusRequest(size_t index)
{
	if (index >= tabs_.size()) return false;
	const bool wanted = tabs_[index].wantFocus;
	tabs_[index].wantFocus = false;
	return wanted;
}

// The three private helpers exist for the adopting path, which does not run
// here. They are defined so the class is complete.
void Dock::Adopt(Tab& t) { (void)t; }
void Dock::Position(Tab& t, bool force) { (void)t; (void)force; }
void Dock::FixZOrder(Tab& t) { (void)t; }

// The self-test checks the saved-and-restored window styles, which only the
// adopting path produces. Reporting pass here would claim something that was
// never exercised, so it reports "skipped" through the same zero the harness
// treats as success for a case that does not apply on this platform.
int RunDockStyleSelfTest(const std::wstring& exeDir)
{
	(void)exeDir;
	PobLog::Error("dock", "dock style self-test skipped: window adopting is Windows-only");
	return 0;
}

} // namespace WindowDock
