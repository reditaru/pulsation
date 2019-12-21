#include <cstring>
#include <fcntl.h>
#include <thread>
#include <chrono>
#include <string>
#include <ctime>
#include <unistd.h>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <functional>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <boost/algorithm/string.hpp>
#include "server.h"


pulsation::Server::Server(unsigned int port, int work_threads): port(port), threads(4), work_threads(work_threads) {
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  int on = 1;
  if (sockfd < 0) {
    perror("Errot Create Socket File!");
    exit(1);
  }
  fcntl(sockfd, F_SETFL, O_NONBLOCK);
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  struct sockaddr_in address;
  bzero(&address, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(sockfd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("Error Binding!");
    exit(1);
  }
  if (listen(sockfd, 5) < 0) {
    perror("Error Listening!");
    exit(1);
  }
  std::cout << "Listening on port: " << port << std::endl;
  this->sockfd = sockfd;

}

pulsation::Server::~Server() {
  while (!workers.empty()) {
    pulsation::Worker* worker = workers.back();
    workers.pop_back();
    delete worker;
  }
}

void pulsation::Server::process() {
  struct epoll_event ev, events[MAX_EVENTS];
  int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    perror("Error create epoll");
    exit(1);
  }

  // 监听端口
  ev.data.fd = sockfd;
  ev.events = EPOLLIN;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &ev) < 0) {
    perror("Error bind listen epoll");
    exit(1);
  }

  bool has_listen_event = true;
  struct sockaddr_in client_address;
  socklen_t client_len;
  char client_addr[32];
  std::unordered_map<int, TCPBuffer> fd_map;
  std::unordered_map<int, time_t> time_map;
  char buf[1024];

  std::cout << "Sub thread " << std::this_thread::get_id() << " start working..." << std::endl;
  while (1) {
    // 为防止惊群现象，尝试获取锁，保证请求到来时，不会有多个线程被唤醒
    if (mutex.try_lock()) {
      if (!has_listen_event) {
        ev.data.fd = sockfd;
        ev.events = EPOLLIN;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &ev) < 0) {
          perror("Error bind listen epoll");
          exit(1);
        }
        has_listen_event = true;
      }
      mutex.unlock();
    } else {
      if (has_listen_event) {
        if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sockfd, &ev) < 0) {
          perror("Error bind listen epoll");
          exit(1);
        }
        has_listen_event = false;
      }
    }

    int readys = epoll_wait(epoll_fd, events, MAX_EVENTS, EVENT_WAIT_TIMEOUT);
    if (readys == -1) {
      perror("Error epoll wait");
      exit(1);
    }
    for (int i = 0; i < readys; ++i) {
      if (events[i].data.fd == sockfd) {
        int client_fd = accept(sockfd, (struct sockaddr *)&client_address, &client_len);
        if (client_fd < 0) {
          perror("Accept socket error!");
          continue;
        }

        if (fcntl(client_fd, F_SETFL, O_NONBLOCK) < 0) {
          perror("Set socket nonblock error!");
          continue;
        }

        ev.data.fd = client_fd;
        ev.events = EPOLLIN;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
          perror("Add client epoll event error!");
          close(client_fd);
        }
        std::cout << "Accept conn request from: " << inet_ntoa(client_address.sin_addr)
          << ":" << client_address.sin_port << std::endl;
      } else if (events[i].events & EPOLLIN) {
        // 读数据
        int fd = events[i].data.fd;
        if (time_map.find(fd) != time_map.end()) {
          time_map[fd] = std::time(0);
        } else {
          time_map.insert(make_pair(fd, std::time(0)));
        }
        if (fd_map.find(fd) == fd_map.end()) {
          TCPBuffer req{epoll_fd, fd, 0, ""};
          fd_map.insert(make_pair(fd, req));
        }
        TCPBuffer& tcp_buf = fd_map[fd];
        std::string s = "";
        int read_count;
        while (1) {
          read_count = read(fd, &buf, 1024);
          if (read_count > 0) {
            s += std::string(buf, read_count);
          } else {
            break;
          }
        }
        if (read_count == 0 || read_count == -1 && errno != EAGAIN) {
          if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &ev) < 0) {
            perror("Error delete client listen");
          }
          fd_map.erase(fd);
          time_map.erase(fd);
          close(fd);
          continue;
        }
        tcp_buf.content += s;
        // 尝试解析request
        std::string::size_type position;
        while (1) {
          if ((position = tcp_buf.content.find("\r\n\r\n")) != std::string::npos) {
            HTTPRequest req;
            req.epoll_fd = epoll_fd;
            req.fd = fd;
            std::istringstream s_buf(tcp_buf.content);
            s_buf >> req.method;
            s_buf >> req.path;
            s_buf >> req.protocal;
            std::string header;
            std::getline(s_buf, header);
            while (std::getline(s_buf, header) && header != "\r") {
              int index = header.find(':', 0);
              if(index != std::string::npos) {
                std::string key = boost::algorithm::to_lower_copy(boost::algorithm::trim_copy(header.substr(0, index)));
                req.headers.insert(make_pair(
                  key,
                  boost::algorithm::trim_copy(header.substr(index + 1))
                ));
              }
            }
            if (req.headers.find("content-length") != req.headers.end()) {
              tcp_buf.len = stoi(req.headers["content-length"]);
            }
            if (tcp_buf.len > 0) {
              if (tcp_buf.content.length() - position - 4 >= tcp_buf.len) {
                req.body = tcp_buf.content.substr(position + 4, tcp_buf.len);
              } else {
                break;
              }
            }
            // 加入队列
            queue.enqueue(req);
            // 更新 tcp buffer
            tcp_buf.content = tcp_buf.content.substr(position + 4 + tcp_buf.len);
            tcp_buf.len = 0;
          } else {
            break;
          }
        }
      } else if (events[i].events & EPOLLERR) {
        perror("Error!");
        close(events[i].data.fd);
      }
    }
    if (readys == 0) {
      std::time_t now = time(0);
      for (std::unordered_map<int, time_t>::iterator iter = time_map.begin(); iter != time_map.end();) {
        if (now - iter->second > MAX_CONNECTION_TIMEOUT) {
          if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, iter->first, &ev) < 0) {
            perror("Error del listen client epoll");
          }
          close(iter->first);
          iter = time_map.erase(iter);
        } else iter++;
      }
    }
  }
}

void pulsation::Server::run() {
  for (int i = 0; i < threads; ++i) {
    std::thread io_thread([this]{
      process();
    });
    io_thread.detach();
  }
  auto worker_func = std::mem_fn(&pulsation::Worker::process);
  for (int i = 0; i < work_threads; ++i) {
    pulsation::Worker* worker = new pulsation::Worker(&queue, &filters);
    workers.push_back(worker);
    std::thread worker_thread(worker_func, worker);
    worker_thread.detach();
  }
  while (1) {
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
  }
}

pulsation::Server& pulsation::Server::use(InitFunc f_init, CallbackFunc f_callback) {
  Filter filter{f_init, f_callback};
  filters.push_back(filter);
  return *this;
}

pulsation::Server& pulsation::Server::use(CallbackFunc f_callback) {
  Filter filter{f_callback};
  filters.push_back(filter);
  return *this;
}