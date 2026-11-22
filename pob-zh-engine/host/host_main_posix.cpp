// POSIX entry point for the launcher.
//
// Windows starts a GUI process at wWinMain and the launcher reads its
// arguments back out of GetCommandLineW. POSIX starts at main with argv
// already in hand, so this stores the command line where the shim's
// GetCommandLineW can find it, then calls the same wWinMain -- which keeps the
// whole startup path identical on both platforms rather than forking it.

#include "../engine/common/win_compat.h"

// Defined in host_main.cpp, which is built on both platforms.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int);

int main(int argc, char** argv)
{
	pob_win_compat::SaveCommandLine(argc, argv);
	return wWinMain(nullptr, nullptr, nullptr, 1 /* SW_SHOWNORMAL */);
}
