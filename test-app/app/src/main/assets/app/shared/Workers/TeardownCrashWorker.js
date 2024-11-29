let workerIndex = -1;
onmessage = function(msg) {
    workerIndex = msg.data;
};

// Notify test that we are starting
postMessage("starting");

let o = NSNotificationCenter.defaultCenter.addObserverForNameObjectQueueUsingBlock('send-to-worker', null, null, (obj) => {
//   console.log("received NSNotificationCenter message " + workerIndex, obj);
});

// Allocate some native objects with JS wrappers
for (var i = 0; i < 100; i++) {
     NSString.alloc();
}

require("../../Infrastructure/timers");
setTimeout(() => {
    NSNotificationCenter.defaultCenter.removeObserver(o);
    // Notify test that we have finished
    postMessage("closing");
    // Necessary for the correct teardown of the worker thread, otherwise native JSBlock will never be deallocated (it holds a strong reference to the VM and
    // the worker thread waits for VM's refcount to reach 1 before destroying it)
//    o = null;
//    __collect();
   // Alternatively,  __releaseNativeCounterpart can be used.
    __releaseNativeCounterpart(o);

    global.close();
}, 700);
