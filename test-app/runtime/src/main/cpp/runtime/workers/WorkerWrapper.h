#ifndef WORKERWRAPPER_H_
#define WORKERWRAPPER_H_

#include <jni.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#if defined(__V8__) && defined(APPLICATION_IN_DEBUG)
#include <vector>

#include "v8.h"
#include <src/inspector/v8-console-message.h>
#endif

#include "ConcurrentQueue.h"
#include "WorkerMessage.h"
#include "js_native_api.h"

namespace tns {

class LooperTasks;
class Runtime;
#if defined(__V8__) && defined(APPLICATION_IN_DEBUG)
class WorkerInspectorClient;
#endif

/*
 * Owns a worker's native thread and its lifecycle, mirroring the iOS
 * runtime's WorkerWrapper. The thread is a std::thread that attaches to the
 * JVM, prepares a Java Looper (so worker/plugin code can keep using Android
 * Handlers) and then drives that single looper, which pumps both Java
 * messages and the worker's C++ inbox (ConcurrentQueue + eventfd).
 *
 * This is the napi (multi-engine) port of the old V8 WorkerWrapper. It uses:
 *  - napi_env instead of v8::Isolate*
 *  - JSON string payloads (worker::Message) instead of V8 ValueSerializer
 *  - a cooperative Terminate() (quit the looper) instead of
 *    v8::TerminateExecution (which has no napi equivalent)
 *
 * Messaging:
 *  - parent -> worker: queue_ + eventfd wakeup on the worker looper
 *  - worker -> parent: the parent runtime's LooperTasks queue
 *
 * The parent may be the main thread or another worker (nested workers); a
 * worker's children are terminated when the worker itself shuts down.
 */
class WorkerWrapper : public std::enable_shared_from_this<WorkerWrapper> {
public:
    WorkerWrapper(napi_env parentEnv, int workerId, std::string workerPath,
                  std::string callingDir, int priority, napi_value workerObject);

    int WorkerId() const { return workerId_; }
    bool IsTerminating() const { return isTerminating_; }
    bool IsClosing() const { return isClosing_; }
    bool IsDisposed() const { return isDisposed_; }

    /*
     * Spawns the (detached) worker thread. The thread holds a shared_ptr to
     * this wrapper, keeping it alive until the thread fully shuts down.
     */
    void Start();

    /*
     * parent -> worker. Queues a JSON message and wakes the worker looper.
     * Messages posted before the worker finishes bootstrapping are drained
     * right after the worker script runs.
     */
    void PostMessage(std::shared_ptr<worker::Message> message);

    /*
     * worker -> parent. Posts a task that fires the Worker object's
     * `onmessage` on the parent runtime's thread.
     */
    void PostMessageToParent(std::shared_ptr<worker::Message> message);

    /*
     * Called on the parent's thread. Cooperatively interrupts the worker by
     * quitting its Java looper (no v8::TerminateExecution equivalent exists in
     * napi, so a runaway busy-loop in the worker is NOT preempted; the loop
     * stops once the current JS callback unwinds).
     */
    void Terminate();

    /*
     * Called on the worker thread (self.close()). Lets the current callback
     * unwind, then the looper quits and the thread shuts down gracefully.
     */
    void Close();

    /*
     * Posts the worker object's `onerror` invocation to the parent's thread.
     * Strings only - must not hold any napi handles from the worker env.
     */
    void PassUncaughtExceptionFromWorkerToParent(const std::string& message,
                                                 const std::string& filename,
                                                 const std::string& stackTrace,
                                                 int lineno);

    /*
     * Registry of live workers, keyed by workerId. Replaces the old
     * CallbackHandlers::id2WorkerMap. Guarded by a mutex because the worker
     * shutdown path posts cleanup from the worker thread.
     */
    static int NextWorkerId();
    static std::shared_ptr<WorkerWrapper> GetById(int workerId);
    static void Insert(int workerId, std::shared_ptr<WorkerWrapper> wrapper);

