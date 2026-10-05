#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Simpsons::Audio {

// Exact shipped SBK cue inventory. All offsets address the decoded original
// SBK payload, never a derived packet buffer.
class ResidentAudioCatalog {
public:
    struct Bank {
        std::string path, name;
        uint64_t payloadBytes{}, metadataBytes{}, audioOffset{}, audioBytes{};
        uint32_t firstCue{}, cueCount{}, entryIndex{};
        std::array<uint8_t,32> payloadHash{}, containerHash{};
    };
    struct Cue {
        uint32_t bankIndex{}, ordinal{}, firstBlock{}, blockCount{};
        uint64_t headerOffset{}, endOffset{};
        uint32_t frames{}, playbackRate{}, channels{}, loopStartSample{};
        bool loop{};
        std::array<uint8_t,8> header{};
    };
    struct Block {
        uint32_t cueIndex{}, ordinal{};
        uint64_t offset{};
        uint32_t bytes{}, frames{}, selector{}, codecRate{}, restoredFF{}, payloadBytes{};
        std::array<uint8_t,32> hash{};
    };

    static ResidentAudioCatalog load(const std::filesystem::path& path);
    const Bank* findBank(std::span<const uint8_t> payload) const;
    const Bank* findBank(const std::array<uint8_t,32>& hash, uint64_t bytes) const noexcept;
    const Cue* cue(const Bank& bank, uint32_t ordinal) const noexcept;
    const Cue* findCue(const Bank& bank, uint64_t headerOffset,
                       std::span<const uint8_t> header) const noexcept;
    const Block* findBlock(const Cue& cue, uint32_t ordinal) const noexcept;
    bool verifyBank(const Bank& bank, std::span<const uint8_t> payload) const;
    bool verifyBlock(const Block& block, std::span<const uint8_t> bytes) const;
    size_t bankCount() const noexcept { return banks_.size(); }
    size_t cueCount() const noexcept { return cues_.size(); }
    size_t blockCount() const noexcept { return blocks_.size(); }

private:
    std::vector<Bank> banks_;
    std::vector<Cue> cues_;
    std::vector<Block> blocks_;
};

}
