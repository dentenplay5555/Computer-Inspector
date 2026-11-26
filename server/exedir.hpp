#pragma once
#include <string>
// Folder that holds the running executable, without a trailing slash.
// Returns "." when the OS does not tell us (the current folder is used instead).
std::string exe_dir();
