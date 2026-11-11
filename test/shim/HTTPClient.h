#pragma once
#include "Arduino.h"
#include "WiFiClientSecure.h"
#include <fstream>
#include <sstream>
#include <vector>
// URL-Präfix -> Datei; Status 200 wenn Datei existiert, sonst Code aus Mock-Tabelle
struct MockRoute { std::string prefix, file; int code; int truncateOnce = 0; };
extern std::vector<MockRoute> g_routes;
extern std::vector<std::string> g_requested;
class HTTPClient {
  std::string url_, body_; int code_ = 0; MemStream stream_;
 public:
  void setConnectTimeout(int) {} void setTimeout(int) {} void setUserAgent(const char*) {} void useHTTP10(bool) {}
  bool begin(WiFiClientSecure&, const String& u) { url_ = u.s; return true; }
  int GET() {
    g_requested.push_back(url_);
    for (auto& r : g_routes) if (url_.compare(0, r.prefix.size(), r.prefix) == 0) {
      if (r.code != 200) return code_ = r.code;
      std::ifstream f(r.file, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); body_ = ss.str();
      if (r.truncateOnce > 0) { r.truncateOnce--; body_ = body_.substr(0, body_.size() / 2); }   // abgebrochene Übertragung
      stream_.data = body_; stream_.pos = 0; return code_ = 200;
    }
    return code_ = 404;
  }
  String getString() { return String(body_); }
  Stream& getStream() { return stream_; }
  void end() {}
};
