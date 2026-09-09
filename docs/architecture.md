
## Work Stealing (Phase 4)

`WorkStealingThreadPool` gives each worker its own `WorkStealingDeque`
(mutex-protected, NOT lock-free). The implementation uses mutex-protected
worker-local deques rather than a production-style lock-free Chase-Lev
deque. A lock-free Chase-Lev deque is intentionally outside the scope of
this final version.

### Architecture at a glance

```text
                    submit()
                       |
          +------------+-------------+
          |                          |
      ThreadPool              WorkStealingThreadPool
          |                          |
    shared WorkQueue          per-worker deques
          |                          |
    worker threads            worker 0 ... worker N
                                     |
                              local pop + stealing
                                     |
                              drain-barrier shutdown
```

`PriorityThreadPool` uses the same general worker-pool model with a priority-
ordered queue. Tasks submitted to the work-stealing pool are distributed across
worker-local queues, while idle workers can steal from sibling queues. Externally submitted tasks distribute round-robin across
worker queues; idle workers steal from a randomly-chosen sibling's opposite
end.

**Load balancing, measured, not asserted**: `examples/work_stealing_usage.cpp`
test 3 submits 200 tasks of 5ms each across 4 workers and checks wall-clock
time against the ideal-parallel bound (`total work / worker count`) rather
than the serial bound — confirming stealing is actually redistributing
work, not merely benefiting from a lucky round-robin split.

**Why contention can still occur**: multiple idle workers can simultaneously
target the same non-empty victim (especially when only one sibling has
remaining work) — their `try_steal()` calls serialize on that one victim's
mutex. Randomized victim selection reduces the *chance* of this by
spreading steal attempts across siblings over time, but cannot eliminate
contention that's inherent to the workload shape (e.g., only one queue
non-empty at all).

### The Shutdown Trap

Every earlier phase's shutdown correctness relied on `submit()`'s
"check-closed-then-push" being atomic, guaranteed by both steps sharing
ONE mutex. Decentralizing to N per-worker queues removes that mutex for
the common case — and with it, the free correctness guarantee. A naive
port (check an atomic `stopping_` flag, then push to a chosen queue)
reopens a real race: a `submit()` call can pass its check and then be
preempted by the OS before it actually pushes, while `shutdown()` proceeds
to notify and join every worker — permanently stranding that task.

**Fix — a drain barrier**, three pieces of state:
- `active_submitters_` (atomic counter): every `enqueue()` call increments
  this *before* checking `stopping_`, and decrements on every exit path
  (RAII guard, so this holds even if the task throws before pushing).
- `stopping_` (atomic bool): set by `shutdown()`'s first caller; makes
  `enqueue()` reject all *new* calls immediately.
- `drained_` (atomic bool): set by `shutdown()` only *after* it has spun
  (a bounded, provably-short wait — see below) until `active_submitters_`
  reaches zero. Only once `drained_` is true can a worker trust that a
  "found nothing anywhere" scan is authoritative and safe to exit on.

