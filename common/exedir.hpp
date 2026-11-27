#pragma once
#include <string>
// folder that holds the running executable, without a trailing slash
// returns "." when the os does not tell us (the current folder is used instead)
std::string exe_dir();
