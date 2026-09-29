# OrcaBench High Level Design

## Why it exists

OrcaBench measures the slicing pipeline. A run records how long each step took, the memory and CPU
it used and how much work it produced, in a document that two builds can be compared through, so a
change that makes slicing slower shows up along with the step that got slower.

## Where it lives

`benchmarks/` builds `orcabench_core`, a static library that never links libslic3r, and the
`orca_bench` command line on top of it, so both and the tests in `tests/orcabench` build without
the slicer. It is built when `ORCA_BENCHMARKS` is on, which defaults to `BUILD_TESTS` in a new build
directory and which the build scripts turn on with the tests, so their test builds compile it and
run its suite. Turning the option off removes it.

## Step timestamps

`PrintStateBase::StateWithTimeStamp` holds `started_at` and `done_at`, `steady_clock` times that
`set_started()` and `set_done()` record under the print's state mutex, next to the state they
already write. `set_started()` clears `done_at`, so a span never mixes two runs. Invalidating a
step leaves both alone, which means they describe the step's last run, and that can predate the
current pass because a step only re-runs after being invalidated. A reader dates them against a
time it took before the pass.

## Platform queries

`benchmarks/core/Host.cpp` is the only file in `benchmarks/` with platform `#ifdef`s. The process
queries (resident memory, its peak, CPU time) answer 0 when the platform call fails. The machine
queries (host name, OS, CPU model, logical cores) fall back to something coarser, such as the
architecture in place of a CPU model, and to `"unknown"` only when nothing coarser exists.

## The result model

`Result.hpp` describes a run: the suite's start time and duration, what the run asked for, the
build and the machine, and one entry per workload. A workload ran, was skipped because a fixture
was missing, or failed because it threw, and the last two have a reason. Neither counts as a
pass, and the outcome counts are computed from the list whenever asked, never stored, so no total
can disagree with it.

A field is typed when every result has it, such as a span's stage and times or an iteration's
wall time and CPU time. Peak memory, the work stats and the output hash are optional, since a run
may not collect them, and are then absent, never zero. A measurement only some producers record
is stored in a `Metrics` map (name to number) on the span, the iteration or the work stats, keyed
by a constant declared beside its producer, and a missing key means it was not collected. Build
and machine identity have a `Properties` map (name to text) for the same purpose.

Measurement identity, what the run asked for, is a map compared whole, so two results are
comparable exactly when it is equal. Build and machine identity may differ between comparable
results, since comparing compilers or machines is one of the uses.

## Policies

A policy sets how many threads slice, how many untimed warmup passes run before the timed
iterations, which stages are timed, what is collected, and whether only the workloads marked for
profile training run. Four presets cover the framework's uses.

| Preset    | Threads  | Warmup | Iterations | Collects              | Stages                |
| --------- | -------- | -----: | ---------: | --------------------- | --------------------- |
| `quick`   | hardware |      1 |          3 | wall, rss, hash, work | process, export       |
| `precise` | 1        |      2 |         10 | wall, rss, hash, work | process, export       |
| `verify`  | hardware |      0 |          1 | hash                  | process, export       |
| `pgo`     | hardware |      0 |          1 | none                  | load, process, export |

`quick` is the edit loop. `precise` runs one thread, where the slicing itself takes the largest
share of the wall time and runs vary least, and its numbers are therefore never comparable with
`quick`'s. `verify` exists for the output hash. `pgo` runs each workload exactly once for profile
coverage, and fixes its warmup and iteration counts.

`PolicyOverrides` replaces a preset's values field by field, so a run can be `precise` with four
threads. `Policy::resolve()` applies them, turns 0 threads into the hardware count, and refuses an
unknown preset, more threads than the hardware runs, a run without iterations or stages, and a
warmup or iteration count on a preset that fixes them. Only export writes G-code, and the hash and
the work stats both come from it, so a run whose stages leave export out drops both from what it
collects, and one left collecting nothing, which is `verify` without export, is refused.

`Policy::identity()` is the only writer of the measurement identity. It names every field of
`Policy` in a structured binding, so a new field does not compile until it is recorded or
explicitly left out. Threads are recorded as the resolved count, and an empty set as `none`. The
corpus and the affinity are recorded as `embedded,handy` and `none`, the only values they have so
far, so the results taken now stay comparable once either can vary.

## The extension seam

A workload kind is code and a catalog entry is data, so one kind runs many benchmarks. A
`CatalogEntry` names a benchmark permanently, since results are keyed by its name, and holds its
kind, its fixture, its tier, whether PGO training may run it, its tags, and the config the kind
applies over its defaults. PGO training may run a macro entry and not a micro one unless the entry
says otherwise. A name has to be printable ASCII without spaces, so a listing holds one name per
line, and building a workload refuses any other.

`Workload` is what a kind implements. `setup()` runs once and returns a reason when the workload
has to be skipped, such as a missing fixture. `prepare()` builds fresh state before each
iteration, so no iteration inherits another's, and `execute()` runs one timed iteration. An
exception from any of them fails the workload. `RunContext` tells a workload which stages to
time, and whatever a timed stage needs runs untimed in `prepare()`.

