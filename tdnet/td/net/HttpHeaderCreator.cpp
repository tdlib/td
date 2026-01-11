//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/net/HttpHeaderCreator.h"

#include "td/utils/logging.h"

namespace td {

void HttpHeaderCreator::add_host_header(HttpUrl::Protocol protocol, Slice host, int32 port) {
  sb_ << "Host: ";
  if ((protocol == HttpUrl::Protocol::Https && port == 443) || (protocol == HttpUrl::Protocol::Http && port == 80)) {
    sb_ << host;
  } else {
    sb_ << host << ':' << port;
  }
  sb_ << "\r\n";
}

string HttpHeaderCreator::get_host_header(HttpUrl::Protocol protocol, Slice host, int32 port) {
  if ((protocol == HttpUrl::Protocol::Https && port == 443) || (protocol == HttpUrl::Protocol::Http && port == 80)) {
    return host.str();
  } else {
    return PSTRING() << host << ':' << port;
  }
}

Result<Slice> HttpHeaderCreator::finish(Slice content) {
  sb_ << "\r\n";
  if (!content.empty()) {
    sb_ << content;
  }
  if (sb_.is_error()) {
    return Status::Error("Too many headers");
  }
  return sb_.as_cslice();
}

CSlice HttpHeaderCreator::get_status_line(int http_status_code) {
  if (http_status_code == 200) {
    return CSlice("OK");
  }
  switch (http_status_code) {
    case 201:
      return CSlice("Created");
    case 202:
      return CSlice("Accepted");
    case 204:
      return CSlice("No Content");
    case 206:
      return CSlice("Partial Content");
    case 301:
      return CSlice("Moved Permanently");
    case 302:
      return CSlice("Found");
    case 303:
      return CSlice("See Other");
    case 304:
      return CSlice("Not Modified");
    case 307:
      return CSlice("Temporary Redirect");
    case 308:
      return CSlice("Permanent Redirect");
    case 400:
      return CSlice("Bad Request");
    case 401:
      return CSlice("Unauthorized");
    case 403:
      return CSlice("Forbidden");
    case 404:
      return CSlice("Not Found");
    case 405:
      return CSlice("Method Not Allowed");
    case 406:
      return CSlice("Not Acceptable");
    case 408:
      return CSlice("Request Timeout");
    case 409:
      return CSlice("Conflict");
    case 410:
      return CSlice("Gone");
    case 411:
      return CSlice("Length Required");
    case 412:
      return CSlice("Precondition Failed");
    case 413:
      return CSlice("Request Entity Too Large");
    case 414:
      return CSlice("Request-URI Too Long");
    case 415:
      return CSlice("Unsupported Media Type");
    case 416:
      return CSlice("Range Not Satisfiable");
    case 417:
      return CSlice("Expectation Failed");
    case 418:
      return CSlice("I'm a teapot");
    case 421:
      return CSlice("Misdirected Request");
    case 426:
      return CSlice("Upgrade Required");
    case 429:
      return CSlice("Too Many Requests");
    case 431:
      return CSlice("Request Header Fields Too Large");
    case 480:
      return CSlice("Temporarily Unavailable");
    case 501:
      return CSlice("Not Implemented");
    case 502:
      return CSlice("Bad Gateway");
    case 503:
      return CSlice("Service Unavailable");
    case 505:
      return CSlice("HTTP Version Not Supported");
    default:
      LOG_IF(ERROR, http_status_code != 500) << "Unsupported status code " << http_status_code << " returned";
      return CSlice("Internal Server Error");
  }
}

}  // namespace td
