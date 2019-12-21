#include "concurrentqueue.h"
#include "filter.h"
#include "http.h"

namespace pulsation {
  class Worker {
    private:
      moodycamel::ConcurrentQueue<HTTPRequest>* queue;
      vector<Filter>* filters;
    public:
      Worker(moodycamel::ConcurrentQueue<HTTPRequest>* queue, vector<Filter>* filters);
      void process();

  };
}