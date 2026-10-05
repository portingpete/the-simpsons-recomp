#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Simpsons::Audio {

// Exact decoded AMX payloads and EA-XMA cues from the shipped STR resources.
// Offsets address the whole AMX payload, including its metadata prefix.
class AmxAudioCatalog {
public:
    struct Payload {
        std::string name;
        uint32_t payloadBytes{}, metadataBytes{}, prefixBytes{}, audioEndOffset{};
        uint32_t firstCue{}, cueCount{};
        std::array<uint8_t,32> payloadHash{}, audioHash{};
    };
    struct Cue {
        uint32_t payloadIndex{}, ordinal{}, headerOffset{}, endOffset{};
        uint32_t frames{}, playbackRate{}, channels{}, loopStartSample{};
        uint32_t firstBlock{}, blockCount{};
        bool loop{};
        std::array<uint8_t,8> header{};
        std::array<uint8_t,32> cueHash{};
    };
    struct Block {
        uint32_t cueIndex{}, ordinal{}, offset{}, bytes{}, frames{};
        uint32_t selector{}, codecRate{}, restoredFF{}, payloadBytes{};
        std::array<uint8_t,32> hash{};
    };

    static AmxAudioCatalog load(const std::filesystem::path& path);
    const Payload* payload(uint32_t index) const noexcept;
    const Payload* findPayload(std::span<const uint8_t> bytes) const;
    const Payload* findPayload(const std::array<uint8_t,32>& hash,
                               uint64_t bytes) const noexcept;
    const Cue* cue(const Payload& payload, uint32_t ordinal) const noexcept;
    const Cue* findCue(const Payload& payload, uint32_t headerOffset,
                       std::span<const uint8_t> header) const noexcept;
    const Block* findBlock(const Cue& cue, uint32_t ordinal) const noexcept;
    bool verifyPayload(const Payload& payload, std::span<const uint8_t> bytes) const;
    bool verifyAudio(const Payload& payload, std::span<const uint8_t> bytes) const;
    bool verifyCue(const Cue& cue, std::span<const uint8_t> bytes) const;
    bool verifyBlock(const Block& block, std::span<const uint8_t> bytes) const;
    size_t payloadCount() const noexcept { return payloads_.size(); }
    size_t cueCount() const noexcept { return cues_.size(); }
    size_t blockCount() const noexcept { return blocks_.size(); }

private:
    std::vector<Payload> payloads_;
    std::vector<Cue> cues_;
    std::vector<Block> blocks_;
};

}
