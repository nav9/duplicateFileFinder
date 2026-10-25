# AI Coding Agent Instructions --- Cross-Platform Duplicate Manager

## 1. Mission and working rules

Build a cross-platform C++20 duplicate-file and directory manager for
Linux, macOS, and Windows. The current deliverable is a **headless core
and command-line development workflow**. Do not build a GUI yet. Design
public interfaces so a future wxWidgets GUI can call the same tested
core APIs without reimplementing business logic. The namespace can be 
called "dupli".

Use CMake for all C++ builds. Use BLAKE3 for full-file content hashing.
Use dependency injection (DI) throughout so file-system behavior,
hashing, container detection, time, journaling, and failure scenarios
can be tested deterministically.

Implement functionality in small, ordered increments. For each
increment: 1. Define the contract and invariants in code comments. 2.
Write failing tests first. 3. Implement the smallest correct solution.
4. Run the relevant tests. 5. Run the full test suite and build in the
active configuration. 6. Fix regressions before continuing.

Do not skip tests, do not leave TODO-only implementations, do not silently weaken
tests, or claim a feature works unless a test exercises it. Do not start
the next milestone until its acceptance criteria pass. Keep the project
buildable after every milestone.

Do not generate separate documentation files. Keep explanations of
non-obvious behavior, safety rules, assumptions, and platform-specific
constraints in source-code comments. This `instructions.md` is the
agent's implementation specification.

## 2. Non-negotiable product and safety requirements

-   Platforms: Linux, macOS, Windows.
-   Language: C++20.
-   Build system: CMake.
-   Hashing: BLAKE3, streaming the **entire contents of every regular
    file** using bounded memory. Never use sampled hashing to assert
    identity.
-   Symlinks are ignored and must not be traversed. On Windows, identify
    and skip symlink/reparse-point entries that behave as links. Do not
    accidentally follow links through path canonicalization.
-   Detect and report inaccessible, corrupt/unreadable, unsupported,
    disappeared, and changed entries. Never silently classify unreadable
    content as unique or as a duplicate.
-   Compare content independently of file size and filename. Size is
    metadata and may be used for diagnostics or an inexpensive rejection
    check, but never as the basis for duplicate identity.
-   File content identity is distinct from directory-tree equivalence
    and from semantic equivalence of projects.
-   Treat metadata differences separately from content identity. Report
    metadata differences and apply an explicit preservation policy; do
    not silently discard metadata.
-   Support preview-only, quarantine duplicates, create options for the user to choose between a unique
    collection, and in-place merge. New-destination merge is the
    default. In-place merge must be an advanced, explicitly approved
    operation.
-   Cross-filesystem operations use copy, verify, publish, and **retain
    the source**. Never delete the source as a consequence of
    cross-filesystem copying.
-   Moving or merging a detected Git repository requires explicit
    approval for that specific operation. Generic file logic must never
    merge `.git` internals.
-   Saved HTML files and associated resource folders may form a logical
    unit. Detect this conservatively; folder-name similarity alone is
    insufficient. Do not move shared resources away from other pages.
-   Perceptual image hashing is a separate feature for image files (which the user may enable or disable), not a substitute for BLAKE3. Do not implement it until exact duplicate
    handling is correct and tested. Similar images are not exact
    duplicates and must never be automatically discarded.
-   The user must be able to review categorized findings and choose
    actions before any mutation.
-   A preview-only operation must not mutate the scanned source trees.
-   Never overwrite a destination silently. Never remove unique data
    merely because it is inside a directory considered similar to
    another.
-   Do not assume rollback is always possible. Journal operations and
    provide explicit recovery states.
-   No GUI in this phase. Do not add wxWidgets as a required dependency.
    Keep a stable, GUI-neutral C++ API so a future wxWidgets application
    can call the same core services.
-   Do not set up continuous integration. Do not generate separate
    documentation files. Add useful comments directly in source and test
    code.

## 3. Repository layout

Create a clear, modular layout similar to this; adjust only when there
is a concrete reason:

``` text
duplicate-manager/
  instructions.md
  menu.py
  CMakeLists.txt
  CMakePresets.json                 # if useful and portable
  cmake/
  include/dupman/
    types.hpp
    result.hpp
    filesystem.hpp
    scanner.hpp
    hasher.hpp
    fingerprint.hpp
    classifier.hpp
    container_detector.hpp
    git_detector.hpp
    web_archive_detector.hpp
    merge_planner.hpp
    action_planner.hpp
    validator.hpp
    executor.hpp
    journal.hpp
    report.hpp
    service.hpp
  src/
    core/
    platform/
    cli/
  tests/
    unit/
    integration/
    safety/
    recovery/
    fixtures/
```

