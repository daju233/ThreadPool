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
    template<class F, class... Args>
    //形如 T&& 的模板参数则表示转发引用。当 T 是一个模板参数时，T&& 可以绑定到左值或右值，并且在结合 std::forward 使用时能够保持原始值类别（即左值或右值）
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;
        //std::result_of 是 C++ 标准库中的一个 traits 类模板，它用于推导出某个可调用对象在其参数为特定类型时的返回类型
        //例如，如果你有一个函数 int add(int, int)，那么 std::result_of<int(*)(int, int)> 将推导出其返回类型为 int
        //由于 std::result_of 是一个嵌套类型别名（即它内部有一个名为 type 的类型），我们需要使用 typename 关键字来告诉编译器我们正在引用一个类型
    ~ThreadPool();
private:
    // need to keep track of threads so we can join them
    std::vector< std::thread > workers;
    // the task queue
    std::queue< std::function<void()> > tasks;//任务队列
    
    // synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};
 
// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads)
    :   stop(false)
{   //构造函数
    for(size_t i = 0;i<threads;++i)
        workers.emplace_back(//emplace将新元素推入队列末尾。该元素是就地构建的，即不执行复制或移动操作。使用与提供给函数完全相同的参数来调用元素的构造函数
            [this]
            {
                for(;;)//忙等
                {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);//获取锁
                        this->condition.wait(lock,
                            [this]{ return this->stop || !this->tasks.empty(); });//释放锁，等待信号量唤醒
                            //线程池停止或不为空则继续执行
                        if(this->stop && this->tasks.empty())//线程池停止且为空则线程返回
                            return;
                        task = std::move(this->tasks.front());//获取任务
                        this->tasks.pop();
                    }

                    task();//执行任务
                }
            }
        );
}

// add new work item to the pool 添加任务队列？？？
template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    //std::packaged_task 是 C++11 引入的一个类模板，它封装了一个可调用对象（如函数、lambda 表达式或函数对象），并允许你将这个任务的结果与 std::future 关联起来。
    //通过这种方式，你可以在一个线程中异步执行任务，并在另一个线程中获取任务的执行结果。
    //共享指针
    auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...) 
            //绑定成员函数？？？？？？？？？？？？？
            //完美转发是指在模板函数中将参数的值类别（左值或右值）传递给另一个函数调用，而不改变其值类别。C++11 引入了 std::forward 来实现这一功能。
        );
        
    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);//获取锁，超出作用域自动释放

        // don't allow enqueueing after stopping the pool
        if(stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace([task](){ (*task)(); });
    }
    condition.notify_one();    // 通知一个等待的工作线程来处理新任务
    return res;    // 返回 future 对象，用于稍后获取任务结果
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();//当一个线程释放锁后，其他线程会自动竞争锁，这里的notify_all会让所有线程都会获得一次锁
    for(std::thread &worker: workers)
        worker.join();
}

#endif
