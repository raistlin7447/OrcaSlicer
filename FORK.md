# Fork maintenance notes (myfork/main)

`myfork/main` (github.com/raistlin7447/OrcaSlicer) is a personal daily-driver build:
latest `upstream/main` plus my in-flight open-PR branches and a WIP feature, each
kept on its own branch so it can be dropped once it lands upstream.

The fork-only docs (this `FORK.md` and the README banner) live on their own
`fork-docs` branch and are re-applied by merging it like any other branch. They are
not on upstream or any feature branch, so a `git checkout -B main upstream/main`
rebuild would otherwise drop them silently (this happened once, on 2026-06-28).

## Features integrated

| Feature | Branch | Upstream PR |
|---------|--------|-------------|
| Fork-only docs (this file + README banner) | `fork-docs` | n/a |
| Restore Ctrl+drag panning on macOS | `fix/macos-ctrl-drag-pan` | OrcaSlicer/OrcaSlicer#15312 |
| Restore compile parallelism for clang-cl (VS generator) | `fix/clang-cl-msbuild-parallel` | OrcaSlicer/OrcaSlicer#15324 |
| Bounds-check the toolchange flush-volume and HRC lookups | `fix/toolchange-hrc-array-oob` | OrcaSlicer/OrcaSlicer#15289 |
| H2D extruder sync on the Motion ability page | `fix/sync-motion-ability-stride` | OrcaSlicer/OrcaSlicer#14931 |
| Slice the same model to the same lightning infill every time | `fix/lightning-infill-determinism` | OrcaSlicer/OrcaSlicer#15311 |
| Klipper/Moonraker upload errors show a raw Python traceback | `fix/moonraker-error-traceback` | OrcaSlicer/OrcaSlicer#14841 |
| Guard H2C per-filament array reads against short config arrays | `fix/h2c-oob-filament-arrays` | OrcaSlicer/OrcaSlicer#14789 |
| Extruder-sync dialog shows the wrong parameter name | `fix/sync-dialog-param-labels` | OrcaSlicer/OrcaSlicer#14939 |
| Resolve relative input paths given on the command line | `fix/cli-relative-input-paths` | OrcaSlicer/OrcaSlicer#14803 |
| Custom G-code reserved-keyword validation uses the right list | `fix/custom-gcode-reserved-keyword-validation` | OrcaSlicer/OrcaSlicer#14908 |
| Run the unit-test suite under the flatpak bounds-checked STL (CI) | `feature/ci-flatpak-tests-separate-job` | OrcaSlicer/OrcaSlicer#14709 |
| One declared cardinality per option (config array sizing refactor) | `refactor/config-cardinality` | OrcaSlicer/OrcaSlicer#14726 |
| Extruder clearance X/Y — _temporarily excluded, upstream conflict_ | `feature/extruder-clearance-rectangle` | none yet (WIP) |

Open-PR branches join the integrated set automatically (they are in-flight work run
on the daily driver) and drop off once their PR merges upstream. The `none yet`
branch is a local WIP feature.

`feature/extruder-clearance-rectangle` conflicts with the current `upstream/main` on
`src/libslic3r/PrintConfig.cpp` and `.hpp` (rerere cannot auto-resolve it), so it is
temporarily left out of `main` pending manual conflict resolution on the branch.
Re-add it (last) in the recipe below once resolved.

