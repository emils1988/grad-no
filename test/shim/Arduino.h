// Minimaler Arduino-Ersatz für Host-Tests der Parser (nicht für das Gerät)
#pragma once
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <algorithm>
#include <chrono>
using std::min; using std::max;
template <class T, class L, class H> T constrain(T v, L lo, H hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline unsigned long millis() {
  using namespace std::chrono;
  static auto t0 = steady_clock::now();
  return (unsigned long)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}
inline size_t strlcpy(char* d, const char* s, size_t n) {
  size_t l = strlen(s); if (n) { size_t c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; } return l;
}
class String {
 public:
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& x) : s(x) {}
  String(char c) : s(1, c) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(unsigned long v) : s(std::to_string(v)) {}
  const char* c_str() const { return s.c_str(); }
  unsigned length() const { return s.size(); }
  bool concat(const char* c) { s += c; return true; }
  bool concat(const char* c, unsigned n) { s.append(c, n); return true; }
  bool concat(char c) { s += c; return true; }
  void reserve(unsigned n) { s.reserve(n); }
  String& operator+=(const String& o) { s += o.s; return *this; }
  String& operator+=(const char* o) { s += o; return *this; }
  String& operator+=(char c) { s += c; return *this; }
  String& operator+=(int v) { s += std::to_string(v); return *this; }
  char operator[](unsigned i) const { return i < s.size() ? s[i] : 0; }
  bool operator==(const String& o) const { return s == o.s; }
  bool operator==(const char* o) const { return s == o; }
  bool operator!=(const String& o) const { return s != o.s; }
  bool operator!=(const char* o) const { return s != o; }
  bool operator<(const String& o) const { return s < o.s; }
  int indexOf(char c, unsigned from = 0) const { auto p = s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const String& x, unsigned from = 0) const { auto p = s.find(x.s, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const char* x, unsigned from = 0) const { auto p = s.find(x, from); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(char c) const { auto p = s.rfind(c); return p == std::string::npos ? -1 : (int)p; }
  String substring(unsigned a) const { return a >= s.size() ? String() : String(s.substr(a)); }
  String substring(unsigned a, unsigned b) const { if (a > b) std::swap(a, b); if (a >= s.size()) return String(); return String(s.substr(a, b - a)); }
  void trim() { size_t a = s.find_first_not_of(" \t\r\n"); if (a == std::string::npos) { s.clear(); return; } size_t b = s.find_last_not_of(" \t\r\n"); s = s.substr(a, b - a + 1); }
  void toLowerCase() { for (auto& c : s) c = tolower((unsigned char)c) ; }
  void toUpperCase() { for (auto& c : s) c = toupper((unsigned char)c); }
  bool startsWith(const String& p) const { return s.compare(0, p.s.size(), p.s) == 0; }
  bool startsWith(const char* p) const { return startsWith(String(p)); }
  void replace(const String& a, const String& b) { if (a.s.empty()) return; size_t p = 0; while ((p = s.find(a.s, p)) != std::string::npos) { s.replace(p, a.s.size(), b.s); p += b.s.size(); } }
  void remove(unsigned i, unsigned n) { if (i < s.size()) s.erase(i, n); }
  void remove(unsigned i) { if (i < s.size()) s.erase(i); }
  void setCharAt(unsigned i, char c) { if (i < s.size()) s[i] = c; }
  int toInt() const { return atoi(s.c_str()); }
};
inline String operator+(const String& a, const String& b) { return String(a.s + b.s); }
inline String operator+(const String& a, const char* b) { return String(a.s + b); }
inline String operator+(const char* a, const String& b) { return String(std::string(a) + b.s); }
inline String operator+(const String& a, int b) { return String(a.s + std::to_string(b)); }
inline String operator+(const String& a, char b) { return String(a.s + b); }
inline void delay(unsigned long) {}
// Stream wie bei Arduino: read/available/peek virtuell, readBytes/readStringUntil darauf aufbauend
class Stream {
 public:
  virtual ~Stream() {}
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual size_t write(uint8_t) { return 0; }
  size_t readBytes(char* b, size_t n) { size_t k = 0; while (k < n) { int c = read(); if (c < 0) break; b[k++] = (char)c; } return k; }
  String readStringUntil(char t) { std::string r; int c; while ((c = read()) >= 0 && c != t) r += (char)c; return String(r); }
};
class MemStream : public Stream {
 public:
  std::string data; size_t pos = 0;
  int available() override { return (int)(data.size() - pos); }
  int read() override { return pos < data.size() ? (unsigned char)data[pos++] : -1; }
  int peek() override { return pos < data.size() ? (unsigned char)data[pos] : -1; }
};
class Print {
 public:
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* b, size_t n) { size_t k = 0; while (n--) k += write(*b++); return k; }
  virtual ~Print() {}
};
class Printable { public: virtual size_t printTo(Print& p) const = 0; virtual ~Printable() {} };
