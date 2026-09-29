# OrcaBench High Level Design

## Why it exists

OrcaBench measures the slicing pipeline. A run records how long each step took, the memory and CPU
it used and how much work it produced, in a document that two builds can be compared through, so a
change that makes slicing slower shows up along with the step that got slower.

## Where it lives

`benchmarks/` builds `orcabench_core`, a static library that never links libslic3r, so the
library and its tests in `tests/orcabench` build without the slicer. It is built when
`ORCA_BENCHMARKS` is on, which follows `BUILD_TESTS` by default, so every test build compiles it
and runs its suite. Turning the option off removes it.

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

An iteration also lists the steps that started and never finished, each with its start, and the
timed steps that never started. With a span that took no time, which is a step that ran below the
clock's resolution, a stage is in one of four states that a reader can always tell apart.

Measurement identity, what the run asked for, is a map compared whole, so two results are
comparable exactly when it is equal. Build and machine identity may differ between comparable
results, since comparing compilers or machines is one of the uses.

## Build identity

`BuildId.cpp` reports the commit and whether the working copy was dirty, read from
`git_commit_hash.h`, which `cmake/modules/GitCommitHash.cmake` rewrites at the start of every build
for the slicer and the benchmarks alike. The compiler, its version, the configuration and the
flags are read from a header CMake generates per configuration. The flags are `CMAKE_CXX_FLAGS`, the
configuration's own, and the `add_compile_options` the directory inherits, so a cache option such
as `SLIC3R_ASAN` shows up in them. `BuildId.cpp` is the only file in `benchmarks/` that includes
either generated header, so a new commit recompiles one file there.

## The result document

`Document.cpp` writes a result as JSON and reads it back, and is the only file that includes
nlohmann/json. The document's `schema` is `major.minor`. A minor version adds fields, which a
reader of the same major ignores when it does not know them. A major version changes or removes
fields, and a reader refuses any major but its own. Entries in the open maps (`measurement`,
`properties`, `metrics`) and stage names are kept whatever they are.

- Durations and CPU time are nanoseconds and memory is bytes, so a document reads back exactly and
  whatever displays it does the rounding.
- An iteration's spans and unfinished steps are written as offsets from its earliest start, since
  `steady_clock` readings mean nothing outside the process, and read back onto the epoch.
- `unfinished` and `not_run` list the steps that never finished and the timed ones that never
  started, and are left out when empty.
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