`fix/layer-height-ignore-honored` (OrcaSlicer/OrcaSlicer#14369),
`chore/test-debug-artifacts` (OrcaSlicer/OrcaSlicer#14785),
`feat/msix-execution-alias` (OrcaSlicer/OrcaSlicer#14799),
`fix/error-dialog-caret-alignment` (OrcaSlicer/OrcaSlicer#14886) and
`test/reenable-3mf-convex-hull` (OrcaSlicer/OrcaSlicer#14892) all merged upstream and
were dropped from this set on 2026-08-21; those changes now come from `upstream/main`
directly.

`docs/test-suite-readmes` (OrcaSlicer/OrcaSlicer#14628),
`fix/mmu-segmentation-zero-width` (OrcaSlicer/OrcaSlicer#14455),
`fix/webview-wx-handler-double-add` (OrcaSlicer/OrcaSlicer#14747) and
`fix/infill-rotation-raft` (OrcaSlicer/OrcaSlicer#14894) all merged upstream on
2026-07-22/23 and were dropped from this set; those changes now come from
`upstream/main` directly.

`fix/pa-pattern-absolute-e-reset` (OrcaSlicer/OrcaSlicer#14473),
`fix/calibration-cancel-crash` (OrcaSlicer/OrcaSlicer#14546),
`fix/filament-printable-oob` (OrcaSlicer/OrcaSlicer#14695),
`fix/bbl-dev-mode-dialog-spam` (OrcaSlicer/OrcaSlicer#14774) and
`fix/oom-new-handler` (OrcaSlicer/OrcaSlicer#14807) all merged upstream on
2026-07-20/21 and were dropped from this set; those fixes now come from
`upstream/main` directly.

`feature/additional_prepare_time` was retired from this set on 2026-07-02, superseded
upstream by OrcaSlicer/OrcaSlicer#14520. The branch is kept on `myfork` for reference
but is no longer merged into `main` or synced with `upstream/main`.

`fix/object-name-placeholder` (OrcaSlicer/OrcaSlicer#14497) and
`fix/wipe-tower-rotate-crash` (OrcaSlicer/OrcaSlicer#14499) both merged upstream on
2026-07-02 and were dropped from this set; those features now come from
`upstream/main` directly. `feature/gcode-test-framework`
(OrcaSlicer/OrcaSlicer#14426) merged upstream on 2026-07-06 and was likewise dropped.
`feat/regex-replace-filename-placeholder` (OrcaSlicer/OrcaSlicer#14650) merged upstream
on 2026-07-08 and was dropped; that feature now comes from `upstream/main` directly.
`feature/ci-cross-platform-tests` (OrcaSlicer/OrcaSlicer#14443),
`fix/calib-temp-dir-per-user` (OrcaSlicer/OrcaSlicer#14619) and
`fix/arachne-interpolate-bounds` (OrcaSlicer/OrcaSlicer#14656) all merged upstream on
2026-07-08 and were dropped; those features now come from `upstream/main` directly.

`fix/toolordering-max-layer-height-oob` (OrcaSlicer/OrcaSlicer#14665) and
`test/placeholder-expression-functions` (OrcaSlicer/OrcaSlicer#14667) merged upstream
on 2026-07-09 and were dropped; those features now come from `upstream/main` directly.
`fix/extrude-support-dangling-static-lambda` (OrcaSlicer/OrcaSlicer#14677) also merged
upstream on 2026-07-09 (it was a standalone local branch, never in this set).

`fix/partplate-stale-instance-crash` (OrcaSlicer/OrcaSlicer#14523) merged upstream on
2026-07-11 and was dropped from this set; that fix now comes from `upstream/main`
directly.

`fix/multiuser-tmpdir-crash` (OrcaSlicer/OrcaSlicer#14583) was closed unmerged on
2026-07-05, superseded by the per-user calibration temp-path fix
`fix/calib-temp-dir-per-user` (OrcaSlicer/OrcaSlicer#14619, since merged upstream).

## Re-syncing onto latest upstream

```
git fetch upstream
git checkout -B main upstream/main
git merge --no-ff fork-docs                          # FIRST: re-applies FORK.md + README banner
git merge --no-ff fix/macos-ctrl-drag-pan
git merge --no-ff fix/clang-cl-msbuild-parallel
git merge --no-ff fix/toolchange-hrc-array-oob
git merge --no-ff fix/sync-motion-ability-stride
git merge --no-ff fix/lightning-infill-determinism
git merge --no-ff fix/moonraker-error-traceback
git merge --no-ff fix/h2c-oob-filament-arrays
git merge --no-ff fix/sync-dialog-param-labels
git merge --no-ff fix/cli-relative-input-paths
git merge --no-ff fix/custom-gcode-reserved-keyword-validation
git merge --no-ff feature/ci-flatpak-tests-separate-job
git merge --no-ff refactor/config-cardinality
# feature/extruder-clearance-rectangle is temporarily excluded: it conflicts with
# upstream/main on PrintConfig.cpp/.hpp. Re-add it here (last) once resolved:
#   git merge --no-ff feature/extruder-clearance-rectangle
git push myfork main
```

Run the branch-sync helper (see below) first so each feature branch reflects any PR
commits added on GitHub before they are merged here.

Merge `fork-docs` first: `checkout -B main upstream/main` resets `main` to a clean
upstream tree, dropping the fork-only docs, and merging `fork-docs` brings them back
(see conflict 1 for the rare README case). Then merge the features smallest first,
clearance last. Incrementally adding only the new open-PR branches onto an
up-to-date `main` works too, since the bug-fix/CI/test/GUI branches don't touch the
clearance code.

### Conflicts to expect

1. `README.md` (textual, from the `fork-docs` merge, only if upstream edits the top
   banner region). The banner is a one-line blockquote at the very top, carried on
   `fork-docs`; the merge re-applies it while keeping upstream's README body. Keep
   the blockquote at the top, take upstream's changes below it. Usually no conflict,
   and git rerere auto-resolves it when there is one.

2. `src/libslic3r/GCode/ToolOrdering.cpp`, between `fix/h2c-oob-filament-arrays` and
   `feature/ci-flatpak-tests-separate-job`. The CI branch carries an older copy of the
   same guard so its bounds-checked test leg is green; take the
   `fix/h2c-oob-filament-arrays` side whichever order they merge in.


## Keeping feature branches current

The feature branches forked off upstream are kept up to date by **merging**
`upstream/main` into each (not rebasing - preserves the commits of any open PR and
avoids force-pushing). Run the helper any time:

    bash ~/bin/sync-myfork-branches.sh

For each branch it fetches upstream + myfork, and if the local copy is behind its
myfork counterpart (e.g. PR commits added via GitHub) it resets to the remote head
first, then merges `upstream/main` and pushes. Branches checked out in worktrees
are handled in place; on conflict it aborts that branch and tells you where to
resolve it. The branch list is derived live from the `feature/`/`fix/` prefixes
(minus open-PR branches, which `sync-open-pr-branches.sh` handles) so it never goes
stale as branches come and go; retiring a branch from maintenance is a one-line add
to `RETIRED_BRANCHES` in the script. `fork-docs` is tracked too, so its README base
tracks upstream and the banner re-merges cleanly.

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
