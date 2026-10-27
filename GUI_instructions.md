# GUI Implementation Instructions for the Duplicate Manager

## Purpose and relationship to `instructions.md`

Implement a cross-platform dark-themed GUI for the existing Duplicate Manager using **wxWidgets**. This file is the GUI implementation specification. Before changing code, read `instructions.md` and inspect the actual repository, public backend headers, tests, build system, and `menu.py`. Do not assume a backend API exists just because it is mentioned in a plan: use the real implemented API, and extend it with tests when needed.

Implement this as a series of reviewable milestones. After each milestone, leave the project buildable, run relevant tests, launch the application if possible, summarize what changed and what the user should manually verify, and wait for early user feedback before moving to the next milestone. Do not implement all tabs at once.

Do not add CI or separate documentation files. Put clear comments in the code, including GUI code. This file is the specification; do not create additional explanatory documents unless explicitly requested.

## Global requirements

1. **Comment existing code first.** Inspect every existing source/header/script/test file. Add comments that help a non-technical reader understand the purpose of each file, class, function, important variable, major branch, and safety check. Explain why non-obvious behavior exists. Do not change behavior merely to add comments. Keep comments accurate and concise; do not comment every punctuation mark or merely repeat each line. Add useful comments to every new GUI component as it is created. Preserve all existing tests and behavior.
2. **Use wxWidgets** for the GUI. Do not replace the existing C++ core or duplicate it in another language. Python is only for the existing developer menu unless the repository has an explicit, tested reason otherwise.
3. **Dark theme.** Provide a consistent dark palette, readable text, clear focus indicators, and adequate contrast. Do not assume one background-color setting themes every native control on every OS. Use wxWidgets controls that can be styled consistently where needed. Respect system accessibility and font scaling where practical.
4. **Cross-platform.** Target Linux, macOS, and Windows. Use CMake to discover/link wxWidgets and isolate OS-specific paths, cache locations, file operations, thumbnail handling, and packaging. Do not hard-code path separators or executable suffixes. Test on the actual OS when available; never claim an untested platform is verified.
5. **Keep UI responsive.** Each tab's long-running task must continue while the user switches tabs. Use a controlled background-task manager, cancellation tokens, and worker threads (`std::jthread` or equivalent) where appropriate. Never access or mutate wxWidgets controls from a worker thread. Deliver progress/results through `wxQueueEvent`, `CallAfter`, or an equivalent safe UI-thread mechanism. Avoid unbounded thread creation; use a bounded task executor or managed worker per operation/tab.
6. **Snapshot settings.** When a task starts, capture an immutable settings/input snapshot. Changes while it runs apply to new tasks only, except explicitly live-safe presentation filters. Show relevant settings in task details.
7. **Use backend interfaces.** The GUI calls the backend's public GUI-neutral service facade; it must not reimplement scanning, hashing, classification, merge planning, validation, execution, undo, or journaling in widget handlers. If required functions are absent, add them to the backend with DI and tests before wiring controls.
8. **Safe defaults.** Preview before destructive/bulk operations. Never silently overwrite. Show affected paths and conflicts. Prefer quarantine over permanent deletion. Permanent operations and in-place image resizing need clear labels and confirmation.
9. **Understandable UI.** Use clear language, concise explanations, contextual help icons, meaningful empty states, keyboard navigation, and visible progress. Avoid showing raw hashes/internal IDs as the main explanation. Put diagnostic data in expandable “Technical details”.
10. **Errors are normal results.** Handle invalid input, inaccessible paths, changed files, corrupt history, missing codecs/dependencies, disk-full errors, cancellation, unsupported operations, and partial completion. Show an actionable explanation; preserve details in logs when enabled.
11. **Scrollable, resizable tabs.** Main window has a horizontal tab row at the top. Each tab's content scrolls both vertically and horizontally and expands/reflows with window resizing. Prefer `wxScrolledWindow` or a tested equivalent and sizers, not fixed coordinates.
12. **Per-task history.** Every task initiated from any tab appears in a task-history dropdown/list with task name, date/time, status, tab/operation type, and undo availability. Selecting it offers details, Undo when safely supported, and Delete history metadata as a separate action. Deleting metadata must not undo the operation or delete affected files. Warn that it may remove undo capability.
13. **Persistent data locations.** Use platform-appropriate per-user config/data/cache directories via a tested adapter. Keep disposable thumbnails/temp files in cache, but durable manifests/journals required for undo in application-data storage that the OS will not ordinarily clear automatically. Never store them in the source tree or current working directory.
14. **Honest status.** Do not show “Completed” until backend confirmation. Label partial results “Partially completed” with succeeded/failed/skipped/recovery-required counts.