**On the bounded spin in `shutdown()`**: `while (active_submitters_.load()
!= 0) yield();` is deliberately NOT the "busy waiting" this project
forbids elsewhere. `enqueue()`'s critical section (one atomic increment,
one push, one atomic decrement) is extremely short, and this wait happens
exactly once, only during shutdown — not in any per-task hot path. This is
the same class of technique as a short spinlock or an RCU quiescence wait:
legitimate for a provably-brief, bounded wait, as distinct from a worker
idling indefinitely for arbitrary future work (which every worker loop
correctly avoids via the doorbell condition variable's bounded timeout).

**Verified**: 500 trials directly racing `submit()` against `shutdown()`
with no artificial delay (`examples/work_stealing_usage.cpp` test 4), run
under both a normal build and ThreadSanitizer across 35 combined TSan
runs — 0 crashes, 0 TSan reports, every trial resolved to either a
successful, eventually-resolved future, or a synchronous throw. No task
was ever silently lost.

### Contention comparison: shared WorkQueue vs. per-worker WorkStealingDeque

`WorkQueue` and `WorkStealingDeque` were instrumented with a `try_lock()`-
first pattern (`contention_count()`): only genuinely *contended* lock
acquisitions are counted, not every call. `examples/contention_comparison.cpp`
runs an identical workload (200,000 trivial tasks, 8 consumer threads,
tasks fully pre-queued before consumers start) against both designs
directly, isolating consumer-side lock contention from everything else.

**Environment caveat, found the hard way**: the sandbox this was first run
in reports `nproc == 1` — a single core. Results from that environment:

```
Shared WorkQueue:       37.80ms avg, 14 contended acquisitions avg (200,000 ops)
Work-stealing deques:   45.41ms avg,  8 contended acquisitions avg (200,000 ops)
Shared queue saw 1.8x more contended lock acquisitions -- direction correct,
but the effect size is negligible, and wall-clock time was actually WORSE
for work-stealing.
```

This is not a failure of the design — it's the correct, honest consequence
of running a multi-core-oriented algorithm on hardware with no second
core to actually exploit. Genuine lock contention requires two threads
*truly concurrently* executing `try_lock()` on different cores; on one
core, that can only happen via OS preemption mid-critical-section, which
is rare. Work stealing's randomized victim-scanning is pure added cost
(RNG calls, iterating siblings) with no parallelism payoff to offset it
when there's no second core to run a stolen task on.

**Final-run note**: the original single-core measurements are retained above as
historical context. The benchmark and contention measurements in the final
Release validation section were collected on the validation machine reporting
16 logical CPUs and are the numbers used by the final README. They remain
machine- and workload-specific rather than universal performance guarantees.

## GoogleTest Suite (Phase 5)

Every example-file smoke test from Phases 1–4 is now a formal GoogleTest
case across four files: `test_thread_pool.cpp` (`ThreadPool`),
`test_priority.cpp` (`PriorityThreadPool`, including the starvation test
formalized from the Phase 3 exercise's promise/shared_future gate
technique), `test_scheduler.cpp` (`WorkStealingThreadPool` — repurposing
this filename from the original plan, since Phase 4 postdates it),
`test_concurrency.cpp` (cross-cutting stress applied identically across
all three pool types).

30 tests total. 100% pass, both in a normal build and under ThreadSanitizer (3 consecutive full-suite TSan runs, 0 reports beyond the documented
suppression). GoogleTest is fetched via `FetchContent` at configure time —
no system package required.

### pending_tasks() overcount, made deterministic (Phase 5 follow-up)

`WorkStealingThreadPool.PendingTasksCanOvercountAcrossIndependentlyLockedQueues`
(`tests/test_scheduler.cpp`) turns the manually-traced overcount scenario
into a regression-tested guarantee, rather than leaving it as a documentation-only
note. Since `pending_tasks()`'s internal `queues_` are private (no test
hook was added to the production method), the test reconstructs the exact
same sampling pattern — sequentially locking and reading `size()` on each
of N independent `WorkStealingDeque` instances — against the same
production type, using a `std::promise`/`shared_future` gate to force a
concurrent push to land strictly between the two reads. No sleeps, no
timing hope: 20/20 isolated runs deterministic, clean under
ThreadSanitizer.

`SCHEDULER_BUILD_TESTS` now also defaults to `ON`: a fresh clone building
with plain `cmake -S . -B build && cmake --build build` gets the full test
suite with no flag required, matching the project's stated purpose of
demonstrating rigor. `-DSCHEDULER_BUILD_TESTS=OFF` remains available for a
minimal build.

## Google Benchmark Suite (Phase 6)

`benchmarks/scheduler_benchmark.cpp` compares five approaches (single-
threaded baseline, `std::async`, `ThreadPool`, `PriorityThreadPool` at
`NORMAL`, `WorkStealingThreadPool`) across two task granularities (a
near-zero-cost atomic increment, isolating pure scheduling overhead; and a
small bounded computation, showing how that overhead amortizes), plus a
dedicated single-task latency benchmark and an uneven-load scenario
(exercising exactly the case work-stealing exists for). Pool
construction/destruction is excluded from the timed region via
`state.PauseTiming()`/`ResumeTiming()`, since a real long-lived server
builds its pool once, not per unit of work measured.

