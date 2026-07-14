#include "LooperTasks.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <vector>

#include "NativeScriptAssert.h"
#include "NativeScriptException.h"

namespace tns {

void LooperTasks::Initialize(ALooper* looper) {
    std::lock_guard<std::mutex> lock(mutex_);

    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd == -1) {
        DEBUG_WRITE_FORCE("LooperTasks: eventfd failed: %s", strerror(errno));
        return;
    }

    if (ALooper_addFd(looper, fd, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT,
                      LooperTasks::TasksReadyCallback, this) != 1) {
        DEBUG_WRITE_FORCE("LooperTasks: ALooper_addFd failed");
        close(fd);
        return;
    }

    looper_ = looper;
    ALooper_acquire(looper_);
    fd_ = fd;
}

void LooperTasks::Post(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (terminated_) {
        // The owning runtime is shutting down (or gone) - drop the task.
        return;
    }

    tasks_.push(std::move(task));

    if (fd_ != -1) {
        uint64_t value = 1;
        write(fd_, &value, sizeof(value));
    }
}

void LooperTasks::Terminate() {
    // Must run on the looper's own thread.
    std::lock_guard<std::mutex> lock(mutex_);
    terminated_ = true;

    if (fd_ != -1) {
        ALooper_removeFd(looper_, fd_);
        close(fd_);
        fd_ = -1;
    }

    if (looper_ != nullptr) {
        ALooper_release(looper_);
        looper_ = nullptr;
    }
}

int LooperTasks::TasksReadyCallback(int fd, int events, void* data) {
    uint64_t value;
    read(fd, &value, sizeof(value));

    static_cast<LooperTasks*>(data)->Drain();
    return 1;
}

void LooperTasks::Drain() {
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!tasks_.empty()) {
            tasks.push_back(std::move(tasks_.front()));
            tasks_.pop();
        }
    }

    for (auto& task : tasks) {
        // A C++ exception must never propagate out of an ALooper callback.
        try {
            task();
        } catch (NativeScriptException& ex) {
            ex.ReThrowToJava(nullptr);
        } catch (std::exception& ex) {
            DEBUG_WRITE_FORCE("Error: c++ exception in looper task: %s", ex.what());
        } catch (...) {
            DEBUG_WRITE_FORCE("Error: unknown c++ exception in looper task!");
        }
    }
}

}  // namespace tns
