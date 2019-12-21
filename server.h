#include <mutex>
#include <cstring>
#include <vector>
#include "concurrentqueue.h"
#include "http.h"
#include "worker.h"
#include "filter.h"

namespace pulsation {
  #define MAX_EVENTS 1024
  #define EVENT_WAIT_TIMEOUT 100
  #define MAX_QUEUE_CAPACITY 2048
  #define MAX_CONNECTION_TIMEOUT 60
  class Server {
  private:
    unsigned int port;
    int threads;
    int work_threads;
    int sockfd;
    std::mutex mutex;
    moodycamel::ConcurrentQueue<HTTPRequest> queue;
    vector<Worker*> workers;
    vector<Filter> filters;
  public:
    Server(unsigned int port, int work_threads);
    ~Server();
    void run();
    void process();
    Server& use(InitFunc f_init, CallbackFunc f_callback);
    Server& use(CallbackFunc f_callback);
  };
}