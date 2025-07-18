#include <arpa/inet.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

namespace network {

// g_pid：静态全局变量，用于缓存进程 ID
static int g_pid = 0;

// t_thread_id：线程局部变量，用于缓存线程 ID
static thread_local int t_thread_id = 0;

// 检查 g_pid 是否已经被赋值
pid_t getPid() {
    if (g_pid != 0) {
        return g_pid;
    }
    return getpid();
}

// 检查 t_thread_id 是否已经被赋值
pid_t getThreadId() {
    if (t_thread_id != 0) {
        return t_thread_id;
    }
    return static_cast<pid_t>(::syscall(SYS_gettid));
}

int64_t getNowMs() {
    timeval val; // 定义一个 timeval 结构体变量 val，用于存储系统时间
    gettimeofday(&val, NULL); // 调用 gettimeofday 函数获取当前系统时间

    // 将秒数乘以 1000 并加上微秒数除以 1000，得到当前时间的毫秒数并返回
    return val.tv_sec * 1000 + val.tv_usec / 1000;
}

// 定义一个 int32_t 类型的变量 re，用于存储转换后的整数
int32_t getInt32FromNetByte(const char *buf) {
    int32_t re;
    // 使用 memcpy 函数将 buf 中的 4 字节数据复制到 re 中
    memcpy(&re, buf, sizeof(re));
    // 调用 ntohl 函数将网络字节序的整数转换为主机字节序并返回
    return ntohl(re);
}

} // namespace network