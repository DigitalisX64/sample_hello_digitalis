package com.example.hellodigitalis.bench

import android.util.Log

/**
 * Timing harness shared by the benchmark samples.
 *
 * A sample keeps its existing correctness probe — the one the suite's StatusTest
 * asserts on — and adds a [run] call for each workload worth timing. Every run
 * emits one machine-readable line to logcat under the tag [TAG]:
 *
 *     BENCH {"schema":1,"module":"hello-zstd","case":"compress-1MB",...}
 *
 * `digitalis/scripts/run-benchmarks.sh` collects those lines across translation
 * modes and aggregates them. Raw per-iteration timings are included so variance
 * can be analysed on the host rather than being averaged away here.
 *
 * The workload must be self-contained and deterministic: same input, same work,
 * every iteration. Anything that allocates lazily, caches, or touches the
 * network belongs in the warmup, not the measurement.
 */
object Bench {

    const val TAG = "DigitalisBench"

    /** Emitted once a module has finished, so the runner knows to stop waiting. */
    fun done(module: String) = Log.i(TAG, "BENCH_DONE $module")

    /** Emitted if a workload throws, so a failure is not silently a missing row. */
    private fun failed(module: String, case: String, t: Throwable) =
        Log.i(TAG, "BENCH_FAIL $module $case ${t.javaClass.simpleName}: ${t.message}")

    /**
     * Times [body] and reports the result.
     *
     * @param warmup iterations run before measurement, to let the translator
     *   reach steady state — the first pass through a region is translation,
     *   not execution, and the second gear only engages once a region is hot.
     *   This is the single most important knob here: too few and the numbers
     *   measure compilation, too many and cold-start cost is hidden. The runner
     *   can sweep it.
     */
    fun run(
        module: String,
        case: String,
        warmup: Int = 5,
        iters: Int = 30,
        body: () -> Unit,
    ) {
        try {
            repeat(warmup) { body() }

            val samples = LongArray(iters)
            for (i in 0 until iters) {
                val start = System.nanoTime()
                body()
                samples[i] = System.nanoTime() - start
            }
            report(module, case, warmup, samples)
        } catch (t: Throwable) {
            failed(module, case, t)
        }
    }

    private fun report(module: String, case: String, warmup: Int, samples: LongArray) {
        val sorted = samples.sortedArray()
        val json = StringBuilder(samples.size * 12 + 256)
        json.append("{\"schema\":1")
            .append(",\"module\":\"").append(module).append('"')
            .append(",\"case\":\"").append(case).append('"')
            .append(",\"warmup\":").append(warmup)
            .append(",\"iters\":").append(samples.size)
            .append(",\"min_ns\":").append(sorted.first())
            .append(",\"p25_ns\":").append(percentile(sorted, 25))
            .append(",\"median_ns\":").append(percentile(sorted, 50))
            .append(",\"p75_ns\":").append(percentile(sorted, 75))
            .append(",\"max_ns\":").append(sorted.last())
            .append(",\"ns\":[")
        for (i in samples.indices) {
            if (i > 0) json.append(',')
            json.append(samples[i])
        }
        json.append("]}")

        // Logcat truncates very long lines; keep raw samples modest (<= ~200).
        Log.i(TAG, "BENCH $json")
    }

    /** Nearest-rank percentile of an already-sorted array. */
    private fun percentile(sorted: LongArray, p: Int): Long {
        if (sorted.isEmpty()) return 0
        val rank = Math.ceil(p / 100.0 * sorted.size).toInt().coerceIn(1, sorted.size)
        return sorted[rank - 1]
    }
}