Do not create empty placeholder modules merely to match this tree. Add a
file when its first tested functionality is implemented.

Build artifacts must live under a build directory, for example
`build/debug` and `build/release`, never intermixed with source files.
Add generated build directories and local state to `.gitignore`.

## 4. Dependency injection and design rules

### 4.1 Use constructor injection

Business logic must receive dependencies explicitly through constructors
or function arguments. Do not use global mutable state, singletons,
hidden filesystem calls inside classifiers, or static service locators.

Examples of injectable dependencies: - `IFileSystem`: enumeration,
metadata, reads, writes, rename/copy, no-overwrite publication, file
identity, permissions, flush/sync, and filesystem-boundary queries. -
`IReadableFile` / `IWritableFile`: bounded byte reads/writes and
explicit close/error handling. - `IHasher`: BLAKE3 streaming hash
implementation. - `IContainerDetector`: detector for a specific
container type. - `IClock`: time in tests and journal timestamps. -
`IJournal`: durable operation records. - `IIdGenerator`: stable IDs in
deterministic tests. - `IProcessRunner`: safely invoke Git read-only
commands without shell interpolation. - `IReporter` or event callback:
progress and structured diagnostics, independent of UI. -
`IImageDecoder` and perceptual-hash service, only if the optional image
feature is later implemented.

Use production adapters for real operating-system behavior and fakes for
unit tests. Keep fake implementations deterministic and configurable to
inject errors at specific operations.

### 4.2 Separate pure logic from I/O

Prefer pure functions for: - Encoding a tree fingerprint. - Comparing
two directory manifests. - Grouping equal digest values. - Classifying
path conflicts. - Resolving user decisions into an action graph. -
Checking whether a plan's dependencies form a valid acyclic graph.

Filesystem traversal, hashing, copying, and mutation should be explicit
I/O operations behind interfaces.

### 4.3 Make failure visible in return types

Use a consistent error/result strategy. Prefer `std::expected` if the compiler/library baseline supports it reliably across all target platforms; otherwise define a small `Result<T, Error>` type or use exceptions at the adapter boundary and translate them into typed errors at module boundaries.

Do not use exceptions for routine control flow. Do use exceptions safely at application boundaries, with a final catch-all that reports failure rather than crashing silently. Ensure cleanup through RAII.

### 4.4 RAII and resource ownership

-   Wrap file handles, OS handles, locks, and temporary paths in RAII
    types.
-   Do not manually pair resource acquisition and release across many
    return paths.
-   Temporary destination files/directories should have a guard that
    cleans them up only if the program can establish ownership and
    cleanup is safe.
-   Do not remove a pre-existing file during temporary-file cleanup.
-   Make ownership and copy/move semantics explicit for resource-owning
    classes.
-   Avoid raw owning pointers. Prefer values, `std::unique_ptr`, or
    `std::shared_ptr` only when shared ownership is actually required.
-   Use `std::filesystem::path` for paths and binary I/O for file
    content.
-   Use `std::jthread` or another controlled worker mechanism only when
    parallelism is introduced; cancellation and shutdown must be
    deterministic.

### 4.5 Future wxWidgets integration

Expose a GUI-neutral service facade, for example:

``` cpp
class DuplicateManagerService {
public:
    ScanResult scan(const ScanOptions&);
    ClassificationResult classify(const ScanResult&);
    ActionPlan create_plan(const PlanRequest&);
    ValidationResult validate(const ActionPlan&);
    ExecutionReport execute(const ActionPlan&, const ApprovalSet&);
};
```

The exact API should use the project's typed result model. It must not
depend on wxWidgets types, GUI event loops, widgets, or GUI-owned
memory. Progress should be reported through an injected
observer/callback or event sink that can later be adapted to wxWidgets.
Document callback threading rules in comments. The GUI must never
implement duplicate logic itself.

## 5. Core data model

Create typed structures rather than passing unstructured strings between
modules. At minimum include:

-   `EntryId`, `ActionId`, `ScanId`, and `PlanId`.
-   `EntryKind`: regular file, directory, symlink, link-like reparse
    point, special file, unknown.
-   `ContentStatus`: not attempted, complete, read error, changed during
    read, unsupported.
-   `FileMetadata`: size, modification time if available,
    permissions/mode if available, platform file identity/device,
    hard-link count when available, and other metadata needed by policy.
-   `ContentDigest`: algorithm (`BLAKE3`), digest bytes/hex, bytes
    hashed, and status.
-   `ScannedEntry`: original path, root-relative path, parent/children
    IDs, kind, metadata, digest, errors, exclusion/protection status,
    and enclosing-container ID if applicable.
-   `ScanIssue` and typed `Error` structures with operation, path, code,
    and human-readable message.
