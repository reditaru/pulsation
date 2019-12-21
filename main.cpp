#include <iostream>
#include <unistd.h>
#include <ctime>
#include <any>
#include <sstream>
#include <fstream>
#include <regex>
#include <zlib.h>
#include <boost/algorithm/string/join.hpp>
#include <boost/filesystem.hpp>
#include "http.h"
#include "filter.h"
#include "base64.h"
#include "server.h"

namespace fs = boost::filesystem;

string read_file_content(const string& path) {
  ifstream f(path);
  stringstream s_f;
  while (f >> s_f.rdbuf());
  return s_f.str();
}

void set_header(unordered_map<string, string>& headers, string key, string value, bool if_empty = false) {
  auto it = headers.find(key);
  if (it == headers.end()) {
    headers.insert(make_pair(key, value));
  } else {
    if (!if_empty) {
      it->second = value;
    }
  }
}

bool check_resource_valid(string parent, string child) {
  fs::path p_parent(parent);
  fs::path p_child(child);
  if (fs::exists(p_child) && !fs::is_directory(p_child)) {
    fs::path abs_parent = fs::canonical(p_parent);
    fs::path abs_child = fs::canonical(p_child);
    if (abs_child.string().find(abs_parent.string(), 0) == 0) {
      return true;
    }
  }
  return false;
}

bool check_path_valid(string path, std::regex regex) {
  return std::regex_search(path, regex);
}

template<typename T>
bool get_and_cast(unordered_map<string, any>& map, string key, T& value) {
  if (map.find(key) != map.end()) {
    value = std::any_cast<T>(map[key]);
    return true;
  }
  return false;
}

bool check_controller(pulsation::HTTPRequest& req, string method, string path) {
  std::regex regex{path};
  return req.method == method && check_path_valid(req.path, regex);
}

