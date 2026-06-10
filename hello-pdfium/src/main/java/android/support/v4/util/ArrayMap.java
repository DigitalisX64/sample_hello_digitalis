package android.support.v4.util;

/**
 * Compatibility shim for the legacy support-library class that the prebuilt
 * com.github.barteksc:pdfium-android AAR references at runtime
 * (android.support.v4.util.ArrayMap). It has no upstream Berberis counterpart;
 * it exists only so this sample links against the same class Jetifier used to
 * rewrite automatically. AGP 9 removed Jetifier, so the support reference in
 * the prebuilt bytecode now resolves to this thin subclass of the AndroidX
 * ArrayMap, which has an identical method surface. This is a packaging shim for
 * a legacy third-party library, not a workaround for any translator behavior.
 */
public class ArrayMap<K, V> extends androidx.collection.ArrayMap<K, V> {
    public ArrayMap() {
        super();
    }

    public ArrayMap(int capacity) {
        super(capacity);
    }
}