## Milestone 0 — Audit, comments, and GUI integration

1. Read `instructions.md`; inspect the actual source tree, CMake targets, tests, service facade, journaling, BLAKE3 and image-hash implementation.
2. Add reader-friendly comments to all existing code, including `menu.py`, without changing behavior. Explain purpose and safety rationale; avoid noisy line-by-line paraphrase.
3. Run tests to prove comment-only changes did not regress behavior.
4. Choose a wxWidgets version supported by target platforms and use CMake discovery/dependency handling. Do not make wxWidgets a dependency of the core library or CLI.
5. Add a separate GUI target, e.g. `dupman_gui`, linked to the core and wxWidgets. Core/CLI/tests must still build without the GUI dependency.
6. Add a GUI-neutral controller/adapter and progress/event model. Keep wxWidgets types out of public backend headers.
7. Add a tested platform adapter for config/data/cache directories.
8. Show a minimal dark window with five horizontal tabs:
   - Duplicate Files
   - Unique Sentences
   - Useless Files
   - Resize Images
   - Settings

**Acceptance/user feedback:** Existing tests pass; core and CLI build without wxWidgets; GUI builds when wxWidgets is installed; window theme/tabs resize properly. Ask the user to review the appearance and labels before continuing.

## Milestone 1 — Main window, scroll framework, task manager

### Layout
- Use a horizontal top tab row such as `wxNotebook` with a tested dark-theme approach; do not use a left navigation rail.
- Each page has a vertically and horizontally scrollable content panel, with sizers so controls adapt to resizing.
- Use a consistent page title, short explanation, task status/progress and history selector.
- Keep long paths and tables inside scrollable areas or expandable details; provide useful minimum sizes and keyboard focus behavior.

### Background tasks
Implement an injected `TaskManager`/`TaskRunner` with task ID, tab/operation, display name, start time, immutable input/settings snapshot, cancellation state, progress, result, error and status. Include queued, running, cancellation requested, cancelled, completed, partially completed, failed, needs review and recovery required states.

- Allow operations in different tabs to continue concurrently within a configurable limit.
- Protect shared resources. Detect overlapping source/destination path scopes and warn or serialize conflicting mutations.
- Switching tabs never cancels work.
- Workers never retain unsafe widget references. Marshal progress to the UI thread and coalesce excessive updates.
- Cancellation is cooperative. Check it before non-interruptible mutations and journal state before/after them.
- On close with active tasks, offer to cancel and wait, keep the application open, or exit after tasks finish. Never terminate a worker mid-mutation without recovery handling.
- Task history must recover incomplete operations on next launch.

When a task starts, pass a settings snapshot to the backend. Never let a settings edit alter an active task unexpectedly.

### Tests
Test dispatch, state transitions, completion/failure/cancellation, settings snapshot isolation, concurrent tasks, overlapping-path safety, and progress delivery. Test task logic independently of wxWidgets.

**Acceptance/user feedback:** Run a long fixture scan, switch tabs, resize the window and verify it continues. Ask for feedback on navigation/progress/history before adding tab operations.

## Milestone 2 — Settings, rotating logs, history integrity

### Settings
Store settings in a versioned format under the user config directory. Validate values and save atomically. On corruption, preserve the bad file when safe, report the issue, load safe defaults, and allow saving corrected settings. Provide Reset to defaults with confirmation and a summary.

### Logs
Implement rotating logs with:
- Enable/disable.
- Log level (Trace/Debug/Info/Warning/Error/Critical as supported).
- Maximum file size and number of rotated files or equivalent retention.
- Log path display and safe “Open log folder”.
- Settings “Logs” section with level filter, substring search, refresh and clear-with-confirmation.
- Bounded/paged viewing; do not load an arbitrarily large log in memory.
- Logging disabled means no ordinary application log writes; safety-critical journals must still work.
- Never log file contents, secrets, access tokens or repository credentials. Handle malformed log lines and files rotated while viewing.

