package com.tns;

import java.lang.ref.PhantomReference;
import java.lang.ref.ReferenceQueue;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.WeakHashMap;
import java.util.concurrent.ConcurrentHashMap;


class GCSubscriber {
    HashMap<PhantomReference<Object>, Integer> referencesMap = new HashMap<>();
    public final ReferenceQueue<Object> referenceQueue = new ReferenceQueue<>();
}

public class GcListener {
    private final Thread gcThread;
    private final Thread monitorThread;
    private final int monitorInterval;
    private final double freeMemoryRatio;
    private final ConcurrentHashMap<Runtime, GCSubscriber> subscribers;
    private boolean firstStart = true;
    private static volatile GcListener instance;

    public void createPhantomReference(Runtime runtime, Object object, Integer objectId) {
        GCSubscriber subscriber =  subscribers.get(runtime);
        if (subscriber != null) {
            HashMap<PhantomReference<Object>, Integer> refMap = subscriber.referencesMap;
            if (!refMap.containsValue(objectId)) {
                PhantomReference<Object> ref = new PhantomReference<>(object, subscriber.referenceQueue);
                refMap.put(ref, objectId);
            }

            if (runtime.getLogger().isEnabled()) {
                runtime.getLogger().write("GC", "Added object to watch list with id: " + String.valueOf(objectId));
            }
        }
    }

    private class GcMonitor implements Runnable {
        public GcMonitor() {
        }

        public void run() {
            while (true) {
                try {
                    if (subscribers.isEmpty()) break;
                    for (Runtime runtime : subscribers.keySet()) {
                        GCSubscriber subscriber = subscribers.get(runtime);
                        if (subscriber != null) {
                            PhantomReference<?> ref;
                            ArrayList<Integer> collectedObjectIds = new ArrayList<>();
                            while ((ref = (PhantomReference<?>) subscriber.referenceQueue.poll()) != null) {
                                Integer id = subscriber.referencesMap.get(ref);
                                subscriber.referencesMap.remove(ref);
                                collectedObjectIds.add(id);
                                if (runtime.getLogger().isEnabled()) {
                                    runtime.getLogger().write("GC", "Collected object for gc: " + String.valueOf(id));
                                }
                            }

                            if (!collectedObjectIds.isEmpty()) {
                                int[] objIds = new int[collectedObjectIds.size()];
                                for (int i =0; i < objIds.length; i++) {
                                    objIds[i] = collectedObjectIds.get(i);
                                }
                                runtime.notifyGc(objIds);
                                if (runtime.getLogger().isEnabled()) {
                                    runtime.getLogger().write("GC", "GC triggered for " + String.valueOf(objIds.length) + "objects");
                                    runtime.getLogger().write("NS.GCListener","Objects referenced after GC: " + subscriber.referencesMap.size());
                                }
                            }
                        }
                    }
                    Thread.sleep(100);
                } catch (InterruptedException e) {
                    if (com.tns.Runtime.isDebuggable()) {
                        e.printStackTrace();
                    }
                }

            }
        }
    }

    private class MemoryMonitor implements Runnable {
        private final int timeInterval;
        private final double freeMemoryRatio;
        private final java.lang.Runtime runtime;

        public MemoryMonitor(int timeInterval, double freeMemoryRatio) {
            this.timeInterval = timeInterval;
            this.freeMemoryRatio = freeMemoryRatio;
            this.runtime = java.lang.Runtime.getRuntime();
        }

        public void run() {
            while (true) {
                try {
                    long freeMemory = MemoryMonitor.this.runtime.freeMemory();
                    long totalMemory = MemoryMonitor.this.runtime.totalMemory();
                    long maxMemory = MemoryMonitor.this.runtime.maxMemory();
                    double ratio = ((double)(maxMemory - totalMemory + freeMemory)) / ((double)maxMemory);
                    if (ratio < freeMemoryRatio) {
                        GcListener.this.notifyGc();
                    }
                    Thread.sleep(timeInterval);
                } catch (InterruptedException e) {
                    if (com.tns.Runtime.isDebuggable()) {
                        e.printStackTrace();
                    }
                }
            }
        }
    }

    private GcListener(int monitorInterval, double freeMemoryRatio) {
        this.monitorInterval = monitorInterval;
        this.freeMemoryRatio = freeMemoryRatio;
        this.subscribers = new ConcurrentHashMap<>();

        gcThread = new Thread(new GcMonitor());
        gcThread.setName("NativeScript GC thread");
        gcThread.setDaemon(true);

        if (monitorInterval > 0) {
            monitorThread = new Thread(new MemoryMonitor(monitorInterval, freeMemoryRatio));
            monitorThread.setName("NativeScript monitor thread");
            monitorThread.setDaemon(true);
        } else {
            monitorThread = null;
        }
    }

    /**
     * @param monitorInterval time in milliseconds
     */
    public static GcListener getInstance(int monitorInterval, double freeMemoryRatio) {
        if (instance == null) {
            synchronized (GcListener.class) {
                if (instance == null) {
                    instance = new GcListener(monitorInterval, freeMemoryRatio);
                }
            }
        }
        return instance;
    }

    public static void subscribe(Runtime runtime) {
        synchronized (instance) {
            if (instance.firstStart) {
                instance.start();
                instance.firstStart = false;
            }
            instance.subscribers.put(runtime, new GCSubscriber());
        }
    }

    public static void unsubscribe(Runtime runtime) {
        synchronized (instance) {
            instance.subscribers.remove(runtime);
        }
    }

    private void start() {
        if (gcThread != null) {
            gcThread.start();
        }
        if (monitorThread != null) {
            monitorThread.start();
        }
    }

    private void notifyGc() {
        synchronized (instance) {
//            ManualInstrumentation.Frame frame = ManualInstrumentation.start("GcListener.notifyGc");
            try {
                for (Runtime runtime : subscribers.keySet()) {
                    runtime.notifyGc();
                }
            } finally {
//                frame.close();
            }
        }
    }
}
