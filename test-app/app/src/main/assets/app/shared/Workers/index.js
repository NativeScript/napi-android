describe("TNS Workers", () => {
    let expectedAliveRuntimes = 1; // Main thread's TNSRuntime
    var originalTimeout;
    var DEFAULT_TIMEOUT_BEFORE_ASSERT = 500;

    if (global.NSObject) { // if platform is iOS
        DEFAULT_TIMEOUT_BEFORE_ASSERT = 1000;
    } else { // if Android
        // necessary in order to accommodate slower and older android emulators
        DEFAULT_TIMEOUT_BEFORE_ASSERT = 4000;
    }

    beforeEach(() => {
        originalTimeout = jasmine.DEFAULT_TIMEOUT_INTERVAL;
        jasmine.DEFAULT_TIMEOUT_INTERVAL = 16000; // For slower android emulators
    });

    afterEach(() => {
        jasmine.DEFAULT_TIMEOUT_INTERVAL = originalTimeout;
    });

    var gC = global.NSObject ? __collect : gc;

    it("Should have self property equal to global", (done) => {
        var worker = new Worker("./EvalWorker");
        worker.postMessage({ eval: "postMessage(self === global);" });
        worker.onmessage = (msg) => {
            expect(msg.data).toBe(true);
            worker.terminate();
            done();
        };
    });

    it("Should throw exception when no parameter is passed", () => {
        expect(() => new Worker()).toThrow();
    });

    if (global.NSObject) {
        it("Should call worker.onerror when script does not exist", (done) => {
            var worker = new Worker("./idonot-exist.js");
            worker.onerror = (e) => {
                expect(e).not.toEqual(null);
                worker.terminate();
                done();
            }
        });
    }

    it("Should throw exception when parameter is not a proper string", () => {
        // with object parameter
        expect(() => new Worker({ filename: "./EvalWorker.js" })).toThrow();
        // with number parameter
        expect(() => new Worker(5)).toThrow();
        // with more complex parameter
        expect(() => {
            new Worker((() => {
                function a() { }
            })())
        }).toThrow();
    });

    it("Should throw exception when not invoked as constructor", () => {
        expect(() => { Worker("./EvalWorker.js"); }).toThrow();
    });

    it("Should be terminated without error", () => {
        var worker = new Worker("./EvalWorker.js");
        worker.terminate();
    });

    it("Should throw exception when Worker.postMessage is called without arguments", () => {
        var w = new Worker("./EvalWorker.js");
        expect(() => { w.postMessage(); }).toThrow();
        w.terminate();
    });

    it("Should throw exception when Worker.postMessage is called more than one argument", () => {
        var w = new Worker("./EvalWorker.js");
        expect(() => { w.postMessage("Message: 1", "Message2") }).toThrow();
        w.terminate();
    });

    it("Should not receiving messages after worker.terminate() call", (done) => {
        var worker = new Worker("./EvalWorker.js");
        worker.terminate();
        worker.postMessage({ eval: "postMessage('two');" });

        var responseCounter = 0;
        worker.onmessage = (msg) => {
            responseCounter++;
        };

        setTimeout(() => {
            expect(responseCounter).toBe(0);
            done();
        }, DEFAULT_TIMEOUT_BEFORE_ASSERT);
    });

    it("Send a message from worker -> worker scope and receive back the same message", (done) => {
        var a = new Worker("./EvalWorker.js");

        var message = {
            value: "This is a very elaborate message that the worker will not know of.",
            eval: "postMessage(value);"
        }

        a.postMessage(message);
        a.onmessage = (msg) => {
            expect(msg.data).toBe(message.value);
            a.terminate();
            done();
        }
    });

    it("Send a LONG message from worker -> worker scope and receive back the same LONG message", (done) => {
        var a = new Worker("./EvalWorker.js");

        var message = {
            value: generateRandomString(5000),
            eval: "postMessage(value);"
        }

        a.postMessage(message);
        a.onmessage = (msg) => {
            expect(msg.data).toBe(message.value);
            a.terminate();
            done();
        }
    });

    it("Send an object and receive back the same object", (done) => {
        var a = new Worker("./EvalWorker.js");

        var message = {
            value: {
                str: "A message from main",
                number: 42,
                obj: { prop: "value", innerObj: { innnerProp: 67 } },
                bool: true,
                nullValue: null
            },
            eval: "postMessage(value);"
        }

        a.postMessage(message);
        a.onmessage = (msg) => {
            expect(msg.data).toEqual(message.value);
            a.terminate();
            done();
        }
    });

    it("Send an object containing repeated references", (done) => {
        var a = new Worker("./EvalWorker.js");

        var ref = { a: "a" };
        var message = {
            value: {
                obj: {
                    someProp: 5,
                    table1: [ref, ref],
                    table2: [ref]
                }
            },
            eval: "postMessage(value);"
        }

        a.postMessage(message);
        a.onmessage = (msg) => {
            expect(msg.data.obj.someProp).toEqual(message.value.obj.someProp);
            expect(msg.data.obj.table1[0].a).toEqual(message.value.obj.table1[0].a);
            expect(msg.data.obj.table1[1].a).toEqual(message.value.obj.table1[1].a);
            expect(msg.data.obj.table2[0].a).toEqual(message.value.obj.table2[0].a);
            a.terminate();
            done();
        }
    });

    it("Send many objects from worker object without waiting for response and terminate", () => {
        var a = new Worker("./EvalWorker.js");
        for (var i = 0; i < 500; i++) {
            a.postMessage({ i: i, data: generateRandomString(100), num: 123456.22 });
        }

        a.terminate();
    });

    it("Should keep the worker alive after error", (done) => {
        var worker = new Worker("./EvalWorker.js");

        worker.postMessage({ eval: "throw new Error('just an error');" });
        worker.postMessage({ eval: "postMessage('pong');" });
        worker.onmessage = function (msg) {
            expect(msg.data).toBe("pong");
            worker.terminate();
            done();
        }
    });

    it("Should not crash if terminate() is called more than once", () => {
        var worker = new Worker("./EvalWorker.js");

        worker.postMessage({ eval: "" });
        worker.terminate();
        worker.terminate();
        worker.terminate();
    });

    it("Should not crash if close() is called more than once", () => {
        var worker = new Worker("./EvalWorker.js");

        worker.postMessage({ eval: "close(); close(); close(); close();" });
    });

//    // Test case for the issue reported in https://github.com/NativeScript/ios-runtime/issues/1137#issuecomment-496450970
    it("Should not crash on close() if native callbacks are still alive", () => {
        var worker = new Worker("./NativeCallbackWorker.js");

        worker.postMessage({ eval: "close();" });
    });

    it("Should not throw error if post message is called with native object", () => {
        var worker = new Worker("./EvalWorker.js");

        var nativeObj = global.NSObject ? new UIView() : new java.lang.Object();
        worker.postMessage(nativeObj);
        worker.terminate();
    });

    it("Should throw error if post circular object", (done) => {
        var worker = new Worker("./EvalWorker.js");

        var parent = { parent: true };
        var child = { parent: true };
        parent.child = child;
        child.parent = parent;

        expect(() => worker.postMessage({
            value: parent,
            eval: "postMessage(value)"
        })).toThrow();

        worker.terminate();
        done();
    });

    if (global.NSObject) {
        it("Should create many worker instances without throwing error", (done) => {
            var workersCount = 10;
            var messagesCount = 100;
            var allWorkersResponseCounter = 0;

            for (let id = 0; id < workersCount; id++) {
                let worker = new Worker("./EvalWorker");
                let responseCounter = 0;
                worker.onmessage = (msg) => {
                    responseCounter++;
                    if (responseCounter < messagesCount) {
                        worker.postMessage({ eval: "postMessage('pong');" });
                    }
                    else {
                        allWorkersResponseCounter += responseCounter;
                        worker.terminate();
                        if (allWorkersResponseCounter == workersCount * messagesCount) {
                            done();
                        }
                    }
                }
                worker.postMessage({ eval: "postMessage('pong');" });
            }
        });
    }

    it("Call close in onclose", (done) => {
        var worker = new Worker("./EvalWorker.js");

        worker.postMessage({
            eval: `onclose = () => {
                postMessage('closed');
                close();
            };
            close()`
        });

        var responseCounter = 0;
        worker.onmessage = (msg) => {
            expect(msg.data).toBe('closed');
            responseCounter++;
        }

        setTimeout(() => {
            expect(responseCounter).toBe(1);
            done();
        }, DEFAULT_TIMEOUT_BEFORE_ASSERT);
    });

    it("Throw error in onerror", (done) => {
        var worker = new Worker("./EvalWorker.js");

        worker.postMessage({
            eval:
                "globalThis.onerror = () => {\
                postMessage('onerror called');\
                throw new Error('error');\
            };\
            throw new Error('error');"
        });

        var onerrorCounter = 0;
        worker.onerror = (err) => {
            onerrorCounter++;
        };

        var onmessageCounter = 0;
        worker.onmessage = (msg) => {
            onmessageCounter++;
        };

        setTimeout(() => {
            expect(onerrorCounter).toBe(2);
            expect(onmessageCounter).toBe(1);
            worker.terminate();
            done();
        }, DEFAULT_TIMEOUT_BEFORE_ASSERT);
    });


    it("If error is thrown in close() should call onerror but should not execute any other tasks ", (done) => {
        var worker = new Worker("./EvalWorker.js");

        worker.postMessage({
            eval:
                "onmessage = (msg) => { postMessage(msg.data + ' pong'); };\
            onerror = (err) => { postMessage('pong'); return false; };\
            onclose = () => { throw new Error('error thrown from close()'); };\
            close();"
        });

        var onerrorCalled = false;
        worker.onerror = (err) => {
            onerrorCalled = true;
        };

        var lastReceivedMessage;
        worker.onmessage = (msg) => {
            lastReceivedMessage = msg.data;
            worker.postMessage(msg.data + " ping");
        };

        setTimeout(() => {
            expect(onerrorCalled).toBe(true);
            expect(lastReceivedMessage).toBe("pong");
            worker.terminate();
            done();
        }, DEFAULT_TIMEOUT_BEFORE_ASSERT);
    });

    it("Should not throw or crash when executing too much JS inside Worker", (done) => {
        var worker = new Worker("./WorkerStressJSTest.js");
        // Worker is not guaranteed to have finished before the check for runtimes leak, so track it manually
        expectedAliveRuntimes++;
        // the specific worker will post a message if something isn't right
        worker.onmessage = (msg) => {
            // Worker sends this message when it finishes successfully
            expectedAliveRuntimes--;
            if (msg.data !== "end") {
                worker.terminate();
                done("Exception is thrown in the web worker: " + msg);
            }
        }
        worker.onerror = (e) => {
            expectedAliveRuntimes--;
            worker.terminate();
            done("Exception is thrown in the web worker: " + e);
        }

        setTimeout(() => {
            worker.terminate();
            done();
        }, DEFAULT_TIMEOUT_BEFORE_ASSERT);
    });

    it("Test worker should close and not receive messages after close() call", (done) => {
        var worker = new Worker("./EvalWorker.js");

        worker.postMessage({
            eval: "close(); postMessage('message after close');"
        });
        worker.postMessage({
            eval: "postMessage('pong');"
        });

        var responseCounter = 0;
        worker.onmessage = (msg) => {
            expect(responseCounter).toBe(0);
            expect(msg.data).toBe("message after close");
            responseCounter++;
        }

        setTimeout(() => {
            expect(responseCounter).toBe(1);
            worker.terminate();
            done();
        }, DEFAULT_TIMEOUT_BEFORE_ASSERT);
    });

    it("Test onerror invoked for a script that has invalid syntax", (done) => {
        var worker = new Worker("./WorkerInvalidSyntax.js");

        worker.onerror = (err) => {
            worker.terminate();
            done();
        };
    });

    it("Test onerror invoked on worker scope and propagate to main's onerror when returning false", (done) => {
        var worker = new Worker("./EvalWorker.js");

        worker.postMessage({
            eval:
                "globalThis.onerror = function(err) { \
                return false; \
            }; \
            throw 42;"
        });
        worker.onerror = (err) => {
            worker.terminate();
            done();
        }
    });

    it("Test onerror invoked on worker scope and do not propagate to main's onerror when returning true", (done) => {
        var worker = new Worker("./EvalWorker.js");

        worker.postMessage({
            eval:
                "onerror = function(err) { \
                postMessage(err); \
                return true; \
            }; \
            throw 42;"
        });

        var onErrorCalled = false;
        var onMessageCalled = false;

        worker.onerror = (err) => {
            onErrorCalled = true;
        }

        worker.onmessage = (msg) => {
            onMessageCalled = true;
        }

        setTimeout(() => {
            expect(onErrorCalled).toBe(false);
            expect(onMessageCalled).toBe(true);
            worker.terminate();
            done();
        }, DEFAULT_TIMEOUT_BEFORE_ASSERT);
    });

    if (global.NSObject) { // platform is iOS
        it("no crash during or after runtime teardown on iOS", (done) => {
            // reduce number of workers on older (32-bit devices) to avoid sporadic failures due to timeout
            const numWorkers = (interop.sizeof(interop.types.id) == 4) ? 4 : 10;
            const timeout = DEFAULT_TIMEOUT_BEFORE_ASSERT * 3.5;

            let messageProducerTimeout = true;
            let iteration = 0;
            const produceMessageInLoop = () => {
                NSNotificationCenter.defaultCenter.postNotificationNameObjectUserInfo('send-to-worker', { iteration }, null);
                iteration++;
                // Prevent against rescheduling after we've been stopped
                if (messageProducerTimeout) {
                    messageProducerTimeout = setTimeout(produceMessageInLoop, 1);
                }
            };
            produceMessageInLoop();

            let onCloseEvents = 0;
            let onStartEvents = 0;
            for (let i = 0; i < numWorkers; i++) {
                const worker = new Worker("./TeardownCrashWorker.js");
                worker.onmessage = (msg) => {
                    if (msg.data === "closing") {
                        onCloseEvents++;
                    }
                    else if (msg.data === "starting") {
                        onStartEvents++;
                        worker.postMessage(i);
                    }
                }
            }

            setTimeout(() => {
                clearTimeout(messageProducerTimeout);
                // Signal we've stopped to prevent against rescheduling by an already queued timer tick
                messageProducerTimeout = null;

                expect(onStartEvents).toBeGreaterThan(0, `At least 1 worker should have started in ${timeout} ms`);
                expect(onCloseEvents).toBeGreaterThan(0, `At least 1 worker should have finished in ${timeout} ms`);
                done();
            }, timeout);
        });

        it("Check for leaked runtimes", function (done) {
            setTimeout(() => {
                const runtimesCount = TNSRuntime.runtimes().count;
                expect(runtimesCount).toBe(expectedAliveRuntimes, `Found ${runtimesCount} runtimes alive. Expected ${expectedAliveRuntimes}.`);
                done();
            }, 1000);
        });

    } // platform is iOS

    function generateRandomString(strLen) {
        var chars = "abcAbc defgDEFG 1234567890 ";
        var len = chars.length;
        var str = "";

        for (var i = 0; i < strLen; i++) {
            str += chars[getRandomInt(0, len - 1)];
        }

        return str;
    }


//     Returns a random integer between min (inclusive) and max (inclusive)
//     Using Math.round() will give you a non-uniform distribution!

    function getRandomInt(min, max) {
        return Math.floor(Math.random() * (max - min + 1)) + min;
    }
});
