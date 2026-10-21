
Project: Cross-Platform File Deduplication and Backup Consolidation Tool

Act as a senior C++ software architect and developer. Build a robust, maintainable, cross-platform desktop application for finding and consolidating duplicate files across multiple folder backups. The application will primarily target Linux but must also support Windows and macOS. The program should have a dark mode by default but the settings should allow configuring it to a lighter theme. Think about the user journey and plan each step of what should be displayed to the user, in a way that minimises manual effort and decision making for the user. Also, design the initial screen to later include more menu options. For example, the user may later want to add options for finding duplicate videos, duplicate images, finding unique sentences in multiple files, removing useless files like `Thumbs.db`. The program should have a working folder where it stores the duplicate files and metadata related to the duplicates it found and any tempoary external file where it needs to store its working memory. The program should check if it has write privileges and ask the user for it if necessary, and even offer to allow the user to browse for an appropriate folder and create a folder if necessary and use it. The program should remember various such selections as part of its settings so that the next time the program is opened, such details are remembered. Use a rotating log with a page where the user can view the logs and be able to filter to view as per log level and also provide a search bar to search for strings within the logs. Also have an option to switch off logging. Even the log preferences should be part of settings. The length of the log files should be configurable. For any other parameters that can be configurable, provide the option to configure it in Settings. Ensure that memory is used efficiently and no memory leaks should happen. Try to use multiple threads to do folder traversals, file hash generation, etc. Choose free and open source libraries only.
Create a Python script which shows a menu that allows the user to choose to build the project, run the project, toggle debug/release mode (the menu option should mention which mode is currently active, and Debug mode should be the default), clean the build, set a specific operating system to build for (the default is Linux), a menu option to check if all necessary dependencies are present for the operating system (if it is not present, it should give the user a choice to automatically download and install such dependencies) and to exit. Unless the user chooses exit, the script should return back to showing the menu again after performing the menu action the user selected. After any build is done, the program should display the absolute path where the executable is, and the executable name.

1. Technology Stack and Requirements
Use modern C++ (C++20 or later) and Qt 6. The current user is using gcc version 11.4.0.
Use Qt Widgets for the desktop UI unless there is a compelling reason to use QML.
Use Qt Core for filesystem operations, threading, task management, and application infrastructure.
Use Qt SQL with SQLite for persistent storage and resumable processing.
Use Qt Concurrent or an appropriate worker-pool architecture for background processing.
Use QCryptographicHash for SHA-256 hashing.
Use CMake as the build system.
Follow RAII, const-correctness, clear ownership rules, and modern C++ practices. 
Keep the core processing engine independent of the UI so it can be tested and maintained separately.
2. Cross-Platform Filesystem Handling

The application must run correctly on Linux, Windows, and macOS.

Use std::filesystem::path, QFileInfo, QDir, and other appropriate cross-platform APIs.
Never manually construct filesystem paths by concatenating strings with / or \. Use std::filesystem::path operations, QDir, or QDir::filePath().
Treat / and \ as platform-dependent path separators, not as interchangeable characters inside arbitrary filenames.
Use native filesystem APIs through a well-defined abstraction where necessary.
Handle absolute and relative paths, Unicode filenames, spaces, symbolic links, permissions, long paths, and platform-specific filesystem behavior.
Define an explicit symlink policy. By default, do not follow directory symlinks during recursive traversal, and never allow a symlink to bypass the protected-root safety checks.
Account for case-sensitive Linux filesystems and case-insensitive filesystems commonly used on Windows and macOS. Do not blindly lowercase paths or assume that paths differing only by case are identical.
Use canonical paths where they can be resolved safely, but support nonexistent destination paths and handle path-resolution errors.
Detect overlapping roots and avoid duplicate scanning or unsafe operations caused by the same directory being selected more than once.
Handle files being renamed, deleted, or modified while the application is running.
Create a dummy icon file for the application to use as the icon which will be shown in the taskbar. Keep the icon in a folder named `gallery`.

All path comparisons, containment checks, and move operations must be implemented in a centralized, tested filesystem utility layer.

3. User Workflow and Folder Roles

The user selects:

One protected main/reference folder.
One or more candidate folders to consolidate.
A destination folder for duplicates, preferably with a quarantine or review workflow.

Example:

Main folder: A
Candidate folders: B, C
Destination: duplicates

Requirements:

Never automatically move, rename, or delete anything inside the main folder.
Identify identical files across all selected roots, including duplicates between candidate folders.
If a file exists in A, B, and C, keep the copy in A and propose moving the redundant copies in B and C.
If identical files exist only in B and C, retain one deterministically selected copy and propose moving the other.
Allow the user to preview, approve, skip, and undo proposed operations.
Do not scan the destination as a new source during the same job.
Support dry-run mode.
Never permanently delete files automatically.
4. Architecture and Data Model

Implement separate modules for:

Filesystem traversal and metadata collection.
Persistent file catalog.
Exact-content hashing and duplicate grouping.
Folder/subtree analysis.
Optional perceptual image matching.
Decision and review logic.
Background task scheduling and checkpointing.
Safe move, quarantine, recovery, and undo operations.
UI and application orchestration.

Use SQLite as the authoritative persistent catalog. Suggested entities include:

Root

Root ID, absolute path, role, enabled state.

Folder

Folder ID, root ID, parent folder ID, relative path, direct file count, recursive file count, total size, subtree signature, classification.

File

File ID, root ID, parent folder ID, relative path, size, modification time, optional device/inode identifiers, SHA-256 hash, scan status, error information.

DuplicateGroup

Group ID, file size, content hash, and associated file memberships.

FolderMatch

Compared folder IDs, match type, overlap score, review status.

Task

