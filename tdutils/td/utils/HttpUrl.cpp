//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2017
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/HttpUrl.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Parser.h"

namespace td {

string HttpUrl::get_url() const {
  string result;
  switch (protocol_) {
    case Protocol::HTTP:
      result += "http://";
      break;
    case Protocol::HTTPS:
      result += "https://";
      break;
    default:
      UNREACHABLE();
  }
  if (!userinfo_.empty()) {
    result += userinfo_;
    result += '@';
  }
  if (is_ipv6) {
    result += '[';
  }
  result += host_;
  if (is_ipv6) {
    result += ']';
  }
  if (specified_port_ > 0) {
    result += ':';
    result += to_string(specified_port_);
  }
  CHECK(!query_.empty() && query_[0] == '/');
  result += query_;
  return result;
}

Result<HttpUrl> parse_url(MutableSlice url, HttpUrl::Protocol default_protocol) {
  // url == [https?://][userinfo@]host[:port]
  Parser parser(url);
  string protocol_str = to_lower(parser.read_till_nofail(':'));

  HttpUrl::Protocol protocol;
  if (parser.start_with("://")) {
    parser.advance(3);
    if (protocol_str == "http") {
      protocol = HttpUrl::Protocol::HTTP;
    } else if (protocol_str == "https") {
      protocol = HttpUrl::Protocol::HTTPS;
    } else {
      return Status::Error("Unsupported URL protocol");
    }
  } else {
    parser = Parser(url);
    protocol = default_protocol;
  }
  Slice userinfo_host_port = parser.read_till_nofail("/?#");

  int port = 0;
  const char *colon = userinfo_host_port.end() - 1;
  while (colon > userinfo_host_port.begin() && *colon != ':' && *colon != ']' && *colon != '@') {
    colon--;
  }
  Slice userinfo_host;
  if (colon > userinfo_host_port.begin() && *colon == ':') {
    port = to_integer<int>(Slice(colon + 1, userinfo_host_port.end()));
    userinfo_host = Slice(userinfo_host_port.begin(), colon);
  } else {
    userinfo_host = userinfo_host_port;
  }
  if (port < 0 || port > 65535) {
    return Status::Error("Wrong port number specified in the URL");
  }

  auto at_pos = userinfo_host.rfind('@');
  Slice userinfo = at_pos == static_cast<size_t>(-1) ? "" : userinfo_host.substr(0, at_pos);
  Slice host = userinfo_host.substr(at_pos + 1);

  bool is_ipv6 = false;
  if (!host.empty() && host[0] == '[' && host.back() == ']') {
    host.remove_prefix(1);
    host.remove_suffix(1);
    is_ipv6 = true;
  }
  if (host.empty()) {
    return Status::Error("URL host is empty");
  }

  int specified_port = port;
  if (port == 0) {
    if (protocol == HttpUrl::Protocol::HTTP) {
      port = 80;
    } else {
      CHECK(protocol == HttpUrl::Protocol::HTTPS);
      port = 443;
    }
  }

  Slice query = parser.read_all();
  while (!query.empty() && is_space(query.back())) {
    query.remove_suffix(1);
  }
  if (query.empty()) {
    query = "/";
  }
  string query_str;
  if (query[0] != '/') {
    query_str = '/';
  }
  for (auto c : query) {
    if (static_cast<unsigned char>(c) <= 0x20) {
      query_str += '%';
      query_str += "0123456789ABCDEF"[c / 16];
      query_str += "0123456789ABCDEF"[c % 16];
    } else {
      query_str += c;
    }
  }

  string host_str = to_lower(host);
  for (size_t i = 0; i < host_str.size(); i++) {
    char c = host_str[i];
    if (('a' <= c && c <= 'z') || c == '.' || ('0' <= c && c <= '9') || c == '-' || c == '_' || c == '!' || c == '$' ||
        c == ',' || c == '~' || c == '*' || c == '\'' || c == '(' || c == ')' || c == ';' || c == '&' || c == '+' ||
        c == '=') {
      // symbols allowed by RFC 7230 and RFC 3986
      continue;
    }
    if (c == '%') {
      c = host_str[++i];
      if (('a' <= c && c <= 'f') || ('0' <= c && c <= '9')) {
        c = host_str[++i];
        if (('a' <= c && c <= 'f') || ('0' <= c && c <= '9')) {
          // percent encoded symbol as allowed by RFC 7230 and RFC 3986
          continue;
        }
      }
    }
    // all other symbols aren't allowed
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 128) {
      // but we allow plain UTF-8 symbols
      continue;
    }
    return Status::Error("Wrong URL host");
  }

  return HttpUrl{protocol, userinfo.str(), std::move(host_str), is_ipv6, specified_port, port, std::move(query_str)};
}

StringBuilder &operator<<(StringBuilder &sb, const HttpUrl &url) {
  sb << tag("protocol", url.protocol_ == HttpUrl::Protocol::HTTP ? "HTTP" : "HTTPS") << tag("userinfo", url.userinfo_)
     << tag("host", url.host_) << tag("port", url.port_) << tag("query", url.query_);
  return sb;
}

}  // namespace td