### Task history and corrupt metadata
- Load manifests independently so one corrupt record cannot hide all history.
- Validate schema/version/IDs/state; show which record failed and why, skip only that record and continue.
- Never trust history blindly for undo. Revalidate current file identity/content/path before acting.
- “Delete metadata” is not “Undo”; confirm and explain that undo may become impossible. It must never change source/output files.
- Do not casually delete metadata for running or recovery-required tasks.
- Irreversible tasks must show why undo is unavailable.

### Settings controls
Use a typed settings model and clear labels, defaults, validation, and tooltips. Include relevant options for:
- General: defaults, concurrency, confirmation behavior, dark theme.
- Scanning: hidden entries, filesystem boundaries, hash buffer, digest verification, protected paths.
- Duplicate handling: quarantine/collection destinations, image similarity threshold.
- Unique sentences: encoding, whitespace policy, output format.
- Useless files: preset selection, patterns, case behavior, destination.
- Resizing: default mode, dimensions/percentage, output folder, format/quality policy.
- Tasks/history: durable journal location, history retention, thumbnail cache.
- Logs: enablement, level, rotation, retention, search and open folder.

Do not expose unsafe values (zero buffer, unbounded worker count, negative retention). Explain that settings changes apply to new tasks. Keep logs independent from the journal.

### Tests
Test settings round trips, corrupt/missing/unsupported versions, validation, save failure, rotation, disabled logs, search/filter, bounded reads, corrupt records, and metadata deletion not mutating files.

**Acceptance/user feedback:** Configure settings, search/filter logs and inspect history. Simulate a corrupt manifest; app must continue. Ask for feedback on setting names/defaults.

## Milestone 3 — Duplicate Files tab

Read `instructions.md` and actual backend APIs before wiring the tab. Reuse tested BLAKE3 classification; do not implement business logic in GUI handlers.

### Inputs and scan
- Multi-root picker; add/remove/reorder/clear roots.
- Show supported scan options: hidden entries, filesystem boundaries, exclusions/protected paths.
- Buttons: Scan and analyse, Cancel scan, Preview plan.
- Show progress (entries, bytes hashed, elapsed time/stage where available).
- Explain inaccessible entries, skipped links, changed files and partial scans. Incomplete scans cannot prove everything is unique.
- Each scan creates a task/history record without erasing older results.

### Grouped results
Provide category filters for:
1. Exact duplicate files.
2. Exact duplicate directory trees.
3. Same content set/different layout.
4. Strict subsets/partial overlaps.
5. Same relative path/different content.
6. Git and other special containers.
7. Inaccessible, unreadable, changed or unsupported entries.
8. Hard links/same underlying file.
9. Empty directories.
10. Excluded/protected entries.
11. Recovery-required items.

Group related entries rather than showing a flat wall of rows. Each group shows reason/evidence, number of copies, paths, file kind, size, metadata differences, confidence/limitations, warnings, available actions and whether review is required. Show digest only in technical details. Add filters/search by path, extension, category, size range and status.

### Decisions and tree comparison
- Let the user select which duplicate copy to keep and which to quarantine. Never select all copies for removal by default.
- Suggestions may consider project context but must be labelled suggestions and never trigger mutation automatically.
- Explain that identical bytes at different project paths may serve different purposes.
- Distinguish hard links; moving one path may not reclaim space.
- Tree view supports expand/collapse and a legend for identical, unique, conflicting, metadata-different and inaccessible entries.
- Side-by-side tree comparison shows relative path, kind, content status, size, metadata differences and conflict state.
- For partial overlap, show shared, left-only, right-only and conflicting entries. Never hide unique files inside a “duplicate folder” summary.
- New destination is default for merge. Preview the entire plan. In-place merge is advanced, warned, explicitly confirmed and backed by a recovery plan.
- Incomplete trees disable actions that require certainty or require a conservative backend-supported review.

### Perceptual image hash and previews
- Present perceptual similarity separately from exact BLAKE3 matches. Use an existing tested backend service or implement it backend-first with DI/tests before exposing it.
- Similarity threshold has safe bounds and explains false positives. Never auto-delete visually similar but non-identical images.
- Generate image thumbnails off the UI thread, cache in disposable cache, and enforce file-size, dimension and decoded-pixel limits.
- Decode failure shows placeholder and explanation. For videos, show a thumbnail only if a safe, supported thumbnail backend exists; otherwise show an icon/metadata. Never execute arbitrary media files.
- Image comparison supports side-by-side view, zoom-to-fit, dimensions, file size and explicit “exact duplicate”/“visually similar” labels.
- Thumbnails are presentation only, not proof of equality.