-   `Relationship`: left/right entries, relationship kind, confidence,
    evidence, shared entries, left-only/right-only entries, warnings.
-   `ContainerInfo`: type, root entry, members, warnings, and whether
    explicit review is mandatory.
-   `PlannedAction`: action ID, kind, dependencies, source/destination,
    expected content identity, preconditions, required approvals.
-   `ActionPlan`: scan/version identifiers, mode, decisions, actions,
    warnings, and unresolved conflicts.
-   `ActionResult` and `ExecutionReport`.
-   Versioned manifest and journal records.

Keep IDs deterministic within a scan and stable enough for
plans/reports. Do not use a path string as the sole identity because a
path can be reused or changed after scanning. Avoid relying on OS file
IDs as cross-platform or cross-filesystem content identity.

## 6. Required functional milestones --- tests first

Complete the milestones in this order. For each milestone, add tests before implementation, run them, and do not proceed until the acceptance
criteria pass. Keep all previous tests passing.

### Milestone 0 --- Build skeleton and menu

1.  Create the CMake project, C++20 settings, library target, CLI
    target, and test target.
2.  Add `menu.py` as specified in section 12.
3.  Add a trivial test to prove the test framework is wired up.
4.  Add compiler warnings and warnings-as-errors where practical without
    breaking third-party code.
5.  Verify Debug and Release configure/build/test flows on the current
    OS.
6.  Add platform checks for Linux, macOS, and Windows. Do not hard-code
    shell syntax or executable suffixes into CMake commands.
7.  Acceptance: clean configure/build/test works using the menu on the
    current OS; errors are displayed and return to the menu.

### Milestone 1 --- Result/error types and filesystem interface

1.  Define typed errors and the consistent result strategy.
2.  Define `IFileSystem`, readable/writable file abstractions, and
    production adapter interfaces.
3.  Implement a fake filesystem for tests, including files, directories,
    symlinks, metadata, and injected failures.
4.  Test not-found, permission denied, read/write failure, destination
    collision, and unsupported-entry behavior.
5.  Acceptance: core logic can be tested without touching the host
    filesystem, and every injected failure is observable.

### Milestone 2 --- Real filesystem adapter and safe traversal

1.  Implement real filesystem operations using `std::filesystem` where
    safe and narrow platform APIs where required.
2.  Do not follow symlinks during traversal. On Windows, explicitly
    handle reparse-point attributes and distinguish link-like entries.
3.  Detect overlapping selected roots and avoid scanning an entry more
    than once.
4.  Add hidden files by default. Do not skip dotfiles.
5.  Make crossing filesystem/mount boundaries configurable; default to
    not crossing boundaries unless the user explicitly enables it.
6.  Continue scanning after local errors while recording the failed path
    and cause.
7.  Detect entry changes/disappearance during scanning when possible.
8.  Test unusual names, Unicode paths, spaces, long paths within
    platform limits, empty directories, hidden files, overlapping roots,
    unreadable directories, and symlink loops.
9.  Acceptance: a scan does not traverse links, does not double-count
    overlapping roots, and reports partial scans honestly.

### Milestone 3 --- BLAKE3 streaming hasher

1.  Select a maintained BLAKE3 implementation and pin/document its
    version in the build configuration. Prefer a reproducible CMake
    dependency mechanism (vendored source or a pinned fetch with a clear
    offline fallback policy).
2.  Use the official BLAKE3 test vectors to test the
    implementation/integration.
3.  Read files in bounded chunks; never allocate a buffer proportional
    to file size.
4.  Hash every byte of every supported regular file. Do not use
    sampling.
5.  Capture metadata/file identity before and after reading and detect
    changes when the platform allows it. A changed file gets a
    non-success status.
6.  Treat any read error as a failed digest; never return a partial
    digest as complete.
7.  Add a configurable buffer size for tests. Do not allow a zero-sized
    buffer.
8.  Test empty files, binary bytes, multi-buffer files, known vectors,
    same content/different names, different content/same size, read
    failures, and file mutation during hashing.
9.  Add a large-file test that checks bounded memory behavior by design
    and avoids relying only on a sparse-file test.
10. Acceptance: only complete BLAKE3 digests can establish exact-content
    groups.

### Milestone 4 --- Duplicate file grouping

1.  Group files by algorithm and complete digest. Do not compare by
    name.
2.  Size may be used to avoid an unnecessary byte comparison, but it is
    not the identity definition.
3.  Optionally verify equal digests with a byte-for-byte comparison
    before high-assurance operations. Keep this policy explicit.
4.  Distinguish multiple paths to the same underlying hard-linked object
    from separate files with identical content when the platform
    supports reliable identity information.
5.  Do not classify unreadable, unstable, or unsupported files as
    unique.
