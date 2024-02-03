
#include <iostream>
#include <cstdlib>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "queue/concurrent_blocking_queue.hpp"

namespace morles {
namespace concurrent {


class ThreadPool {
public:
    class Runable {
    public:
        virtual bool operator()() noexcept { return true; }
        virtual ~Runable() {}
    };
    using Queue = ConcurrentBlockingQueue<Runable*>;    
    template <typename T> class Future;
    enum Code { SUCCESS, FAILED };

    template <typename T>
    class Status {
    public:
        Status() {}
        Status(Code code, const std::string& err) : code_(code), error_msg_(err) {}
        Status(Code code, const T& r) : code_(code), result_(r) {}
        ~Status() {}
        Status(const Status& s) {
            code_ = s.code_;
            result_ = s.result_;
            error_msg_ = s.error_msg_;
        }
        Status& operator=(const Status& s) {
            code_ = s.code_;
            result_ = s.result_;
            error_msg_ = s.error_msg_;
            return *this;
        }
        Code code() const { return code_; }
        std::string& errorMsg() { return error_msg_; }
        bool success() { return code_ == SUCCESS; }
        T& result() { return result_; }
        int code_;
        T result_;
        std::string error_msg_;
    };

    template <typename T>
    class Task : public Runable {
    public:
        enum Type { Future, Normal };
        using processor = std::packaged_task<Status<T>()>;
        using post_processor = std::function<void(Status<T>&)>;
        
        template <typename U, typename C, bool return_void = std::is_void<U()>::value>
        static Runable* Create(U&& run, C&& callback, bool return_future = false) {
            using return_type = decltype(run());
            auto r = [&] () -> Status<T> {
                Status<T> s;
                try {
                    if (return_void) {
                        run();
                        s = Status<T>(SUCCESS, nullptr);
                    } else {
                        return_type result = run();
                        s = Status<T>(SUCCESS, result);
                    }
                } catch (std::exception& e) {
                    s = Status<T>(FAILED, e.what());
                } catch (...) {
                    s = Status<T>(FAILED, "Unkown Error");
                }
                return s;
            };
            if (return_future) {
                processor pr(std::move(r));
                return new FutureTask<return_type>(std::move(pr), std::move(callback));
            } else {
                return new AsyncTask<return_type>(std::move(r), std::move(callback));
            }
        }

        Task(post_processor&& r) : post_(r) {}
        ~Task() {}
        virtual Type GetTaskType() = 0;
    protected:
        post_processor post_;
    };

    template <typename T>
    class FutureTask : public Task<T> {
    public:    
        using processor = std::packaged_task<Status<T>()>;
        using post_processor = std::function<void(Status<T>&)>;
        FutureTask(processor&& p, post_processor&& r) : Task<T>(std::move(r)), 
                pr_(std::forward<processor>(p)) {}
        auto GetFuture() -> Future<T> { 
            return Future<T>(pr_.get_future()); 
        }
        bool operator()() noexcept {
            pr_();
            Status<T> fake_s;
            this->post_(fake_s);
            return true;
        }
        typename Task<T>::Type GetTaskType() { return Task<T>::Future; }

    private:
        processor pr_;
    };

    template <typename T>
    class AsyncTask : public Task<T> {
    public:    
        using processor_func = std::function<Status<T>()>;
        using post_processor = std::function<void(Status<T>&)>;
        AsyncTask(processor_func&& p, post_processor&& r) : Task<T>(std::move(r)), pr_(p) {}
        bool operator()() noexcept {
            auto s = pr_();
            this->post_(s);
            return true;
        }
        typename Task<T>::Type GetTaskType() { return Task<T>::Normal; }
    private:
        processor_func pr_;
    };

    template <typename T>
    class Future : public std::future<T> {
    public:
        Future(std::future<Status<T>>&& f) : f_(std::move(f)) {}
        T Get() {
            Status<T> status = f_.get();
            return status.result_;
        } 
    private:
        std::future<Status<T>> f_;
    };

    class Thread {
    public:
        Thread() {}
        ~Thread() {
          for (size_t i=0; i<queues_.size(); i++) {
            queues_[i]->Wait();
          }
          if (thr_.joinable()) thr_.join();
        }
        void Bind(Queue* q) {
            queues_.push_back(q);
        }
        void Start() {
            thr_ = std::thread([&](){
               size_t sz = queues_.size(); 
               size_t counter = 0;
               size_t failed_tries = 0;
               while(!stop_) {
                Runable* runable;
                if(queues_[counter++ % sz]->TryPopQ(runable)) {
                  (*runable)();
                  delete runable;
                } else {
                  failed_tries ++;
                  if (failed_tries == sz) {
                    if(queues_[counter ++ % sz]->Pop(runable)) {
                      (*runable)();
                      delete runable;
                    }
                    failed_tries = 0;
                  }
                }                  
               }
            });
        }
        void Stop() { 
            stop_ = true; 
        }
    private:
        std::thread thr_;
        std::vector<Queue*> queues_;
        volatile bool stop_{false};
    };

    ThreadPool(int pool_size, int capacity): pool_size_(pool_size) {
        threads_ = new Thread[pool_size];
        queues_ = new Queue*[pool_size];
        for (int i=0; i<pool_size; i++) {
            queues_[i] = new Queue(capacity, 10);
            // dispatch queue to thread
            threads_[i].Bind(queues_[i]);
        }
        for (int i=0; i<pool_size_; i++) {
            threads_[i].Start();
        }
    }
    ~ThreadPool() {
        stop_ = false;
        for (int i=0; i<pool_size_; i++) {
            threads_[i].Stop();
        }
        delete [] threads_;
        for (int i=0; i<pool_size_; i++) {
            delete queues_[i];    
        }
        delete [] queues_;
    }
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template<class F, class... Args,
        typename = typename std::enable_if<std::is_function<F(Args...)>::value>::type>
    auto Post(F&& f, Args&&... args)
    -> Future<typename std::result_of<F(Args...)>::type> {
        using result_type = typename std::result_of<F(Args...)>::type;
        Runable* task = Task<result_type>::Create(
                    std::bind(std::forward<F>(f), std::forward<Args>(args)...),
                    [&](Status<result_type>&) {}, true);
        ChooseQueue()->Push(task);
        return static_cast<FutureTask<result_type>*>(task)->GetFuture();
    }

    template<class F, class C,
        typename = typename std::enable_if<std::is_function<F()>::value>::type,
        typename = typename std::enable_if<std::is_function<C()>::value>::type>
    void Post(F&& f, C&& callback) {
        using result_type = typename std::result_of<F()>::type;
        Runable* task = Task<result_type>::Create(f,callback, false);
        ChooseQueue()->Push(task);
    }
    
private:
    Queue* ChooseQueue() {
        return queues_[rand() % pool_size_];
    }
private:
    ConcurrentBlockingQueue<Runable*> **queues_;
    Thread* threads_;
    volatile bool stop_ = false;
    int pool_size_;
};

}
}
