# SocketProxy
Simple port forwarding with simple rule, simple command line shell

``` bash
Usage:

Basic load balancing mode:
 socketproxy -d -p 80 -t 120 -r 192.168.0.10:80 -r 192.168.0.11:80 -r 192.168.0.12:80

Multiple service use one port:
 socketproxy -d -p 80 -r 192.168.0.10:1935 -f "GET /api" 192.168.0.11:80 -f GET 192.168.0.12:80 -f POST 192.168.0.13:8080

Command line shell on http
 socketproxy -d -hr 80 mypassword

Useful scripts on http
 socketproxy -d -hr 80 scriptonly

Port forwarding DDNS
 socketproxy -d -p 80 -r 202.1.2.3:80 -hr 8080 updateforward

Params:
  -d : deamon mode
  -p portnum : listen port
  -a address : listen addr, default is 0.0.0.0
  -r host:port or [host port] : forward to host:port
  -f key host:port or [host port]: check first bytes if match 'key' then forward to host:port
  -t second : timeout second to release session if not data transfer, default is unlimit
  -hr port auth : listen port to handle http request for execute command,use 'auth' for authorization verification
       e.g.:'GET /?auth=xxx&cmd=ls' for execute 'ls' and return results via http directly
       e.g.:'GET /' will open a simple console
       if auth is 'scriptonly', only limit to run script where in './scripts/' directory without authorization
           e.g.:'GET /?cmd=run.sh 123' for execute ./scripts/run.sh 123
           The special character &|;`$!*()<>? is not allow
           Although the directory is limited, there are no restrictions on parameters. Consider a typical case: /?cmd=openfile.sh /etc/sth
           e.g.:'GET /?cmd=asynccall.sh 123 456' Submit to run in background and return immediately
           e.g.:'GET /?cmd=synccall.sh' Real time output of time-consuming scripts
           e.g.:'GET /?cmd=crfconvert.sh' Use crf feature to convert video
           e.g.:'GET /?cmd=realtimestream.sh' Real time video stream with delogo test
       if auth is 'updateforward', use for update forward ip and port specified in input param -r ip:port
           e.g.:'GET /?cmd=2.3.4.5[:1234]' update forward ip to '2.3.4.5', and update port to 1234 if specify with ':'
           e.g.:'GET /?cmd=' update with empty param means that update forward ip with request client ip
           e.g.:'GET /?cmd=query' print current forward ip
```

这是一个非常轻量级的端口转发工具，可以实现端口复用，http执行简单脚本，以及简单的shell

## 编译

SocketProxy 依赖 libevent，因此需要event库。

### Mac

``` bash
brew install libevent
make
```

### Centos

``` bash
yum -y install libevent-devel gcc-c++
make
```

## 用法

### 基础的负载均衡模式

``` bash
#Http负载均衡，或多个MySql读库轮询等场景
socketproxy -d -p 80 -t 120 -r 192.168.0.10:80 -r 192.168.0.11:80 -r 192.168.0.12:80
```

### 多个服务复用端口

这个例子只对外暴露了80端口，但是同时运行了四个服务：

* 当是 http://xxx/api 请求时，转发到 192.168.0.11
* 当是 http://xxx/ 请求时，转发到 192.168.0.12
* 当是 HTTP POST 数据时，转发到 192.168.0.13
* 否则转发到默认的 192.168.0.10:1935 (rtmp流)

``` bash
socketproxy -d -p 80 -r 192.168.0.10:1935 -f "GET /api" 192.168.0.11:80 -f GET 192.168.0.12:80 -f POST 192.168.0.13:8080
```

### 简单的http命令行

``` bash
socketproxy -d -hr 80 mypassword
```

通过 http://xxx/ 访问，输入登录凭证之后，可以在页面上执行 shell 命令（该功能依赖 scripts/console.html 文件）

<table><tr><td bgcolor=#26292C>
<font color=#00fe00>
Welcome to use Simple Console!<br>
<br>
Type "auth &lt;yourtoken&gt;" to setup authorization information.<br>
Type "help" for more information.<br>
<br>
$ auth mypassword<br>
</font><font color=#fda112>
Updated authorization information.<br>
<font color=#00fe00>
$ ls<br>
a.txt<br>
b.txt<br>
</font></td></tr></table>

也可以直接执行 http://xxx/?auth=mypassword&cmd=ls 获取执行结果

``` bash
$ curl "http://127.0.0.1/?auth=mypassword&cmd=ls"
a.txt
b.txt
```


### 远程运行脚本

注意，该功能不需要授权，直接可以运行scripts里的脚本（限定只能运行scripts里的脚本），应充分考虑脚本的安全性，例如传递进去的参数不要直接是文件名

``` bash
socketproxy -d -hr 80 scriptonly
```

#### 例子：异步脚本，提交后台异步运行，并立即返回

``` bash
http://127.0.0.1/?cmd=asynccall.sh some log can be record
```

#### 例子：同步执行脚本，实时输出结果

``` bash
http://127.0.0.1/?cmd=synccall.sh
```

#### 例子：视频crf动态码率转码

``` bash
http://127.0.0.1/?cmd=crfconvert.sh

#转码结果
-------------------start------------------
Meidia file 1:[http://vfx.mtime.cn/Video/2019/02/04/mp4/190204084208765161.mp4]
result: crf=24 \tpsnr = 48.569  \tbitrate = 798kb/s  \tfilesize = 3538 KB
```

#### 例子：视频实时遮标预览播放

* 使用flash flv播放器，指定flv地址即可播放：

``` bash
http://127.0.0.1/?cmd=realtimestream.sh
```

* 使用ffplay命令行进行播放：

``` bash
ffplay "http://127.0.0.1/?cmd=realtimestream.sh"
```

* 播放实时ts流，实现ts播放方法请参考：https://www.jianshu.com/p/4d90f81b01bc ，ts文件路径指定为：

``` bash
http://127.0.0.1/?cmd=realtimestream.sh ts
```

#### 注意，出于安全考虑，脚本执行不允许出现 &|;`$!*()<>? 等特殊字符

``` bash
http://127.0.0.1/?cmd=a.sh | grep v > a.txt
Permission denied!
```

### 动态更新转发，类似DDNS功能

``` bash
 socketproxy -d -p 80 -r 202.1.2.3:80 -hr 8080 updateforward
```

该功能实现后端灵活的转发，可用于ADSL拨号后更新等场景

``` bash
#获取当前转发的目标地址和端口
$ curl "http://127.0.0.1:8080/?cmd=query"
["202.1.2.3:80"]

#更新目标地址和端口
$ curl "http://127.0.0.1:8080/?cmd=202.3.4.5:888"
["202.3.4.5:888"]

#更新目标地址为当前请求的客户端ip
$ curl "http://202.1.2.3:8080/?cmd="
["202.7.8.9:888"]

#请求 xxx:80 端口会转发到 202.7.8.9:888

```

## 参数列表

``` bash
  -d : 后台运行模式
  -p portnum : 监听端口
  -a address : 监听的网卡地址，默认为 0.0.0.0
  -r host:port or [host port] : 转发到指定的地址和端口
  -f key host:port or [host port]: 如果链接前面的字节匹配'key'则转发到指定地址和端口
  -t second : 链接没有数据交换，达到空闲时间后关闭，默认为不限制
  -hr port auth : 通过http执行简单命令的监听端口，auth为用户登录凭证，支持字母数字和大部分特殊字符，不支持 # & < >
      其中 auth = scriptonly 时，为执行常用脚本模式
      其中 auth = updateforward 时，为动态更新转发模式
```