6.  Tests: identical bytes/different names; identical bytes/different
    directories; different bytes/equal size; empty files; hard links;
    failed hashes; changed-after-scan files.
7.  Acceptance: exact groups contain only fully hashed files with
    matching BLAKE3 digests and valid status.

### Milestone 5 --- Folder fingerprints

Implement three separate concepts:

1.  **Exact-tree fingerprint:** deterministic encoding of relative
    paths, entry kinds, complete content digests, child-directory
    fingerprints, and empty directories.
2.  **Content multiset:** file-content digests plus occurrence counts,
    independent of path layout.
3.  **Partial-overlap report:** matching contents/paths, unique entries,
    and path/type conflicts.

Use bottom-up directory processing and deterministic sorting. Encode
path lengths and entry types unambiguously; do not concatenate ambiguous
strings. Version the fingerprint format with a domain separator. Do not
use a simple XOR or sum of hashes.

Do not include modification time or permissions in content identity.
Record metadata differences separately. If any required subtree is
inaccessible or incomplete, do not certify the entire tree as an exact
duplicate.

Tests must cover identical trees with different root names; different
relative layouts; empty directories; repeated identical files at
different paths; unusual filenames; metadata-only differences; partial
scans; and deterministic fingerprints across runs.

Acceptance: equivalent trees produce equal fingerprints under the same
policy, while meaningful structural differences are detected.

### Milestone 6 --- Relationship classifier

Implement explicit relationship types: - Exact file duplicate. - Exact
tree duplicate. - Content-set match. - Strict subset. - Partial
overlap. - Same name but different content. - Relative-path conflict. -
Same underlying file/hard link. - Perceptual image similarity (only if
later implemented). - Inconclusive comparison.

The classifier is read-only. It must never move, delete, copy, or rename
anything.

For every relationship, retain evidence explaining why it was reported.
Never infer equivalence merely from matching names or similar folder
sizes.

Tests must cover all relationship types, nested duplicates, duplicates
inside one selected root, cross-root duplicates, overlapping roots, and
cases where the result must be inconclusive.

Acceptance: classification is deterministic and does not mutate the
filesystem.

### Milestone 7 --- Container detectors

Use a registry of injected detectors. A detector must not mutate files.

#### Git detector

-   Recognize `.git` directories and `.git` files used by linked
    worktrees/submodules.
-   Support or explicitly report bare repositories and nested
    repositories.
-   If Git is available, use a safely invoked `IProcessRunner` for
    read-only Git inspection. Never interpolate user paths into a shell
    command; pass argument arrays directly to the process API.
-   Report working-tree differences, uncommitted changes, untracked
    files, current commits/branches, submodule/worktree context, and
    incomplete inspection where obtainable.
-   Do not equate working-tree equality with
    repository-history/configuration equality.
-   Never merge Git internals with generic directory logic.
-   Every plan that moves or merges a detected repository must carry
    explicit per-operation approval.

#### Saved web-page detector

-   Recognize common HTML/resource-folder conventions conservatively.
-   Inspect local references when practical.
-   Detect shared resources, missing resources, and references outside
    the candidate unit.
-   A resource directory shared by multiple pages must not be moved away
    from the remaining pages.
-   Return a candidate with warnings when the relationship is not
    proven.

Add tests for two identical independent clones; modified and untracked
files; `.git` file form; nested repositories; same tree with different
repository metadata; HTML plus resource folder; differently named saved
copies; shared resources; and incomplete archives.

Acceptance: detector output is evidence-based and every potentially
destructive container operation is review-gated.

### Milestone 8 --- Merge planner

Build merge plans without performing I/O mutations. Support: - A new
destination (default). - An advanced in-place merge mode, not enabled by
default. - Identical files at the same relative path. - Unique files
from either source. - Same path/different content. -
File-versus-directory conflicts. - Metadata differences. - Empty
directories. - Special-container rules.

Default conflict policy is `RequireDecision`. Optional keep-both
renaming must be deterministic and must never overwrite. Preserve unique
content unless the user explicitly excludes it. A plan with unresolved
conflicts is not executable.

Tests: two-way and multi-way merges; unique-file preservation;
identical-content deduplication; conflicting content; file/directory
conflicts; deterministic renamed paths; metadata differences; empty
directories; and rejection of incomplete source trees.

Acceptance: the plan is deterministic, explains all conflicts, and
leaves source trees untouched.

### Milestone 9 --- Action planning and approval gates

Implement these modes: - `PreviewOnly`. - `QuarantineDuplicates`. -
`CreateUniqueCollection`. - `InPlaceMerge`.

Generate a dependency graph of actions. Detect cycles and reject invalid
graphs. If a parent directory is moved as a unit, suppress duplicate
child moves unless a specific valid plan requires otherwise. Do not plan
two incompatible actions for the same source entry.

