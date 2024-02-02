
#include "threadpool.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char* argv[]) {
    using Future = morles::concurrent::ThreadPool::Future<int>;
    morles::concurrent::ThreadPool thread_pool(4, 1024);
    Future f = thread_pool.Post([&]() -> int { 
                                std::this_thread::sleep_for(std::chrono::seconds(5));
                                return 100; }
                                );
    Future f1 = thread_pool.Post([&]() -> int { 
                                std::this_thread::sleep_for(std::chrono::seconds(3));
                                return 200; }
                                );
    std::cout << f.Get() << std::endl;
    std::cout << f1.Get() << std::endl;
    return 0;
}
