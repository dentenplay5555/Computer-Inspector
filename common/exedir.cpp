#include "exedir.hpp"
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <vector>
#else
#include <unistd.h>
#include <limits.h>
#endif

std::string exe_dir() {
  std::string path;
#ifdef _WIN32
  char buf[MAX_PATH];
  DWORD n=GetModuleFileNameA(nullptr,buf,MAX_PATH);
  if (n>0&&n<MAX_PATH) path.assign(buf,n);
#elif defined(__APPLE__)
  uint32_t size=0;
  _NSGetExecutablePath(nullptr,&size);
  std::vector<char> buf(size+1,0);
  if (_NSGetExecutablePath(buf.data(),&size)==0) path=buf.data();
#else
  char buf[PATH_MAX];
  ssize_t n=readlink("/proc/self/exe",buf,sizeof(buf)-1);
  if (n>0) path.assign(buf,(size_t)n);
#endif
  if (path.empty()) return ".";
  std::error_code ec;
  std::filesystem::path p=std::filesystem::weakly_canonical(path,ec);
  if (ec) p=path;
  return p.parent_path().string();
}
