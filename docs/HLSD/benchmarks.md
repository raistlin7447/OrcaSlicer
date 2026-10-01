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
`cpu_time_step()` is the step the CPU time moves in, 15.625 ms on Windows, where `GetProcessTimes`
charges each thread's time at the clock interrupt, and zero elsewhere.

## The result model

`Result.hpp` describes a run: the suite's start time and duration, what the run asked for, the
build and the machine, and one entry per workload. A workload ran, was skipped because a fixture
was missing, or failed because it threw or the Runner refused what it reported, and the last two
have a reason. Neither counts as a pass, and the outcome counts are computed from the list
whenever asked, never stored, so no total can disagree with it.

A field is typed when every result has it, such as a span's stage and times or an iteration's
wall time and CPU time. Peak memory, the work stats and the output hash are optional, since a run
may not collect them, and are then absent, never zero. A measurement only some producers record
is stored in a `Metrics` map (name to number) on the span, the iteration or the work stats, keyed
by a constant declared beside its producer, and a missing key means it was not collected. Build
and machine identity have a `Properties` map (name to text) for the same purpose, which holds the
machine's CPU time step where it has one.

An iteration also lists the steps that started and never finished, each with its start, and the
timed steps that never started. With a span that took no time, which is a step that ran below the
clock's resolution, a stage is in one of four states that a reader can always tell apart.

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
print or one object) and times, each with its own metrics, the steps that never finished and the
timed ones that never started, then metrics of the whole iteration, and the work stats and the
output hash once each. The tests' fake writes through the same type, so nothing between a workload
and its result depends on slicing. A span that ends before it starts, a stage reported twice for
one scope in any of the three forms, a number that is not finite and a metric reported twice are
refused where they are reported, so one bad value fails only its own workload and never reaches
the document writer, which would refuse the whole result.

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
and leaves it after the last. A run ends before any workload starts when an entry name is one
`validate()` refuses or one that repeats, since results are keyed by name, or when the environment
fails to enter.

Each workload is built, set up once, then prepared and executed for every warmup and timed pass.
Under `pgo`, an entry that may not train is left out of the run, the same as one the selection
never named. Nothing a workload throws leaves the Runner. The workload fails with a reason that
names the call and the pass, such as `execute() threw on timed pass 2: bad allocation`, and the
next one runs. An empty reason to skip fails the workload as well, since the document cannot hold
a skip without a reason.

Only `execute()` is timed, between two readings of the process the Runner takes around it, which
give its wall time and the CPU time the process used. The Runner builds each pass's `Measurement`
with the time of the first reading, and a span that starts before it, or ends after it is
reported, is refused. A workload that reuses state from an earlier pass therefore fails, and every
recorded span lies inside its iteration.

Every pass, warmups included, must reproduce the first pass's output hash and work stats, since a
result holds one of each, and a pass that differs fails the workload. The hash therefore leaves out
whatever varies between runs of the same input, such as the time the G-code header records. Every
pass must also agree on which stages finished, stayed unfinished or never ran, in whatever order it
reports them, so no stage's row mixes states. A result keeps the hash and the work stats only when
the policy collects them, and a timed pass becomes an iteration only when wall time is collected,
so `verify` records its hash and no iterations. The sampler adds each iteration's peak memory and
each span's readings.

The Runner reports as it runs through `RunEvents`: the run starting, each workload starting and
finishing, each pass finishing with the time `execute()` took, and the run finishing with the whole
result once the environment is left. A pass's event fires after the pass's last reading and before
the next pass is prepared, never while a pass is timed or sampled, so whatever listens cannot slow a
measurement. A listener that throws after a pass fails that workload, as the workload throwing
would.

## The sampler

`Sampler.cpp` reads the process every 5 ms on its own thread through each timed pass, taking its
resident memory and the CPU time it has used through the `Host` queries. The Runner starts the
thread before the pass's first reading and stops it after the last, and warmup passes, `verify`
and `pgo` never sample. The interval is recorded in the measurement identity, as the corpus and
the affinity are, and Windows rounds the wait up to its clock tick, 15.6 ms unless something has
raised the timer resolution.

An iteration's peak memory is its highest reading, the same way on every platform, so a spike
shorter than the interval can go unseen. The operating system's own high-water mark would be
exact only for an iteration that sets a new record for the process, and only Linux can reset it.
Each span gets its highest reading, and the CPU time used between the first and last readings
inside it with the time between them, so its utilization is exact where it was measured. A span
holding no reading has no memory and one holding fewer than two has no CPU, and `Measurement`
refuses these keys from a workload. A span's readings are the whole process's, so spans that
overlap, such as the same step on two objects, share them.

Windows CPU time moves in 15.6 ms steps, and Microsoft documents its precise cycle counts as not
convertible to time, so a short span there says little about CPU. A `ProcessProbe` takes each
reading, from `Host` in a run and from a script in the tests.

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
iterations, a workload that ran with a reason, a workload named twice, NaN or infinity, a negative
integer and a start time outside the years 0000 to 9999, all of which the reader would reject. The
reader also refuses an integer that is fractional or too big for its field and a span that would end
past the largest time the clock can hold. Both throw `DocumentError`, and the reader converts the
parser's own exceptions to it, so no caller needs nlohmann to catch them.

