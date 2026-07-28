package com.tns;

import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.Message;
import android.os.Process;
import android.util.Log;

import com.tns.bindings.ProxyGenerator;
import com.tns.system.classes.caching.impl.ClassCacheImpl;
import com.tns.system.classes.loading.ClassStorageService;
import com.tns.system.classes.loading.impl.ClassStorageServiceImpl;
import com.tns.system.classloaders.impl.ClassLoadersCollectionImpl;

import java.io.File;
import java.io.IOException;
import java.lang.ref.WeakReference;
import java.lang.reflect.Array;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.Collections;
import dalvik.annotation.optimization.CriticalNative;
import dalvik.annotation.optimization.FastNative;
import java.util.Queue;
import java.util.concurrent.Callable;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.FutureTask;
import java.util.concurrent.RunnableFuture;
import java.util.concurrent.atomic.AtomicInteger;

public class Runtime {
    private native void initNativeScript(int runtimeId, String filesPath, String nativeLibDir, boolean verboseLoggingEnabled, boolean isDebuggable, String packageName,
                                         Object[] v8Options, String callingDir, int maxLogcatObjectSize, boolean forceLog);

    private native void runModule(int runtimeId, String filePath) throws NativeScriptException;

    private native Object runScript(int runtimeId, String filePath) throws NativeScriptException;

    private native Object callJSMethodNative(int runtimeId, int javaObjectID, Class<?> claz, String methodName, int retType, boolean isConstructor, Object... packagedArgs) throws NativeScriptException;

    private native void createJSInstanceNative(int runtimeId, Object javaObject, int javaObjectID, String canonicalName);

    // Dynamic JNI lookup for @CriticalNative / @FastNative is unimplemented on
    // Android 8-10 and buggy on Android 11; it works reliably only on Android 12+
    // (API 31). For 8-11 the annotated methods are bound via RegisterNatives in
    // JNI_OnLoad; older devices fall back to the auto-bound *Legacy variants.
    private static final boolean SUPPORTS_OPTIMIZED_NATIVE = android.os.Build.VERSION.SDK_INT >= 26;

    @CriticalNative
    private static native int generateNewObjectIdCritical(int runtimeId);
    private static native int generateNewObjectIdLegacy(int runtimeId);

    private static int generateNewObjectId(int runtimeId) {
        return SUPPORTS_OPTIMIZED_NATIVE
                ? generateNewObjectIdCritical(runtimeId)
                : generateNewObjectIdLegacy(runtimeId);
    }

    @FastNative
    private native boolean notifyGcFast(int runtimeId, int[] javaObjectIds);
    private native boolean notifyGcLegacy(int runtimeId, int[] javaObjectIds);

    private boolean notifyGc(int runtimeId, int[] javaObjectIds) {
        return notifyGcLegacy(runtimeId, javaObjectIds);
    }

    private native void lock(int runtimeId);

    private native void unlock(int runtimeId);

    private native void passExceptionToJsNative(int runtimeId, Throwable ex, String message, String fullStackTrace, String jsStackTrace, boolean isDiscarded, boolean isPendingError);

    @CriticalNative
    private static native int getCurrentRuntimeIdCritical();
    private static native int getCurrentRuntimeIdLegacy();

    public static int getCurrentRuntimeId() {
        return SUPPORTS_OPTIMIZED_NATIVE ? getCurrentRuntimeIdCritical() : getCurrentRuntimeIdLegacy();
    }

    @CriticalNative
    private static native int getPointerSizeCritical();
    private static native int getPointerSizeLegacy();

    public static int getPointerSize() {
        return SUPPORTS_OPTIMIZED_NATIVE ? getPointerSizeCritical() : getPointerSizeLegacy();
    }

    @FastNative
    private static native void setManualInstrumentationModeFast(String mode);
    private static native void setManualInstrumentationModeLegacy(String mode);

    public static void SetManualInstrumentationMode(String mode) {
        if (SUPPORTS_OPTIMIZED_NATIVE) {
            setManualInstrumentationModeFast(mode);
        } else {
            setManualInstrumentationModeLegacy(mode);
        }
    }

    private static native void ResetDateTimeConfigurationCache(int runtimeId);

    void passUncaughtExceptionToJs(Throwable ex, String message, String fullStackTrace, String jsStackTrace) {
        passExceptionToJsNative(getRuntimeId(), ex, message, fullStackTrace, jsStackTrace, false, false);
    }

    void passExceptionToJS(Throwable ex, boolean isPendingError, boolean isDiscarded) {
        //String message = prefix + ex.getMessage();
        // we'd better not prefix the error with something like - Error on "main" thread for reportSupressedException
        // as it doesn't seem very useful for the users
        passExceptionToJsNative(getRuntimeId(), ex, ex.getMessage(), Runtime.getStackTraceErrorMessage(ex), Runtime.getJSStackTrace(ex), isDiscarded, isPendingError);
    }

    public static void passSuppressedExceptionToJs(Throwable ex, String methodName) {
        com.tns.Runtime runtime = com.tns.Runtime.getCurrentRuntime();
        if (runtime != null) {
            String errorMessage = "Error on \"" + Thread.currentThread().getName() + "\" thread for " + methodName + "\n";
            runtime.passExceptionToJS(ex, false , true);
        }
    }

    private boolean initialized;

    private final static String FAILED_CTOR_RESOLUTION_MSG = "Check the number and type of arguments.\n" +
            "Primitive types need to be manually wrapped in their respective Object wrappers.\n" +
            "If you are creating an instance of an inner class, make sure to always provide reference to the outer `this` as the first argument.";

    private Map<Integer, Object> strongInstances = new HashMap<>();

    private Map<Integer, WeakReference<Object>> weakInstances = new HashMap<>();

    private Map<Object, Integer> strongJavaObjectToID = new NativeScriptHashMap<Object, Integer>();

    private Map<Object, Integer> weakJavaObjectToID = new NativeScriptWeakHashMap<Object, Integer>();

