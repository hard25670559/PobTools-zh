// The modern UI on this platform, bridged to the browser path.
//
// modern_ui_window.cpp hosts the page in a WebView2 control and is the only
// definition of these three functions on Windows. WebView2 does not exist
// here, but the author already wrote the fallback that serves the same page to
// the system browser over a loopback server (modern_ui_browser.cpp) for the
// case where WebView2 cannot run -- Wine being the one named in that file.
//
// So on this platform the fallback is not a fallback: it is the only path, and
// these three forward to it. Everything the launcher knows about the modern UI
// stays the same.

#include "modern_ui_window.h"
#include "modern_ui_browser.h"

#include <string>

bool ModernUiAvailable(const std::wstring& exeDir, std::wstring* why)
{
	if (ModernUiBrowserAvailable(exeDir)) return true;
	if (why != nullptr) {
		*why = L"the modern UI pages are missing (ui/index.html, ui/theme2.html)";
	}
	return false;
}

// Windows checks the installed WebView2 runtime's version here. The browser
// path has no such requirement -- any browser renders the same page -- so the
// only question left is whether the pages are present.
bool ModernUiUsableFor(const std::wstring& exeDir, const std::wstring& pobDir,
                       const std::string& gate)
{
	(void)pobDir; (void)gate;
	return ModernUiBrowserAvailable(exeDir);
}

int ShowModernUi(const std::wstring& exeDir, const std::wstring& game,
                 const std::wstring& locale, const LauncherConfig& cfg,
                 const std::wstring& openBuild, const std::wstring& importUri)
{
	return ShowModernUiInBrowser(exeDir, game, locale, cfg, openBuild,
	                             std::wstring(), importUri);
}
