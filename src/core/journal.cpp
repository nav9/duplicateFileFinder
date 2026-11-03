#include "dupli/journal.hpp"

#include "dupli/json.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <map>
#include <string>
#include <utility>

namespace dupli {

namespace {

constexpr const char* kJournalFormat = "dupli-journal";
constexpr int kJournalFormatVersion = 1;

json::Value record_to_json(const JournalRecord& record) {
    json::Value value;
    value.set("format", kJournalFormat);
    value.set("version", kJournalFormatVersion);
    value.set("id", record.id);
    value.set("action_id", record.action_id);
    value.set("kind", record.kind);
    value.set("source", path_to_utf8(record.source));
    value.set("destination", path_to_utf8(record.destination));
    value.set("expected_digest", record.expected_digest);
    value.set("state", to_string(record.state));
    value.set("error", record.error);
    value.set("timestamp", record.timestamp);
    return value;
}

/// Returns false for lines that are not valid journal records. Those are skipped
/// by readers: a crash can leave a torn final line, and a partially written
/// record must never drive a recovery decision.
bool record_from_json(const json::Value& value, JournalRecord& record) {
    if (value["format"].as_string() != kJournalFormat) return false;
    if (value["version"].as_int() != kJournalFormatVersion) return false;
    record.id = value["id"].as_string();
    if (record.id.empty()) return false;
    record.action_id = value["action_id"].as_string();
    record.kind = value["kind"].as_string();
    record.source = path_from_utf8(value["source"].as_string());
    record.destination = path_from_utf8(value["destination"].as_string());
    record.expected_digest = value["expected_digest"].as_string();
    record.error = value["error"].as_string();
    record.timestamp = value["timestamp"].as_string();
    if (!parse_journal_state(value["state"].as_string(), record.state)) return false;
    return true;
}

} // namespace

std::string to_string(JournalState state) {
    switch (state) {
    case JournalState::Started: return "started";
    case JournalState::Completed: return "completed";
    case JournalState::Failed: return "failed";
    case JournalState::RolledBack: return "rolled_back";
    }
    return "started";
}

bool parse_journal_state(std::string_view text, JournalState& out) {
    if (text == "started") { out = JournalState::Started; return true; }
    if (text == "completed") { out = JournalState::Completed; return true; }
    if (text == "failed") { out = JournalState::Failed; return true; }
    if (text == "rolled_back") { out = JournalState::RolledBack; return true; }
    return false;
}

std::string SystemClock::now_iso8601() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t seconds = system_clock::to_time_t(now);
    std::tm broken_down{};
#ifdef _WIN32
    gmtime_s(&broken_down, &seconds);
#else
    gmtime_r(&seconds, &broken_down);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &broken_down);
    return buffer;
}

std::string IJournal::next_id() {
    return "j_" + std::to_string(m_counter++);
}

std::string IJournal::start_action(const std::string& action_id,
                                   const std::filesystem::path& source,
                                   const std::filesystem::path& destination,
                                   const std::string& kind,
                                   const std::string& expected_digest) {
    JournalRecord record;
    record.id = next_id();
    record.action_id = action_id;
    record.kind = kind;
    record.source = source;
    record.destination = destination;
    record.expected_digest = expected_digest;
    record.state = JournalState::Started;
    // A journal write failure cannot be reported through this legacy signature;
    // Executor uses append() directly so write failures abort the action.
    (void)append(record);
    return record.id;
}

Result<void> IJournal::append_state(const std::string& journal_id, JournalState state,
                                    const std::string& error) {
    auto records = entries();
    if (records.has_error()) return records.error();
    for (const auto& record : latest_records(records.value())) {
        if (record.id != journal_id) continue;
        JournalRecord updated = record;
        updated.state = state;
        updated.error = error;
        return append(updated);
    }
    return Error{ErrorCode::NotFound, "Journal entry not found", "", "journal"};
}

Result<void> IJournal::complete_action(const std::string& journal_id) {
    return append_state(journal_id, JournalState::Completed, "");
}

Result<void> IJournal::fail_action(const std::string& journal_id, const std::string& error) {
    return append_state(journal_id, JournalState::Failed, error);
}

Result<std::vector<std::string>> IJournal::recover_pending() {
    auto records = entries();
    if (records.has_error()) return records.error();
    std::vector<std::string> pending;
    for (const auto& record : latest_records(records.value())) {
        if (record.state == JournalState::Started) pending.push_back(record.id);
    }
    return pending;
}

Result<void> SimpleJournal::append(const JournalRecord& record) {
    m_records.push_back(record);
    return Result<void>();
}

Result<std::vector<JournalRecord>> SimpleJournal::entries() const {
    return m_records;
}

