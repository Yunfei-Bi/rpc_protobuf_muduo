include (CheckFunctionExists)

# 检查当前系统的库里是否有 accept4 这个函数（accept4 是 Linux 下的一个系统调用，用于创建 socket 连接时设置额外的标志）。
# 检查结果会存到变量 HAVE_ACCEPT4 里，值为 1（有）或空（没有）。
check_function_exists(accept4 HAVE_ACCEPT4)

# 给 SocketsOps.cc 这个源文件添加编译选项 -DNO_ACCEPT4，相当于在编译时定义了一个宏 NO_ACCEPT4。
# 这样在 SocketsOps.cc 里可以用 #ifdef NO_ACCEPT4 做条件编译，兼容没有 accept4 的系统。
if (NOT HAVE_ACCEPT4)
    set_source_files_properties(SocketsOps.cc PROPERTIES COMPILE_FLAGS "-DNO_ACCEPT4")
endif()

set(net_SRCS
    Acceptor.cc
    Buffer.cc
    Channel.cc
    Connector.cc
    EventLoop.cc
    EventLoopThread.cc
    EventLoopThreadPool.cc
    InetAddress.cc
    Poller.cc
    Socket.cc
    SocketsOps.cc
    TcpClient.cc
    TcpCOnenction.cc
    TcpServer.cc
    util.cc
)

# network 是库的名字，编译后会生成 libnetwork.a（静态库）或 libnetwork.so（动态库），
# 具体取决于 CMakeLists.txt 里有没有指定 STATIC 或 SHARED，默认是静态库
add_library(network ${net_SRCS})

# 作用：指定 network 这个库在编译/链接时需要依赖 glog 库（Google 的日志库）。
# 效果：编译 network 时会自动加上 -lglog，确保用到的日志相关函数能正确链接。
# 这两行代码让 network 库在编译时自动链接 glog，
# 并且让所有用到 network 的代码都能方便地找到 ../include 里的头文件。
target_link_libraries(network glog)
target_include_directories(network PUBLIC
    ../include
)

# DESTINATION ${PROJECT_BINARY_DIR}/lib 指定安装的目标路径。
# ${PROJECT_BINARY_DIR} 是 CMake 的内置变量，表示你的构建输出目录（比如 build/ 目录），lib 是子目录。
install(TARGETS network DESTINATION ${PROJECT_BINARY_DIR}/lib)