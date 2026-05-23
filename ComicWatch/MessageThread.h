#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <type_traits>
#include <utility>

using Task = std::move_only_function<void()>;

class MessageThread {
public:
    MessageThread() : worker([this] { run(); }) {}

    ~MessageThread() { shutdown(); }

    // ==================== 同步调用 ≡ SendMessage ====================
    template<typename Func, typename... Args>
    auto send(Func&& f, Args&&... args)
        -> std::invoke_result_t<Func, Args...>
    {
        using R = std::invoke_result_t<Func, Args...>;

        std::promise<R> promise;
        auto future = promise.get_future();

        // 把 promise 捕获到 lambda 中
        auto task = [p = std::move(promise),
            func = std::forward<Func>(f),
            ...args = std::forward<Args>(args)]() mutable {
            try {
                if constexpr (std::is_void_v<R>) {
                    std::invoke(std::move(func), std::forward<decltype(args)>(args)...);
                    p.set_value();
                }
                else {
                    p.set_value(std::invoke(std::move(func), std::forward<decltype(args)>(args)...));
                }
            }
            catch (...) {
                p.set_exception(std::current_exception());
            }
            };

        {
            std::lock_guard<std::mutex> lk(mtx);
            sync_queue.emplace(std::move(task));
        }
        cv.notify_one();

        return future.get();   // 阻塞等待结果
    }

    // ==================== 异步投递 ≡ PostMessage ====================
    template<typename Func, typename... Args>
    void post(Func&& f, Args&&... args)
    {
        auto task = [func = std::forward<Func>(f),
            ...args = std::forward<Args>(args)]() mutable {
            try {
                std::invoke(std::move(func), std::forward<decltype(args)>(args)...);
            }
            catch (...) {
                // 异步任务默认吃掉异常，你也可以改成 std::terminate 或日志
            }
            };

        {
            std::lock_guard<std::mutex> lk(mtx);
            async_queue.emplace(std::move(task));
        }
        cv.notify_one();
    }
    // ==================== 投递空闲任务 ====================
    template<typename Func, typename... Args>
    void post_idle(Func&& f, Args&&... args) {
        auto task = [func = std::forward<Func>(f),
            ...args = std::forward<Args>(args)]() mutable {
            try {
                std::invoke(std::move(func), std::forward<decltype(args)>(args)...);
            }
            catch (...) {
                // 空闲任务默认吃掉异常，你也可以改成 std::terminate 或日志
            }
            };

        {
            std::lock_guard<std::mutex> lk(mtx);
            IdleTask = std::move(task);
        }
        cv.notify_one();
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lk(mtx);
            running = false;
        }
        cv.notify_one();
    }

    void stop_and_join() {
        shutdown();
        if (worker.joinable()) {
            worker.join();
        }
    }
private:
    void run() {
        while (true) {
            Task task;

            {
                std::unique_lock<std::mutex> lk(mtx);
                cv.wait(lk, [this] {
                    return !sync_queue.empty() || !async_queue.empty() || IdleTask || !running;
                    });

                if (!running) {
                    break;
                }

                // 优先处理同步任务
                if (!sync_queue.empty()) {
                    task = std::move(sync_queue.front());
                    sync_queue.pop();
                }
                else if (!async_queue.empty()) {
                    task = std::move(async_queue.front());
                    async_queue.pop();
                }
                else if (IdleTask) {
                    task = std::move(IdleTask);
                    IdleTask = nullptr;
                }
            }

            if (task) task();
        }
    }

    std::jthread worker;                    // C++20，更安全
    std::queue<Task> sync_queue;            // 高优先级
    std::queue<Task> async_queue;           // 低优先级
    Task IdleTask = nullptr;                      // 空闲任务，被顶替就不执行了
    std::mutex mtx;
    std::condition_variable cv;
    bool running = true;
};