    /*
     * Parent thread only: resets the Worker object's napi_ref and removes the
     * wrapper from the registry. Idempotent.
     */
    static void ClearWorkerOnParent(int workerId);

    /*
     * Terminates and clears all workers whose parent is the given env.
     * Must run on the parent's thread, before the parent env is disposed
     * (the children's Worker object refs live in that env).
     * Cascades: each child terminates its own children during shutdown.
     */
    static void TerminateChildren(napi_env parentEnv);

    /*
     * Resolves the wrapper of a worker env via the env->wrapper registry.
     * Returns nullptr on the main env.
     */
    static WorkerWrapper* FromEnv(napi_env env);

    /*
     * Resolves the jclass/jmethodID handles used by the worker thread
     * bootstrap, before the first worker starts. The first call is always on
     * the main thread (nested workers require a main-thread worker first),
     * where class loading is safe.
     */
    static void EnsureJniCached();

#if defined(__V8__) && defined(APPLICATION_IN_DEBUG)
    /*
     * Worker thread (console.* fires on the isolate's own thread). Forwards
     * to the worker's inspector so the log reaches DevTools' worker target.
     */
    void ConsoleLog(v8_inspector::ConsoleAPIType method,
                    const std::vector<v8::Local<v8::Value>>& args);
#endif

private:
    void BackgroundLooper(std::shared_ptr<WorkerWrapper> self);
    void DrainPendingTasks();
    void QuitLooper();
    static int DrainCallback(int fd, int events, void* data);
    static void FireMessageOnParentWorkerObject(int workerId,
                                                std::shared_ptr<worker::Message> message);
    static void FireErrorOnParentWorkerObject(int workerId, const std::string& message,
                                              const std::string& stackTrace,
                                              const std::string& filename, int lineno,
                                              const std::string& threadName);

    napi_env parentEnv_;
    // The parent runtime's task queue; weak so a child outliving its parent
    // just drops its posts instead of touching a dead runtime.
    std::weak_ptr<LooperTasks> parentTasks_;
    std::atomic<napi_env> workerEnv_;
    Runtime* runtime_;

    const int workerId_;
    const std::string workerPath_;
    const std::string callingDir_;
    const std::string threadName_;
    const int priority_;

    napi_ref poWorker_;

    std::atomic_bool isClosing_;
    std::atomic_bool isTerminating_;
    std::atomic_bool isDisposed_;

    ConcurrentQueue queue_;

    std::mutex looperMutex_;
    jobject javaLooperRef_;

#if defined(__V8__) && defined(APPLICATION_IN_DEBUG)
    /*
     * Expose this worker to an attached Chrome DevTools frontend as a child
     * target. Created on the worker thread before the script runs, destroyed
     * on the worker thread before the env/isolate is disposed.
     */
    void CreateInspector(napi_env env);
    void DestroyInspector();

    WorkerInspectorClient* inspector_ = nullptr;
    std::mutex inspectorMutex_;
#endif

    static std::mutex registryMutex_;
    static std::map<int, std::shared_ptr<WorkerWrapper>> registry_;
    static std::map<napi_env, WorkerWrapper*> envRegistry_;
    static std::atomic_int nextWorkerId_;

    static jclass RUNTIME_CLASS;
    static jclass LOOPER_CLASS;
    static jclass PROCESS_CLASS;
    static jmethodID INIT_WORKER_RUNTIME_METHOD_ID;
    static jmethodID RUN_WORKER_LOOP_METHOD_ID;
    static jmethodID DETACH_WORKER_RUNTIME_METHOD_ID;
    static jmethodID MY_LOOPER_METHOD_ID;
    static jmethodID LOOPER_QUIT_METHOD_ID;
    static jmethodID SET_THREAD_PRIORITY_METHOD_ID;
};

}  // namespace tns

#endif /* WORKERWRAPPER_H_ */
