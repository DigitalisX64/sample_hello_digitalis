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

    /**
     * Warm-up runs for at least the declared iteration count and then keeps
     * going until this much wall clock has passed.
     *
     * A fixed iteration count cannot serve both a 30us workload and a 2.4s one.
     * Five iterations left the heavy tier still compiling inside the measured
     * window on secp256k1 — its gear-up and compile landed in the samples,
     * which read as a 90% relative IQR and a median worse than the lite tier,
     * making the second gear look like a regression when it is in fact 1.8x
     * faster there. The same five iterations are already many seconds for
     * bcrypt. Budgeting by time costs a slow workload nothing (its first
     * iterations exceed the budget on their own) and buys a fast one the
     * hundreds of passes the second gear needs to engage and settle.
     */
    private const val WARMUP_BUDGET_NS = 400_000_000L

    /** Backstop so a microsecond-scale workload cannot spin here forever. */
    private const val WARMUP_MAX_ITERS = 2000

    /** Emitted once a module has finished, so the runner knows to stop waiting. */
    fun done(module: String) = Log.i(TAG, "BENCH_DONE $module")

    /** Emitted if a workload throws, so a failure is not silently a missing row. */
    private fun failed(module: String, case: String, t: Throwable) =
        Log.i(TAG, "BENCH_FAIL $module $case ${t.javaClass.simpleName}: ${t.message}")

    /**
     * Times [body] and reports the result.
     *
     * @param warmup *minimum* iterations run before measurement. Warm-up then
     *   continues until [WARMUP_BUDGET_NS] has elapsed, so this is a floor
     *   rather than the whole story: the first pass through a region is
     *   translation, not execution, and the second gear only engages once a
     *   region is hot and then has to compile it. The reported `warmup` field
     *   is the count actually run, not this value.
     */
    fun run(
        module: String,
        case: String,
        warmup: Int = 5,
        iters: Int = 30,
        body: () -> Unit,
    ) {
        try {
            var warmed = 0
            val warmupStart = System.nanoTime()
            while (warmed < warmup ||
                (System.nanoTime() - warmupStart < WARMUP_BUDGET_NS &&
                    warmed < WARMUP_MAX_ITERS)
            ) {
                body()
                warmed++
            }

            val samples = LongArray(iters)
            for (i in 0 until iters) {
                val start = System.nanoTime()
                body()
                samples[i] = System.nanoTime() - start
            }
            report(module, case, warmed, samples)
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