int gzcompress(Bytef *data, uLong ndata, Bytef *zdata, uLong *nzdata) {
  z_stream c_stream;
  int err = 0;
  if(data && ndata > 0) {
      c_stream.zalloc = NULL;
      c_stream.zfree = NULL;
      c_stream.opaque = NULL;
      //只有设置为MAX_WBITS + 16才能在在压缩文本中带header和trailer
      if(deflateInit2(&c_stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                      MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) return -1;
      c_stream.next_in  = data;
      c_stream.avail_in  = ndata;
      c_stream.next_out = zdata;
      c_stream.avail_out  = *nzdata;
      while(c_stream.avail_in != 0 && c_stream.total_out < *nzdata) {
          if(deflate(&c_stream, Z_NO_FLUSH) != Z_OK) return-1;
      }
      if(c_stream.avail_in != 0) return c_stream.avail_in;
      for(;;) {
          if((err = deflate(&c_stream, Z_FINISH)) == Z_STREAM_END) break;
          if(err != Z_OK) return-1;
      }
      if(deflateEnd(&c_stream) != Z_OK) return-1;
      *nzdata = c_stream.total_out;
      return 0;
  }
  return-1;
}

int main(int, char**) {
    pulsation::Server server{8080, 4};
    // response and error filter
    server.use([](pulsation::FilterProperties& properties, pulsation::Context& ctx, pulsation::NextFunc next) {
      try {
        next();
        // 没有默认为404
        if (pulsation::status_codes.find(ctx.response.status_code) == pulsation::status_codes.end()) {
          ctx.response.status_code = "404";
          ctx.response.body = "404 - Not Found.(From Server pulsation)";
        }
      } catch (pulsation::ServerException& e) {
        ctx.response.status_code = e.status;
        ctx.response.body = e.msg;
      }
      
      // 通用响应头
      set_header(ctx.response.headers, "content-type", "text/plain", true);
      set_header(ctx.response.headers, "server", "pulsation");
      std::ostringstream s_header;
      s_header << "HTTP/1.1 " << ctx.response.status_code << " " << pulsation::status_codes.at(ctx.response.status_code) << "\r\n";
      unordered_map<string, string>::iterator h_iter = ctx.response.headers.begin();
      while(h_iter != ctx.response.headers.end()) {
        s_header << h_iter->first << ": " << h_iter->second << "\r\n";
        h_iter++;
      }
      s_header << "content-length" << ": " << ctx.response.body.size() << "\r\n";
      s_header << "\r\n";
      std::string header = s_header.str();
      write(ctx.fd, header.c_str(), header.size());
      write(ctx.fd, ctx.response.body.c_str(), ctx.response.body.size());
    });
    // log filter
    server.use([](pulsation::FilterProperties& properties, pulsation::Context& ctx, pulsation::NextFunc next) {
      std::ostringstream s_log;
      time_t now = time(0);
      struct tm  tstruct;
      char       buf[80];
      tstruct = *localtime(&now);
      strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);
      s_log << "[Log] " << buf << " [main - thread " << std::this_thread::get_id() << "] ";
      s_log << ctx.request.method << " " << ctx.request.path << std::endl;
      std::cout << s_log.str();
      next();
    });
    // cors filter
    server.use([](pulsation::FilterProperties& map) {
      vector<string> allow_methods = {"GET", "POST", "PUT", "DELETE"};
      map.insert(make_pair("allow_methods", allow_methods));
      vector<string> allow_headers = {"Content-Type", "Authorization", "Acceprt"};
      map.insert(make_pair("allow_headers", allow_headers));
      vector<string> expose_headers;
      map.insert(make_pair("expose_headers", expose_headers));
      map.insert(make_pair("credentials", true));
      map.insert(make_pair("max_age", 5));
      map.insert(make_pair("origin", string("*")));
    }, [](pulsation::FilterProperties& properties, pulsation::Context& ctx, pulsation::NextFunc next) {
      if (ctx.request.headers.find("origin") == ctx.request.headers.end()) {
        next();
        return;
      }
      string origin = ctx.request.headers["origin"];
      get_and_cast<string>(properties, "origin", origin);
      if (ctx.request.method != "OPTIONS") {
        set_header(ctx.response.headers, "access-control-allow-origin", origin);
        bool credentials = false;
        get_and_cast<bool>(properties, "credentials", credentials);
        if (credentials) {
          set_header(ctx.response.headers, "access-control-allow-credentials", "true");
        }
        vector<string> expose_headers;
        get_and_cast<vector<string>>(properties, "expose_headers", expose_headers);
        if (expose_headers.size() > 0) {
          set_header(ctx.response.headers, "access-control-expose-headers", boost::algorithm::join(expose_headers, ","));
        }
        next();
      } else {
        if (ctx.request.headers.find("access-control-request-method") == ctx.request.headers.end()) {
          next();
          return;
        }
        set_header(ctx.response.headers, "access-control-allow-origin", origin);
        bool credentials = false;
        get_and_cast<bool>(properties, "credentials", credentials);
        if (credentials) {
          set_header(ctx.response.headers, "access-control-allow-credentials", "true");
        }
        int max_age;
        bool has_value = get_and_cast<int>(properties, "max_age", max_age);
        if (has_value) {
          set_header(ctx.response.headers, "access-control-max-age", std::to_string(max_age));
        }
        vector<string> allow_methods;
        get_and_cast<vector<string>>(properties, "allow_methods", allow_methods);
        if (allow_methods.size() > 0) {
          set_header(ctx.response.headers, "access-control-allow-methods", boost::algorithm::join(allow_methods, ","));
        }
        vector<string> allow_headers;
        has_value = get_and_cast<vector<string>>(properties, "allow_headers", allow_headers);
        if (!has_value) {
          set_header(ctx.response.headers, "access-control-allow-headers", ctx.request.headers["access-control-allow-headers"]);
        } else {
          set_header(ctx.response.headers, "access-control-allow-methods", boost::algorithm::join(allow_methods, ","));
        }
        ctx.response.status_code = "204";
      }

    });
    // compress filter
    server.use([](pulsation::FilterProperties& map) {
      // 需要压缩的mime types
      vector<string> mime_types = {"text/html", "image/jpeg", "application/x-javascript", "text/css", "image/png"};
      map.insert(make_pair("mime_types", mime_types));
    }, [](pulsation::FilterProperties& properties, pulsation::Context& ctx, pulsation::NextFunc next) {
      next();
      if (ctx.response.headers.find("content-type") != ctx.response.headers.end()) {
        string header = ctx.response.headers["content-type"];
        vector<string> mime_types;
        get_and_cast(properties, "mime_types", mime_types);
        if (std::find(mime_types.begin(), mime_types.end(), header) != mime_types.end()) {
          // 压缩
          Bytef* body = (Bytef*)ctx.response.body.data();
          uLong len = compressBound(ctx.response.body.size());
          Bytef compressed[len];
          int err = gzcompress(body, ctx.response.body.size(), compressed, &len);
          ctx.response.body = string(reinterpret_cast<char*>(compressed), len);
          set_header(ctx.response.headers, "content-encoding", "gzip");
        }
      }
    });
    // static filter
    server.use([](pulsation::FilterProperties& map) {
      // 静态目录配置
      map.insert(make_pair("dir", std::string("./static")));
      // 是否处理错误页面
      map.insert(make_pair("error_handle_page", true));
    }, [](pulsation::FilterProperties& properties, pulsation::Context& ctx, pulsation::NextFunc next) {
      if (ctx.request.method == "GET") {
        std::string base_dir = std::any_cast<std::string>(properties["dir"]);
        bool error_handle_page = std::any_cast<bool>(properties["error_handle_page"]);
        std::string path = base_dir + ctx.request.path;
        fs::path abs_p(path);
        if (check_resource_valid(base_dir, path)) {
          ctx.response.body = read_file_content(path);
          string ext = abs_p.extension().string();
          string type = "text/plain";
          if (pulsation::ext_type.find(ext) != pulsation::ext_type.end()) {
            type = pulsation::ext_type.at(ext);
          }
          set_header(ctx.response.headers, "content-type", type);
          ctx.response.status_code = "200";
          // 直接返回，不交给后续Filter处理
          return;
        }
        // 如果不存在，这里可以redirect到存在的404/5xx，由用户决定处理
        next();
        // 后续没有返回的例子
        if (error_handle_page) {
          // 后续filter返回 404（或未设置status_code），则尝试返回404页面，取代第一个Filter
          if (ctx.response.status_code == "404") {
            // 重定向至404.html
            ctx.response.status_code = "302";
            set_header(ctx.response.headers, "location", "/404.html");
            return;
          } else if (ctx.response.status_code.find("5", 0) == 0) {
            if (check_resource_valid(base_dir, base_dir + "/50x.html")) {
              ctx.response.body = read_file_content(base_dir + "/50x.html");
              set_header(ctx.response.headers, "content-type", "text/html");
              ctx.response.status_code = "200";
              return;
            }
          }
        }
      } else {
        next();
      }
    });
    // Basic 鉴权
    server.use([](pulsation::FilterProperties& map) {
      // 存活时间 / second
      map.insert(make_pair("session_live", 3600));
      unordered_map<string, time_t> session_map;
      map.insert(make_pair("session_map", session_map));
      // 监听path regex
      map.insert(make_pair("path", std::regex{"^/api/(.*)"}));
      // 失败跳转，为空则不跳
      // map.insert(make_pair("fail_jump", string("/login.html")));
      map.insert(make_pair("fail_jump", string("")));
    }, [](pulsation::FilterProperties& map, pulsation::Context& ctx, pulsation::NextFunc next) {
      string path = ctx.request.path;
      std::regex path_regexp = std::any_cast<std::regex>(map["path"]);
      string fail_jump = std::any_cast<string>(map["fail_jump"]);
      bool success = false;
      if (check_path_valid(path, path_regexp)) {
        if (ctx.request.headers.find("authorization") != ctx.request.headers.end()) {
          string token = ctx.request.headers["authorization"];
          unordered_map<string, time_t> session_map = std::any_cast<unordered_map<string, time_t>>(map["session_map"]);
          int session_live = std::any_cast<int>(map["session_live"]);
          auto it = session_map.find(token);
          if (it != session_map.end()) {
            if (time(0) - it->second < session_live) {
              string d_token = base64_decode(token);
              ctx.extra.insert(make_pair("user_details", d_token));
              it->second = time(0);
              success = true;
            } else {
              session_map.erase(it);
            }
          }
        } else {
          ctx.extra.insert(make_pair("user_details", ""));
        }
        if (!success && fail_jump != "") {
          ctx.response.status_code = "302";
          set_header(ctx.response.headers, "location", fail_jump);
          return;
        }
      }
      next();
    });
    // view 动态页面模板
    server.use([](pulsation::FilterProperties& map) {
      // 动态目录配置
      map.insert(make_pair("dir", std::string("./view")));
    }, [](pulsation::FilterProperties& properties, pulsation::Context& ctx, pulsation::NextFunc next) {
      next();
      // controller filter通过传递模板路径以及参数来告知view filter处理
      if (ctx.request.method == "GET") {
        string tpl_path;
        unordered_map<string, string> tpl_params;
        if (get_and_cast<string>(ctx.extra, "tpl_path", tpl_path) && 
        get_and_cast<unordered_map<string, string>>(ctx.extra, "tpl_params", tpl_params)) {
          string dir;
          get_and_cast<string>(properties, "dir", dir);
          if (check_resource_valid(dir, dir + tpl_path)) {
            string tpl = read_file_content(dir + tpl_path);
            auto it = tpl_params.begin();
            while (it != tpl_params.end()) {
              std::regex regex{"\\$\\{" + it->first + "\\}"};
              tpl = std::regex_replace(tpl, regex, it->second);
              it++;
            }
            ctx.response.body = tpl;
            fs::path p(dir + tpl_path);
            string ext = p.extension().string();
            string type = "text/plain";
            if (pulsation::ext_type.find(ext) != pulsation::ext_type.end()) {
              type = pulsation::ext_type.at(ext);
            }
            set_header(ctx.response.headers, "content-type", type);
            ctx.response.status_code = "200";
          }
        }
      }
    });
    // controller 动态页面
    server.use([](pulsation::FilterProperties& properties, pulsation::Context& ctx, pulsation::NextFunc next) {
      if (check_controller(ctx.request, "GET", "/(.*)")) {
        unordered_map<string, string> params;
        time_t now = time(0);
        struct tm  tstruct;
        char       buf[80];
        tstruct = *localtime(&now);
        strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);
        params.insert(make_pair("now", string(buf)));
        stringstream ss;
        ss << std::this_thread::get_id();
        params.insert(make_pair("thread_id", ss.str()));
        params.insert(make_pair("method", ctx.request.method));
        params.insert(make_pair("path",  ctx.request.path));
        params.insert(make_pair("protocal",  ctx.request.protocal));
        ctx.extra.insert(make_pair("tpl_path", string("/dynamic.html")));
        ctx.extra.insert(make_pair("tpl_params", params));
      } else {
        next();
      }
    });
  
    server.run();
}
