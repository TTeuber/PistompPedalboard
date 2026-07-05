// store_util.h -- shared helpers for the on-disk library (rigs / presets /
// setlists / manifest).
//
//   * validName(): rig/preset/setlist names arrive from the web API and become
//     filename components; reject anything that could escape the library
//     directories (path separators, a leading dot -- which also covers "." and
//     "..") before it ever touches the filesystem.
//   * writeFileAtomic(): saves go to a ".tmp" sibling, fsync, then rename over
//     the target -- so a power cut mid-save (this is a stage device) leaves the
//     previous file intact instead of a truncated one.

#pragma once

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>

namespace store {

// True if `n` is safe to use as a single filename component. The device's own
// char-picker only emits [A-Za-z0-9 -_], so this only ever rejects hostile or
// malformed web input.
inline bool validName(const std::string& n) {
  if (n.empty() || n.size() > 64) return false;
  if (n.front() == '.') return false;   // hidden files, ".", ".."
  for (char c : n)
    if (c == '/' || c == '\\' || (unsigned char)c < 0x20) return false;
  return true;
}

// Write `data` to `path` atomically: tmp sibling -> flush -> fsync -> rename.
// On any failure the target file is untouched and the tmp is cleaned up.
inline bool writeFileAtomic(const std::filesystem::path& path,
                            const std::string& data) {
  std::filesystem::path tmp = path;
  tmp += ".tmp";
  std::FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) return false;
  bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  if (std::fflush(f) != 0) ok = false;
  if (fsync(fileno(f)) != 0) ok = false;   // data on disk before the rename
  if (std::fclose(f) != 0) ok = false;
  std::error_code ec;
  if (ok) {
    std::filesystem::rename(tmp, path, ec);   // atomic on POSIX
    if (!ec) return true;
  }
  std::filesystem::remove(tmp, ec);
  return false;
}

}  // namespace store