Result<void> FileJournal::append(const JournalRecord& record) {
    if (m_path.empty()) {
        return Error{ErrorCode::InvalidArgument, "Journal path is empty", "", "journal_append"};
    }
    std::error_code ec;
    if (m_path.has_parent_path()) {
        std::filesystem::create_directories(m_path.parent_path(), ec);
        if (ec) {
            return Error{ErrorCode::IoError,
                         "Cannot create journal directory: " + ec.message(),
                         path_to_utf8(m_path.parent_path()), "journal_append"};
        }
    }

    JournalRecord stamped = record;
    if (stamped.timestamp.empty()) stamped.timestamp = m_clock.now_iso8601();

    // A crash can leave a partial final line without a newline. Start a new line
    // first so the new record stays readable; the torn line is then skipped by
    // readers instead of swallowing a valid record.
    bool needs_newline = false;
    {
        std::ifstream check(m_path, std::ios::binary | std::ios::ate);
        if (check) {
            const std::streamoff size = check.tellg();
            if (size > 0) {
                check.seekg(-1, std::ios::end);
                char last = '\0';
                check.get(last);
                needs_newline = last != '\n';
            }
        }
    }

    std::ofstream file(m_path, std::ios::app | std::ios::binary);
    if (!file) {
        return Error{ErrorCode::IoError, "Cannot open journal for append",
                     path_to_utf8(m_path), "journal_append"};
    }
    if (needs_newline) file << "\n";
    file << json::dump(record_to_json(stamped), false) << "\n";
    // Flush after every record: the append-only format means a crash can only
    // tear the final line, which readers ignore.
    file.flush();
    if (!file) {
        return Error{ErrorCode::IoError, "Failed to write journal record",
                     path_to_utf8(m_path), "journal_append"};
    }
    return Result<void>();
}

std::string FileJournal::next_id() {
    if (m_next_id < 0) {
        m_next_id = 0;
        auto existing = entries();
        if (existing.ok()) {
            for (const auto& record : existing.value()) {
                if (record.id.size() <= 2 || record.id.compare(0, 2, "j_") != 0) continue;
                try {
                    const long long value = std::stoll(record.id.substr(2));
                    if (value >= m_next_id) m_next_id = value + 1;
                } catch (...) {
                    // Non-numeric ids are ignored; new ids stay numeric.
                }
            }
        }
    }
    return "j_" + std::to_string(m_next_id++);
}

Result<std::vector<JournalRecord>> FileJournal::entries() const {
    std::vector<JournalRecord> records;
    std::error_code ec;
    if (!std::filesystem::exists(m_path, ec)) {
        if (ec) {
            return Error{ErrorCode::IoError, ec.message(), path_to_utf8(m_path), "journal_read"};
        }
        return records; // No journal yet is a normal, empty history.
    }

    std::ifstream file(m_path, std::ios::binary);
    if (!file) {
        return Error{ErrorCode::IoError, "Cannot open journal for reading",
                     path_to_utf8(m_path), "journal_read"};
    }
    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        auto parsed = json::parse(line);
        if (!parsed) continue; // Torn or corrupt line: skip it, never act on it.
        JournalRecord record;
        if (!record_from_json(*parsed, record)) continue;
        records.push_back(std::move(record));
    }
    return records;
}

std::vector<JournalRecord> latest_records(const std::vector<JournalRecord>& records) {
    std::vector<JournalRecord> latest;
    std::map<std::string, size_t> index_by_id;
    for (const auto& record : records) {
        auto it = index_by_id.find(record.id);
        if (it == index_by_id.end()) {
            index_by_id[record.id] = latest.size();
            latest.push_back(record);
        } else {
            latest[it->second] = record;
        }
    }
    return latest;
}

Result<RecoveryReport> RecoveryEngine::inspect(const IJournal& journal) const {
    auto records = journal.entries();
    if (records.has_error()) return records.error();

    RecoveryReport report;
    for (const auto& record : latest_records(records.value())) {
        if (record.state != JournalState::Started) continue;

        RecoveryFinding finding;
        finding.record = record;

        auto source_exists = m_fs.exists(record.source);
        auto destination_exists = m_fs.exists(record.destination);
        if (source_exists.has_error() || destination_exists.has_error()) {
            finding.action = RecoveryAction::NeedsManualReview;
            finding.explanation = "Cannot inspect the source/destination paths";
        } else if (source_exists.value() && !destination_exists.value()) {
            finding.action = RecoveryAction::RetrySafe;
            finding.explanation =
                "Source is still present and the destination does not exist; the action can be retried";
        } else if (!source_exists.value() && destination_exists.value()) {
            finding.action = RecoveryAction::AlreadyCompleted;
            finding.explanation =
                "Destination exists and the source is gone; the operation probably completed before "
                "its journal record was written. Do not repeat it.";
        } else if (source_exists.value() && destination_exists.value()) {
            finding.action = RecoveryAction::NeedsManualReview;
            finding.explanation =
                "Both source and destination exist; the result is ambiguous and needs a human decision";
        } else {
            finding.action = RecoveryAction::NeedsManualReview;
            finding.explanation = "Neither source nor destination exists; data may be lost";
        }

        if (finding.action == RecoveryAction::NeedsManualReview) ++report.needs_attention;
        report.findings.push_back(std::move(finding));
    }
    return report;
}

