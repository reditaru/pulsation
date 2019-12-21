#pragma once
#include <functional>
#include <cstring>
#include <any>
#include <unordered_map>
#include "http.h"

namespace pulsation {
  typedef std::unordered_map<std::string, std::any> FilterProperties;
  typedef std::function<void(FilterProperties&, Context&, std::function<void()>)> CallbackFunc;
  typedef std::function<void(FilterProperties&)> InitFunc;
  typedef std::function<void()> NextFunc;
  class Filter {
    private:
      FilterProperties properties;
      CallbackFunc f_callback;
      InitFunc f_init;
      NextFunc f_next;
    public:
      Filter(InitFunc f_init, CallbackFunc f_callback);
      Filter(CallbackFunc f_callback);
      void setNextFunc(NextFunc next);
      void doCallback(Context& ctx, NextFunc next);
      void doCallback(Context& ctx);
  };
}