    private Map<Class<?>, JavaScriptImplementation> loadedJavaScriptExtends = new HashMap<Class<?>, JavaScriptImplementation>();

    // Classes already registered via classStorageService.storeClass, so repeated
    // object registrations of the same class (e.g. thousands of java.util.Date
    // instances) skip the redundant getClass().getName() string + cache/classloader
    // puts. Class uses identity equals/hashCode, so a plain concurrent set is
    // correct and thread-safe.
    private final java.util.Set<Class<?>> storedClasses =
            java.util.Collections.newSetFromMap(new ConcurrentHashMap<Class<?>, Boolean>());

    private final java.lang.Runtime dalvikRuntime = java.lang.Runtime.getRuntime();

    private final Object keyNotFoundObject = new Object();
    private int currentObjectId = -1;

    private ExtractPolicy extractPolicy;

    private ArrayList<Constructor<?>> ctorCache = new ArrayList<Constructor<?>>();

    private Logger logger;

    private boolean isLiveSyncStarted;

    public boolean getIsLiveSyncStarted() {
        return this.isLiveSyncStarted;
    }

    public void setIsLiveSyncStarted(boolean value) {
        this.isLiveSyncStarted = value;
    }

    public Logger getLogger() {
        return this.logger;
    }

    private ThreadScheduler threadScheduler;

    private DexFactory dexFactory;

    private final ClassResolver classResolver;

    private final GcListener gcListener;

    private final static Comparator<Method> methodComparator = new Comparator<Method>() {
        public int compare(Method lhs, Method rhs) {
            return lhs.getName().compareTo(rhs.getName());
        }
    };

    private final StaticConfiguration config;
    private static StaticConfiguration staticConfiguration;

    // Config flags hoisted out of the boxed AppConfig.values[] Object[] and cached
    // as primitive fields, so every JS->Java callback dispatch avoids two enum
    // ordinal lookups + Boolean unboxes. Set once in init(); config is immutable
    // after load.
    private boolean cachedDiscardUncaughtJsExceptions;
    private boolean cachedEnableMultithreadedJavascript;

    private final DynamicConfiguration dynamicConfig;

    private final int runtimeId;

    /*
        The worker's id (0 for the main thread's runtime)
     */
    private final int workerId;

    private static AtomicInteger nextRuntimeId = new AtomicInteger(0);
    private final static ThreadLocal<Runtime> currentRuntime = new ThreadLocal<Runtime>();
    private final static Map<Integer, Runtime> runtimeCache = new ConcurrentHashMap<>();
    public static boolean nativeLibraryLoaded;

    private static final ClassStorageService classStorageService = new ClassStorageServiceImpl(ClassCacheImpl.INSTANCE, ClassLoadersCollectionImpl.INSTANCE);

    public Runtime(ClassResolver classResolver, GcListener gcListener, StaticConfiguration config, DynamicConfiguration dynamicConfig, int runtimeId, int workerId, HashMap<Integer, Object> strongInstances, HashMap<Integer, WeakReference<Object>> weakInstances, NativeScriptHashMap<Object, Integer> strongJavaObjectToId, NativeScriptWeakHashMap<Object, Integer> weakJavaObjectToId) {
        this.classResolver = classResolver;
        this.gcListener = gcListener;
        this.config = config;
        this.dynamicConfig = dynamicConfig;
        this.runtimeId = runtimeId;
        this.workerId = workerId;
        this.strongInstances = strongInstances;
        this.weakInstances = weakInstances;
        this.strongJavaObjectToID = strongJavaObjectToId;
        this.weakJavaObjectToID = weakJavaObjectToId;
    }

    public Runtime(StaticConfiguration config, DynamicConfiguration dynamicConfiguration) {
        synchronized (Runtime.currentRuntime) {
            ManualInstrumentation.Frame frame = ManualInstrumentation.start("new Runtime");
            try {
                Runtime existingRuntime = currentRuntime.get();
                if (existingRuntime != null) {
                    throw new NativeScriptException("There is an existing runtime on this thread with id=" + existingRuntime.getRuntimeId());
                }

                this.runtimeId = nextRuntimeId.getAndIncrement();

                Log.d("TNS.Runtime.ID", String.valueOf(this.runtimeId));

                this.config = config;
                this.dynamicConfig = dynamicConfiguration;
                this.threadScheduler = dynamicConfiguration.myThreadScheduler;
                this.workerId = dynamicConfiguration.workerId;

                // if multithreadedJS, make all instance maps concurrent or synchronized:
                if (config.appConfig.getEnableMultithreadedJavascript()) {
                    this.strongInstances = new ConcurrentHashMap<>();
                    this.weakInstances = new ConcurrentHashMap<>();
                    // loadedJavaScriptExtends can store null values (unsupported by ConcurrentHashMap),
                    // so use a synchronized map instead.
                    this.loadedJavaScriptExtends = Collections.synchronizedMap(new HashMap<>());
                    this.strongJavaObjectToID = Collections.synchronizedMap(new NativeScriptHashMap<>());
                    this.weakJavaObjectToID = Collections.synchronizedMap(new NativeScriptWeakHashMap<>());
                }

                classResolver = new ClassResolver(classStorageService);

                runtimeCache.put(this.runtimeId, this);

                gcListener = GcListener.getInstance(config.appConfig.getMemoryCheckInterval(), config.appConfig.getFreeMemoryRatio());
                currentRuntime.set(this);
            } finally {
                frame.close();
            }
        }
    }

    public int getRuntimeId() {
        return this.runtimeId;
    }

    public static Runtime getCurrentRuntime() {
        Runtime runtime = currentRuntime.get();

        if (runtime == null && nativeLibraryLoaded) {
            // Attempt to retrieve the runtime id from the currently
            // entered V8 isolate
            int runtimeId = getCurrentRuntimeId();
            runtime = runtimeCache.get(runtimeId);
        }

        return runtime;
    }

    public static boolean isDebuggable() {
        Runtime runtime = com.tns.Runtime.getCurrentRuntime();
        if (runtime != null) {
            return runtime.config.isDebuggable;
        } else {
            return false;
        }
    }

