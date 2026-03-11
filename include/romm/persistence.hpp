#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "romm/app_core.hpp"

namespace romm {

enum class ResumeMode {
    StartFresh = 0,
    ResumeFromPartial,
    RestartDueToGap
};

struct PartialChunk {
    uint64_t offset{0};
    uint64_t size{0};
};

struct ResumeDecision {
    ResumeMode mode{ResumeMode::StartFresh};
    uint64_t resumeBytes{0};
    std::string reason;
};

bool saveQueueState(const Status& status, const std::string& path, std::string& outError);
bool loadQueueState(Status& status, const std::string& path, std::string& outError);

bool writeCompletedManifest(const QueueItem& item, const std::string& manifestDir,
                            std::string& outPath, std::string& outError);
bool loadCompletedManifests(Status& status, const std::string& manifestDir, std::string& outError);

std::vector<PartialChunk> detectPartChunks(const std::string& dir, const std::string& baseName,
                                           std::string& outError);
ResumeDecision planResume(uint64_t expectedBytes, const std::vector<PartialChunk>& chunks);
const char* resumeModeName(ResumeMode mode);

} // namespace romm
