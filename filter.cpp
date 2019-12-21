#include "filter.h"

pulsation::Filter::Filter(InitFunc f_init, CallbackFunc f_callback): f_init(f_init), f_callback(f_callback) {
  f_init(properties);
}

pulsation::Filter::Filter(CallbackFunc f_callback): f_callback(f_callback) {}

void pulsation::Filter::doCallback(Context& ctx, NextFunc next) {
  f_callback(properties, ctx, next);
}

void pulsation::Filter::doCallback(Context& ctx) {
  if (f_next) {
    f_callback(properties, ctx, f_next);
  } else {
    f_callback(properties, ctx, []{});
  }
}

void pulsation::Filter::setNextFunc(NextFunc next) {
  this->f_next = next;
}