Task ID, task type, target ID, status, attempt count, error information.

ActionLog

Source path, destination path, expected file identity, operation type, status, timestamp, recovery information.

Use foreign keys, appropriate indexes, schema migrations, and prepared SQL statements. Avoid storing large lists of IDs in a single database column.

5. Efficient Exact Duplicate Detection

Implement a staged comparison algorithm:

Traverse the selected roots and record filesystem metadata.
Group files by size.
Skip hashing files that have no possible same-size match within the selected comparison scope, unless the user explicitly requests complete hashing.
Hash potential duplicates using SHA-256 and streaming reads with bounded memory.
Persist successful hashes and reuse them when file identity and metadata validation justify it.
Group files by (size, SHA-256).
Build duplicate groups across the main and all candidate roots.
Apply root-protection and keeper-selection rules to determine proposed actions.

Requirements:

Never load entire large files into RAM to hash them.
Detect files that change during hashing and invalidate or retry their hashes.
Record inaccessible files and failed tasks instead of silently ignoring them.
Support bounded-memory database queries and batch processing.
Use appropriate SQLite indexes, including an index on file size and hash.
Treat SHA-256 as a practical exact-content fingerprint, with optional byte-for-byte verification before high-assurance moves.
Make hashing concurrency configurable to avoid overwhelming disks.
Do not use file names, modification times, or sizes alone as proof of identical content.
6. Folder and Project Structure Analysis

The program must not automatically scatter the contents of structured directories into the duplicates destination.

Examples include Git repositories, websites, application projects, and other nested folder structures.

Implement:

Deterministic subtree signatures derived from sorted relative paths and file content fingerprints.
Folder summaries including recursive file count, total size, and duplicate coverage.
Detection of exact duplicate subtrees.
Detection of partial subtree overlap and identical content arranged under different paths.
An overlap score that helps prioritize candidates for user review.
Conservative recognition of structured projects, including Git repositories containing .git, websites containing HTML/CSS/JavaScript/assets, and common software project manifests.

Do not assume that repositories with identical working files have identical Git history or configuration.

When a folder is an exact duplicate subtree, offer the user the option to move the entire subtree. When only part of a subtree matches, or the folder is a Git repository or other structured project, request explicit review before moving anything that could damage its structure.

Folder overlap scores are evidence for review, not sufficient authorization for destructive actions.

7. Persistent Jobs, Pause, and Resume

The application must handle very large collections and allow the user to close it and resume later.

Persist scan progress, discovered files, hashes, comparison results, task statuses, and pending actions in SQLite.
Use a persistent task queue with statuses such as pending, running, completed, failed, and awaiting review.
Make tasks idempotent where practical.
Checkpoint progress regularly.
On restart, recover interrupted tasks and reconcile incomplete filesystem operations.
Revalidate file metadata and hashes when necessary because files may change between sessions.
Invalidate dependent folder signatures and comparison results when their inputs change.
Support pause, resume, and cancellation without corrupting the database or filesystem.
Use transactions for database updates, but do not assume SQLite transactions make filesystem moves atomic.
Do not depend on a single traversal cursor to resume a scan.
8. Safe File Operations and Recovery

Before executing any approved move:

Revalidate the source file or directory.
Verify that the source is not the protected main root or any descendant of it.
Verify that the destination is not the source itself or a descendant of the source.
Check destination collisions and determine a safe, deterministic naming strategy.
Ensure the operation cannot recursively include the destination in its own source.
Persist the intended operation before performing the move.
Execute the move using safe filesystem operations.
Verify the result and persist the final operation status.

Use rename-based moves when appropriate. For cross-filesystem moves, copy and verify the destination before removing the source, and ensure interrupted copies can be safely recovered.

Maintain enough information to undo completed operations. So for every operation performed, there should be a screen where the user can review the files or folders moved, and click a button to undo the action. Handle application crashes between filesystem operations and database commits by reconciling the actual filesystem state on restart.

Centralize safety checks so that no UI component or worker can bypass them.
The file and folder searching has to be designed in a decoupled way so that the same algorithms can be re-used for Perceptual Image Matching.

9. Perceptual Image Matching

Design image similarity as a separate, optional module that can be added after exact duplicate detection works reliably.

Support a future implementation of perceptual hashes such as pHash, with algorithm versioning and Hamming-distance-based similarity thresholds.

Keep exact-content groups separate from perceptual-similarity groups.
Do not automatically treat visually similar images as exact duplicates.
Use indexed candidate retrieval rather than comparing every image against every other image.
Require review before acting on perceptual matches.
Keep the exact-deduplication engine independent of image-specific libraries.

10. Size reduction: Provide an option to allow the user to search a specified for images and videos that can be made smaller in size. 
For images for example, an image may be 2MiB in size but it can still look good enough if reduced to 600KiB by reducing it from a 3000 pixel width to maybe 1024 pixel width. The algorithm shoudl be able to detect if there is text in the image. Because if there is text, then it becomes important to scale the image only as much as would allow the text to be clearly seen. So the user should be allowed to specify what size they consider to be large enough that the algorithm should highlight it to the user, and display a scrollable area where the user is shown a grid of thumbnails of all such found images along with their width and height displayed below the image and information about whether the image contains text. If the user clicks any image the user should be shown a modal where the user can view what the image would look like when shrunk by the suggested size, and the user could choose an appropriate size. There should also be options to remove metadata like EXIF data etc from the images. Consider what kinds of tools the user would need and how the process could be automated to resize multiple images at once. Also whether to do it in-place or by making a copy first.
For videos there should be a similar option by using ffmpeg, to reduce the size of multiple videos and perhaps even be able to detect if there is text in any of the frames.

Build a practical Qt desktop interface for all of this.