Approvals must be attached to particular plan/action IDs and bound to
the relevant source identity and expected state. A global "approve all"
setting must not bypass Git review or unresolved conflicts. If the plan
changes, the old approval is invalid.

Tests: nested action suppression, duplicate-action conflict, dependency
ordering, missing approval, stale approval after plan modification, Git
approval gate, unresolved merge conflict, and preview-only no-mutation.

Acceptance: no unsafe plan can be executed just by setting a Boolean
flag.

### Milestone 10 --- Pre-launch validation

Immediately before execution, validate: - Source still exists and has
the expected entry kind. - File identity and expected BLAKE3 digest
still match when applicable. - Source has not changed since planning. -
Relevant parent/container state is still valid. - Destination does not
exist unexpectedly and cannot overwrite unrelated content. - The plan is
internally consistent and all approvals remain valid. - Required paths
are accessible and the operation is supported on this filesystem.

If a precondition fails, mark the action `Stale` or `NeedsReview`; do
not silently recalculate and proceed with the old decision.

Tests: file changed, file replaced at same path, parent renamed,
destination collision, permission change, repository changed, stale
approval, and incomplete preflight.

Acceptance: stale plans cannot mutate the affected entries.

### Milestone 11 --- Execution engine and safe copy/move

The executor is the only component allowed to mutate user data. It must
accept a validated plan and use the injected filesystem and journal.

#### Same-filesystem quarantine

-   Prefer a rename when safe and supported.
-   Do not overwrite destinations.
-   Revalidate source and destination immediately before operation.
-   Verify the resulting path and record the result.
-   Do not follow symlinks.
-   Preserve original path and metadata details in the manifest.

#### Cross-filesystem copy

-   Revalidate source identity and complete digest.
-   Copy to a unique temporary destination using bounded buffers.
-   Hash the destination and compare its complete BLAKE3 digest with the
    expected digest.
-   Preserve supported metadata according to the selected policy.
-   Flush/sync where supported.
-   Publish with a no-overwrite operation.
-   Retain the source; do not delete it as part of cross-filesystem
    copy.
-   Clean up only temporary objects created by this operation, and only
    when ownership is certain.
-   For directory trees, stage and verify the full tree before
    publishing; report platform limitations and partial-recovery states.

Metadata preservation is separate from content verification. Where
relevant, handle permissions, timestamps, ACLs, extended attributes,
resource forks, and Windows alternate data streams according to
documented-in-code platform capabilities. Do not claim full metadata
preservation when unsupported.

Tests must inject failures at every stage: temp creation, read, write,
hash, flush, metadata copy, publication, journal commit, and
verification. Verify that the source remains intact for copy operations
and that unrelated destination content is never overwritten.

Acceptance: incomplete copies are not reported as successful, source
data is retained, and the executor reports actionable recovery states.

### Milestone 12 --- Journal, manifest, and recovery

Use a versioned, durable journal. SQLite is acceptable and recommended
for transactional updates; an append-only journal is acceptable if
implemented with explicit crash-consistency guarantees. Do not rewrite
one JSON file naïvely after every action and assume it is crash-safe.

Record: - Scan/plan IDs and versions. - Source/destination paths and
identities. - Expected digest and metadata. - User decisions and
approval references. - Action dependencies and states. - Start/end
timestamps through injected `IClock`. - Errors, retries, and recovery
instructions. - Final verification state.

On startup/recovery, inspect the actual filesystem before retrying
incomplete operations. A crash may occur after an operation succeeds but
before its completion record is committed. Never blindly repeat a move
or copy based solely on a missing journal completion flag.

Tests: interrupted write, corrupt/truncated journal, crash after copy
but before commit, crash after rename but before commit, temporary-file
cleanup, resume idempotency, and manual-recovery-required cases.

Acceptance: restart recovery determines state conservatively and never
blindly overwrites or repeats a potentially completed action.

### Milestone 13 --- Reports and CLI

Provide a headless CLI that can: - Scan roots. - Run classification. -
Print categorized results and warnings. - Save a machine-readable
report/manifest. - Generate a preview plan. - Execute only a validated
and explicitly approved plan. - Return nonzero exit codes for failures.

Keep report serialization versioned. Test Unicode paths and escaping,
stable IDs, partial-scan warnings, unresolved conflicts, and
serialization round trips. Do not create a GUI yet.

Acceptance: the CLI calls the same public service API that a future
wxWidgets GUI will use.

## 7. Required categories in scan/preview results