## The stage table

`Summary.cpp` turns a workload's iterations into rows of stages and totals. A stage's row sums its
spans across objects within each iteration, then takes the mean, the minimum and the coefficient of
variation over the iterations, and its share is its mean over summed work, the sum of the rows. In
verbose mode each scope gets its own row. The wall envelope is the mean iteration wall time, and
unaccounted is the mean time no span covers, so objects slicing side by side can take summed work
past the wall envelope but cannot make unaccounted negative. Each row carries its stage's state, so
the four stay apart.

`collapse()` folds the rows under 1% of summed work into one other row, which counts the stages that
never ran. A row whose stage started without finishing at least once never folds, even when it also
ran, since the time it hides lands in unaccounted, and neither does a row marked significant, so a
change that matters stays visible however small the stage. A row whose CV exceeds the 3%
significance bar is noisy, since a change that size there cannot be told from noise.

Each row also carries its sampled CPU and peak memory, and is marked shared when its spans
overlapped another row's, since its readings then include that work. A CPU figure is left out when
the worst-case rounding of its readings exceeds a tenth of the CPU time it shows. That rounding is
the CPU time step the result's machine recorded, once per pair of readings for each of the run's
threads and for the sampler's, so CPU from a machine that recorded no step is never left out.

## The reporters

A `Reporter` turns a run into what `orca_bench` prints. `console` is for people, `json` for anything
that reads results, and `null` for a run whose output nobody reads. A reporter is fed the identities
before the first workload, each workload as it finishes and the whole result at the end, so the
console prints each workload's table as the run goes, while `json`, valid only whole, writes the
document at the end. A new reporter is a class and one entry in the table in `Reporters.cpp` that
`make_reporter()` reads.

The console marks a noisy or shared row, and one whose stage also started without finishing, with a
symbol that a legend explains. Its header names the configuration and the machine, and says
`unoptimized` for flags that turn optimization off, as OrcaSlicer's RelWithDebInfo does. `json`
writes the result itself, so nothing folds in it.

Both console views can color their text, as a second channel that never carries meaning the words
lack, so a pasted log, a monochrome screenshot or a colorblind reader loses nothing. Bold vermillion
marks worse and bold blue better, never red against green, inverse marks an alarm and dim what did
not clear the bar. Color is on when standard output is a terminal and `NO_COLOR` is unset or empty,
and `--color` overrides both. `Host` turns on escape sequences in a Windows console, and a terminal
that hands programs a pipe, as mintty does, gets color only from `--color always`.

`Progress` shows where a run is on stderr, apart from the report, so piping `json` never carries it.
In a log it prints a line as each workload starts. In a terminal it redraws one line after each
pass, with the time the pass took and the time the workload has left at its pace so far, and erases
it before the workload's table prints.

## Comparing results

`orca_bench --compare a.json b.json` reads two result documents, `a` from before a change and `b`
from after it, and `Compare.cpp` pairs their workloads by name and their stages by stage, or by
stage and scope in verbose mode. Runs compare only when their measurement identities are equal,
since a different thread count or policy changes the times with no change to the code, and
`--allow-mismatch` compares them anyway after listing each difference. Builds and machines may
differ, since telling them apart is what a comparison is for.

Every figure is the minimum over the iterations: each stage, summed work, unaccounted time, the wall
and peak memory. Interference only ever adds time, so the minimum is the steadiest estimate of what
the code costs, and the change of the mean prints beside it to show a spread that moved. A change
counts when it reaches the 3% significance bar and exceeds both runs' CV. One that reaches the bar
inside a CV is marked `~` as possible noise, and with one iteration there is no CV, so nothing is
judged.

Output comes before time. A workload whose output hash or work stats differ measures different work,
so the comparison lists every hash and stat that changed before any time, and words such a
workload's changes longer or shorter, since slower means the same work cost more. Only there does
the view also show the wall per million moves, per layer and per cm3, and the geometric mean of the
wall covers only the workloads whose output did not change. A stage whose state changed, or that
only one run has, is significant, and pairs fold into the other row only when small in both runs and
unchanged, so a stage that grew surfaces however small it was before.

Each column of the view holds one unit at one precision, and the view is as wide as its columns, up
to 120. `orca_bench` exits 0 after a comparison, 3 when a workload's output changed, 1 when it
refuses the runs or cannot read a document, and 2 for a bad command line.

The two runs are timed apart, one after the other, so anything that changes on the machine between
them, such as its temperature or a background job, moves the figures as a code change would, and the
comparison cannot tell the two apart. Runs made back to back on an idle machine keep that small.

## Tests

`tests/data/orcabench/result_v1.json` is written by hand from the schema and is never regenerated
from the writer, which would make the golden test agree with whatever the writer does, and the
expected text of both console views is laid out apart from the code that prints it for the same
reason. The document tests compare text with the whitespace between tokens removed, so a change in
indentation alone does not fail them. Stripping the escape sequences from colored output must give
the plain text exactly. Every refusal above has a test that fails without it.
