package com.example.helloobjectbox;

import android.os.Bundle;
import android.util.Log;
import android.widget.TextView;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import com.example.hellodigitalis.helloobjectbox.R;

import java.util.ArrayList;
import java.util.List;

import io.objectbox.Box;
import io.objectbox.BoxStore;

/**
 * Exercises ObjectBox — an on-device NoSQL object database — under Berberis
 * ARM64->x86_64 translation. MyObjectBox.builder().build() loads the arm64-v8a
 * libobjectbox-jni.so and opens a native mmap-backed store; the probe writes a
 * couple of Note objects, reads them back through the Box, and self-checks the
 * round-trip (text, num, and count), logging "OBJECTBOX OK" or "OBJECTBOX FAIL"
 * so the suite's StatusTest can assert a clean run.
 *
 * MyObjectBox and the Note_ metadata are generated at build time by the
 * ObjectBox annotation processor (wired in via the io.objectbox Gradle plugin),
 * so they are not present as source in this module. The activity is written in
 * Java so it shares the javac compilation unit with that generated code (AGP
 * 9's built-in Kotlin has no kapt, and Kotlin compiles before the Java
 * annotation processor runs, so a Kotlin activity could not see MyObjectBox).
 */
public class MainActivity extends AppCompatActivity {

    private static final String TAG = "HelloObjectBox";

    @Nullable
    private BoxStore boxStore;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        ((TextView) findViewById(R.id.sample_text)).setText(runObjectBoxProbe());
    }

    private String runObjectBoxProbe() {
        try {
            BoxStore store = MyObjectBox.builder()
                    .androidContext(getApplicationContext())
                    .build();
            boxStore = store;

            Box<Note> box = store.boxFor(Note.class);
            // Start from a clean slate so re-runs are deterministic.
            box.removeAll();

            Note n1 = new Note("héllo-objectbox-ⓦ", 1234567);
            Note n2 = new Note("second-note", -42);
            box.put(n1, n2);

            // put() assigns the auto-incremented @Id back onto each object.
            boolean assignedIds = n1.id != 0L && n2.id != 0L;

            long count = box.count();
            List<Note> all = box.getAll();
            Note r1 = null;
            Note r2 = null;
            for (Note n : all) {
                if (n.id == n1.id) r1 = n;
                if (n.id == n2.id) r2 = n;
            }

            List<String> failed = new ArrayList<>();
            if (count != 2L) failed.add("count");
            if (!assignedIds) failed.add("ids");
            if (r1 == null || !"héllo-objectbox-ⓦ".equals(r1.text)) failed.add("note1.text");
            if (r1 == null || r1.num != 1234567) failed.add("note1.num");
            if (r2 == null || !"second-note".equals(r2.text)) failed.add("note2.text");
            if (r2 == null || r2.num != -42) failed.add("note2.num");

            String msg;
            if (failed.isEmpty()) {
                msg = "OBJECTBOX OK (v" + BoxStore.getVersion() + ", count=" + count
                        + ", ids=" + n1.id + "," + n2.id + ")";
            } else {
                msg = "OBJECTBOX FAIL: round-trip mismatch for " + String.join(",", failed);
            }
            Log.i(TAG, msg);
            return msg;
        } catch (Throwable t) {
            String msg = "OBJECTBOX FAIL: " + t.getClass().getSimpleName() + ": " + t.getMessage();
            Log.e(TAG, msg, t);
            return msg;
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (boxStore != null) {
            boxStore.close();
            boxStore = null;
        }
    }
}