    private static Runtime getObjectRuntime(Object object) {
        Runtime runtime = null;

        for (Runtime r : runtimeCache.values()) {
            if (r.getJavaObjectID(object) != null) {
                runtime = r;
                break;
            }
        }

        return runtime;
    }

    /*
        Removes the error message lines to leave just the stack trace
     */
    private static String getStackTraceOnly(String content) {
        String[] lines = content.split("\n");
        while (lines.length > 0 && !lines[0].trim().startsWith("at")) {
            lines = Arrays.copyOfRange(lines, 1, lines.length);
        }
        String result = "";
        for (String line : lines) {
            result += line + "\n";
        }
        return result;
    }

    public static String getJSStackTrace(Throwable ex) {
        Throwable cause = ex;
        while (cause != null) {
            if (cause instanceof NativeScriptException) {
                return ((NativeScriptException) cause).getIncomingStackTrace();
            }
            cause = cause.getCause();
        }
        return null;
    }

    public static String getStackTraceErrorMessage(Throwable ex) {
        String content;
        java.io.PrintStream ps = null;

        try {
            java.io.ByteArrayOutputStream baos = new java.io.ByteArrayOutputStream();
            ps = new java.io.PrintStream(baos);
            ex.printStackTrace(ps);

            try {
                content = baos.toString("UTF-8");
                String jsStackTrace = Runtime.getJSStackTrace(ex);
                if (jsStackTrace != null) {
                    content = getStackTraceOnly(content);
                    content = jsStackTrace + content;
                }
            } catch (java.io.UnsupportedEncodingException e) {
                content = e.getMessage();
            }
        } finally {
            if (ps != null) {
                ps.close();
            }
        }

        return content;
    }

    public DynamicConfiguration getDynamicConfig() {
        return dynamicConfig;
    }

    @RuntimeCallable
    public static boolean getLineBreakpointsEnabled() {
        if (staticConfiguration != null && staticConfiguration.appConfig != null) {
            return staticConfiguration.appConfig.getLineBreakpointsEnabled();
        } else {
            return ((boolean) AppConfig.KnownKeys.EnableLineBreakpoins.getDefaultValue());
        }
    }

    @RuntimeCallable
    public int getMarkingModeOrdinal() {
        if (staticConfiguration != null && staticConfiguration.appConfig != null) {
            return staticConfiguration.appConfig.getMarkingMode().ordinal();
        } else {
            return ((MarkingMode) AppConfig.KnownKeys.MarkingMode.getDefaultValue()).ordinal();
        }
    }

    public static boolean isInitialized() {
        Runtime runtime = Runtime.getCurrentRuntime();
        return (runtime != null) ? runtime.isInitializedImpl() : false;
    }

    public int getWorkerId() {
        return workerId;
    }

    public Handler getHandler() {
        return this.threadScheduler.getHandler();
    }

    public void ResetDateTimeConfigurationCache() {
        Runtime runtime = getCurrentRuntime();
        if (runtime != null) {
            ResetDateTimeConfigurationCache(runtime.getRuntimeId());
        }
    }

    public void releaseNativeCounterpart(int nativeObjectId) {
        Object strongRef = strongInstances.get(nativeObjectId);
        if (strongRef != null) {
            strongInstances.remove(nativeObjectId);
            strongJavaObjectToID.remove(strongRef);
        }

        WeakReference<Object> weakRef = weakInstances.get(nativeObjectId);
        if (weakRef != null) {
            weakInstances.remove(nativeObjectId);
            weakJavaObjectToID.remove(weakRef);
        }
    }

    /*
        This method initializes the runtime and should always be called first and through the main thread
        in order to set static configuration that all following workers can use
     */
    public static Runtime initializeRuntimeWithConfiguration(StaticConfiguration config) {
        staticConfiguration = config;
        WorkThreadScheduler mainThreadScheduler = new WorkThreadScheduler(new Handler(Looper.myLooper()));
        DynamicConfiguration dynamicConfiguration = new DynamicConfiguration(0, mainThreadScheduler, null);
        Runtime runtime = initRuntime(dynamicConfiguration);
        return runtime;
    }

    /*
        Called via JNI from a native-spawned worker thread (WorkerWrapper), after
        the thread has attached to the JVM and before the worker script runs.
        Prepares the thread's Java Looper - this must happen before initRuntime so
        that the native runtime (timers, worker inbox, message loop) binds its fds
        to the looper that runWorkerLoop later pumps - and creates the worker's
        Runtime with a Looper-backed scheduler so cross-thread Java->JS calls
        (dispatchCallJSMethodNative, runScript) keep working.
        Should only be called after the static configuration has been initialized.
        Returns the created runtime's id.
     */
    @RuntimeCallable
    public static int initWorkerRuntime(int workerId, String callingJsDir) {
        Looper.prepare();

        WorkThreadScheduler workThreadScheduler = new WorkThreadScheduler(new Handler(Looper.myLooper()));
        DynamicConfiguration dynamicConfiguration = new DynamicConfiguration(workerId, workThreadScheduler, null, callingJsDir);

        if (staticConfiguration.logger.isEnabled()) {
            staticConfiguration.logger.write("Worker (id=" + workerId + ")'s Runtime is initializing!");
        }

        Runtime runtime = initRuntime(dynamicConfiguration);

        if (staticConfiguration.logger.isEnabled()) {
            staticConfiguration.logger.write("Worker (id=" + workerId + ")'s Runtime initialized!");
        }

        return runtime.getRuntimeId();
    }

    /*
        Called via JNI from the native worker thread. Blocks pumping the worker's
        looper (Java Handler messages and the native fds registered on it) until
        the looper is quit by WorkerWrapper on terminate()/close().
     */
    @RuntimeCallable
    public static void runWorkerLoop() {
        Looper.loop();
    }

