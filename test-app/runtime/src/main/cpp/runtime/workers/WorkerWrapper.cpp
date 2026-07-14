#include "WorkerWrapper.h"

#include <android/looper.h>
#include <pthread.h>
#include <unistd.h>

#include <thread>
#include <vector>

#include "ArgConverter.h"
#include "CallbackHandlers.h"
#include "GlobalHelpers.h"
#include "JEnv.h"
#include "JniLocalRef.h"
#include "LooperTasks.h"
#include "NativeScriptAssert.h"
#include "NativeScriptException.h"
#include "Runtime.h"
#include "jsr.h"
#include "native_api_util.h"

#if defined(__V8__) && defined(APPLICATION_IN_DEBUG)
#include "JsV8InspectorClient.h"
#include "WorkerInspectorClient.h"
#endif

namespace tns {

WorkerWrapper::WorkerWrapper(napi_env parentEnv, int workerId, std::string workerPath,
                             std::string callingDir, int priority, napi_value workerObject)
        : parentEnv_(parentEnv),
          // runs on the parent's thread, where the parent runtime is alive
          parentTasks_(Runtime::GetRuntime(parentEnv)->GetLooperTasks()),
          workerEnv_(nullptr),
          runtime_(nullptr),
          workerId_(workerId),
          workerPath_(std::move(workerPath)),
          callingDir_(std::move(callingDir)),
          // workerPath_ (not workerPath) - the parameter was just moved from
          threadName_("W" + std::to_string(workerId) + ": " + workerPath_),
          priority_(priority),
          poWorker_(nullptr),
          isClosing_(false),
          isTerminating_(false),
          isDisposed_(false),
          javaLooperRef_(nullptr) {
    napi_status status;
    NAPI_GUARD(napi_create_reference(parentEnv, workerObject, 1, &poWorker_)) {}
}

void WorkerWrapper::Start() {
    auto self = shared_from_this();
    std::thread thread([self]() {
        self->BackgroundLooper(self);
    });
    thread.detach();
}

void WorkerWrapper::PostMessage(std::shared_ptr<worker::Message> message) {
    if (!isTerminating_ && !isClosing_) {
        queue_.Push(message);
    }
}

void WorkerWrapper::PostMessageToParent(std::shared_ptr<worker::Message> message) {
    if (isTerminating_) {
        return;
    }

    auto parentTasks = parentTasks_.lock();
    if (parentTasks == nullptr) {
        // the parent runtime is gone (e.g. a parent worker that shut down)
        return;
    }

    int workerId = workerId_;
    parentTasks->Post([workerId, message]() {
        WorkerWrapper::FireMessageOnParentWorkerObject(workerId, message);
    });
}

void WorkerWrapper::Terminate() {
    if (isClosing_ || isDisposed_) {
        // The worker is already shutting down on its own; nothing to do.
        return;
    }

    bool wasTerminating = isTerminating_.exchange(true);
    if (wasTerminating) {
        return;
    }

#if defined(__V8__) && defined(APPLICATION_IN_DEBUG)
    {
        // A worker paused at a breakpoint sits in the inspector's nested pause
        // loop, not in Looper.loop() - kick it loose so the cooperative looper
        // quit below can take effect.
        std::lock_guard<std::mutex> lock(inspectorMutex_);
        if (inspector_ != nullptr) {
            inspector_->NotifyTerminating();
        }
    }
#endif

    // Cooperative: there is no cross-thread "interrupt running JS" primitive in
    // napi (v8::TerminateExecution has no equivalent), so we simply quit the
    // worker's Java looper. Any JS already running finishes its current turn;
    // once the callback unwinds, Looper.loop() returns and the thread shuts
    // down. A worker stuck in a synchronous busy-loop will NOT be preempted.
    QuitLooper();
}

void WorkerWrapper::Close() {
    bool wasClosing = isClosing_.exchange(true);
    if (wasClosing) {
        return;
    }

    // Once the current callback unwinds, Looper.loop() returns and the
    // thread proceeds to cleanup. Pending messages are dropped, matching the
    // previous front-of-queue TerminateAndCloseThread behavior.
    QuitLooper();
}

void WorkerWrapper::QuitLooper() {
    std::lock_guard<std::mutex> lock(looperMutex_);
    if (javaLooperRef_ != nullptr) {
        JEnv env;
        env.CallVoidMethod(javaLooperRef_, LOOPER_QUIT_METHOD_ID);
    }
}

int WorkerWrapper::DrainCallback(int fd, int events, void* data) {
    uint64_t value;
    read(fd, &value, sizeof(value));

    auto wrapper = static_cast<WorkerWrapper*>(data);
    wrapper->DrainPendingTasks();
    return 1;
}

void WorkerWrapper::DrainPendingTasks() {
    napi_env env = workerEnv_.load();
    if (env == nullptr || isTerminating_) {
        return;
    }

    auto messages = queue_.PopAll();
    if (messages.empty()) {
        return;
    }

    NapiScope scope(env);
    napi_status status;
    napi_value globalObject;
    NAPI_GUARD(napi_get_global(env, &globalObject)) {
        return;
    }

    for (auto& message : messages) {
        if (isTerminating_ || isClosing_) {
            break;
        }

        napi_value callback;
        NAPI_GUARD(napi_get_named_property(env, globalObject, "onmessage", &callback)) {}
        if (!napi_util::is_of_type(env, callback, napi_function)) {
            DEBUG_WRITE(
                    "WORKER: couldn't fire a worker's `onmessage` callback because it isn't implemented!");
            continue;
        }

        napi_value event;
        NAPI_GUARD(napi_create_object(env, &event)) {}
        napi_value data = tns::JsonParseString(env, message->data);
        if (!napi_util::is_null_or_undefined(env, data)) {
            NAPI_GUARD(napi_set_named_property(env, event, "data", data)) {}
        }

        napi_value args[1] = {event};
        napi_value result;
        status = napi_call_function(env, globalObject, callback, 1, args, &result);
        if (status == napi_pending_exception && !isTerminating_) {
            napi_value error;
            NAPI_GUARD(napi_get_and_clear_last_exception(env, &error)) {}
            CallbackHandlers::CallWorkerScopeOnErrorHandle(env, error);
        }
    }
}

void WorkerWrapper::FireMessageOnParentWorkerObject(int workerId,
                                                    std::shared_ptr<worker::Message> message) {
    auto wrapper = WorkerWrapper::GetById(workerId);
    if (wrapper == nullptr) {
        DEBUG_WRITE("MAIN: no worker instance was found with workerId=%d.", workerId);
        return;
    }

    napi_env env = wrapper->parentEnv_;
    NapiScope scope(env);

    if (wrapper->poWorker_ == nullptr) {
        DEBUG_WRITE(
                "MAIN: couldn't fire a worker(id=%d) object's `onmessage` callback because the worker has been cleared.",
                workerId);
        return;
    }

    napi_value worker = napi_util::get_ref_value(env, wrapper->poWorker_);
    if (napi_util::is_null_or_undefined(env, worker)) {
        return;
    }

    napi_status status;
    napi_value callback;
    NAPI_GUARD(napi_get_named_property(env, worker, "onmessage", &callback)) {}
    if (!napi_util::is_of_type(env, callback, napi_function)) {
        DEBUG_WRITE(
                "MAIN: couldn't fire a worker(id=%d) object's `onmessage` callback because it isn't implemented.",
                workerId);
        return;
    }

    napi_value event;
    NAPI_GUARD(napi_create_object(env, &event)) {}
    napi_value data = tns::JsonParseString(env, message->data);
    NAPI_GUARD(napi_set_named_property(env, event, "data", data)) {}

    napi_value args[1] = {event};
    napi_value result;
    status = napi_call_function(env, worker, callback, 1, args, &result);
    if (status == napi_pending_exception) {
        napi_value error;
        NAPI_GUARD(napi_get_and_clear_last_exception(env, &error)) {}
        // Surface to Java; LooperTasks::Drain wraps this in a try/catch.
        throw NativeScriptException(env, error, "Error calling onmessage on Worker object");
    }
}

void WorkerWrapper::PassUncaughtExceptionFromWorkerToParent(const std::string& message,
                                                            const std::string& filename,
                                                            const std::string& stackTrace,
                                                            int lineno) {
    auto parentTasks = parentTasks_.lock();
    if (parentTasks == nullptr) {
        // the parent runtime is gone (e.g. a parent worker that shut down)
        return;
    }

    int workerId = workerId_;
    std::string threadName = threadName_;

    parentTasks->Post([workerId, message, filename, stackTrace, lineno, threadName]() {
        WorkerWrapper::FireErrorOnParentWorkerObject(workerId, message, stackTrace, filename,
                                                     lineno, threadName);
    });
}

void WorkerWrapper::FireErrorOnParentWorkerObject(int workerId, const std::string& message,
                                                  const std::string& stackTrace,
                                                  const std::string& filename, int lineno,
                                                  const std::string& threadName) {
    auto wrapper = WorkerWrapper::GetById(workerId);
    if (wrapper == nullptr) {
        DEBUG_WRITE("MAIN: no worker instance was found with workerId=%d.", workerId);
        return;
    }

    napi_env env = wrapper->parentEnv_;
    NapiScope scope(env);

    if (wrapper->poWorker_ == nullptr) {
        DEBUG_WRITE(
                "MAIN: couldn't fire a worker(id=%d) object's `onerror` callback because the worker has been cleared.",
                workerId);
        return;
    }

    napi_value worker = napi_util::get_ref_value(env, wrapper->poWorker_);
    if (napi_util::is_null_or_undefined(env, worker)) {
        return;
    }

    napi_status status;
    napi_value callback;
    NAPI_GUARD(napi_get_named_property(env, worker, "onerror", &callback)) {}

    if (napi_util::is_of_type(env, callback, napi_function)) {
        napi_value errEvent;
        napi_value msgValue;
        NAPI_GUARD(napi_create_string_utf8(env, message.c_str(), NAPI_AUTO_LENGTH, &msgValue)) {}
        napi_value codeValue;
        NAPI_GUARD(napi_create_string_utf8(env, "", 0, &codeValue)) {}
        NAPI_GUARD(napi_create_error(env, codeValue, msgValue, &errEvent)) {}

        // Combine the worker-side stack trace with the Worker object's captured
        // construction stack (main thread), mirroring the old fork behavior.
        napi_value mainStackValue;
        NAPI_GUARD(napi_get_named_property(env, worker, "__stack__", &mainStackValue)) {}
        std::string fullStack = stackTrace;
        if (napi_util::is_of_type(env, mainStackValue, napi_string)) {
            std::string mainStack = ArgConverter::ConvertToString(env, mainStackValue);
            auto nl = mainStack.find_first_of("\n");
            if (nl != std::string::npos) {
                fullStack = stackTrace + "\n" + mainStack.substr(nl + 1);
            }
        }

        napi_value fullStackValue;
        NAPI_GUARD(napi_create_string_utf8(env, fullStack.c_str(), fullStack.size(), &fullStackValue)) {}
        NAPI_GUARD(napi_set_named_property(env, errEvent, "stack", fullStackValue)) {}

        napi_value args[1] = {errEvent};
        napi_value result;
        status = napi_call_function(env, worker, callback, 1, args, &result);

        if (status == napi_pending_exception) {
            napi_value exception;
            NAPI_GUARD(napi_get_and_clear_last_exception(env, &exception)) {}
            throw NativeScriptException(env, exception, "Error calling onerror on Worker object");
        }

        // If the handler returns a truthy value, the exception is handled.
        if (!napi_util::is_null_or_undefined(env, result)) {
            bool handled = false;
            NAPI_GUARD(napi_get_value_bool(env, result, &handled)) {}
            if (handled) {
                return;
            }
        }
    }

    DEBUG_WRITE(
            "Unhandled exception in '%s' thread. file: %s, line %d, message: %s\nStackTrace: %s",
            threadName.c_str(), filename.c_str(), lineno, message.c_str(), stackTrace.c_str());
}

void WorkerWrapper::BackgroundLooper(std::shared_ptr<WorkerWrapper> self) {
    JavaVM* jvm = Runtime::GetJVM();
    JNIEnv* jniEnv = nullptr;

    JavaVMAttachArgs attachArgs;
    attachArgs.version = JNI_VERSION_1_6;
    attachArgs.name = const_cast<char*>(threadName_.c_str());
    attachArgs.group = nullptr;
    jvm->AttachCurrentThread(&jniEnv, &attachArgs);

    // pthread names are limited to 15 chars
    pthread_setname_np(pthread_self(), threadName_.substr(0, 15).c_str());

    int runtimeId = -1;

    try {
        JEnv env;

        // Performs the cgroup/scheduling-policy move in addition to the nice
        // value, exactly like the previous Java-side
        // Process.setThreadPriority(THREAD_PRIORITY_BACKGROUND) call.
        env.CallStaticVoidMethod(PROCESS_CLASS, SET_THREAD_PRIORITY_METHOD_ID, priority_);

        if (!isTerminating_ && !isClosing_) {
            // Prepares the Java Looper for this thread and creates the
            // per-worker com.tns.Runtime (which creates the worker env on this
            // thread via initNativeScript).
            JniLocalRef callingDir(env.NewStringUTF(callingDir_.c_str()));
            runtimeId = env.CallStaticIntMethod(RUNTIME_CLASS, INIT_WORKER_RUNTIME_METHOD_ID,
                                                workerId_, (jstring) callingDir);
            runtime_ = Runtime::GetRuntime(runtimeId);

            {
                std::lock_guard<std::mutex> lock(looperMutex_);
                JniLocalRef looper(env.CallStaticObjectMethod(LOOPER_CLASS, MY_LOOPER_METHOD_ID));
                javaLooperRef_ = env.NewGlobalRef(looper);
            }

            // Looper.prepare() ran above, so ALooper_forThread() returns the
            // native looper backing the Java one - fds added here are pumped by
            // Looper.loop().
            queue_.Initialize(ALooper_forThread(), WorkerWrapper::DrainCallback, this);

            napi_env napiEnv = runtime_->GetNapiEnv();
            workerEnv_.store(napiEnv);
            {
                std::lock_guard<std::mutex> lock(registryMutex_);
                envRegistry_[napiEnv] = this;
            }

            if (!isTerminating_) {
                NapiScope scope(napiEnv);

#if defined(__V8__) && defined(APPLICATION_IN_DEBUG)
                // Expose this worker to an attached Chrome DevTools frontend
                // as a child target, mirroring the iOS runtime. Created before
                // the script runs so the worker's scripts are visible to the
                // debugger from the start.
                CreateInspector(napiEnv);
#endif

                runtime_->RunWorker(workerPath_);

                napi_status status;
                bool pending = false;
                NAPI_GUARD(napi_is_exception_pending(napiEnv, &pending)) {}
                if (pending) {
                    napi_value error;
                    NAPI_GUARD(napi_get_and_clear_last_exception(napiEnv, &error)) {}

                    std::string emsg;
                    std::string estack;
                    if (napi_util::is_of_type(napiEnv, error, napi_object)) {
                        napi_value m;
                        napi_value s;
                        NAPI_GUARD(napi_get_named_property(napiEnv, error, "message", &m)) {}
                        NAPI_GUARD(napi_get_named_property(napiEnv, error, "stack", &s)) {}
                        if (napi_util::is_of_type(napiEnv, m, napi_string)) {
                            emsg = ArgConverter::ConvertToString(napiEnv, m);
                        }
                        if (napi_util::is_of_type(napiEnv, s, napi_string)) {
                            estack = ArgConverter::ConvertToString(napiEnv, s);
                        }
                    } else {
                        napi_value m;
                        NAPI_GUARD(napi_coerce_to_string(napiEnv, error, &m)) {}
                        emsg = ArgConverter::ConvertToString(napiEnv, m);
                    }

                    if (!isTerminating_) {
                        PassUncaughtExceptionFromWorkerToParent(emsg, workerPath_, estack, 0);
                    }
                }
            }

            // Deliver messages that were posted before the worker was ready.
            DrainPendingTasks();

            if (!isTerminating_ && !isClosing_) {
                // Blocks, pumping Java Handler messages (cross-thread Java->JS
                // calls), timers and the worker inbox until quit() is called.
                env.CallStaticVoidMethod(RUNTIME_CLASS, RUN_WORKER_LOOP_METHOD_ID);
            }
        }
    } catch (NativeScriptException& ex) {
        if (jniEnv->ExceptionCheck()) {
            jniEnv->ExceptionClear();
        }
        if (!isTerminating_) {
            PassUncaughtExceptionFromWorkerToParent(std::string(ex.what()), workerPath_, "", 0);
        }
    } catch (std::exception& ex) {
        DEBUG_WRITE_FORCE("Worker(id=%d) error: c++ exception: %s", workerId_, ex.what());
    } catch (...) {
        DEBUG_WRITE_FORCE("Worker(id=%d) error: unknown c++ exception!", workerId_);
    }

    // ----- Shutdown (close, terminate or bootstrap failure) -----

    isTerminating_ = true;

    // Terminate any workers this worker created (nested workers). Their Worker
    // object refs live in this env, so they must be released before the env is
    // disposed below. Each child cascades to its own children during shutdown.
    {
        napi_env napiEnv = workerEnv_.load();
        if (napiEnv != nullptr) {
            TerminateChildren(napiEnv);
        }
    }

#if defined(__V8__) && defined(APPLICATION_IN_DEBUG)
    // The inspector must be gone before the Runtime (and with it the env/isolate)
    // is disposed below; unregistering also tells DevTools the target is gone.
    DestroyInspector();
#endif

    // On this thread: safe to unregister the inbox fd from the looper.
    queue_.Terminate();

    if (runtime_ != nullptr) {
        napi_env napiEnv = workerEnv_.load();

        try {
            // Java-side detach (GcListener.unsubscribe + runtimeCache.remove)
            // must happen before the env is disposed.
            JEnv env;
            env.CallStaticVoidMethod(RUNTIME_CLASS, DETACH_WORKER_RUNTIME_METHOD_ID, runtimeId);
        } catch (NativeScriptException& ex) {
            if (jniEnv->ExceptionCheck()) {
                jniEnv->ExceptionClear();
            }
            DEBUG_WRITE_FORCE("Worker(id=%d) error while detaching Java runtime: %s", workerId_,
                              ex.what());
        }

        {
            std::lock_guard<std::mutex> lock(registryMutex_);
            if (napiEnv != nullptr) {
                envRegistry_.erase(napiEnv);
            }
        }

        workerEnv_.store(nullptr);

        // Enters the engine scope, tears down the runtime and frees the env,
        // then deletes the Runtime (which frees the underlying engine runtime).
        Runtime::DisposeWorkerRuntime(runtime_);
        runtime_ = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(looperMutex_);
        if (javaLooperRef_ != nullptr) {
            jniEnv->DeleteGlobalRef(javaLooperRef_);
            javaLooperRef_ = nullptr;
        }
    }

    isDisposed_ = true;

    // Notify the parent thread so the Worker object's ref and the registry
    // entry are released (no-op if terminate() or the parent's own shutdown
    // already cleared them).
    if (auto parentTasks = parentTasks_.lock()) {
        int workerId = workerId_;
        parentTasks->Post([workerId]() {
            WorkerWrapper::ClearWorkerOnParent(workerId);
        });
    }

    // ART aborts if a native thread exits while still attached. This must be
    // the very last JNI-touching action on this thread.
    jvm->DetachCurrentThread();
}

int WorkerWrapper::NextWorkerId() {
    return nextWorkerId_.fetch_add(1, std::memory_order_relaxed) + 1;
}

std::shared_ptr<WorkerWrapper> WorkerWrapper::GetById(int workerId) {
    std::lock_guard<std::mutex> lock(registryMutex_);
    auto it = registry_.find(workerId);
    return it != registry_.end() ? it->second : nullptr;
}

void WorkerWrapper::Insert(int workerId, std::shared_ptr<WorkerWrapper> wrapper) {
    std::lock_guard<std::mutex> lock(registryMutex_);
    registry_.emplace(workerId, std::move(wrapper));
}

void WorkerWrapper::ClearWorkerOnParent(int workerId) {
    std::shared_ptr<WorkerWrapper> wrapper;
    {
        std::lock_guard<std::mutex> lock(registryMutex_);
        auto it = registry_.find(workerId);
        if (it == registry_.end()) {
            return;
        }
        wrapper = it->second;
        registry_.erase(it);
    }

    if (wrapper->poWorker_ != nullptr) {
        NapiScope scope(wrapper->parentEnv_);
        napi_status status;
        NAPI_GUARD(napi_delete_reference(wrapper->parentEnv_, wrapper->poWorker_)) {}
        wrapper->poWorker_ = nullptr;
    }
}

void WorkerWrapper::TerminateChildren(napi_env parentEnv) {
    std::vector<std::shared_ptr<WorkerWrapper>> children;
    {
        std::lock_guard<std::mutex> lock(registryMutex_);
        for (auto& entry : registry_) {
            if (entry.second->parentEnv_ == parentEnv) {
                children.push_back(entry.second);
            }
        }
    }

    for (auto& child : children) {
        DEBUG_WRITE("Terminating nested worker(id=%d) because its parent is shutting down",
                    child->workerId_);
        child->Terminate();
        ClearWorkerOnParent(child->workerId_);
    }
}

WorkerWrapper* WorkerWrapper::FromEnv(napi_env env) {
    std::lock_guard<std::mutex> lock(registryMutex_);
    auto it = envRegistry_.find(env);
    return it != envRegistry_.end() ? it->second : nullptr;
}

void WorkerWrapper::EnsureJniCached() {
    if (RUNTIME_CLASS != nullptr) {
        return;
    }

    JEnv env;

    RUNTIME_CLASS = env.FindClass("com/tns/Runtime");
    assert(RUNTIME_CLASS != nullptr);
    INIT_WORKER_RUNTIME_METHOD_ID =
            env.GetStaticMethodID(RUNTIME_CLASS, "initWorkerRuntime", "(ILjava/lang/String;)I");
    RUN_WORKER_LOOP_METHOD_ID = env.GetStaticMethodID(RUNTIME_CLASS, "runWorkerLoop", "()V");
    DETACH_WORKER_RUNTIME_METHOD_ID =
            env.GetStaticMethodID(RUNTIME_CLASS, "detachWorkerRuntime", "(I)V");

    LOOPER_CLASS = env.FindClass("android/os/Looper");
    assert(LOOPER_CLASS != nullptr);
    MY_LOOPER_METHOD_ID = env.GetStaticMethodID(LOOPER_CLASS, "myLooper", "()Landroid/os/Looper;");
    LOOPER_QUIT_METHOD_ID = env.GetMethodID(LOOPER_CLASS, "quit", "()V");

    PROCESS_CLASS = env.FindClass("android/os/Process");
    assert(PROCESS_CLASS != nullptr);
    SET_THREAD_PRIORITY_METHOD_ID =
            env.GetStaticMethodID(PROCESS_CLASS, "setThreadPriority", "(I)V");
}

#if defined(__V8__) && defined(APPLICATION_IN_DEBUG)
void WorkerWrapper::CreateInspector(napi_env env) {
    // Only when the root inspector client exists (debuggable app, created on
    // the main thread during runtime init) - never construct it from here.
    JsV8InspectorClient* root = JsV8InspectorClient::GetInstanceIfCreated();
    if (root == nullptr) {
        return;
    }

    // Same url scheme the module loader reports in Debugger.scriptParsed.
    // workerPath_ may still be relative to the caller's dir at this point
    // (resolution happens in require); callingDir_ ends with '/'.
    std::string url =
            "file://" + (workerPath_[0] == '/' ? workerPath_ : callingDir_ + workerPath_);

    auto* client = new WorkerInspectorClient(workerId_, env, ALooper_forThread(), url);
    {
        std::lock_guard<std::mutex> lock(inspectorMutex_);
        inspector_ = client;
    }

    // Register only once fully constructed: registration makes the client
    // reachable from the socket thread.
    root->RegisterWorkerTarget(workerId_, client);
}

void WorkerWrapper::DestroyInspector() {
    WorkerInspectorClient* client = nullptr;
    {
        std::lock_guard<std::mutex> lock(inspectorMutex_);
        client = inspector_;
        inspector_ = nullptr;
    }

    if (client == nullptr) {
        return;
    }

    // Unregister first: after this returns no other thread can reach the
    // client through the root's registry.
    JsV8InspectorClient* root = JsV8InspectorClient::GetInstanceIfCreated();
    if (root != nullptr) {
        root->UnregisterWorkerTarget(workerId_);
    }

    delete client;
}

void WorkerWrapper::ConsoleLog(v8_inspector::ConsoleAPIType method,
                               const std::vector<v8::Local<v8::Value>>& args) {
    std::lock_guard<std::mutex> lock(inspectorMutex_);
    if (inspector_ != nullptr) {
        inspector_->consoleLog(method, args);
    }
}
#endif

std::mutex WorkerWrapper::registryMutex_;
std::map<int, std::shared_ptr<WorkerWrapper>> WorkerWrapper::registry_;
std::map<napi_env, WorkerWrapper*> WorkerWrapper::envRegistry_;
std::atomic_int WorkerWrapper::nextWorkerId_(0);

jclass WorkerWrapper::RUNTIME_CLASS = nullptr;
jclass WorkerWrapper::LOOPER_CLASS = nullptr;
jclass WorkerWrapper::PROCESS_CLASS = nullptr;
jmethodID WorkerWrapper::INIT_WORKER_RUNTIME_METHOD_ID = nullptr;
jmethodID WorkerWrapper::RUN_WORKER_LOOP_METHOD_ID = nullptr;
jmethodID WorkerWrapper::DETACH_WORKER_RUNTIME_METHOD_ID = nullptr;
jmethodID WorkerWrapper::MY_LOOPER_METHOD_ID = nullptr;
jmethodID WorkerWrapper::LOOPER_QUIT_METHOD_ID = nullptr;
jmethodID WorkerWrapper::SET_THREAD_PRIORITY_METHOD_ID = nullptr;

}  // namespace tns
