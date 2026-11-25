#pragma once

#if defined(__APPLE__)

// The Settings item in the application menu.
//
// Every Mac program has one, with Command-comma beside it, and people look
// there to find out the shortcut exists at all. The window library builds the
// menu (About, Services, Hide, Quit) but has nothing to say about a program's
// own settings, so the item is added here.
//
// Declared apart from its implementation because that has to include the
// Objective-C runtime, whose BOOL is a different type from the one the Win32
// compatibility layer defines -- the two cannot meet in one file.
namespace pob_mac {

// Adds "Settings... <Cmd-,>" under About. Call after glfwInit, once.
void AddSettingsMenuItem(const char* title, void (*action)());

// Makes Quit (<Cmd-Q>) call `action` instead of ending the process.
//
// The program ends with POB inside it, so quitting has to go through POB's
// "save before exiting?" first. The handler is expected to start that and let
// the program end once every POB has answered.
void TakeOverQuitMenuItem(void (*action)());

} // namespace pob_mac

#endif