    /*
        Called via JNI from the native worker thread during shutdown, before the
        worker's runtime is disposed.
     */
    @RuntimeCallable
    public static void detachWorkerRuntime(int runtimeId) {
        Runtime runtime = runtimeCache.remove(runtimeId);
        if (runtime != null) {
            GcListener.unsubscribe(runtime);

            if (runtime.logger != null && runtime.logger.isEnabled()) {
                runtime.logger.write("Worker(id=" + runtime.workerId + ", name=\"" + Thread.currentThread().getName() + "\") has terminated execution. Don't make further function calls to it.");
            }
        }
        currentRuntime.remove();
    }

    /*
        This method deals with initializing the runtime with given configuration
        Does it for both workers and for the main thread
     */
    private static Runtime initRuntime(DynamicConfiguration dynamicConfiguration) {
        Runtime runtime = new Runtime(staticConfiguration, dynamicConfiguration);
        try {
            runtime.init();
            runtime.runScript(new File(staticConfiguration.appDir, "internal/ts_helpers.js"));
        } catch (Throwable t) {
            // the constructor already registered the instance - roll back so a
            // failed bootstrap doesn't leave a stale, half-initialized runtime
            // reachable through the caches
            runtimeCache.remove(runtime.getRuntimeId());
            currentRuntime.remove();
            GcListener.unsubscribe(runtime);
            throw t;
        }

        return runtime;
    }

    private boolean isInitializedImpl() {
        return initialized;
    }

    public void init() {
        init(config.logger, config.appName, config.nativeLibDir, config.rootDir, config.appDir, config.classLoader, config.dexDir, config.dexThumb, config.appConfig, dynamicConfig.callingJsDir, config.isDebuggable);
    }

    private void init(Logger logger, String appName, String nativeLibDir, File rootDir, File appDir, ClassLoader classLoader, File dexDir, String dexThumb, AppConfig appConfig, String callingJsDir, boolean isDebuggable) throws RuntimeException {
        if (initialized) {
            throw new RuntimeException("NativeScriptApplication already initialized");
        }

        ManualInstrumentation.Frame frame = ManualInstrumentation.start("Runtime.init");
        try {
            this.logger = logger;

            // Cache the hot per-call config flags as primitives (see field decls).
            if (appConfig != null) {
                this.cachedDiscardUncaughtJsExceptions = appConfig.getDiscardUncaughtJsExceptions();
                this.cachedEnableMultithreadedJavascript = appConfig.getEnableMultithreadedJavascript();
            }

            // Only inject generated proxies into the app's PathClassLoader on the main
            // thread, so Class.forName() (used by framework components like FragmentFactory)
            // can find them.
            boolean isMainThread = this.workerId == 0;
            this.dexFactory = new DexFactory(logger, classLoader, dexDir, dexThumb, classStorageService, isMainThread);

            if (logger.isEnabled()) {
                logger.write("Initializing NativeScript JAVA");
            }

            try {
                Module.init(logger, rootDir, appDir);
            } catch (IOException ex) {
                throw new RuntimeException("Fail to initialize Require class", ex);
            }

            boolean forceConsoleLog = appConfig.getForceLog() || "timeline".equalsIgnoreCase(appConfig.getProfilingMode());

            initNativeScript(getRuntimeId(), Module.getApplicationFilesPath(), nativeLibDir, logger.isEnabled(), isDebuggable, appName, appConfig.getAsArray(),
                    callingJsDir, appConfig.getMaxLogcatObjectSize(), forceConsoleLog);

            if (logger.isEnabled()) {
                Date d = new Date();
                int pid = android.os.Process.myPid();
                File f = new File("/proc/" + pid);
                Date lastModDate = new Date(f.lastModified());
                logger.write("init time=" + (d.getTime() - lastModDate.getTime()));
            }

            GcListener.subscribe(this);

            initialized = true;
        } finally {
            frame.close();
        }
    }

    @RuntimeCallable
    public void enableVerboseLogging() {
        logger.setEnabled(true);
        ProxyGenerator.IsLogEnabled = true;
    }


    @RuntimeCallable
    public void disableVerboseLogging() {
        logger.setEnabled(false);
        ProxyGenerator.IsLogEnabled = false;
    }

    public void run() {
        ManualInstrumentation.Frame frame = ManualInstrumentation.start("Runtime.run");
        try {
            String mainModule = Module.bootstrapApp();
            runModule(new File(mainModule));
        } catch (NativeScriptException e){
            passExceptionToJS(e, false, false);
        } finally {
            frame.close();
        }
    }

    public void runModule(File jsFile) {
        try {
            String filePath = jsFile.getPath();
            runModule(getRuntimeId(), filePath);
        } catch (NativeScriptException ex) {
            passExceptionToJS(ex, false, false);
        }
    }

    public Object runScript(File jsFile) {
        try {
            return this.runScript(jsFile, true);
        } catch (NativeScriptException ex) {
            passExceptionToJS(ex, false, false);
            return null;
        }
    }

    public Object runScript(File jsFile, final boolean waitForResultOnMainThread) {
        Object result = null;
        try {
            if (jsFile.exists() && jsFile.isFile()) {
                final String filePath = jsFile.getAbsolutePath();

                boolean isWorkThread = threadScheduler.getThread().equals(Thread.currentThread());

                if (isWorkThread) {
                    result = runScript(getRuntimeId(), filePath);
                } else {
                    final Object[] arr = new Object[2];

                    Runnable r = new Runnable() {
                        @Override
                        public void run() {
                            synchronized (this) {
                                try {
                                    arr[0] = runScript(getRuntimeId(), filePath);
                                } finally {
                                    this.notify();
                                    arr[1] = Boolean.TRUE;
                                }
                            }
                        }
                    };

                    boolean success = threadScheduler.post(r);

                    if (success) {
                        synchronized (r) {
                            try {
                                if (arr[1] == null && waitForResultOnMainThread) {
                                    r.wait();
                                }
                            } catch (InterruptedException e) {
                                result = e;
                            }
                        }
                    }
                }
            }
        } catch (NativeScriptException ex) {
            passExceptionToJS(ex, false, false);
        }

        return result;
    }

