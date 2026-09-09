# Multithreaded Task Scheduler

> Status: **Complete** — implementation, testing, sanitizer validation, benchmarking,
> architecture documentation, and final project polish are complete.
> `WorkStealingThreadPool`'s drain-barrier atomics (`relaxed` where safe,
> `release`/`acquire` where the standard requires it). Includes an honest
> correction of an earlier claim about what ThreadSanitizer can and can't
> catch — see `docs/architecture.md`.

A C++20 thread-pool / job-scheduling system built from first principles —
no third-party thread-pool library — to demonstrate systems programming and
concurrency fundamentals: lock-based synchronization, condition variables,
futures/promises, priority scheduling, and work-stealing.

## Build and run the tests

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DSCHEDULER_BUILD_TESTS=ON
cmake --build build
cd build && ctest --output-on-failure
```

## Build and run the benchmarks

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DSCHEDULER_BUILD_BENCHMARKS=ON
cmake --build build-release
./build-release/benchmarks/scheduler_benchmark
```

**Benchmark caveat**: the latest benchmark was collected in a Release build on
a Windows machine reporting 16 logical CPUs. These are machine-specific
measurements, not universal performance guarantees; see `docs/architecture.md`
for methodology and interpretation.

## ThreadSanitizer

```bash
./scripts/run_tsan_sweep.sh 10    # recommended: repeatable, CI-suitable
```

`tsan_suppressions.txt` contains exactly two entries, both a documented
`std::exception_ptr` false positive inside uninstrumented `libstdc++`.
Nothing in this project's own code is, or has ever been, suppressed.

## Design notes worth reading

- **Priority scheduling cannot preempt running tasks** — `docs/architecture.md`.
- **Work stealing here is mutex-based, not lock-free** — `worker.hpp`;
  a lock-free Chase-Lev deque is intentionally outside the scope of this final version.
- **The drain-barrier's memory ordering, and the limits of what
  ThreadSanitizer can verify about it** — `docs/architecture.md`'s Phase 8
  section is worth reading in full: it documents a real correction to an
  earlier claim in this project's own reasoning, including an empirical
  test that disproved it. Not every concurrency bug is a data race TSan
  can catch; some are standard-permitted reorderings with zero
  unsynchronized memory access anywhere, which is a distinct and
  important category.
- **`pending_tasks()` can overcount, deterministically reproduced** —
  `tests/test_scheduler.cpp`.
- **Benchmark results are machine-specific** — the latest Release run used 16
  logical CPUs; see `docs/architecture.md` for the full results and caveats.
- **The TSan sweep script is validated, not just trusted** — Phase 7's
  deliberate-bug experiment in `docs/architecture.md`.

## Latest validation

The current Release build passes the full GoogleTest suite:

- **30/30 tests passed**
- Basic, priority, and work-stealing example smoke tests pass
- Contention comparison completed successfully
- Google Benchmark suite completed successfully in Release mode
- Work-stealing `LightWork` at 4 workers measured **9.41M items/sec** versus
  **1.70M items/sec** for the shared-queue `ThreadPool` in the same run
- The contention comparison averaged **6.46ms** for work-stealing deques versus
  **78.21ms** for the shared queue, with **6.5x fewer contended acquisitions**

These measurements are specific to the benchmark machine and workload; they are
not presented as universal performance claims.

## Roadmap

- [x] Phase 1 — Minimal thread pool
- [x] Phase 2 — WorkQueue extraction, futures/promises, hardened shutdown
- [x] Phase 3 — Priority scheduling
- [x] Phase 4 — Work stealing
- [x] Phase 5 — GoogleTest suite, on by default (30/30 passing)
- [x] Phase 6 — Google Benchmark suite
- [x] Phase 7 — ThreadSanitizer sweep, formally validated
- [x] Phase 8 — Memory-ordering tuning
- [x] Phase 9 — Documentation, architecture, benchmark results and final polish
