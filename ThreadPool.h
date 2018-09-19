#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool {
public:
    ThreadPool(size_t);
    // take variable parameters
    template<class F, class... Args>
    // rvalue reference
    // new syntax for function declaration in c++11
    // old : return-type identifier ( argument-declarations... )
    // new : auto identifier ( argument-declarations... ) -> return_type
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>;
    ~ThreadPool();
private:
    // need to keep track of threads so we can join them
    std::vector< std::thread > workers;
    // the task queue
    std::queue< std::function<void()> > tasks;

    // synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads)
    :   stop(false)
{
    for(size_t i = 0;i<threads;++i)
        // construct threads using below function
        workers.emplace_back(
            // a void function
            // these threads will wait here at the beginning
            [this]
            {
                // keep executing while there are still unfinished tasks
                for(;;)
                {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        // can only unlock this thread when pred is true
                        // keep waiting when tasks is empty
                        this->condition.wait(lock,
                            [this]{ return this->stop || !this->tasks.empty(); });
                        // return when receive a stop signal or the tasks are empty
                        if(this->stop && this->tasks.empty())
                            return;
                        // transfer it to task, using rvalue reference
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    // execute the task
                    task();
                }
            }
        );
}

// add new work item to the pool
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    // get result type
    using return_type = typename std::result_of<F(Args...)>::type;
    // make_shared wraps it to a shared_ptr
    // A packaged_task wraps a callable element and allows its result to be retrieved asynchronously.
    auto task = std::make_shared< std::packaged_task<return_type()> >(
            // bind return a function object, with its arguments bound to args.
            // forward returns an rvalue reference to arg if arg is not an lvalue reference.
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
    // packaged_task -> get_future
    // a future object associated with the object's shared state.
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow enqueueing after stopping the pool
        if(stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace([task](){ (*task)(); });
    }
    // Unblocks one of the threads (arbitrarily) currently waiting for this condition.
    condition.notify_one();
    return res;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for(std::thread &worker: workers)
        worker.join();
}

#endif