Fetched via `FetchContent` like GoogleTest; requires
`-DSCHEDULER_BUILD_BENCHMARKS=ON` (default off — heavier dependency,
slower to build, and only meaningful in a Release configuration) and
`-DCMAKE_BUILD_TYPE=Release` (the benchmark CMake target warns loudly if
built any other way).

### Latest Release benchmark run

The latest benchmark was collected on the development machine used for this
validation run. The system reported **16 logical CPUs** and the benchmark was
built in **Release** mode with `-DCMAKE_BUILD_TYPE=Release`.

The benchmark completed without the earlier Debug-library warning or the
`PauseTiming()` assertion.

| Benchmark | Result |
|---|---:|
| Single-threaded, trivial task | 699.3M items/sec |
| `std::async`, trivial task | 56.6K items/sec |
| `ThreadPool`, trivial task, 1 worker | 6.52M items/sec |
| `PriorityThreadPool`, trivial task, 1 worker | 2.35M items/sec |
| `WorkStealingThreadPool`, trivial task, 1 worker | 3.87M items/sec |
| `ThreadPool`, LightWork, 4 workers | 1.70M items/sec |
| `WorkStealingThreadPool`, LightWork, 4 workers | **9.41M items/sec** |
| `ThreadPool`, uneven load, 4 workers | 85.6µs |
| `WorkStealingThreadPool`, uneven load, 4 workers | 131.7µs |

The strongest same-run throughput comparison is the 4-worker `LightWork`
case: work stealing measured about **5.5x higher reported throughput** than
the shared-queue `ThreadPool` (9.41M vs. 1.70M items/sec). This is a result for
this specific workload and machine, not a claim that work stealing is always
5.5x faster.

The uneven-load benchmark is deliberately interpreted more cautiously. At
4 workers, the work-stealing result was slower in this run; at 8 and 16 workers,
the work-stealing implementation was substantially faster for this particular
uneven workload. That variation is exactly why the benchmark suite reports
multiple worker counts rather than reducing the design to one headline number.

### Contention comparison

The dedicated contention benchmark used **200,000 trivial tasks and 8 consumer
threads** with the workload fully pre-queued before consumers started. Over
five runs:

| Metric | Shared `WorkQueue` | Work-stealing deques |
|---|---:|---:|
| Average time | 78.21ms | **6.46ms** |
| Contended acquisitions | 27,271 | **4,179** |
| Share of 200,000 operations | 13.6% | **2.1%** |

The shared queue therefore recorded about **6.5x more contended lock
acquisitions** than the work-stealing design in this workload. This directly
supports the architectural motivation for distributing work across
per-worker queues, while still remaining a workload-specific measurement.

### Benchmark interpretation

1. **Pooling is dramatically cheaper than launching one asynchronous execution
   context per task** in the measured workload: `std::async` was about two
   orders of magnitude lower in reported throughput than the pooled designs.
2. **Work stealing shows its value under parallel workloads and contention.**
   In the current Release run it substantially outperformed the shared queue
   at several worker counts, especially the 4-worker `LightWork` case.
3. **Worker-count scaling is not monotonic on every benchmark.** The results
   depend on task granularity, contention, scheduling overhead, and the number
   of available logical CPUs. A single benchmark point should therefore not be
   used as a general scaling claim.
4. **The contention result and throughput result measure different things.**
   The contention comparison isolates consumer-side lock contention, while
   Google Benchmark measures end-to-end benchmark throughput/latency. Their
   results should be discussed separately.
5. **All numbers are machine- and workload-specific.** They are useful evidence
   for this implementation and configuration, but should not be presented as
   universal characteristics of work stealing.

## ThreadSanitizer Sweep (Phase 7)

Phase 7 formalizes what had already been done ad hoc every phase since
Phase 1 into a repeatable, CI-suitable process: `scripts/run_tsan_sweep.sh`.

### What the sweep script does

Builds the full test suite under `-fsanitize=thread`, then runs the
*entire* CTest suite `N` times (default 10) with `tsan_suppressions.txt`
applied. Exit code is the source of truth — 0 only if every repetition
produced zero warnings beyond the two documented suppression entries, and
every test passed cleanly. Repetition matters because this project has
directly observed (Phase 2) a real race manifesting on only ~20% of runs;
a single clean run proves very little on its own.

