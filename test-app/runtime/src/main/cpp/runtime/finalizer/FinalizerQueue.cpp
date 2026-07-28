#include "FinalizerQueue.h"
#include "JEnv.h"
#include "NativeScriptException.h"
#include "NativeScriptAssert.h"
#include "Runtime.h"
#include "jsr.h" // NapiScope (resolved to the active engine's implementation)
#include <sstream>

using namespace tns;

jclass FinalizerQueue::HANDLER_CLASS = nullptr;
jmethodID FinalizerQueue::HANDLER_CTOR = nullptr;
jmethodID FinalizerQueue::HANDLER_SCHEDULE = nullptr;
jmethodID FinalizerQueue::HANDLER_RELEASE = nullptr;

FinalizerQueue::FinalizerQueue(napi_env env) : env_(env) {
    JEnv jEnv;
    if (HANDLER_CLASS == nullptr) {
        HANDLER_CLASS = jEnv.FindClass("com/tns/FinalizerHandler");
        assert(HANDLER_CLASS != nullptr);
        HANDLER_CTOR = jEnv.GetMethodID(HANDLER_CLASS, "<init>", "(J)V");
        HANDLER_SCHEDULE = jEnv.GetMethodID(HANDLER_CLASS, "schedule", "()V");
        HANDLER_RELEASE = jEnv.GetMethodID(HANDLER_CLASS, "release", "()V");
    }

    // Bind a FinalizerHandler to the current (runtime) thread's Looper.
    jobject localHandler = jEnv.NewObject(HANDLER_CLASS, HANDLER_CTOR,
                                          reinterpret_cast<jlong>(this));
    handler_ = jEnv.NewGlobalRef(localHandler);
}

FinalizerQueue::~FinalizerQueue() {
    Destroy();
}

void FinalizerQueue::Post(napi_finalize cb, void *data, void *hint) {
    if (cb == nullptr) {
        return;
    }

    bool runInline = false;
    jobject handler = nullptr; // captured under the lock so Destroy can't free it mid-use
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) {
            // Teardown: the loop is no longer draining us. Fall back to running the
            // cleanup inline (matches the pre-deferral behavior for this edge).
            runInline = true;
        } else {
            queue_.push_back({cb, data, hint});
            // Only wake the loop on the empty -> non-empty transition; further posts
            // ride the already-scheduled drain.
            if (!scheduled_) {
                scheduled_ = true;
                handler = handler_;
            }
        }
    }

    if (runInline) {
        cb(env_, data, hint);
        return;
    }

    if (handler != nullptr) {
        JEnv jEnv;
        jEnv.CallVoidMethod(handler, HANDLER_SCHEDULE);
    }
}

void FinalizerQueue::Drain() {
    // Take the whole batch under the lock, then run callbacks outside it: a
    // callback may free objects whose GC finalizers Post() again, and that must
    // not deadlock on the queue mutex. Re-posted work sets scheduled_ = true and
    // wakes the loop for the next tick.
    std::vector<Entry> batch;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        scheduled_ = false;
        batch.swap(queue_);
    }

    for (auto &entry: batch) {
        if (entry.cb != nullptr) {
            // A handle scope roots any napi values the callback materializes. The
            // JS lock/context are already held by the NapiScope opened at the JNI
            // entry point (nativeDrainFinalizers).
            napi_handle_scope scope;
            napi_open_handle_scope(env_, &scope);
            entry.cb(env_, entry.data, entry.hint);
            napi_close_handle_scope(env_, scope);
        }
    }
}

void FinalizerQueue::Destroy() {
    // Mark stopped and detach the handler under the lock, so a concurrent Post
    // (possible from a background JS thread's GC) either observes stopped_ and
    // runs inline, or has already captured the handler before we release it.
    std::vector<Entry> batch;
    jobject handler = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) {
            return;
        }
        stopped_ = true;
        handler = handler_;
        handler_ = nullptr;
        batch.swap(queue_);
    }

    if (handler != nullptr) {
        JEnv jEnv;
        jEnv.CallVoidMethod(handler, HANDLER_RELEASE);
        jEnv.DeleteGlobalRef(handler);
    }

    // Run whatever was still queued while env/context are valid; finalizers that
    // fire during the subsequent env teardown then run inline via PostFinalizer.
    for (auto &entry: batch) {
        if (entry.cb != nullptr) {
            entry.cb(env_, entry.data, entry.hint);
        }
    }
}

void tns::PostFinalizer(napi_env env, napi_finalize cb, void *data, void *hint) {
    Runtime::PostFinalizer(env, cb, data, hint);
}

// Reverse-native for com.tns.FinalizerHandler.nativeDrainFinalizers (bound by
// symbol name). Runs on the runtime thread at a safe, post-GC message-loop tick.
extern "C" JNIEXPORT void JNICALL
Java_com_tns_FinalizerHandler_nativeDrainFinalizers(JNIEnv *jniEnv, jclass clazz, jlong queuePtr) {
    auto *queue = reinterpret_cast<tns::FinalizerQueue *>(queuePtr);
    if (queue == nullptr) {
        return;
    }
    try {
        // Enter the JS scope (lock + context + handle scope) before running any
        // callback, since they make Node-API calls (e.g. napi_delete_reference).
        NapiScope scope(queue->Env());
        queue->Drain();
    } catch (NativeScriptException &e) {
        e.ReThrowToJava(nullptr);
    } catch (std::exception &e) {
        std::stringstream ss;
        ss << "Error: c++ exception: " << e.what() << std::endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToJava(nullptr);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToJava(nullptr);
    }
}
