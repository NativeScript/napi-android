package org.nativescript.runtime.napi;

import android.os.Handler;
import android.os.Looper;

import androidx.annotation.Keep;

import org.nativescript.runtime.napi.system.classes.caching.impl.ClassCacheImpl;
import org.nativescript.runtime.napi.system.classes.loading.ClassStorageService;
import org.nativescript.runtime.napi.system.classes.loading.impl.ClassStorageServiceImpl;

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
import java.util.Queue;
import java.util.concurrent.Callable;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.FutureTask;
import java.util.concurrent.RunnableFuture;
import java.util.concurrent.atomic.AtomicInteger;

@Keep
public class Runtime {
    private native Object callJSMethodNative(int runtimeId, int javaObjectID, String methodName, int retType, boolean isConstructor, Object... packagedArgs) throws NativeScriptException;

    private native void createJSInstanceNative(int runtimeId, Object javaObject, int javaObjectID, String canonicalName);

    private native int generateNewObjectId(int runtimeId);

    private native boolean notifyGc(int runtimeId);

    private native void lock(int runtimeId);

    private native void unlock(int runtimeId);

    private native void passExceptionToJsNative(int runtimeId, Throwable ex, String message, String fullStackTrace, String jsStackTrace, boolean isDiscarded);

    private static native int getCurrentRuntimeId();

    public static native int getPointerSize();

    private final Object keyNotFoundObject = new Object();
    private int currentObjectId = -1;

    private HashMap<Integer, Object> strongInstances = new HashMap<>();

    private HashMap<Integer, WeakReference<Object>> weakInstances = new HashMap<>();

    private NativeScriptHashMap<Object, Integer> strongJavaObjectToID = new NativeScriptHashMap<Object, Integer>();

    private NativeScriptWeakHashMap<Object, Integer> weakJavaObjectToID = new NativeScriptWeakHashMap<Object, Integer>();

    private final Map<Class<?>, JavaScriptImplementation> loadedJavaScriptExtends = new HashMap<Class<?>, JavaScriptImplementation>();

    public native void startNAPIRuntime(String filesPath, int runtimeId);

    private final java.lang.Runtime dalvikRuntime = java.lang.Runtime.getRuntime();

    private ArrayList<Constructor<?>> ctorCache = new ArrayList<Constructor<?>>();

    private static final ClassStorageService classStorageService = new ClassStorageServiceImpl(ClassCacheImpl.INSTANCE, org.nativescript.runtime.napi.system.classloaders.impl.ClassLoadersCollectionImpl.INSTANCE);

    public Logger logger;

    public Logger getLogger() {
        return this.logger;
    }

    private DexFactory dexFactory;

    private final ClassResolver classResolver;

    private ThreadScheduler threadScheduler;

    private final GcListener gcListener;

    private final int runtimeId;

    private static AtomicInteger nextRuntimeId = new AtomicInteger(0);
    private final static ThreadLocal<Runtime> currentRuntime = new ThreadLocal<Runtime>();
    private final static Map<Integer, Runtime> runtimeCache = new ConcurrentHashMap<>();
    public static boolean nativeLibraryLoaded;

    public void startRuntimeBridge(String filesPath) {
        this.startNAPIRuntime(filesPath, this.runtimeId);
    }


    public Runtime(ClassResolver classResolver, GcListener gcListener, int runtimeId, int workerId, HashMap<Integer, Object> strongInstances, HashMap<Integer, WeakReference<Object>> weakInstances, NativeScriptHashMap<Object, Integer> strongJavaObjectToId, NativeScriptWeakHashMap<Object, Integer> weakJavaObjectToId) {
        this.classResolver = classResolver;
        this.gcListener = gcListener;
        this.runtimeId = runtimeId;
        this.strongInstances = strongInstances;
        this.weakInstances = weakInstances;
        this.strongJavaObjectToID = strongJavaObjectToId;
        this.weakJavaObjectToID = weakJavaObjectToId;
    }

