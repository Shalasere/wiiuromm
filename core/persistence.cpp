#include "romm/persistence.hpp"

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace romm {
namespace {
namespace fs = std::filesystem;

bool parseQueueState(int raw, QueueState &out) {
    switch (raw) {
        case 0: out = QueueState::Pending; return true;
        case 1: out = QueueState::Downloading; return true;
        case 2: out = QueueState::Completed; return true;
        case 3: out = QueueState::Failed; return true;
        default: return false;
    }
}

bool parseU64Strict(const std::string &text, uint64_t &out) {
    if (text.empty()) return false;
    errno = 0;
    char *end = nullptr;
    unsigned long long v = std::strtoull(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0') return false;
    out = static_cast<uint64_t>(v);
    return true;
}

std::string manifestFileForRom(const std::string &dir, const std::string &romId) {
    return (fs::path(dir) / (romId + ".manifest")).string();
}

} // namespace

bool saveQueueState(const Status &status, const std::string &path, std::string &outError) {
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
        if (ec) {
            outError = "failed to create queue state parent dir: " + ec.message();
            return false;
        }
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        outError = "failed to open queue state file for write: " + path;
        return false;
    }

    out << "version=1\n";
    for (const auto &q : status.downloadQueue) {
        out << "Q\t"
            << std::quoted(q.rom.id) << "\t"
            << std::quoted(q.rom.title) << "\t"
            << std::quoted(q.rom.subtitle) << "\t"
            << q.rom.sizeMb << "\t"
            << static_cast<int>(q.state) << "\t"
            << static_cast<int>(q.progressPercent) << "\t"
            << static_cast<int>(q.attempts) << "\t"
            << std::quoted(q.error) << "\n";
    }
    return true;
}

bool loadQueueState(Status &status, const std::string &path, std::string &outError) {
    std::ifstream in(path);
    if (!in.is_open()) {
        outError = "failed to open queue state file: " + path;
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("version=", 0) == 0) continue;
        if (line.rfind("Q\t", 0) != 0) continue;

        std::istringstream row(line.substr(2));
        QueueItem item;
        int sizeMb = 0;
        int rawState = 0;
        int progress = 0;
        int attempts = 0;

        row >> std::quoted(item.rom.id)
            >> std::quoted(item.rom.title)
            >> std::quoted(item.rom.subtitle)
            >> sizeMb
            >> rawState
            >> progress
            >> attempts
            >> std::quoted(item.error);
        if (row.fail()) {
            outError = "malformed queue row";
            return false;
        }

        item.rom.sizeMb = static_cast<uint32_t>(std::max(0, sizeMb));
        if (!parseQueueState(rawState, item.state)) {
            outError = "invalid queue state in queue row";
            return false;
        }
        item.progressPercent = static_cast<uint8_t>(std::clamp(progress, 0, 100));
        item.attempts = static_cast<uint8_t>(std::clamp(attempts, 0, 255));

        bool duplicate = false;
        for (const auto &q : status.downloadQueue) {
            if (q.rom.id == item.rom.id) duplicate = true;
        }
        for (const auto &h : status.downloadHistory) {
            if (h.rom.id == item.rom.id && h.state == QueueState::Completed) duplicate = true;
        }
        if (!duplicate) status.downloadQueue.push_back(item);
    }

    if (!status.downloadQueue.empty()) {
        status.selectedQueueIndex = std::min(
            status.selectedQueueIndex, static_cast<int>(status.downloadQueue.size()) - 1);
    } else {
        status.selectedQueueIndex = 0;
    }
    return true;
}

bool writeCompletedManifest(const QueueItem &item, const std::string &manifestDir,
                            std::string &outPath, std::string &outError) {
    if (item.rom.id.empty()) {
        outError = "manifest write requires rom id";
        return false;
    }
    std::error_code ec;
    fs::create_directories(manifestDir, ec);
    if (ec) {
        outError = "failed to create manifest dir: " + ec.message();
        return false;
    }

    outPath = manifestFileForRom(manifestDir, item.rom.id);
    std::ofstream out(outPath, std::ios::trunc);
    if (!out.is_open()) {
        outError = "failed to open manifest for write: " + outPath;
        return false;
    }

    out << "version=1\n";
    out << "id=" << item.rom.id << "\n";
    out << "title=" << item.rom.title << "\n";
    out << "subtitle=" << item.rom.subtitle << "\n";
    out << "size_mb=" << item.rom.sizeMb << "\n";
    out << "state=" << queueStateName(item.state) << "\n";
    out << "progress=" << static_cast<int>(item.progressPercent) << "\n";
    return true;
}