### Actions
- Preview is non-mutating.
- Quarantine moves selected entries to a chosen destination and records original path, identity, metadata, digest and action status. Never permanently delete by default.
- Unique collection must clarify structure-preserving vs global content-deduplicating behavior. Default to preserving structure where collapsing paths could break projects.
- Git move/merge requires per-operation explicit approval and summary of working tree, untracked files, commit/branch differences, worktrees and submodules where available.
- Revalidate immediately before action. Changed sources invalidate affected actions and require re-scan/re-plan.
- Confirmation summarizes source count, destination, estimated data volume where reliable, warnings and undo limits.
- Report progress, cancellation, partial completion and recovery state. Cross-filesystem copy verifies destination and retains source.
- On completion, provide history details, Undo if safe, and Open destination.

### Tests and acceptance
Test exact duplicates, partial overlap, incomplete scans, stale plans, Git approval, perceptual category separation, thumbnail errors, cancellation and history. Manually scan a fixture, inspect tree, preview, then quarantine one chosen duplicate and undo it. Ask for feedback before remaining tabs.

## Milestone 4 — Unique Sentences tab

### Input
- Add multiple text files and multiple pasted multiline strings; label/remove each source independently.
- Detect encoding when possible; ask for encoding when uncertain. Warn/skip binary-looking files; preserve sources.
- Clipboard paste is an ordinary text input, not a shell or executable input.

### Exact sentence handling
The purpose is exact duplicate removal, not semantic similarity.
- Define deterministic sentence-boundary behavior in code comments. Regex-only splitting must not be advertised as linguistically perfect.
- Default comparison key uses extracted sentence text with only explicitly selected boundary-whitespace trimming. Do not lowercase, stem, paraphrase or normalize punctuation silently. Internal whitespace remains significant unless the user explicitly selects a tested alternative.
- Keep first occurrence in selected source order by default.
- Output one sentence per line; explain original paragraph layout is not preserved.
- Handle abbreviations, decimals, initials, quotes, Unicode punctuation and line breaks conservatively. Avoid silently dropping ambiguous text.
- Preview extracted sentences, unique/duplicate counts, source provenance and why a sentence matched. Allow correction/skip for ambiguous splits if implemented.
- Do not put sentence content into logs by default.

### Output
- Standard Save dialog. Never overwrite without explicit confirmation.
- Write temporary output and atomically publish where supported. Failed writes must not be presented as success or leave a misleading partial output.
- Record sources/labels, encoding and extraction policy, counts, output path/digest and status.
- Undo is usually unavailable because this creates an output; optional removal must verify the output still matches recorded identity/content and require confirmation.

### Tests and acceptance
Test repeated sentences within/across files, case/punctuation differences, whitespace policy, Unicode, quotes, abbreviations, decimals, blank lines, malformed encoding, clipboard input, output collision/failure, stable order/provenance and source immutability. User can preview/save correct output. Ask for feedback on splitting/whitespace behavior.

## Milestone 5 — Useless Files tab

### OS-aware presets
Show OS-labelled checkbox groups; presets are opt-in and available even for other OSes' files in copied folders. Verify each preset's relevance before enabling it.
- Windows examples: `Thumbs.db`, `ehthumbs.db`; `Desktop.ini` must be described as configuration metadata and not selected by default.
- macOS examples: `.DS_Store`; `._*` AppleDouble sidecars only when explicitly selected and explained.
- Linux/cross-platform examples: `Thumbs.db` when found, plus only well-understood generated cache patterns.
Never label potentially useful metadata “useless” without a warning.

### Custom patterns/help
Provide a multiline comma-separated pattern input and help icon. Explain:
- Exact name: `Thumbs.db`
- Hidden name: `.bashrc`
- Wildcard: `*.txt`
- Prefix: `.bash*`
- Suffix: `*_cache`
- Commas separate patterns; state clearly if literal commas cannot be escaped in version 1.
- Matching applies to the filename component, not arbitrary path substrings, unless path matching is explicitly added.
- State case sensitivity policy.
- Symlinks/reparse points are not traversed.
- Patterns are glob patterns, not shell commands or executable regexes.

