package org.nativescript;

// Marshalling micro-benchmark surface exercised from JS by
// app/src/main/assets/app/marshalling-benchmark.js. Each method/field targets a
// specific JS <-> Java marshalling path (primitives, strings, arrays, objects,
// callbacks) so the runtime's binding overhead can be measured per call.
public class Benchmarks {
    public Benchmarks() {}

    public static String Field = "Field";

    public static void voidMethod() {}

    public void voidMethod1() {}

    public String InstanceField = "Field";

    public int IntFieldInstance = 1;

    public static String passAndReturnString(String str) {
        return str;
    }

    public static int multiply(int a, int b) {
        return a + b;
    }

    public static String returnString() {
        return "Method";
    }

    public static int returnInt() {
        return 10;
    }

    public static void passString(String arg) {}

    public static boolean returnBoolean() {
        return true;
    }

    public static double returnDouble() {
        return 10.5;
    }

    public static void passInt(int arg) {}

    public static void passDouble(double arg) {}

    public static void passBoolean(boolean arg) {}

    public static int[] returnIntArray() {
        return new int[]{1, 2, 3, 4, 5};
    }

    public static void passIntArray(int[] arg) {}

    public static String[] returnStringArray() {
        return new String[]{"one", "two", "three"};
    }

    public static void passStringArray(String[] arg) {}

    public static double[] returnDoubleArray() {
        return new double[]{1.1, 2.2, 3.3};
    }

    public static void passDoubleArray(double[] arg) {}

    public static boolean[] returnBooleanArray() {
        return new boolean[]{true, false, true};
    }

    public static void passBooleanArray(boolean[] arg) {}

    public static java.util.Date returnDateObject() {
        return new java.util.Date();
    }

    public static void passDateObject(java.util.Date date) {}

    public static void invokeCallback(BenchmarkCallback callback) {
        callback.onCallback();
    }
}
