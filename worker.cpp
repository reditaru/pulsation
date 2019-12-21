#include <unistd.h>
#include <iostream>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <thread>
#include "worker.h"

pulsation::Worker::Worker(moodycamel::ConcurrentQueue<HTTPRequest>* queue, vector<Filter>* filters): queue(queue), filters(filters) {}
void pulsation::Worker::process() {
  HTTPRequest req;
  while (1) {
    if ((*queue).try_dequeue(req)) {
      HTTPResponse response;
      Context ctx{req.epoll_fd, req.fd, req, response};
      std::vector<Filter>::reverse_iterator f_iter = (*filters).rbegin();
      while (f_iter != (*filters).rend()) {
        if (f_iter == (*filters).rbegin()) {
          f_iter->setNextFunc([]{});
        } else {
          // auto do_callback_func = std::mem_fn(&pulsation::Filter::doCallback);
          std::function<void()> func = [f_iter, &ctx]{
            (f_iter - 1)->doCallback(ctx);
          };
          f_iter->setNextFunc(func);
        }
        f_iter++;
      }
      if ((*filters).size() > 0) {
        (*filters).begin()->doCallback(ctx);
      }
    }
  }
}