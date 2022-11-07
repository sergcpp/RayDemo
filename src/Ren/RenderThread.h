#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "RingBuffer.h"

namespace Ren {
typedef void(*TaskFunc)(void *arg);
struct Task {
    TaskFunc    func;
    void        *arg;
};

struct TaskList : public std::vector<Task> {
    std::shared_ptr<void> done_event;

    TaskList();
    explicit TaskList(size_t size) : TaskList() {
        this->reserve(size);
    }

    void Submit(class RenderThread *r);
    void Wait();
};

class RenderThread {
protected:
    RingBuffer<TaskList> task_lists_;
    std::mutex add_list_mtx_;
public:
    RenderThread() : task_lists_(128) {}

    void AddTaskList(TaskList &&list);
    void AddSingleTask(TaskFunc func, void *arg);
    void ProcessSingleTask(TaskFunc func, void *arg);

    template<typename T>
    void ProcessSingleTask(T func) {
        auto f = new T(func);
        ProcessSingleTask([](void *arg) {
            auto ff = (T *)arg;
            (*ff)();
            delete ff;
        }, f);
    }

    bool ProcessTasks();
};
}