Use a tested glob library or well-tested matcher. Never use shell expansion or `system()`. Validate malformed patterns before scanning.

### Scan and quarantine
- Recursively scan chosen roots using the safe backend scanner.
- Group matches by pattern/folder. Show full path, size, time if available and matching reason.
- List each file once even if multiple patterns match; show all matching patterns.
- Provide select all/none, per-file selection and count/size summary.
- Require destination. Prefer quarantine outside scan roots. If destination is inside a root, warn and exclude its subtree.
- Move selected files instead of deleting. Preserve relative path context; collisions require a decision or deterministic keep-both name, never overwrite.
- Journal original/destination paths, identity, digest where available, metadata and outcome.
- Undo validates that originals are free or offers safe alternate restore location; never overwrites unrelated files.
- Files changed after preview are skipped as stale. Continue safely after individual permission failures.

### Tests and acceptance
Test exact/hidden/wildcard names, malformed patterns, overlaps, case policy, Unicode, nested roots, symlink exclusion, destination-inside-root, collisions, stale file and undo. `Desktop.ini` must not be included unless explicitly selected. User can scan, review, quarantine and undo; ask for feedback on presets/help.

## Milestone 6 — Resize Images tab

### Discovery
- Select root folder; recurse through subfolders using the link-safe scanner.
- Show images grouped by folder with selection checkboxes, type, dimensions, file size and status.
- List only formats the integrated decoder/encoder actually supports.
- Explicitly handle animated/multi-page formats, alpha, color profiles, EXIF orientation and metadata preservation. If unsafe or unsupported, warn and skip rather than silently degrade.
- Apply file-size/pixel limits before decode to defend against malformed files/decompression bombs.

### Resize controls and preview
Offer:
1. Target maximum width and height, preserving aspect ratio by default and not upscaling by default.
2. Percentage, such as 50%, with validated bounds and nonzero dimensions.
Explain behavior for already-small images. Preview selected image off the UI thread, side-by-side original/resized, with dimensions and actual/estimated output size clearly labelled. Do not promise exact output size from estimates.

### Output modes
- **Create copies (default):** create `resized_images` under selected root unless user chooses another path. If it exists, select a non-conflicting path or ask; never overwrite. Exclude output subtree from future input discovery. Show full output path prominently.
- **Resize in place:** label destructive and generally irreversible; require strong confirmation. Write a temporary sibling, verify decodability/dimensions, preserve supported metadata, then atomically replace only if safe. Disable for unsafe format/platform combinations.
- Never replace original until output is complete and verified. Undo requires a verified original backup in the journal; otherwise say unavailable.

### Execution
- Preview selected image first; do not eagerly decode every image at full size.
- Show batch count, destination, resize rule, format/quality policy, reliable storage estimate, skipped formats and conflict policy.
- Show per-file status and progress. One failed image must not crash the batch.
- Record input digest/dimensions, settings, output path/digest, metadata-preservation results and status.
- Source changes after preview cause a skip and refreshed preview request.

### Tests and acceptance
Test landscape/portrait/square/small/corrupt/unsupported images, alpha, animation/multipage policy, EXIF orientation, both resize modes, no-upscale, collision, destination-inside-root, encode failure, disk full, stale source and in-place recovery. Assert dimensions and source preservation in copy mode. User can preview and create copies, then separately test in-place on disposable fixtures. Ask for feedback.

## Milestone 7 — Settings integration

Use a typed settings model and validation. Add controls for every genuinely configurable parameter introduced, with label, default, allowed range and plain-language help. Sections:
1. General: default paths, concurrency, confirmations, dark theme.
2. Scanning/duplicates: hidden entries, filesystem boundaries, hash buffer, digest verification, protected paths and similarity settings.
3. Unique sentences: encoding, whitespace policy, extraction behavior, output format.
4. Useless files: presets, patterns, case behavior and destination.
5. Resizing: default mode, bounds/percentage, output folder, format/quality policy.
6. Tasks/history: durable journal location, history retention, cache/thumbnail policy and undo retention.
7. Logs: enablement, level, rotation, retention, search and open folder.
8. Advanced: only settings with a clear user benefit.

