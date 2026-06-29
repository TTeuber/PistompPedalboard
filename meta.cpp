// meta.cpp -- see meta.h.

#include "meta.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <random>

using nlohmann::json;

namespace meta {

std::string uuidv4() {
  // One PRNG per thread, seeded once from the platform entropy source. mt19937_64
  // is plenty for unique ids here (not cryptographic, and we don't need it to be).
  static thread_local std::mt19937_64 rng(std::random_device{}());
  uint64_t a = rng();
  uint64_t b = rng();
  a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;  // version 4
  b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;  // variant 10xx

  unsigned char by[16];
  for (int i = 0; i < 8; i++) by[i] = (unsigned char)(a >> (8 * (7 - i)));
  for (int i = 0; i < 8; i++) by[8 + i] = (unsigned char)(b >> (8 * (7 - i)));

  char buf[37];
  std::snprintf(
      buf, sizeof(buf),
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      by[0], by[1], by[2], by[3], by[4], by[5], by[6], by[7], by[8], by[9],
      by[10], by[11], by[12], by[13], by[14], by[15]);
  return std::string(buf);
}

std::string nowIso8601() {
  std::time_t t = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::now());
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buf);
}

std::string contentHash(const nlohmann::json& content) {
  const std::string s = content.dump();
  uint64_t h = 14695981039346656037ULL;  // FNV-1a 64 offset basis
  for (char c : s) {
    h ^= (unsigned char)c;
    h *= 1099511628211ULL;  // FNV prime
  }
  char buf[32];
  std::snprintf(buf, sizeof(buf), "fnv1a64:%016llx", (unsigned long long)h);
  return std::string(buf);
}

void stamp(nlohmann::json& doc, const nlohmann::json& content, int schema) {
  doc["schema"] = schema;
  if (!doc.contains("id") || !doc["id"].is_string() ||
      doc["id"].get<std::string>().empty())
    doc["id"] = uuidv4();
  const std::string now = nowIso8601();
  if (!doc.contains("createdAt") || !doc["createdAt"].is_string())
    doc["createdAt"] = now;
  doc["updatedAt"] = now;
  doc["contentHash"] = contentHash(content);
  if (!doc.contains("origin"))
    doc["origin"] = json{{"by", nullptr}, {"sourceId", nullptr}};
  if (!doc.contains("owner")) doc["owner"] = nullptr;
}

}  // namespace meta