    @RuntimeCallable
    private Class<?> resolveClass(String baseClassName, String fullClassName, String[] methodOverrides, String[] implementedInterfaces, boolean isInterface) throws ClassNotFoundException, IOException {
        Class<?> javaClass = classResolver.resolveClass(baseClassName, fullClassName, dexFactory, methodOverrides, implementedInterfaces, isInterface);

        return javaClass;
    }

    @RuntimeCallable
    private long getUsedMemory() {
        long usedMemory = dalvikRuntime.totalMemory() - dalvikRuntime.freeMemory();
        return usedMemory;
    }

    public void notifyGc() {
        int[] objectIds = new int[0];
        notifyGc(runtimeId, objectIds);
    }

    public void notifyGc(int[] objectIds) {
        for (int id: objectIds) {
                weakInstances.remove(id);
        }
        notifyGc(runtimeId, objectIds);
    }

    public void lock() {
        lock(runtimeId);
    }

    public void unlock() {
        unlock(runtimeId);
    }

    public static void initInstanceFromPossibleNonMainThread(final Object instance) {
        if (isNotOnMainThread()) {
            Runnable runnable = new Runnable() {
                @Override
                public void run() {
                    initInstance(instance);
                }
            };

            RunnableFuture<Void> task = new FutureTask<>(runnable, null);
            getMainThreadHandler().post(task);

            try {
                task.get(); // this will block until Runnable completes
            } catch (InterruptedException | ExecutionException e) {
                throw new RuntimeException(e);
            }

        } else {
            initInstance(instance);
        }
    }

    private static Handler getMainThreadHandler() {
        return new Handler(Looper.getMainLooper());
    }

    private static boolean isNotOnMainThread() {
        return Looper.myLooper() != Looper.getMainLooper();
    }

    public static void initInstance(Object instance) {
        ManualInstrumentation.Frame frame = ManualInstrumentation.start("Runtime.initInstance");
        try {
            Runtime runtime = Runtime.getCurrentRuntime();

            int objectId = runtime.currentObjectId;

            if (objectId != -1) {
                runtime.makeInstanceStrong(instance, objectId);
            } else {
                runtime.createJSInstance(instance);
            }
        } finally {
            frame.close();
        }
    }

    private void createJSInstance(Object instance) {
        int javaObjectID = generateNewObjectId(getRuntimeId());

        makeInstanceStrong(instance, javaObjectID);

        Class<?> clazz = instance.getClass();

        if (!loadedJavaScriptExtends.containsKey(clazz)) {
            JavaScriptImplementation jsImpl = clazz.getAnnotation(JavaScriptImplementation.class);
            if (jsImpl != null) {
                File jsFile = new File(jsImpl.javaScriptFile());
                runModule(jsFile);
            } else {
                logger.write("Couldn't find JavaScriptImplementation annotation for class " + clazz.toString());
            }
            loadedJavaScriptExtends.put(clazz, jsImpl);
        }

        String className = clazz.getName();

        createJSInstanceNative(getRuntimeId(), instance, javaObjectID, className);

        if (logger.isEnabled()) {
            logger.write("JSInstance for " + instance.getClass().toString() + " created with overrides");
        }
    }

    @RuntimeCallable
    private static String[] getTypeMetadata(String className, int index) throws ClassNotFoundException {
        Class<?> clazz = classStorageService.retrieveClass(className);

        String[] result = getTypeMetadata(clazz, index);

        return result;
    }

    private static String[] getTypeMetadata(Class<?> clazz, int index) {
        Class<?> mostOuterClass = clazz.getEnclosingClass();
        ArrayList<Class<?>> outerClasses = new ArrayList<Class<?>>();
        while (mostOuterClass != null) {
            outerClasses.add(0, mostOuterClass);
            Class<?> nextOuterClass = mostOuterClass.getEnclosingClass();
            if (nextOuterClass == null) {
                break;
            }
            mostOuterClass = nextOuterClass;
        }

        Package p = (mostOuterClass != null)
                ? mostOuterClass.getPackage()
                : clazz.getPackage();
        int packageCount = p != null ? 1 : 0;

        String pname = p != null ? p.getName() : "";

        for (int i = 0; i < pname.length(); i++) {
            if (pname.charAt(i) == '.') {
                ++packageCount;
            }
        }

        String name = clazz.getName();
        String[] parts = name.split("[\\.\\$]");

        int endIdx = parts.length;
        int len = endIdx - index;
        String[] result = new String[len];

        int endOuterTypeIdx = packageCount + outerClasses.size();

        for (int i = index; i < endIdx; i++) {
            if (i < packageCount) {
                result[i - index] = "P";
            } else {
                if (i < endOuterTypeIdx) {
                    result[i - index] = getTypeMetadata(outerClasses.get(i - packageCount));
                } else {
                    result[i - index] = getTypeMetadata(clazz);
                }
            }
        }

        return result;
    }

    private static String getTypeMetadata(Class<?> clazz) {
        StringBuilder sb = new StringBuilder();

        if (clazz.isInterface()) {
            sb.append("I ");
        } else {
            sb.append("C ");
        }

        if (Modifier.isStatic(clazz.getModifiers())) {
            sb.append("S\n");
        } else {
            sb.append("I\n");
        }

        Class<?> baseClass = clazz.getSuperclass();
        sb.append("B " + ((baseClass != null) ? baseClass.getName() : "").replace('.', '/') + "\n");

        Method[] methods = clazz.getDeclaredMethods();
        Arrays.sort(methods, methodComparator);

        for (Method m : methods) {
            int modifiers = m.getModifiers();
            if (!Modifier.isStatic(modifiers) && (Modifier.isPublic(modifiers) || Modifier.isProtected(modifiers))) {
                sb.append("M ");
                sb.append(m.getName());
                Class<?>[] params = m.getParameterTypes();
                String sig = MethodResolver.getMethodSignature(m.getReturnType(), params);
                sb.append(" ");
                sb.append(sig);
                int paramCount = params.length;
                sb.append(" ");
                sb.append(paramCount);
                sb.append("\n");
            }
        }

        Field[] fields = clazz.getDeclaredFields();
        for (Field f : fields) {
            int modifiers = f.getModifiers();
            if (!Modifier.isStatic(modifiers) && (Modifier.isPublic(modifiers) || Modifier.isProtected(modifiers))) {
                sb.append("F ");
                sb.append(f.getName());
                sb.append(" ");
                String sig = MethodResolver.getTypeSignature(f.getType());
                sb.append(sig);
                sb.append(" 0\n");
            }
        }

        String ret = sb.toString();

        return ret;
    }