Provide distinct categories for: 1. Exact duplicate files. 2. Exact
duplicate directory trees. 3. Partial overlaps and subset relationships.
4. Special containers, including Git and saved web pages. 5. Optional
visually similar images, if implemented later. 6.
Inaccessible/corrupt/unsupported entries. 7. Changed-since-scan entries.
8. Conflicting relative paths. 9. Hard-linked or same-underlying-file
entries. 10. Excluded/protected entries. 11. Empty directories. 12.
Failed or recovery-required operations.

Each category should report the evidence, affected paths, available
actions, warnings, and whether explicit review is required. Never mix
perceptual image matches with exact byte duplicates.

## 8. Cross-platform implementation rules

-   Use `std::filesystem` where its behavior is adequate; isolate
    OS-specific code behind platform adapters.
-   Compile and test on Linux, macOS, and Windows. No CI is required;
    local test commands must be reliable.
-   Use CMake target-based configuration. Avoid global compiler flags
    that break dependencies.
-   Use `std::filesystem::path` and proper native path handling; do not
    manually join paths with `/` or `\\`.
-   Handle case sensitivity as a filesystem property, not a universal OS
    rule. macOS and Windows can use case-sensitive filesystems in some
    configurations; Linux filesystems can have other semantics. Do not
    assume case-folded names are equivalent.
-   Do not use a path's canonicalized target to follow symlinks. Use
    link-aware inspection.
-   Avoid shell commands for filesystem operations. For Git, use a
    process API that passes an executable and argument vector without
    shell interpolation.
-   Handle Windows long paths and file-sharing/locking failures where
    supported.
-   Handle POSIX permissions, device/inode identity, and mount
    boundaries where available.
-   Make metadata capabilities explicit and test them conditionally when
    the platform supports them.
-   Treat unsupported behavior as a typed warning/error, not as silent
    success.

## 9. Code quality requirements

-   Use RAII for file handles, locks, temporary files, and other
    resources.
-   Use `const` correctness and narrow interfaces.
-   Avoid raw owning pointers, global mutable state, and unexplained
    magic constants.
-   Validate all user-provided paths and plan fields.
-   Avoid catch-all exception suppression. Catch exceptions at
    module/application boundaries, translate them into errors, and
    preserve diagnostic context.
-   Never log file contents, secrets, or repository credentials.
-   Avoid executing code found in scanned folders.
-   Keep functions cohesive and reasonably small.
-   Add comments explaining **why** safety checks and non-obvious
    behavior exist, not comments that merely repeat the code.
-   Do not create separate documentation files or CI configuration.
-   Keep third-party dependencies minimal and pinned/reproducible.
-   Add tests for bug fixes and regressions.
-   Do not report a test as passed if it was skipped or could not run;
    state the actual result.

## 10. CMake and test setup

Use modern target-based CMake: - Require C++20. - Define separate
library, CLI, and test targets. - Enable tests with CTest. - Use a
maintained test framework such as Catch2 or GoogleTest, pinned to a
known version. - Find or fetch BLAKE3 in a reproducible way. If
dependencies cannot be downloaded, report the problem clearly and
provide a documented-in-code offline dependency path; do not silently
substitute another hash. - Use `target_include_directories`,
`target_link_libraries`, and `target_compile_features` rather than
global include/link settings. - Add warning flags per compiler (MSVC
versus GCC/Clang). - Support `CMAKE_BUILD_TYPE` for single-configuration
generators and `CMAKE_CONFIGURATION_TYPES` for multi-configuration
generators such as Visual Studio. - Ensure Debug and Release
configurations use separate build directories. - Do not add
continuous-integration workflows.

Test requirements: - Unit tests use fake dependencies and do not mutate
user files. - Integration tests use a unique temporary directory and
clean it through RAII. - Safety/recovery tests inject failures
deterministically. - Add randomized/property-based tests only where they
improve coverage and ensure they are reproducible with recorded seeds. -
Test on the actual OS for platform behavior; a fake filesystem does not
replace integration tests.

## 11. `menu.py` behavior

Create a Python 3 script at the repository root named `menu.py`. It is a
persistent development menu, not part of the C++ runtime.

### 11.1 Menu options

Display at least: 1. Build 2. Run 3. Test 4. Switch between Debug and
Release 5. Install dependencies 6. Create standalone executable/package
7. Exit

After any option finishes or fails, show the menu again automatically,
without asking the user to press Enter or any other key. Exit only when
the user selects Exit or explicitly terminates the process. Invalid
input should show an error and immediately redisplay the menu.

### 11.2 Operating-system detection

Use `platform.system()` and Python's standard library to detect Windows,
macOS, and Linux. Do not ask the user to choose the OS. Print the
detected OS and selected build configuration in the menu.

Use `subprocess.run([...], check=False)` with argument lists, never a
shell command assembled from strings. Use `sys.executable` only when
invoking Python itself. Set the working directory to the repository
root. Capture/display useful output and the actual exit code. Handle
`KeyboardInterrupt`, missing tools, subprocess failures, and unexpected
exceptions without permanently terminating the menu; an explicit Exit
selection is the normal exit path.

