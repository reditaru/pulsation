#pragma once
#include <cstring>
#include <regex>
#include <any>
#include <unordered_map>
using namespace std;

namespace pulsation {
  const unordered_map<string, string> status_codes = {
    {"100", "Continue"}, {"101", "Switching Protocols"}, {"200", "OK"},
    {"201", "Created"}, {"202", "Accepted"}, {"203", "Non-Authoritative Information "},
    {"204", "No Content"}, {"205", "Reset Content"}, {"206", "Partial Content"},
    {"300", "Multiple Choices"}, {"301", "Moved Permanently"}, {"302", "Found"},
    {"303", "See Other"}, {"304", "Not Modified"}, {"305", "Use Proxy"},
    {"307", "Temporary Redirect"}, {"400", "Bad Request"}, {"401", "Unauthorized"},
    {"402", "Payment Required"}, {"403", "Forbidden"}, {"404", "Not Found"},
    {"405", "Method Not Allowed"}, {"406", "Not Acceptable"}, {"407", "Proxy Authentication Required"},
    {"408", "Request Time-out"}, {"409", "Conflict"}, {"410", "Gone"},
    {"411", "Length Required"}, {"412", "Precondition Failed"}, {"413", "Request Entity Too Large"},
    {"414", "Request-URI Too Large"}, {"415", "Unsupported Media Type"}, {"416", "Requested range not satisfiable"},
    {"502", "Bad Gateway"}, {"503", "Service Unavailable"}, {"504", "Gateway Time-out"},
    {"505", "HTTP Version not supported"}
  };
  const unordered_map<string, string> ext_type = {
    {".au", "audio/base"}, {".bmp", "application/x-bmp"}, {".html", "text/html"},
    {".htx", "text/html"}, {".iff", "application/x-iff"}, {".img", "application/x-img"},
    {".jpe", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".jsp", "text/html"}, {".jpg", "image/jpeg"},
    {".mp4", "video/mpeg4"}, {".css", "text/css"}, {".dtd", "text/xml"},
    {".htm", "text/html"}, {".js", "application/x-javascript"}, {".png", "image/png"},
  };
  struct TCPBuffer {
      int epoll_fd;
      int fd;
      int len;
      string content;
  };
  struct HTTPRequest {
    int epoll_fd;
    int fd;
    string method;
    string path;
    string protocal;
    unordered_map<string, string> headers;
    string body;
  };
  struct HTTPResponse {
    string status_code;
    unordered_map<string, string> headers;
    string body;
  };
  struct Context {
    int epoll_fd;
    int fd;
    HTTPRequest& request;
    HTTPResponse& response;
    unordered_map<string, any> extra;
  };
  struct ServerException {
    string status;
    string msg;
  };
}