Result<UndoReport> RecoveryEngine::undo(IJournal& journal, IClock& clock) const {
    auto records = journal.entries();
    if (records.has_error()) return records.error();

    UndoReport report;
    auto latest = latest_records(records.value());

    // Reverse chronological order: the newest operation is undone first so
    // intermediate paths stay consistent.
    for (auto it = latest.rbegin(); it != latest.rend(); ++it) {
        const JournalRecord& record = *it;
        if (record.state != JournalState::Completed) continue;
        if (record.source.empty() || record.destination.empty()) continue;
        if (record.source.lexically_normal() == record.destination.lexically_normal()) continue;

        UndoOutcome outcome;
        outcome.journal_id = record.id;
        outcome.action_id = record.action_id;

        auto fail = [&report, &outcome](std::string message) {
            outcome.undone = false;
            outcome.message = std::move(message);
            report.failed++;
            report.outcomes.push_back(outcome);
        };

        auto destination_exists = m_fs.exists(record.destination);
        auto source_exists = m_fs.exists(record.source);
        if (destination_exists.has_error() || source_exists.has_error()) {
            fail("Cannot inspect current paths");
            continue;
        }
        if (!destination_exists.value()) {
            if (source_exists.value()) {
                outcome.message = "Already restored (source exists, destination is gone)";
                report.skipped++;
                report.outcomes.push_back(outcome);
            } else {
                fail("Neither the source nor the destination exists; manual recovery required");
            }
            continue;
        }

        // Never undo over a different file that now occupies the source path when
        // reversing a move. A copy's source is expected to still exist (copying
        // always retains it); if it is gone the copy may be the only remaining
        // data, so it is not removed automatically.
        if (record.kind == "move" && source_exists.value()) {
            fail("Source path is occupied again; refusing to overwrite it");
            continue;
        }
        if (record.kind == "copy" && !source_exists.value()) {
            fail("Original source is missing; refusing to remove the only copy");
            continue;
        }

        // Content check: only undo when the destination still holds the content
        // this journal entry claims to have created.
        if (!record.expected_digest.empty()) {
            auto opened = m_fs.open_read(record.destination);
            if (opened.has_error()) {
                fail("Cannot read the destination to verify it before undo");
                continue;
            }
            auto digest = m_hasher.hash_file(*opened.value());
            if (digest.has_error() || !digest.value().complete ||
                digest.value().hex != record.expected_digest) {
                fail("Destination content changed since the operation; refusing to undo");
                continue;
            }
        }

        auto ensure_parent = [this](const std::filesystem::path& path) -> bool {
            auto parent = path.parent_path();
            if (parent.empty()) return true;
            auto exists = m_fs.exists(parent);
            if (exists.ok() && exists.value()) return true;
            if (exists.has_error()) return false;
            auto created = m_fs.create_directory(parent);
            return created.ok();
        };

        if (record.kind == "move") {
            if (!ensure_parent(record.source)) {
                fail("Cannot recreate the original parent folder");
                continue;
            }
            auto moved = m_fs.rename(record.destination, record.source);
            if (moved.has_error()) {
                fail("Could not move the file back: " + moved.error().message);
                continue;
            }
        } else if (record.kind == "copy") {
            auto removed = m_fs.remove(record.destination);
            if (removed.has_error()) {
                fail("Could not remove the copied file: " + removed.error().message);
                continue;
            }
        } else {
            outcome.message = "Unknown journal action kind; skipped";
            report.skipped++;
            report.outcomes.push_back(outcome);
            continue;
        }

        JournalRecord rolled_back = record;
        rolled_back.state = JournalState::RolledBack;
        rolled_back.timestamp = clock.now_iso8601();
        if (journal.append(rolled_back).has_error()) {
            fail("The file was restored but the journal could not record the undo");
            continue;
        }

        outcome.undone = true;
        outcome.message = record.kind == "move" ? "Moved back to its original path"
                                                 : "Copied file removed (original was kept)";
        report.undone++;
        report.outcomes.push_back(std::move(outcome));
    }
    return report;
}

} // namespace dupli
