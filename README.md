# Pulsation
## 特性
- HTTP/1.1 长连接与超时
- log记录访问请求
- 静态目录资源服务
- 动态模板
- 基本Basic鉴权
- 报文Gzip压缩
- CORS访问控制
- 基于洋葱模型的请求处理模型，方便增加更多中间件处理请求

## 实现
### 多IO线程
使用IO线程控制客户端连接请求，超时以及对基本TCP粘包拆包问题的处理。  
其中IO线程使用epoll的IO事件机制进行处理，保证大量客户端连接请求下服务端也能进行处理，同时使用多个IO线程来保证在进行对TCP包处理的情况下也能保证IO不断。  
由于使用多个IO线程，且多个IO线程都监听主线程的服务端socket端口，在请求到来时，当前wait epoll event的请求都会被唤醒，但是只有一个io thread能成功accept 客户端的请求，为了解决多个IO线程被唤醒的问题，使用锁来控制IO线程，保证同一时间，只有一个IO线程监听主线程的socket端口。  

IO线程在accept客户端的连接请求后，会将客户端的socket fd也添加到epoll中进行客户端的IO管理。  
客户端发来TCP包数据，在IO线程中根据HTTP报文进行包的拆分合并，将完整不多余的包添加到队列中供工作线程处理。
### 工作线程
由于HTTP是无状态的协议，因此不需要考虑请求与线程的相关性，且考虑到业务在处理请求时间可能会很长，会有阻塞（如数据库连接），为了不影响IO线程的工作，使用工作线程处理相应请求。
工作线程不断从队列中拿取请求，并生成相应的ctx上下文对象，通过Filter链进行HTTP请求的处理。  
ctx还保证了filter之间的交互。  
```c++
struct Context {
  int epoll_fd;
  int fd;
  HTTPRequest& request;
  HTTPResponse& response;
  unordered_map<string, any> extra; // filter间通过extra进行交互，前一个filter处理的结果可以通过extra给后续的filter提供帮助。
};
```

### 高度自定义的洋葱模型
这里借鉴koa的思想，抽象出Filter对象来作为最基本的HTTP请求的请求，HTTP请求的Content-Type解析等全都可以在这里完成。  
![](./koa-onion.png)  
如：
- 代码中使用response and error filter filter，来对默认响应报文进行处理，同时这里也是所有filter的开端，通过在这里try catch，来捕获后续所有filter的异常，对其进行处理，并返回给客户端。  
- log filter。 基本的日志中间件，用来输出请求。  
- cors filter。 访问控制中间件，能够接受跨域请求的OPTIONS请求并对响应头进行响应的设置，以达到跨域访问的功能。
- compress filter。压缩中间件，对响应头的Content-Type进行判断，若匹配配置好的压缩类型，则用gzip进行压缩，减少网络传输流量。
- static filter。基本的静态资源中间件。对请求路径进行判断，若路径匹配预先配置好的静态目录中的资源，则直接返回，各种异常错误的40x，50x的页面也可以存放在这以进行返回。
- Basic filter。只实现Basic鉴权的中间件。通过对需要鉴权的请求路径（如配置/api/*）以及相应的请求头（Authorization）进行判断，通过base64进行解码处理传递处理后的用户信息给后续controller filter进行判断。
- view filter。动态页面中间件。controller filter传递回的相应的模板路径以及参数在这里进行拼接。
- controller filter。控制器，基本的业务在这里处理。

以compress中间件为例  
```c++
// compress filter
server.use([](pulsation::FilterProperties& map) {
  /**
   * 在这里初始化中间件的配置
   **/
  // 需要压缩的mime types
  vector<string> mime_types = {"text/html", "image/jpeg", "application/x-javascript", "text/css", "image/png"};
  map.insert(make_pair("mime_types", mime_types));
}, [](pulsation::FilterProperties& properties, pulsation::Context& ctx, pulsation::NextFunc next) {
  /**
   * 中间件具体执行逻辑
   * properties: 中间件配置
   * ctx: 请求上下文
   * next: function，用来调用下一个中间件进行处理。
   **/
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
```