### 11.3 Build directory and configuration handling

Use separate build directories, such as: - `build/debug` -
`build/release`

For single-configuration generators: - Configure Debug with
`-DCMAKE_BUILD_TYPE=Debug`. - Configure Release with
`-DCMAKE_BUILD_TYPE=Release`.

For multi-configuration generators, configure once and pass
`--config Debug` or `--config Release` to build/test/run/install
commands. The script must not assume that every Windows installation
uses Visual Studio; detect the selected/default CMake generator and let
CMake report configuration problems.

Keep the active configuration in a small local state file under `build/`
or infer it from a safe default. Do not place generated state in source
directories. Default to Debug.

### 11.4 Build

The Build option must: 1. Check for Python, CMake, and a suitable C++
compiler. 2. Configure the active build directory if it is not
configured or its configuration is stale. 3. Build the project using
CMake's build interface and the active configuration. 4. Display
success/failure and return to the menu without a pause.

Use `cmake --build <build-dir> --config <configuration> --parallel`
where supported. Do not hard-code the output executable path; determine
it from CMake target properties or define a predictable target output
location in CMake.

### 11.5 Run

The Run option must: 1. Ensure the project is configured and built in
the active configuration, or clearly report if building failed. 2. Run
the CLI executable with no arguments or a safe `--help`/demo mode that
does not modify user files. 3. Never launch a destructive scan/move
workflow without explicit command-line arguments and approval. 4. Show
the program's exit code and return to the menu.

Make the CLI's no-argument behavior safe and useful, for example showing
usage and available subcommands.

### 11.6 Test

The Test option must: 1. Configure/build test targets if necessary. 2.
Invoke `ctest --test-dir <build-dir> --output-on-failure`, adding
`-C <configuration>` for multi-config generators. 3. Report passed,
failed, and not-run tests clearly. 4. Return to the menu regardless of
failure.

Do not treat "no tests found" as success. The menu should report that as
a configuration/test setup failure.

### 11.7 Switch Debug/Release

Toggle the active configuration between Debug and Release, persist it
safely, display the new value, and return immediately to the menu. Do
not require a keypress. The next Build, Run, or Test operation should
use the newly selected configuration.

### 11.8 Install dependencies

Provide OS-specific dependency handling, but never use administrator
privileges automatically and never silently change the user's system.

-   First check whether required tools/dependencies are already
    available.
-   Explain which packages are missing.
-   Prefer project-local or user-level dependency installation where
    practical.
-   Offer supported package-manager commands for the detected OS.
-   Ask for explicit confirmation before running a package-manager
    command that installs software or modifies the system. This
    confirmation is allowed; the "no keypress" rule means do not pause
    after menu actions just to dismiss output.
-   Never use `sudo` automatically, never run `apt`, `brew`, `winget`,
    `choco`, or similar install commands without consent, and never run
    package-manager commands on an unsupported OS.
-   If a dependency is already provided by the pinned CMake dependency
    mechanism, explain that and let CMake handle it.
-   Check command availability (`cmake`, compiler, Git if optional Git
    inspection is enabled, Python) and report exact missing tools.
-   Keep dependency versions reproducible. Do not install arbitrary
    latest packages silently.

### 11.9 Create standalone executable/package

The menu option must use CMake's install/package mechanisms rather than
manually copying guessed output paths.

-   Configure/build Release if needed.
-   Run `cmake --install` into a project-local staging directory.
-   If CPack is configured, invoke `cpack` to produce a package
    appropriate to the current OS/generator.
-   On Windows, account for executable suffixes and runtime DLLs.
-   On macOS, account for bundle/runtime dependencies if packaging is
    implemented.
-   On Linux, provide a portable staging directory or a supported
    package format where configured.
-   Do not claim that a binary is universally standalone if it
    dynamically depends on system libraries.
-   Report the exact output path and any remaining runtime
    prerequisites.
-   If packaging is not yet implemented, make the option report that
    clearly rather than pretending success. Implement packaging only
    after the core build/install works and tests cover its packaging
    scripts.

### 11.10 Menu implementation quality

-   Keep the menu script standard-library-only unless a dependency is
    essential.
-   Use functions for each action: `build_project`, `run_project`,
    `run_tests`, `switch_configuration`, `install_dependencies`,
    `create_package`.
-   Use a loop for repeated menu display; do not use recursion.
-   Never call `input()` except for the menu choice and explicit safety
    confirmations.
-   Do not call `os.system`.
-   Print subprocess output and status in a readable way.
-   Use a clear error message when a required executable is missing.
-   Catch errors per menu action so one failure does not terminate the
    menu.