    @RuntimeCallable
    private void makeInstanceStrong(Object instance, int objectId) {
        if (instance == null) {
            throw new IllegalArgumentException("instance cannot be null");
        }

        strongInstances.put(objectId, instance);
        strongJavaObjectToID.put(instance, objectId);

        Class<?> clazz = instance.getClass();
        // Only resolve the name + store the class the first time we see it; the
        // storage is keyed by class name and the classloader set is idempotent, so
        // repeated registrations of the same class are pure overhead.
        if (storedClasses.add(clazz)) {
            classStorageService.storeClass(clazz.getName(), clazz);
        }

        if (logger != null && logger.isEnabled()) {
            logger.write("MakeInstanceStrong (" + objectId + ", " + instance.getClass().toString() + ")");
        }
        // TODO: This can be done async
        this.gcListener.createPhantomReference(this, instance, objectId);
    }

    @RuntimeCallable
    private void makeInstanceStrong(int objectId) {
        try {
            Object instance = getJavaObjectByID(objectId);
            if (instance == null) return;
            makeInstanceStrong(instance, objectId);

            weakJavaObjectToID.remove(instance);
            weakInstances.remove(objectId);

        } catch (Exception e) {

        }
    }

    @RuntimeCallable
    private void makeInstanceWeak(int javaObjectID) {
        this.makeInstanceWeak(javaObjectID, true);
    }

    private void makeInstanceWeak(int javaObjectID, boolean keepAsWeak) {
        if (logger.isEnabled()) {
            logger.write("makeInstanceWeak instance " + javaObjectID + " keepAsWeak=" + keepAsWeak);
        }
        Object instance = strongInstances.get(javaObjectID);

        // object is marked as weak already,
        if (instance == null) return;

        if (keepAsWeak) {
            weakJavaObjectToID.put(instance, Integer.valueOf(javaObjectID));
            weakInstances.put(javaObjectID, new WeakReference<Object>(instance));
        }

        strongInstances.remove(javaObjectID);
        strongJavaObjectToID.remove(instance);
    }

    @RuntimeCallable
    private void makeInstanceWeak(ByteBuffer buff, int length, boolean keepAsWeak) {
        buff.position(0);
        for (int i = 0; i < length; i++) {
            int javaObjectId = buff.getInt();
            makeInstanceWeak(javaObjectId, keepAsWeak);
        }
    }

    @RuntimeCallable
    private boolean makeInstanceWeakAndCheckIfAlive(int javaObjectID) {
        if (logger.isEnabled()) {
            logger.write("makeInstanceWeakAndCheckIfAlive instance " + javaObjectID);
        }
        Object instance = strongInstances.get(javaObjectID);
        if (instance == null) {
            WeakReference<Object> ref = weakInstances.get(javaObjectID);
            if (ref == null) {
                return false;
            } else {
                instance = ref.get();
                if (instance == null) {
                    // The Java was moved from strong to weak, and then the Java instance was collected.
                    weakInstances.remove(javaObjectID);
                    weakJavaObjectToID.remove(ref);
                    return false;
                } else {
                    return true;
                }
            }
        } else {
            strongInstances.remove(javaObjectID);
            strongJavaObjectToID.remove(instance);

            weakJavaObjectToID.put(instance, javaObjectID);
            weakInstances.put(javaObjectID, new WeakReference<Object>(instance));

            return true;
        }
    }

    @RuntimeCallable
    private void checkWeakObjectAreAlive(ByteBuffer input, ByteBuffer output, int length) {
        input.position(0);
        output.position(0);
        for (int i = 0; i < length; i++) {
            int javaObjectId = input.getInt();

            WeakReference<Object> weakRef = weakInstances.get(javaObjectId);

            int isReleased;

            if (weakRef != null) {
                Object instance = weakRef.get();

                if (instance == null) {
                    isReleased = 1;
                    weakInstances.remove(javaObjectId);
                } else {
                    isReleased = 0;
                }
            } else {
                isReleased = (strongInstances.get(javaObjectId) == null) ? 1 : 0;
            }

            output.putInt(isReleased);
        }
    }

    @RuntimeCallable
    private Object getJavaObjectByID(int javaObjectID) throws Exception {
        if (logger.isEnabled()) {
            logger.write("Platform.getJavaObjectByID:" + javaObjectID);
        }

        Object instance = strongInstances.get(javaObjectID);

        if (instance == null) {
            instance = keyNotFoundObject;
        }

        if (instance == keyNotFoundObject) {
            WeakReference<Object> wr = weakInstances.get(javaObjectID);
            if (wr == null) {
                throw new NativeScriptException("No weak reference found. Attempt to use cleared object reference id=" + javaObjectID);
            }

            instance = wr.get();
            if (instance == null) {
                throw new NativeScriptException("Attempt to use cleared object reference id=" + javaObjectID);
            }
        }

        return instance;
    }

    private Integer getJavaObjectID(Object obj) {
        Integer id = strongJavaObjectToID.get(obj);
        if (id == null) {
            id = weakJavaObjectToID.get(obj);
        }

        return id;
    }

    @RuntimeCallable
    private int getOrCreateJavaObjectID(Object obj) {
        Integer result = getJavaObjectID(obj);

        if (result == null) {
            int objectId = generateNewObjectId(getRuntimeId());
            makeInstanceStrong(obj, objectId);
            result = objectId;
        }

        return result;
    }