Each `ctest` invocation is wrapped in `timeout 120` — a lesson learned the
hard way during the validation experiment below, not a defensive
guess. Undefined behavior doesn't reliably manifest as a clean crash; it
can just as easily corrupt container state into an infinite loop. A sweep
script that can hang forever is a CI liability of its own, so a timeout is
treated as an explicit sweep failure.

### Formal suppression-file audit

`tsan_suppressions.txt` contains exactly two entries, both scoped to
specific mangled symbols inside `std::` (the libstdc++ `exception_ptr`
false positive documented under Phase 2). Confirmed by direct inspection:
neither entry's pattern can match anything inside the `scheduler::`
namespace. No suppression in this project ever has, or currently does,
cover our own code.

**On "fix the design, don't suppress" — the alternative we explicitly
considered and rejected**: one way to eliminate the `exception_ptr` false
positive entirely would be to stop using `std::future`'s built-in
exception propagation — catch exceptions inside `packaged_task` ourselves
and hand callers a `std::string` error message through a separate channel
instead of a real rethrown exception. This was considered and rejected:
it would abandon `std::future`'s standard, expected contract (callers
reasonably expect `future.get()` to rethrow the *actual* exception type,
not a stringly-typed approximation) purely to route around a gap in one
platform's sanitizer instrumentation — a strictly worse trade than a
narrow, written-down, two-line suppression that's been independently
re-verified at every phase since it was found.

### Validating the safety net: deliberately breaking the code

An untested safety net is a hope, not a guarantee. To confirm the sweep
script actually catches a real race rather than passing trivially, a
single line was deliberately removed in a disposable scratch copy: the
`std::lock_guard` in `WorkStealingDeque::push_back()`, leaving `tasks_`
mutated with zero synchronization against concurrent `try_pop_back()` /
`try_steal()` calls.

Result: a genuine `ThreadSanitizer: heap-use-after-free` inside
`std::_Sp_counted_base::_M_release()`, immediately followed by a `SIGSEGV`
inside `std::function`'s move constructor — real memory corruption from
an unsynchronized `std::deque` mutation, not a benign report. The first
attempt at running the sweep against this broken copy **hung** rather
than crashing cleanly — the exact class of failure mode described above,
discovered in practice, not hypothesized in advance. After adding the
`timeout` wrapper, three subsequent runs against the same broken code all
failed reliably (crash or CTest failure every time, zero silent passes).
The scratch copy was discarded after the experiment; the hardened script
(with the timeout) is what ships in this repository.

### Final result

10/10 full-suite sweep runs (300 total test executions, based on the current
30-test suite) against the real, correct codebase: 0 unsuppressed warnings, 0 failures, 0 hangs. Combined
with every phase's individual verification along the way, this is the
project's complete concurrency-correctness evidence, not a new claim on
top of it.

## Memory-Ordering Tuning (Phase 8)

`WorkStealingThreadPool`'s four atomics (`next_queue_`, `active_submitters_`,
`stopping_`, `drained_`) previously used default `seq_cst` everywhere.
Each was re-examined individually rather than blanket-relaxed:

- **`next_queue_`**: `relaxed` throughout. Only used for round-robin index
  selection; atomicity (no two submitters get the same index) is all
  that's required, not ordering.
- **`stopping_`**: `relaxed` throughout. A stale read causes, at worst, a
  submitter "winning" its race against a concurrent `shutdown()` — one of
  the two outcomes already stress-tested and accepted (Phase 4, 500
  trials) — never a silently-lost task, because `active_submitters_`
  independently guarantees `shutdown()` waits for that submitter
  regardless of what `stopping_` shows it.
- **`active_submitters_`**: `relaxed` fetch_add (increment), but
  **`release`** on the decrement and **`acquire`** on the spin-wait's
  terminal load.
- **`drained_`**: **`release`** store, **`acquire`** load — the pairing
  that closes the drain barrier's happens-before chain.

### A wrong turn, corrected rather than hidden

