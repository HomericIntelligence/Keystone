# Local CI and hosted gate mapping

`just ci-all` runs the complete supported local check set sequentially and stops
when a check fails. Builds and CTest use at most two parallel jobs. The launcher
uses the selected container engine; a failed image build does not trigger a
second build with another engine.

## Run the checks

1. Use an isolated checkout with complete Git history for the secrets scan.
   Record the commit and any working-tree changes before the run.
2. On Linux amd64 or arm64, run `just ci-build` against that checkout. The pinned
   uv, NATS, and Gitleaks archives are selected for the image's native
   architecture and checked against official SHA-256 sums before extraction.
   macOS hosts need a Linux container engine. `CONTAINER_ENGINE` can select the
   engine explicitly; otherwise the launcher selects Podman, then Docker when
   Podman is unavailable. Docker uses the root `.dockerignore`; Podman uses
   `ci/.dockerignore`. Both run as the image's `ci` user (UID 1000). Podman maps
   the caller to that user with `keep-id`; Docker bind mounts must be writable
   by UID 1000. A custom engine wrapper must accept Podman's options, or be
   named `docker` when it implements Docker's interface.
3. Run `just ci-all` with the operator's external time and memory limits. The
   image must be rebuilt when its inputs change. Do not run against a stale
   `keystone-ci:local` tag and represent that result as current-source evidence.
4. Retain the raw command output, exit code, source revision, image identity,
   package files, and coverage output. An interrupted or failed run remains a
   failed or incomplete gate. Run the failing subset after repair, then complete
   the remaining checks against the final source.
5. Confirm the required hosted checks and their artifact identities for that
   same commit before merge. Local CI does not replace these checks.

## Check ownership

| Required check | Local entry point or subset | What runs |
| --- | --- | --- |
| Suppression policy | `forbid-suppressions` | Tracked-source checks matching the required workflow's three suppression rules |
| Lint | `just ci-lint` | Mypy, scoped CI-helper Ruff/ShellCheck, pre-commit, extraction guard, launcher/installer regressions, Fleet clang-tidy |
| Markdown | `just ci-markdownlint` | The image's markdownlint-cli2 |
| uv lock and dependency synchronization | `uv-lock-check`, `deps-version-sync` | Locked environment and manifest checks |
| Justfile and symlinks | `justfile-check`, `symlink-check` | Justfile parsing and tracked symlink integrity; generated build caches are excluded |
| Workflow schema and queue contract | `just ci-schema-validation` | Bundled GitHub workflow schema for both extensions, its invalid-workflow regression, and actual required-job eligibility |
| Unit tests | `just ci-unit-tests` | Current debug build and discovered GoogleTest cases labeled `unit`, plus the sanitizer feature guard; failure cannot fall back to a second selection |
| Integration and sanitizers | `just ci-integration-tests` | ASan, UBSan, TSan, and LSan, including the Fleet gateway and installed gateway checks against private brokers; bounded retries preserve the main branch's discovery policy |
| Build | `just ci-release-build` | Current release build with Conan dependencies |
| Install | `just ci-install` | Canonical staging-layout and `find_package` consumer check |
| Package | `just ci-package` | Real CPack metadata regression checks, DEB/RPM/TGZ/ZIP generation, and nonempty artifacts |
| Coverage | `just ci-coverage` | Coverage build, tests, and the existing report/threshold script |
| Static release validation | `just ci-release-check` | Literal release-validation commands read from the canonical required workflow; no publication |
| Secrets | `just ci-security-secrets-scan` | Gitleaks with redacted output and its real failure status |
| Python build-tool dependencies | `security-dependency-scan` | pip-audit against the installed locked build-tool environment |
| Production-image vulnerability scan | Hosted only | The existing production build and Trivy policy, reports, and gate remain required |
| CodeQL, provider uploads and release publication | Hosted only | Existing provider jobs and permissions remain authoritative |

Subsets without a named recipe can be selected with
`just --command bash scripts/run_ci_local.sh SUBSET`. The local `all` command
includes every supported local check above. It does not upload reports, publish
releases, or change provider state.

## Evidence boundaries

The `unit` label belongs to the core, concurrency, scheduler-backoff, simulation,
transport, and bridge unit targets. The separate profiling target joins this
selection only when `ENABLE_PROFILING` is enabled. Scheduler-backoff cases retain
`RUN_SERIAL`; E2E and integration targets remain outside the unit selection.
The ordinary debug build runs `ThreadPoolTest.CreateAndDestroy` and
`ThreadPoolTest.HardwareConcurrency`, which intentionally skip under sanitizers.

The scheduler's SPIN and YIELD iteration budgets remain 100 and 1000. Its SLEEP
phase checks queues and enters its condition-variable wait under the same mutex
that submission uses for notification. Each wake rechecks work; the one-millisecond
timeout is a fallback. Shutdown still wakes workers and drains remaining work.

`SchedulerSleepTest` exercises that shared SLEEP implementation with the real
queue and a controlled condition-variable boundary. It checks notification before
and during a wait, repeated notifications, and shutdown without measuring host
speed. `SchedulerPerformanceTest.WorkerQueuedHandoffLatency` retains the existing
200-microsecond limit (5 milliseconds under ASan or TSan). The sole worker queues
its next callback before returning to the work loop; a sleep cannot establish a
worker's backoff phase. This performance case and the existing idle-wakeup and
load-latency cases remain in the required `unit` selection with `RUN_SERIAL`.
These focused checks do not establish idle CPU consumption or Fleet throughput;
those claims require separate measurements.

`just test-ci-local` first exercises the repository's literal GoogleTest
discovery calls with actual CMake/CTest and imported test-list fixtures. It
checks selected case names, nonunit exclusions, and the serial property without
compiling C++ or fetching dependencies. This registration regression does not
replace execution of the real test bodies in `just ci-unit-tests`.

The coverage script evaluates the same lcov summary that it prints. It accepts
the separator widths used by different lcov versions and requires exactly one
valid line-coverage percentage. Missing, malformed, or duplicate summaries fail
the gate. Coverage-tool failures retain their exit status, and the minimum stays
at 75%. `just test-ci-local` runs the public script with controlled lcov/genhtml
processes to test summary parsing, the threshold boundary, and failure propagation.
These synthetic fixtures do not measure Keystone coverage; `just ci-coverage`
must produce the actual report and enforce its threshold.

`just test-ci-local` runs the real launcher with controlled engine/tool
boundaries. It tests dispatch, failure propagation, missing validators, package
results, and source-policy checks. The installer tests use actual checksum
verification over deliberately corrupt bytes. These tests do not establish a
successful container build or a successful installation from a release archive.

The queue fixtures run the actual readiness checker against private
workflow copies. They cover conditional required jobs and dependencies,
missing or duplicate contexts, and forbidden publisher admission. The launcher
fixture also verifies that a rejected queue contract fails the schema subset.
Version checks use the canonical required workflow's comparison against CMake,
Conan, and release-please metadata, including disagreement.
All 13 live required contexts execute in `_required.yml` on merge groups;
the extras workflow also retains its real coverage producer. See
[queue verification](../CICD_QUALITY_GATES.md#merge-queue-verification)
for the separate actual queue-head acceptance step.

The release adapter reads the canonical workflow instead of maintaining another
copy of its static checks. Unsupported hosted context is rejected. This Python
file is repository CI tooling; Keystone's transport and installed Fleet gateway
remain C++20.

Fleet gateway tests, complete local CI, hosted CI, and the 108-agent Fleet
acceptance run are separate evidence. A passing gateway test or idle conversation
does not demonstrate concurrent admitted issue execution.