    public static Object callJSMethodFromPossibleNonMainThread(int runtimeId, Object javaObject, String methodName, Class<?> retType, Object... args) throws NativeScriptException {
        return callJSMethodFromPossibleNonMainThread(runtimeId, javaObject, methodName, retType, false /* isConstructor */, args);
    }

    public static Object callJSMethodFromPossibleNonMainThread(int runtimeId, Object javaObject, String methodName, Class<?> retType, boolean isConstructor, Object... args) throws NativeScriptException {
        return callJSMethodFromPossibleNonMainThread(runtimeId, javaObject, methodName, retType, isConstructor, 0, args);
    }

    public static Object callJSMethodFromPossibleNonMainThread(int runtimeId, Object javaObject, String methodName, boolean isConstructor, Object... args) throws NativeScriptException {
        return callJSMethodFromPossibleNonMainThread(runtimeId, javaObject, methodName, void.class, isConstructor, 0, args);
    }

    public static Object callJSMethodFromPossibleNonMainThread(int runtimeId, final Object javaObject, final String methodName, final Class<?> retType, final boolean isConstructor, final long delay, final Object... args) throws NativeScriptException {
        if (isNotOnMainThread()) {
            Callable<Object> callable = new Callable<Object>() {
                @Override
                public Object call() {
                    return callJSMethod(runtimeId, javaObject, methodName, retType, isConstructor, delay, args);
                }
            };

            RunnableFuture<Object> task = new FutureTask<>(callable);
            getMainThreadHandler().post(task);

            try {
                return task.get(); // this will block until Runnable completes
            } catch (InterruptedException | ExecutionException e) {
                throw new RuntimeException(e);
            }

        } else {
            return callJSMethod(runtimeId, javaObject, methodName, retType, isConstructor, delay, args);
        }
    }


    // sends args in pairs (typeID, value, null) except for objects where its
    // (typeid, javaObjectID, javaJNIClassPath)
    public static Object callJSMethod(int runtimeId, Object javaObject, String methodName, Class<?> retType, Object... args) throws NativeScriptException {

        return callJSMethod(runtimeId, javaObject, methodName, retType, false /* isConstructor */, args);
    }

    public static Object callJSMethod(int runtimeId, Object javaObject, String methodName, Class<?> retType, boolean isConstructor, Object... args) throws NativeScriptException {

        return callJSMethod(runtimeId, javaObject, methodName, retType, isConstructor, 0, args);
    }

    public static Object callJSMethod(int runtimeId, Object javaObject, String methodName, boolean isConstructor, Object... args) throws NativeScriptException {
        return callJSMethod(runtimeId, javaObject, methodName, void.class, isConstructor, 0, args);
    }

    public static Object callJSMethodWithDelay(int runtimeId, Object javaObject, String methodName, Class<?> retType, long delay, Object... args) throws NativeScriptException {
        return callJSMethod(runtimeId, javaObject, methodName, retType, false /* isConstructor */, delay, args);
    }

    public static Object callJSMethod(int runtimeId, Object javaObject, String methodName, Class<?> retType, boolean isConstructor, long delay, Object... args) throws NativeScriptException {
        Runtime runtime = Runtime.runtimeCache.get(runtimeId);

        // Resolve the object's id on the found runtime once and reuse it (the common
        // path previously looked it up here for the ownership check AND again in
        // callJSMethodImpl). Re-resolve only when we switch to a different runtime.
        Integer javaObjectID = (runtime != null) ? runtime.getJavaObjectID(javaObject) : null;

        // If we didn't find a runtime by id, or the one we found doesn't own this
        // object, locate the runtime that actually created it. This happens when a
        // worker fires a JS method on an object created in the main thread or another worker.
        if (runtime == null || javaObjectID == null) {
            runtime = getObjectRuntime(javaObject);
            javaObjectID = (runtime != null) ? runtime.getJavaObjectID(javaObject) : null;
        }

        if (runtime == null) {
            runtime = Runtime.getCurrentRuntime();
            javaObjectID = (runtime != null) ? runtime.getJavaObjectID(javaObject) : null;
        }

        if (runtime == null) {
            throw new NativeScriptException("Cannot find runtime for instance=" + ((javaObject == null) ? "null" : javaObject));
        }

        return runtime.callJSMethodImpl(javaObjectID, javaObject, methodName, retType, isConstructor, delay, args);
    }

    private Object callJSMethodImpl(Integer javaObjectID, Object javaObject, String methodName, Class<?> retType, boolean isConstructor, long delay, Object... args) throws NativeScriptException {
        if (javaObjectID == null) {
            throw new NativeScriptException("Cannot find object id for instance=" + ((javaObject == null) ? "null" : javaObject));
        }

        if (logger.isEnabled()) {
            logger.write("Platform.CallJSMethod: calling js method " + methodName + " with javaObjectID " + javaObjectID + " type=" + ((javaObject != null) ? javaObject.getClass().getName() : "null"));
        }

        Object result = dispatchCallJSMethodNative(javaObjectID, javaObject.getClass(), methodName, isConstructor, delay, retType, args);

        return result;
    }

    // Packages args in format (typeID, value, null) except for objects where it is
    // (typeid, javaObjectID, canonicalName)
    // if javaObject has no javaObjecID meaning javascript object does not
    // exists for this object we assign one.
    private Object[] packageArgs(Object... args) {
        int len = (args != null) ? (args.length * 3) : 0;
        Object[] packagedArgs = new Object[len];

        if (len > 0) {
            int jsArgsIndex = 0;

            for (int i = 0; i < args.length; i++) {
                Object value = args[i];
                int typeId = TypeIDs.GetObjectTypeId(value);
                String javaClassPath = null;

                if (typeId == TypeIDs.JsObject) {
                    javaClassPath = value.getClass().getName();
                    value = getOrCreateJavaObjectID(value);
                }

                packagedArgs[jsArgsIndex++] = typeId;

                packagedArgs[jsArgsIndex++] = value;

                packagedArgs[jsArgsIndex++] = javaClassPath;
            }
        }

        return packagedArgs;
    }