    public Runtime() {
        synchronized (Runtime.currentRuntime) {
            try {
                Runtime existingRuntime = currentRuntime.get();
                if (existingRuntime != null) {
                    throw new NativeScriptException("There is an existing runtime on this thread with id=" + existingRuntime.getRuntimeId());
                }

                this.runtimeId = nextRuntimeId.getAndIncrement();

                classResolver = new ClassResolver(classStorageService);
                currentRuntime.set(this);
                this.threadScheduler = new WorkThreadScheduler(new Handler(Looper.myLooper()));

                runtimeCache.put(this.runtimeId, this);
                gcListener = null;

//                gcListener = GcListener.getInstance(config.appConfig.getGcThrottleTime(), config.appConfig.getMemoryCheckInterval(), config.appConfig.getFreeMemoryRatio());
            } finally {
//                frame.close();
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

    private Integer getJavaObjectID(Object obj) {
        Integer id = strongJavaObjectToID.get(obj);
        if (id == null) {
            id = weakJavaObjectToID.get(obj);
        }

        return id;
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

    public static boolean isDebuggable() {
        return true;
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

    public static void initInstance(Object instance) {
        try {
            Runtime runtime = Runtime.getCurrentRuntime();

            int objectId = runtime.currentObjectId;

            if (objectId != -1) {
                runtime.makeInstanceStrong(instance, objectId);
            } else {
                runtime.createJSInstance(instance);
            }
        } finally {
        }
    }

    private static Handler getMainThreadHandler() {
        return new Handler(Looper.getMainLooper());
    }

    private static boolean isNotOnMainThread() {
        return Looper.myLooper() != Looper.getMainLooper();
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

    private boolean isInitializedImpl() {
        return true;
    }


    public static boolean isInitialized() {
        Runtime runtime = Runtime.getCurrentRuntime();
        return (runtime != null) ? runtime.isInitializedImpl() : false;
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
        notifyGc(runtimeId);
    }

    public void lock() {
        lock(runtimeId);
    }

    public void unlock() {
        unlock(runtimeId);
    }

    private void createJSInstance(Object instance) {
        int javaObjectID = generateNewObjectId(getRuntimeId());

        makeInstanceStrong(instance, javaObjectID);

        Class<?> clazz = instance.getClass();

        if (!loadedJavaScriptExtends.containsKey(clazz)) {
            JavaScriptImplementation jsImpl = clazz.getAnnotation(JavaScriptImplementation.class);
            if (jsImpl != null) {
                File jsFile = new File(jsImpl.javaScriptFile());
//                runModule(jsFile);
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

    private final static Comparator<Method> methodComparator = new Comparator<Method>() {
        public int compare(Method lhs, Method rhs) {
            return lhs.getName().compareTo(rhs.getName());
        }
    };

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

        int key = objectId;
        strongInstances.put(key, instance);
        strongJavaObjectToID.put(instance, key);

        Class<?> clazz = instance.getClass();
        classStorageService.storeClass(clazz.getName(), clazz);

        if (logger != null && logger.isEnabled()) {
            logger.write("MakeInstanceStrong (" + key + ", " + instance.getClass().toString() + ")");
        }
    }

    private void makeInstanceWeak(int javaObjectID, boolean keepAsWeak) {
        if (logger.isEnabled()) {
            logger.write("makeInstanceWeak instance " + javaObjectID + " keepAsWeak=" + keepAsWeak);
        }
        Object instance = strongInstances.get(javaObjectID);

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

        // Log.d(DEFAULT_LOG_TAG,
        // "Platform.getJavaObjectByID found strong object with id:" +
        // javaObjectID);
        return instance;
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

    public static Object callJSMethodFromPossibleNonMainThread(Object javaObject, String methodName, Class<?> retType, Object... args) throws NativeScriptException {
        return callJSMethodFromPossibleNonMainThread(javaObject, methodName, retType, false /* isConstructor */, args);
    }

    public static Object callJSMethodFromPossibleNonMainThread(Object javaObject, String methodName, Class<?> retType, boolean isConstructor, Object... args) throws NativeScriptException {
        return callJSMethodFromPossibleNonMainThread(javaObject, methodName, retType, isConstructor, 0, args);
    }

    public static Object callJSMethodFromPossibleNonMainThread(Object javaObject, String methodName, boolean isConstructor, Object... args) throws NativeScriptException {
        return callJSMethodFromPossibleNonMainThread(javaObject, methodName, void.class, isConstructor, 0, args);
    }

    public static Object callJSMethodFromPossibleNonMainThread(final Object javaObject, final String methodName, final Class<?> retType, final boolean isConstructor, final long delay, final Object... args) throws NativeScriptException {
//        if (isNotOnMainThread()) {
//            Callable<Object> callable = new Callable<Object>() {
//                @Override
//                public Object call() {
//                    return callJSMethod(javaObject, methodName, retType, isConstructor, delay, args);
//                }
//            };
//
//            RunnableFuture<Object> task = new FutureTask<>(callable);
//            getMainThreadHandler().post(task);
//
//            try {
//                return task.get(); // this will block until Runnable completes
//            } catch (InterruptedException | ExecutionException e) {
//                throw new RuntimeException(e);
//            }
//
//        } else {
//            return callJSMethod(javaObject, methodName, retType, isConstructor, delay, args);
//        }

        return new Object();
    }

    // sends args in pairs (typeID, value, null) except for objects where its
    // (typeid, javaObjectID, javaJNIClassPath)
    public static Object callJSMethod(Object javaObject, String methodName, Class<?> retType, Object... args) throws NativeScriptException {
        return callJSMethod(javaObject, methodName, retType, false /* isConstructor */, args);
    }

    public static Object callJSMethod(Object javaObject, String methodName, Class<?> retType, boolean isConstructor, Object... args) throws NativeScriptException {
        return callJSMethod(javaObject, methodName, retType, isConstructor, 0, args);
    }

    public static Object callJSMethod(Object javaObject, String methodName, boolean isConstructor, Object... args) throws NativeScriptException {
        return callJSMethod(javaObject, methodName, void.class, isConstructor, 0, args);
    }

    public static Object callJSMethodWithDelay(Object javaObject, String methodName, Class<?> retType, long delay, Object... args) throws NativeScriptException {
        return callJSMethod(javaObject, methodName, retType, false /* isConstructor */, delay, args);
    }

    public static Object callJSMethod(Object javaObject, String methodName, Class<?> retType, boolean isConstructor, long delay, Object... args) throws NativeScriptException {
        Runtime runtime = Runtime.getCurrentRuntime();

        if (runtime == null) {
            runtime = getObjectRuntime(javaObject);
        }

        if (runtime == null) {
            throw new NativeScriptException("Cannot find runtime for instance=" + ((javaObject == null) ? "null" : javaObject));
        }

        return runtime.callJSMethodImpl(javaObject, methodName, retType, isConstructor, delay, args);
    }

    private Object callJSMethodImpl(Object javaObject, String methodName, Class<?> retType, boolean isConstructor, long delay, Object... args) throws NativeScriptException {
        Integer javaObjectID = getJavaObjectID(javaObject);
        if (javaObjectID == null) {
            throw new NativeScriptException("Cannot find object id for instance=" + ((javaObject == null) ? "null" : javaObject));
        }

        if (logger.isEnabled()) {
            logger.write("Platform.CallJSMethod: calling js method " + methodName + " with javaObjectID " + javaObjectID + " type=" + ((javaObject != null) ? javaObject.getClass().getName() : "null"));
        }

        Object result = dispatchCallJSMethodNative(javaObjectID, methodName, isConstructor, delay, retType, args);

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
            throw new Exception("Failed resolving constructor for class \'" + clazz.getName() + "\' with " + (args != null ? args.length : 0) + " parameters. ");
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

    private Object dispatchCallJSMethodNative(final int javaObjectID, final String methodName, boolean isConstructor, Class<?> retType, final Object[] args) throws NativeScriptException {
        return dispatchCallJSMethodNative(javaObjectID, methodName, isConstructor, 0, retType, args);
    }

    private Object dispatchCallJSMethodNative(final int javaObjectID, final String methodName, boolean isConstructor, long delay, Class<?> retType, final Object[] args) throws NativeScriptException {
        final int returnType = TypeIDs.GetObjectTypeId(retType);
        Object ret = null;

        boolean isWorkThread =  threadScheduler.getThread().equals(Thread.currentThread());

        final Object[] tmpArgs = extendConstructorArgs(methodName, isConstructor, args);
        final boolean discardUncaughtJsExceptions = false;
        boolean enableMultithreadedJavascript = false;

        if (enableMultithreadedJavascript || isWorkThread) {
            Object[] packagedArgs = packageArgs(tmpArgs);
            try {
                ret = callJSMethodNative(getRuntimeId(), javaObjectID, methodName, returnType, isConstructor, packagedArgs);
            } catch (NativeScriptException e) {
                if (discardUncaughtJsExceptions) {
                    String errorMessage = "Error on \"" + Thread.currentThread().getName() + "\" thread for callJSMethodNative\n";
                    android.util.Log.w("Warning", "NativeScript discarding uncaught JS exception!");
//                    passDiscardedExceptionToJs(e, errorMessage);
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
                            arr[0] = callJSMethodNative(getRuntimeId(), javaObjectID, methodName, returnType, isCtor, packagedArgs);
                        } catch (NativeScriptException e) {
                            if (discardUncaughtJsExceptions) {
                                String errorMessage = "Error on \"" + Thread.currentThread().getName() + "\" thread for callJSMethodNative\n";
//                                passDiscardedExceptionToJs(e, errorMessage);
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
    public int getMarkingModeOrdinal() {
//        if (staticConfiguration != null && staticConfiguration.appConfig != null) {
//            return staticConfiguration.appConfig.getMarkingMode().ordinal();
//        } else {
//            return ((MarkingMode) AppConfig.KnownKeys.MarkingMode.getDefaultValue()).ordinal();
//        }
        return 0;
    }

    @RuntimeCallable
    public static boolean getLineBreakpointsEnabled() {
//        if (staticConfiguration != null && staticConfiguration.appConfig != null) {
//            return staticConfiguration.appConfig.getLineBreakpointsEnabled();
//        } else {
//            return ((boolean) AppConfig.KnownKeys.EnableLineBreakpoins.getDefaultValue());
//        }

        return true;
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
        int JELLY_BEAN = 16;
        boolean useGlobalRefs = android.os.Build.VERSION.SDK_INT >= JELLY_BEAN;
        return useGlobalRefs;
    }


    @RuntimeCallable
    public void enableVerboseLogging() {

        // logger.setEnabled(true);
        // ProxyGenerator.IsLogEnabled = true;
    }


    @RuntimeCallable
    public void disableVerboseLogging() {
        // logger.setEnabled(false);
        // ProxyGenerator.IsLogEnabled = false;
    }

}