A workload reports through `Measurement`. It takes spans with their stage, `Scope` (the whole
print or one object) and times, each with its own metrics, then metrics of the whole iteration,
and the work stats and the output hash once each. The tests' fake writes through the same type,
so nothing between a workload and its result depends on slicing. A span that ends before it
starts, a number that is not finite and a metric reported twice are refused where they are
reported, so one bad value fails only its own workload and never reaches the document writer,
which would refuse the whole result.

`WorkloadKinds` maps kind names to the factories that build a workload from its entry. A kind
adds itself from its own file through a `WorkloadKindRegistrar`, and the kinds compile into
`orca_bench` itself, since a static library's linker drops registrars that nothing references.
The process-wide registry is a function-local static, so a registrar reaches it during static
initialization in any order, and tests give a registrar their own registry. An exception cannot
leave a static initializer without ending the process, so a registrar keeps a failed registration,
such as a kind added twice, for `require_registered()`, which `orca_bench` calls before anything
else.

`orca_bench` reads its arguments with a parser that shares one table of flags with the usage
text, prints the catalog's workloads with `--list`, and prints the usage for `--help`, even beside
`--list`. An argument it does not know exits 2 with the usage, and any other error exits 1
without it.

## The Runner

`run_suite()` in `Runner.cpp` takes the catalog entries a run selected, the resolved policy, the
registry and a `RunEnvironment`, and returns the `Result`. The environment is what a run sets up
around all its workloads, such as the thread cap, the locale, `resources_dir()`, the temporary
directory and logging. Those are libslic3r and TBB calls, so core only declares the interface and
the code that links libslic3r implements it. The Runner enters it once before the first workload
and leaves it after the last, and only an environment that fails to enter ends the run.

Each workload is built, set up once, then prepared and executed for every warmup and timed pass.
Under `pgo`, an entry that may not train is left out of the run, the same as one the selection
never named. Nothing a workload throws leaves the Runner. The workload fails with a reason that
names the call and the pass, such as `execute() threw on timed pass 2: bad allocation`, and the
next one runs. An empty reason to skip fails the workload as well, since the document cannot hold
a skip without a reason.

Only `execute()` is timed, with its wall time and the process's CPU time taken around it. The
Runner builds each pass's `Measurement` with the time `execute()` starts, and a span that starts
before it, or ends after it is reported, is refused. A workload that reuses state from an earlier
pass therefore fails, and every recorded span lies inside its iteration.

Every pass, warmups included, must reproduce the first pass's output hash and work stats, since a
result holds one of each, and a pass that differs fails the workload. A result keeps the hash and
the work stats only when the policy collects them, and a timed pass becomes an iteration only when
wall time is collected, so `verify` records its hash and no iterations. The Runner does not record
peak memory, since the process's high-water mark would give every later iteration an earlier
workload's maximum.

## Build identity

`BuildId.cpp` reports the commit and whether the working copy was dirty, read from
`git_commit_hash.h`, which `cmake/modules/GitCommitHash.cmake` rewrites at the start of every build
for the slicer and the benchmarks alike. Dirty means a tracked file changed or, since `orca_bench`
compiles its kinds by glob, a kind exists that git does not track yet. The compiler, its version,
the configuration and the flags are read from a header CMake generates per configuration. The
flags are `CMAKE_CXX_FLAGS`, the configuration's own, and the `add_compile_options` the directory
inherits, so a cache option such as `SLIC3R_ASAN` shows up in them. `BuildId.cpp` is the only file
in `benchmarks/` that includes either generated header, so a new commit recompiles one file there.

## The result document

`Document.cpp` writes a result as JSON and reads it back, and is the only file that includes
nlohmann/json. The document's `schema` is `major.minor`. A minor version adds fields, which a
reader of the same major ignores when it does not know them. A major version changes or removes
fields, and a reader refuses any major but its own. Entries in the open maps (`measurement`,
`properties`, `metrics`) and stage names are kept whatever they are.

- Durations and CPU time are nanoseconds and memory is bytes, so a document reads back exactly and
  whatever displays it does the rounding.
- A timeline is written as offsets from its earliest start, since `steady_clock` readings mean
  nothing outside the process, and read back onto the epoch.
- `output_hash` is 16 hex digits, because jq and JavaScript read every JSON number as a double.
- An optional value that is absent, such as the hash of a workload that wrote no G-code, is left
  out, so a reader never takes a zero for a measurement.
- The suite's start time is UTC to the millisecond, converted with Howard Hinnant's calendar
  algorithms, so there is no platform call.
- A workload that did not run is its name and a `skipped` or `failed` string holding the reason.

Every document written reads back. The writer refuses a skip or failure without a reason or with
iterations, a workload that ran with a reason, NaN or infinity, a negative integer and a start
time outside the years 0000 to 9999, all of which the reader would reject. The reader also refuses
an integer that is fractional or too big for its field and a span that would end past the largest
time the clock can hold. Both throw `DocumentError`, and the reader converts the parser's own
exceptions to it, so no caller needs nlohmann to catch them.

## Tests

`tests/data/orcabench/result_v1.json` is written by hand from the schema and is never regenerated
from the writer, which would make the golden test agree with whatever the writer does. The
document tests compare text with the whitespace between tokens removed, so a change in indentation
alone does not fail them. Every refusal above has a test that fails without it.