-   Do not wait for a keypress between actions and redisplaying the
    menu.
-   The Exit option should terminate cleanly.
-   Add a small test or validation mode for menu helper logic where
    practical, without requiring interactive input.

## 12. Packaging and runtime considerations

-   Define a predictable CLI target name, e.g. `dupman_cli`.
-   Use CMake install rules for the CLI and any required runtime assets.
-   Do not bundle or install the future GUI toolkit in this phase.
-   Packaging must include the BLAKE3 dependency correctly according to
    its license and linking model.
-   Keep licenses/notices required by dependencies where applicable.
-   Treat "standalone" as platform-specific: verify actual runtime
    dependencies rather than assuming static linking is always possible
    or desirable.
-   Do not create installers that require elevated privileges by
    default.

## 13. Test-case minimums and invariants

Add automated tests at minimum for the following cases. Tests must
assert both classification and filesystem state where applicable.

### Hashing

-   Empty file and known BLAKE3 vectors.
-   Binary content including zero bytes.
-   Same content under different names and directories.
-   Different contents of the same size.
-   File larger than the configured buffer.
-   Read error and partial read.
-   File modified during hashing.
-   Bounded-memory design for large files.

### Scanning

-   Overlapping roots.
-   Hidden files.
-   Symlink to file, symlink to directory, symlink loop.
-   Windows link-like reparse point handling where available.
-   Permission-denied directory.
-   Disappearing file.
-   Unicode and unusual path names.
-   Filesystem boundary policy.
-   Empty directory and empty root.

### Directory relationships

-   Exact tree under different root names.
-   Same content set but different relative layout.
-   Strict subset.
-   Partial overlap with unique files on both sides.
-   Same path/different content.
-   File/directory path conflict.
-   Inaccessible child means result is inconclusive.
-   Metadata-only differences do not change content identity.
-   Duplicate nested inside one selected root.
-   Parent move suppresses conflicting child move.

### Special containers

-   Independent Git clones with identical working trees.
-   Git repository with uncommitted changes.
-   Untracked files and submodules where Git is available.
-   Linked worktree `.git` file.
-   Git approval required for each destructive operation.
-   HTML/resource folder pair.
-   Shared webpage resources.
-   Missing referenced resources.
-   Detector uncertainty requires review.

### Planning and execution

-   Preview does not mutate source or destination trees.
-   Quarantine moves only user-selected copies.
-   Unique collection retains all unique files and leaves sources
    unchanged.
-   Cross-filesystem copy verifies the destination and retains the
    source.
-   Destination collision never overwrites.
-   Source changed after preview blocks execution.
-   Missing approval blocks execution.
-   Unresolved merge conflict blocks execution.
-   Failure at each copy/publish/journal step is reported.
-   Parent and child actions do not conflict.
-   Restart recovery does not blindly repeat a completed operation.
-   Manifest accurately records original paths and final outcomes.

### Menu/build workflow

-   Menu choice dispatches to the right action.
-   Invalid input redisplays the menu.
-   Action failure returns to the menu.
-   Configuration toggle persists and changes the selected build
    configuration.
-   Missing CMake/compiler is reported without crashing.
-   Test command treats zero discovered tests as failure.
-   Package action reports exact output path or a clear
    unsupported/not-yet-implemented status.
-   Exit terminates cleanly.
-   No action waits for an arbitrary "press any key to continue."

## 14. Definition of done

The project is ready for later wxWidgets GUI work only when: 1. All
implemented milestones have tests that prove their stated behavior. 2.
The project builds in Debug and Release with CMake on each supported OS
when tested there. 3. `menu.py` provides the required menu and returns
to it after every action without a dismissal pause. 4. The core API is
GUI-neutral and injected dependencies can be replaced with test doubles.
5. BLAKE3 hashes complete file contents with bounded memory. 6. The
scan/classification report separates exact duplicates, tree duplicates,
partial overlaps, special containers, inaccessible/changed entries,
conflicts, and unsupported entries. 7. The planner cannot silently
discard unique content or overwrite a destination. 8. Cross-filesystem
copy verifies output and retains sources. 9. Git movement requires
per-operation explicit review. 10. The journal and recovery path handle
interrupted operations conservatively. 11. No GUI, CI setup, or separate
documentation files have been added.

At the end of each implementation milestone, report briefly in the
agent's response: - Files created/changed. - Tests added. - Exact
build/test commands run. - Pass/fail/skip counts. - Known limitations or
untested platform behavior. - The next milestone proposed.

Do not state that a platform-specific feature is verified unless it was
actually built/tested on that platform. Continue implementing the next
milestone only after the current milestone's acceptance criteria pass.

Regards,
Navin


