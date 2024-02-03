
#include <chrono>
#include <iostream>
#include <thread>
#include "threadpool.hpp"

int main(int argc, char* argv[]) {
    using Future = morles::concurrent::ThreadPool::Future<int>;
    using Status = morles::concurrent::ThreadPool::Status<int>;
    {
        morles::concurrent::ThreadPool thread_pool(4, 1024);
        Future f = thread_pool.Post([&]() -> int { 
                                    std::this_thread::sleep_for(std::chrono::seconds(2));
                                    return 100; 
                                    });
        thread_pool.Post([&]() -> int { return 200; }
                                    , [&](Status& s) { 
                                        std::cout << "leihao" << std::endl; 
                                        if (s.success()) {
                                            std::cout << (s.result() + 102) << std::endl;
                                        }
                                    });
        std::cout << f.Get() << std::endl;
    }
    std::cout << "success" << std::endl;
    return 0;
}