Provide Save/Apply and Reset. Validate inline. Explain safety consequences. Show how many tasks are using earlier settings. Running tasks retain their snapshots. Logging off must not disable journals.

Tests cover validation, snapshot isolation, migrations, reset persistence, corruption recovery and journal independence. Ask for feedback on settings layout/defaults.

## Milestone 8 — Unified task history, undo, metadata deletion

### History
Show task name, local date/time and timezone, tab/operation, source labels/paths, destination, status, outcome counts, undo availability/reason, warnings and technical details. Support filtering/sorting by date, tab, status, name and undo availability. Load large manifests incrementally.

### Undo
- Ask backend for eligibility and preview plan.
- Show what will be restored/moved/removed and any conflicts.
- Revalidate paths, identities, hashes and destination conditions.
- Never overwrite an unrelated file at an occupied original path; offer a new restore location or leave unchanged.
- For copy operations where source was retained, undo may remove the generated copy only after verifying it matches the recorded output and user confirms.
- Permanent actions/in-place resize without backup show why undo is unavailable.
- Partial undo failure retains journal and provides recovery information.

### Delete metadata
Separate from Undo. Explain metadata deletion does not reverse operations and may remove undo capability. Disallow casual deletion for running/recovery-required tasks. If undo is available, require the configured explicit confirmation or undo-first policy. Metadata deletion must not alter files.

Tests cover undo preview, stale output, restore conflicts, partial failure, metadata-only deletion, recovery-required restrictions and corrupt-record isolation. Ask for feedback after the user tries undo and metadata deletion.

## Milestone 9 — Final integration and platform hardening

- All tabs use shared theme, task manager, settings, logger, journal, progress and history services.
- All long operations are off the UI thread. No worker references a destroyed widget.
- Cancellation/shutdown preserves journal consistency.
- Test keyboard navigation, focus, screen-reader labels where supported, scaling, horizontal/vertical scrolling, long paths and readable help.
- Use plain-language summaries with expandable diagnostics; escape filenames when rendering.
- Verify settings/history/journal persist across restarts; one corrupt record never hides others.
- Ensure log disabling never disables safety-critical journals.
- Use disposable test fixtures only; automated tests must never operate on user folders.
- Test on each available OS: CMake builds, dark theme, native dialogs, app-data/cache paths, Unicode/long paths, link safety, background progress/shutdown, thumbnail errors, log rotation, corrupt history and packaging.
- Report untested OS-specific behavior honestly.

### Final acceptance
The GUI is ready only when:
- All five tabs work and call tested GUI-neutral backend APIs.
- Tasks continue when switching tabs; running tasks use settings snapshots.
- User can preview operations, inspect history and undo supported actions.
- Corrupt settings/history/log records do not crash the app.
- Duplicate tab separates exact BLAKE3 matches from perceptual image similarity.
- Useless-file cleanup moves files to a chosen destination by default.
- Image copy mode retains originals; in-place mode is clearly destructive and safe.
- Sentence tab removes exact duplicates under a deterministic, code-commented policy.
- All safety-relevant actions are journaled; disabling logs leaves journals intact.
- Tests pass and platform support is reported honestly.

## Required milestone report

At the end of each milestone, report:
1. Implemented behavior.
2. Files created/changed.
3. Tests added and exact commands run.
4. Pass/fail/skip counts.
5. Manual steps the user can try.
6. Known limitations and untested platforms.
7. Specific questions or decisions needed before proceeding.

Do not continue automatically past a milestone that asks for feedback. The goal is visible progress and early user correction, not one enormous unreviewable code drop.

## Update for Milestone 8 - Backend integration
When implementing Milestone 8, the GUI already has the task history UI component (added in Milestone 1). The backend integration requires:
- Implementing persistent storage for task history manifests in platform-appropriate directories
- Loading/saving task history with proper validation and corruption handling
- Implementing undo functionality that revalidates file state
- Implementing metadata deletion separate from undo
- Ensuring task history recovery on application launch

## Milestone 2 Updates
- Settings system implemented with versioned JSON format in platform config directory
- Logger with rotating log support (disabled by default, doesn't affect journals)
- Platform paths for config/data/cache directories
- Task history table auto-adjusts width to fit window
