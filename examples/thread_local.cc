#include <iostream>
#include <thread>

class EventLoop {
public:
    EventLoop() : threadId(std::this_thread::get_id()) {}

    void printThreadId() {
        std::cout << " Thread ID: " << threadId << std::endl;
        std::cout << " this " << this << std::endl;
    }

    void printMemberFunctionAddress() {
        std::cout << " Member Function Address: " << &EventLoop::printThreadId
                    << std::endl;
    }

    void printMemberVariableAddress() {
        std::cout << " Member Variable Address: " << &threadId << std::endl;
    }

private:
    std::thread::id threadId;
};

/**
 * 这是一个线程局部静态变量。
 * 每个线程都有自己独立的 t_loopInThisThread 指针，互不影响。
 * 初始值为 nullptr。
 */
static thread_local EventLoop *t_loopInThisThread = nullptr;

/**
 * 每个线程第一次进入时，t_loopInThisThread 是 nullptr，于是 new 一个 EventLoop
 * 每个线程都会 new 一个自己的 EventLoop，并打印自己的信息。
 * t_loopInThisThread 在每个线程中互不干扰。
 * 打印的成员函数地址、成员变量地址，主要是演示 C++ 的语法特性。
 */
void threadFunction() {
    if (!t_loopInThisThread) {
        t_loopInThisThread = new EventLoop();
    }

    t_loopInThisThread->printThreadId();

    t_loopInThisThread->printMemberFunctionAddress();

    t_loopInThisThread->printMemberVariableAddress();
}

/**
 * t1 和 t2 都是子线程，它们是由主线程（也就是运行 main() 的线程）创建的。
 * 主线程就是程序一启动时自动运行 main() 函数的那个线程。
 * t1 和 t2 分别在各自的线程中执行 threadFunction()，
 * 而主线程则负责创建它们、等待它们结束（join()），以及最后执行 delete t_loopInThisThread;。
 */
int main() {
    std::thread t1(threadFunction);
    std::thread t2(threadFunction);

    t1.join();
    t2.join();

    delete t_loopInThisThread;

    return 0;
}