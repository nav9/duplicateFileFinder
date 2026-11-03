#pragma once

#include "dupli/filesystem.hpp"
#include "dupli/hasher.hpp"
#include "dupli/result.hpp"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace dupli {

// Injected clock so journal timestamps are deterministic in tests.
class IClock {
public:
    virtual ~IClock() = default;
    /// ISO-8601 UTC timestamp, e.g. 2026-11-01T10:00:00Z.
    virtual std::string now_iso8601() = 0;
};

class SystemClock : public IClock {
public:
    std::string now_iso8601() override;
};

class FakeClock : public IClock {
public:
    explicit FakeClock(std::string fixed) : m_fixed(std::move(fixed)) {}
    std::string now_iso8601() override { return m_fixed; }

private:
    std::string m_fixed;
};

enum class JournalState { Started, Completed, Failed, RolledBack };

std::string to_string(JournalState state);
bool parse_journal_state(std::string_view text, JournalState& out);

struct JournalRecord {
    std::string id;
    std::string action_id;
    std::string kind;
    std::filesystem::path source;
    std::filesystem::path destination;
    std::string expected_digest;
    JournalState state = JournalState::Started;
    std::string error;
    std::string timestamp;
};

// Journals are append-only: every state transition is a new record with the same
// id. That makes a crash mid-write lose at most the final line (which readers
// skip as truncated) and never corrupt earlier history.
class IJournal {
public:
    virtual ~IJournal() = default;

    virtual Result<void> append(const JournalRecord& record) = 0;
    virtual Result<std::vector<JournalRecord>> entries() const = 0;

    /// Legacy convenience API. Returns the new journal id.
    std::string start_action(const std::string& action_id,
                             const std::filesystem::path& source,
                             const std::filesystem::path& destination,
                             const std::string& kind = "copy",
                             const std::string& expected_digest = "");
    Result<void> complete_action(const std::string& journal_id);
    Result<void> fail_action(const std::string& journal_id, const std::string& error);
    Result<std::vector<std::string>> recover_pending();

    /// Id for a new journal record. Public so callers that build a full
    /// JournalRecord (executor) get ids from the same sequence.
    virtual std::string next_id();

private:
    Result<void> append_state(const std::string& journal_id, JournalState state,
                              const std::string& error);
    int m_counter = 0;
};

/// In-memory journal used by tests and preview-only runs. It is NOT durable; use
/// FileJournal whenever something can actually be mutated.
class SimpleJournal : public IJournal {
public:
    Result<void> append(const JournalRecord& record) override;
    Result<std::vector<JournalRecord>> entries() const override;

private:
    std::vector<JournalRecord> m_records;
};

/// Append-only JSON-lines journal on disk. Each record is flushed as it is
/// written; a torn final line is ignored when reading so recovery never acts on
/// a half-parsed record.
class FileJournal : public IJournal {
public:
    FileJournal(std::filesystem::path path, IClock& clock)
        : m_path(std::move(path)), m_clock(clock) {}

    Result<void> append(const JournalRecord& record) override;
    Result<std::vector<JournalRecord>> entries() const override;

    /// Ids continue after the highest id already in the file, so records from
    /// separate program runs never collide (a colliding id would make recovery
    /// and undo fold two different operations together).
    std::string next_id() override;

    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
    IClock& m_clock;
    long long m_next_id = -1; // -1 = not seeded from the file yet
};

/// Fold append-only records to the newest record per journal id, preserving the
/// order in which ids first appeared.
std::vector<JournalRecord> latest_records(const std::vector<JournalRecord>& records);

enum class RecoveryAction {
    /// Nothing was written yet or the source still exists and the destination
    /// does not: retrying the action is safe.
    RetrySafe,
    /// The destination exists and the source is gone for a move: the operation
    /// very likely completed before the journal record was flushed. Do not retry.
    AlreadyCompleted,
    /// Both or neither exist in a state that cannot be resolved automatically.
    NeedsManualReview
};

struct RecoveryFinding {
    JournalRecord record;
    RecoveryAction action = RecoveryAction::NeedsManualReview;
    std::string explanation;
};

struct RecoveryReport {
    std::vector<RecoveryFinding> findings;
    size_t needs_attention = 0;
    bool clean() const { return needs_attention == 0; }
};

struct UndoOutcome {
    std::string journal_id;
    std::string action_id;
    bool undone = false;
    std::string message;
};

struct UndoReport {
    std::vector<UndoOutcome> outcomes;
    size_t undone = 0;
    size_t failed = 0;
    size_t skipped = 0;
    bool complete() const { return failed == 0; }
};

// Inspects the real filesystem before recommending anything. A crash can happen
// after an operation succeeded but before its completion record was written, so
// the journal alone is never trusted.
class RecoveryEngine {
public:
    RecoveryEngine(IFileSystem& fs, IHasher& hasher) : m_fs(fs), m_hasher(hasher) {}

    Result<RecoveryReport> inspect(const IJournal& journal) const;

    /// Reverses completed journal entries in reverse order. Copies are removed
    /// (their source was retained); moves are renamed back. Never overwrites and
    /// never touches a file whose digest no longer matches.
    Result<UndoReport> undo(IJournal& journal, IClock& clock) const;

private:
    IFileSystem& m_fs;
    IHasher& m_hasher;
};

} // namespace dupli