    static Class<?> getClassForName(String className) {
        return classStorageService.retrieveClass(className);
    }

    @RuntimeCallable
    private String resolveConstructorSignature(Class<?> clazz, Object[] args) throws Exception {
        // Pete: cache stuff here, or in the cpp part

        if (logger.isEnabled()) {
            logger.write("resolveConstructorSignature: Resolving constructor for class " + clazz.getName());
        }

        String res = MethodResolver.resolveConstructorSignature(clazz, args);

        if (res == null) {
            throw new Exception("Failed resolving constructor for class \'" + clazz.getName() + "\' with " + (args != null ? args.length : 0) + " parameters. " + FAILED_CTOR_RESOLUTION_MSG);
        }

        return res;
    }

    @RuntimeCallable
    private String resolveMethodOverload(String className, String methodName, Object[] args) throws Exception {
        if (logger.isEnabled()) {
            logger.write("resolveMethodOverload: Resolving method " + methodName + " on class " + className);
        }

        Class<?> clazz = classStorageService.retrieveClass(className);


        String res = MethodResolver.resolveMethodOverload(clazz, methodName, args);
        if (logger.isEnabled()) {
            logger.write("resolveMethodOverload: method found :" + res);
        }
        if (res == null) {
            throw new Exception("Failed resolving method " + methodName + " on class " + className);
        }

        return res;
    }

    private Object[] extendConstructorArgs(String methodName, boolean isConstructor, Object[] args) {
        Object[] arr = null;

        if (methodName.equals("init")) {
            if (args == null) {
                arr = new Object[]
                        {isConstructor};
            } else {
                arr = new Object[args.length + 1];
                System.arraycopy(args, 0, arr, 0, args.length);
                arr[arr.length - 1] = isConstructor;
            }
        } else {
            arr = args;
        }

        return arr;
    }

    private Object dispatchCallJSMethodNative(final int javaObjectID, Class<?> claz, final String methodName, boolean isConstructor, Class<?> retType, final Object[] args) throws NativeScriptException {
        return dispatchCallJSMethodNative(javaObjectID, claz, methodName, isConstructor, 0, retType, args);
    }

    private Object dispatchCallJSMethodNative(final int javaObjectID, Class<?> claz, final String methodName, final boolean isConstructor, long delay, Class<?> retType, final Object[] args) throws NativeScriptException {
        final int returnType = TypeIDs.GetObjectTypeId(retType);
        Object ret = null;

        boolean isWorkThread = threadScheduler.getThread().equals(Thread.currentThread());

        final Object[] tmpArgs = extendConstructorArgs(methodName, isConstructor, args);
        final boolean discardUncaughtJsExceptions = this.cachedDiscardUncaughtJsExceptions;
        boolean enableMultithreadedJavascript = this.cachedEnableMultithreadedJavascript;

        if (enableMultithreadedJavascript || isWorkThread) {
            Object[] packagedArgs = packageArgs(tmpArgs);
            try {
                ret = callJSMethodNative(getRuntimeId(), javaObjectID, claz, methodName, returnType, isConstructor, packagedArgs);
            } catch (NativeScriptException e) {
                if (discardUncaughtJsExceptions) {
                    String errorMessage = "Error on \"" + Thread.currentThread().getName() + "\" thread for callJSMethodNative\n";
                    android.util.Log.w("Warning", "NativeScript discarding uncaught JS exception!");
                    passExceptionToJS(e, false, true);
                } else {
                    throw e;
                }
            }
        } else {
            final Object[] arr = new Object[2];

            final boolean isCtor = isConstructor;

            Runnable r = new Runnable() {
                @Override
                public void run() {
                    synchronized (this) {
                        try {
                            final Object[] packagedArgs = packageArgs(tmpArgs);
                            arr[0] = callJSMethodNative(getRuntimeId(), javaObjectID, claz, methodName, returnType, isCtor, packagedArgs);
                        } catch (NativeScriptException e) {
                            if (discardUncaughtJsExceptions) {
                                String errorMessage = "Error on \"" + Thread.currentThread().getName() + "\" thread for callJSMethodNative\n";
                                passExceptionToJS(e, false, true);
                                android.util.Log.w("Warning", "NativeScript discarding uncaught JS exception!");
                            } else {
                                throw e;
                            }
                        } finally {
                            this.notify();
                            arr[1] = Boolean.TRUE;
                        }
                    }
                }
            };

            if (delay > 0) {
                try {
                    Thread.sleep(delay);
                } catch (InterruptedException e) {
                }
            }

            boolean success = threadScheduler.post(r);

            if (success) {
                synchronized (r) {
                    try {
                        if (arr[1] == null) {
                            r.wait();
                        }
                    } catch (InterruptedException e) {
                        ret = e;
                    }
                }
            }

            ret = arr[0];
        }

        return ret;
    }

    @RuntimeCallable
    private static Class<?> getCachedClass(String className) {
        Class<?> clazz;

        try {
            clazz = classStorageService.retrieveClass(className);
            return clazz;
        } catch (RuntimeException e) {
            return null;
        }
    }

    @RuntimeCallable
    private Class<?> findClass(String className) throws ClassNotFoundException {
        Class<?> clazz = dexFactory.findClass(className);
        return clazz;
    }

    private void purgeAllProxies() {
        if (dexFactory == null) {
            return;
        }

        dexFactory.purgeAllProxies();
    }

    @RuntimeCallable
    private static Object createArrayHelper(String arrayClassName, int size) throws ClassNotFoundException {
        Class<?> clazz = getClassForName(arrayClassName);

        Object arr = Array.newInstance(clazz, size);

        return arr;
    }

    @RuntimeCallable
    private static boolean useGlobalRefs() {
        return true;
    }


    @Override
    protected void finalize() throws Throwable {
        GcListener.unsubscribe(this);
        super.finalize();
    }
}
