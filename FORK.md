# Fork maintenance notes (myfork/main)

`myfork/main` (github.com/raistlin7447/OrcaSlicer) is a personal daily-driver build:
latest `upstream/main` plus my in-flight open-PR branches and a couple of WIP
features, each kept on its own branch so it can be dropped once it lands upstream.

The fork-only docs (this `FORK.md` and the README banner) live on their own
`fork-docs` branch and are re-applied by merging it like any other branch. They are
not on upstream or any feature branch, so a `git checkout -B main upstream/main`
rebuild would otherwise drop them silently (this happened once, on 2026-06-28).

## Features integrated

| Feature | Branch | Upstream PR |
|---------|--------|-------------|
| Fork-only docs (this file + README banner) | `fork-docs` | n/a |
| MMU slicing crash with line width 0 | `fix/mmu-segmentation-zero-width` | OrcaSlicer/OrcaSlicer#14455 |
| PA-pattern calibration over-retracts with absolute E | `fix/pa-pattern-absolute-e-reset` | OrcaSlicer/OrcaSlicer#14473 |
| Unit tests on Windows/macOS | `feature/ci-cross-platform-tests` | OrcaSlicer/OrcaSlicer#14443 |
| Honor "Ignore" when layer height exceeds max | `fix/layer-height-ignore-honored` | OrcaSlicer/OrcaSlicer#14369 |
| fff_print test framework | `feature/gcode-test-framework` | OrcaSlicer/OrcaSlicer#14426 |
| First printable object name in filename | `fix/object-name-placeholder` | OrcaSlicer/OrcaSlicer#14497 |
| Crash when rotating the prime tower | `fix/wipe-tower-rotate-crash` | OrcaSlicer/OrcaSlicer#14499 |
| Stale instance ids in PartPlate scans | `fix/partplate-stale-instance-crash` | OrcaSlicer/OrcaSlicer#14523 |
| Additional prepare time | `feature/additional_prepare_time` | none yet (WIP) |
| Extruder clearance X/Y | `feature/extruder-clearance-rectangle` | none yet (WIP) |

Open-PR branches join the integrated set automatically (they are in-flight work run
on the daily driver) and drop off once their PR merges upstream. The two `none yet`
branches are local WIP features.

## Re-syncing onto latest upstream

```
git fetch upstream
git checkout -B main upstream/main
git merge --no-ff fork-docs                          # FIRST: re-applies FORK.md + README banner
git merge --no-ff fix/mmu-segmentation-zero-width
git merge --no-ff fix/pa-pattern-absolute-e-reset
git merge --no-ff feature/ci-cross-platform-tests
git merge --no-ff fix/layer-height-ignore-honored
git merge --no-ff feature/additional_prepare_time
git merge --no-ff feature/gcode-test-framework
git merge --no-ff fix/object-name-placeholder
git merge --no-ff fix/wipe-tower-rotate-crash
git merge --no-ff fix/partplate-stale-instance-crash
git merge --no-ff feature/extruder-clearance-rectangle
git push myfork main
```

Run the branch-sync helper (see below) first so each feature branch reflects any PR
commits added on GitHub before they are merged here.

Merge `fork-docs` first: `checkout -B main upstream/main` resets `main` to a clean
upstream tree, dropping the fork-only docs, and merging `fork-docs` brings them back
(see conflict 1 for the rare README case). Then merge the features smallest first,
clearance last. `feature/additional_prepare_time` must come **before**
`feature/gcode-test-framework` (see conflict 2 below). Incrementally adding only the
new open-PR branches onto an up-to-date `main` works too, since the
bug-fix/CI/test/GUI branches don't touch the clearance code.

### Conflicts to expect

1. `README.md` (textual, from the `fork-docs` merge, only if upstream edits the top
   banner region). The banner is a one-line blockquote at the very top, carried on
   `fork-docs`; the merge re-applies it while keeping upstream's README body. Keep
   the blockquote at the top, take upstream's changes below it. Usually no conflict,
   and git rerere auto-resolves it when there is one.

2. `tests/fff_print/test_gcode.cpp` (modify/delete, `feature/gcode-test-framework`).
   The framework branch deletes `test_gcode.cpp` (its "Origin manipulation" test
   moves to `test_gcodewriter.cpp`), but `feature/additional_prepare_time` added two
   fork-only `machine_additional_prepare_time` SCENARIOs to that same file. rerere
   does not handle modify/delete. Resolve by keeping a slimmed `test_gcode.cpp` that
   holds **only** the two prepare-time SCENARIOs (drop the duplicated origin test),
   and re-add `test_gcode.cpp` to `tests/fff_print/CMakeLists.txt` (the framework
   branch removes it). This is why prepare-time must merge before the framework.

## Keeping feature branches current

The feature branches forked off upstream are kept up to date by **merging**
`upstream/main` into each (not rebasing - preserves the commits of any open PR and
avoids force-pushing). Run the helper any time:

    bash ~/bin/sync-myfork-branches.sh

For each branch it fetches upstream + myfork, and if the local copy is behind its
myfork counterpart (e.g. PR commits added via GitHub) it resets to the remote head
first, then merges `upstream/main` and pushes. Branches checked out in worktrees
are handled in place; on conflict it aborts that branch and tells you where to
resolve it. Edit the `BRANCHES` list in the script as branches come and go (drop
one once its PR merges upstream). `fork-docs` is in that list too, so its README
base tracks upstream and the banner re-merges cleanly.

Manual equivalent for a single branch:

    git checkout <branch>
    git merge --no-ff upstream/main
    git push myfork HEAD:<branch>

## Building (Windows)

`build_release_vs.bat slicer` from a VS Developer prompt (generator VS 18 2026,
Release, build dir `build`).

If a VS update breaks configure with `CMAKE_C_COMPILER ... is not a full path to an
existing compiler tool`, the cached MSVC toolset was removed by the upgrade. Delete
`build/CMakeCache.txt` and `build/CMakeFiles/` (this keeps the compiled `.obj` files)
and rebuild; CMake re-detects the new toolset.

## CI on the fork

The `Build all` workflow publishes unit-test results via
`EnricoMi/publish-unit-test-result-action`, which creates a check run. A fresh fork
defaults its `GITHUB_TOKEN` to read-only, so that step fails with
`Resource not accessible by integration: 403`. Fix once per fork by setting the
default workflow token to read+write:

```
gh api -X PUT repos/raistlin7447/OrcaSlicer/actions/permissions/workflow \
  -f default_workflow_permissions=write
```

(Equivalent: repo Settings, Actions, General, Workflow permissions, Read and write.)