bool loadCompletedManifests(Status &status, const std::string &manifestDir, std::string &outError) {
    std::error_code ec;
    if (!fs::exists(manifestDir, ec)) return true;
    if (ec) {
        outError = "failed to inspect manifest dir: " + ec.message();
        return false;
    }

    for (const auto &entry : fs::directory_iterator(manifestDir, ec)) {
        if (ec) {
            outError = "failed to iterate manifest dir: " + ec.message();
            return false;
        }
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".manifest") continue;

        std::ifstream in(entry.path());
        if (!in.is_open()) {
            outError = "failed to open manifest: " + entry.path().string();
            return false;
        }

        QueueItem item;
        item.state = QueueState::Completed;
        item.progressPercent = 100;

        std::string line;
        while (std::getline(in, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = line.substr(0, eq);
            const std::string val = line.substr(eq + 1);
            if (key == "id") item.rom.id = val;
            else if (key == "title") item.rom.title = val;
            else if (key == "subtitle") item.rom.subtitle = val;
            else if (key == "size_mb") {
                uint64_t parsed = 0;
                if (!parseU64Strict(val, parsed)) {
                    outError = "invalid size_mb in manifest: " + entry.path().string();
                    return false;
                }
                item.rom.sizeMb = static_cast<uint32_t>(std::min<uint64_t>(parsed, 0xFFFFFFFFull));
            }
        }

        if (item.rom.id.empty()) continue;
        bool duplicate = false;
        for (const auto &h : status.downloadHistory) {
            if (h.rom.id == item.rom.id) duplicate = true;
        }
        if (!duplicate) status.downloadHistory.push_back(item);
    }
    return true;
}

std::vector<PartialChunk> detectPartChunks(const std::string &dir, const std::string &baseName,
                                           std::string &outError) {
    std::vector<PartialChunk> out;
    const fs::path root(dir);
    std::error_code ec;
    if (!fs::exists(root, ec)) return out;
    if (ec) {
        outError = "failed to inspect part chunk dir: " + ec.message();
        return out;
    }

    const std::string prefix = baseName + ".part.";
    for (const auto &entry : fs::directory_iterator(root, ec)) {
        if (ec) {
            outError = "failed to scan part chunks: " + ec.message();
            out.clear();
            return out;
        }
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) != 0) continue;

        const std::string offStr = name.substr(prefix.size());
        uint64_t offset = 0;
        if (!parseU64Strict(offStr, offset)) continue;

        PartialChunk c;
        c.offset = offset;
        c.size = static_cast<uint64_t>(fs::file_size(entry.path(), ec));
        if (ec) continue;
        out.push_back(c);
    }
    return out;
}

ResumeDecision planResume(uint64_t expectedBytes, const std::vector<PartialChunk> &chunks) {
    ResumeDecision d;
    if (chunks.empty()) {
        d.mode = ResumeMode::StartFresh;
        d.reason = "No part chunks found.";
        return d;
    }

    std::vector<PartialChunk> sorted = chunks;
    std::sort(sorted.begin(), sorted.end(), [](const PartialChunk &a, const PartialChunk &b) {
        if (a.offset == b.offset) return a.size > b.size;
        return a.offset < b.offset;
    });

    if (sorted.front().offset != 0) {
        d.mode = ResumeMode::RestartDueToGap;
        d.reason = "First part does not start at offset 0.";
        return d;
    }

    uint64_t contiguous = 0;
    for (const auto &c : sorted) {
        if (c.offset > contiguous) {
            d.mode = ResumeMode::RestartDueToGap;
            d.reason = "Gap detected in part chunks.";
            return d;
        }
        const uint64_t end = c.offset + c.size;
        if (end > contiguous) contiguous = end;
    }

    d.mode = ResumeMode::ResumeFromPartial;
    d.resumeBytes = std::min(expectedBytes, contiguous);
    d.reason = "Contiguous part chunks detected.";
    return d;
}

const char *resumeModeName(ResumeMode mode) {
    switch (mode) {
        case ResumeMode::StartFresh: return "StartFresh";
        case ResumeMode::ResumeFromPartial: return "ResumeFromPartial";
        case ResumeMode::RestartDueToGap: return "RestartDueToGap";
        default: return "Unknown";
    }
}

} // namespace romm