The first-pass argument for relaxing `active_submitters_`/`drained_` too
was: "the real data lives behind `WorkStealingDeque`'s own mutex, which
independently guarantees visibility to whoever locks it next — so these
atomics only need to guarantee *timing* (that a push has already happened
in real time), and that's true from per-thread program order plus the
standard's total-modification-order guarantee on a single atomic, with no
acquire/release needed." This is the same class of reasoning behind a
well-documented real bug pattern (naive relaxed reference-count
decrements — see Herb Sutter's atomic<> talks) and it does not hold as a
*formal* C++ standard argument: "total modification order" is a
per-object coherence guarantee, not a happens-before relation, and it
does not by itself transitively order *other* threads' operations.

**This was tested empirically, not just reasoned about on paper.** A
scratch copy with `active_submitters_`/`drained_` fully relaxed was run
through the same 20-repetition TSan sweep used to validate the real
change: **0 warnings, across 600 test executions.** This is the important,
corrected finding: TSan's clean result does NOT prove the fully-relaxed
version is safe. It proves there is no *data race* — and there genuinely
isn't one, because every access to `tasks_` is unconditionally
mutex-protected regardless of these atomics' ordering. The potential bug
in the fully-relaxed version is not a memory race at all; it's whether a
worker's "safe to exit" decision could, under a standard-permitted (if
practically rare) reordering on weakly-ordered hardware, fire before a
still-in-flight push has genuinely completed — a pure event-sequencing
question that never touches an actual unsynchronized memory location, and
therefore sits entirely outside what a data-race detector like TSan is
built to catch.

**This corrects an earlier overclaim in this project's own reasoning**
(made when discussing this same acquire-load, before it was tested): that
"TSan should catch a real ordering mistake regardless of hardware, since
it models the abstract memory model via declared tags." That claim holds
for bugs where relaxing the ordering would create an actual unsynchronized
access to shared data (the Phase 7 deliberate-bug experiment is exactly
that case, and TSan caught it immediately and reliably). It does not hold
here, because there is no such access to catch — the bug class this
particular acquire/release pairing guards against is a logical, standard-
permitted reordering with no observable memory race, which is a real and
important limitation of dynamic race detectors worth stating plainly
rather than glossing over.

**The release/acquire version ships, not the relaxed one**, on the
strength of the standard's formal rules rather than any dynamic tool's
confirmation (none is available for this bug class) — and because, on
this benchmarked hardware, there is no measured performance cost to
keeping it correct (see below).

### Performance impact

On x86-64, acquire-loads and release-stores compile to the same plain
load/store instructions as relaxed ones — the TSO memory model gives them
away for free. The only real cost `seq_cst` adds over any weaker ordering
on x86 is a memory fence on stores/RMW operations specifically. This means
the correctly-scoped release/acquire version and a hypothetical
fully-relaxed version should measure identically on this hardware, while
only the change away from the original blanket `seq_cst` should show any
difference at all — there is no real performance argument for taking the
provably-riskier fully-relaxed path on this architecture. (This
differential would very likely look different on ARM, where acquire/
release are not free — another item for the "re-run on different real
hardware" list alongside Phases 4 and 6.)

**Measured, not just predicted**: `WorkStealingThreadPool` benchmarks were
run for both the original `seq_cst` version and the tuned
release/acquire/relaxed version, same hardware, same binary flags
(Release, `--benchmark_min_time=0.3s`). Every benchmark — bulk throughput
at every worker count, single-task latency, uneven-load — landed within
noise of the other version (differences under ~3%, consistent with normal
run-to-run variance rather than a real effect). This matches the
theoretical x86 prediction exactly: these atomics are touched rarely
enough (once per task for the cheap ones, once per pool lifetime for the
drain barrier's spin) that even a real fence removal wouldn't be
measurable against everything else this pool does per task. The change is
justified on correctness and standard-compliance grounds, explicitly not
on a performance claim this hardware can substantiate — an honest
distinction worth preserving rather than overselling a "we optimized it"
narrative the data doesn't support.


## Final project status

The project is considered complete. The final validation covers the GoogleTest
suite, example smoke tests, shutdown-race validation, contention comparison,
Release benchmarks, and ThreadSanitizer methodology documented above. Future
work such as a lock-free Chase-Lev deque or broader cross-platform performance
characterization can be treated as separate follow-on work rather than unfinished
requirements of this version.
