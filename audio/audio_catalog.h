#pragma once
#include "ea_xma_block.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Simpsons::Audio {

// Exact shipped EA-XMA inventory. Paths identify the offline source of each
// certificate; playback matches only original reader-owned block bytes.
class AudioCatalog {
public:
    struct Block {
        uint32_t bytes{}, frames{}, terminalPadding{};
        std::array<uint32_t,3> payloadBytes{}, restoredFF{};
        std::array<uint8_t,32> normalizedHash{};
    };
    struct Stream {
        uint32_t source{}, ordinal{}, firstBlock{}, blockCount{};
        uint32_t sampleRate{}, channels{}, frames{}, loopStartSample{}, loopOffsetRelative{};
        bool loop{};
        std::array<uint8_t,8> header{};
        uint64_t headerOffset{}, audioOffset{}, audioBytes{};
    };
    // A copied SIMAUD01 certificate row, scoped to this loaded catalog. It is
    // a candidate, not proof that this source path was opened or read. The
    // serialized digest is not checked against source-file bytes by load().
    struct CandidateIdentity {
        uint32_t streamIndex{}, sourceIndex{}, sourceKind{}, ordinal{};
        std::string sourcePath;
        std::array<uint8_t,32> sourceSha256{};
        uint64_t sourceFileBytes{}, headerOffset{}, audioOffset{}, audioBytes{};
        std::array<uint8_t,8> header{};
    };
    struct Match {
        std::vector<uint32_t> candidates;
        Block block;
    };

    static AudioCatalog load(const std::filesystem::path& path);
    // One bounded, owning copy; invalid indices return nullopt. Use each
    // Match::candidates index independently: findHeader() returns a first alias
    // for shared control metadata and cannot establish source identity. This
    // accessor does not open files, merge aliases, or establish runtime ownership.
    std::optional<CandidateIdentity> candidateIdentity(uint32_t streamIndex) const;
    const Stream* findHeader(std::span<const uint8_t> header) const;
    Match match(std::span<const uint8_t> header, std::span<const uint32_t> prior,
                uint64_t sequence, std::span<const uint8_t> ownedBytes) const;
    bool complete(std::span<const uint32_t> candidates, uint64_t sequence) const;
    size_t sourceCount() const noexcept { return sources_.size(); }
    size_t streamCount() const noexcept { return streams_.size(); }
    size_t blockCount() const noexcept { return blocks_.size(); }

private:
    struct Source {std::string path; uint32_t firstStream{}, streamCount{}, kind{};uint64_t fileBytes{};std::array<uint8_t,32> fileHash{};};
    std::vector<Source> sources_;
    std::vector<Stream> streams_;
    std::vector<Block> blocks_;
    std::unordered_map<uint64_t,std::vector<uint32_t>> byHeader_;
};

// Uses the same packet restoration and verified framing as the existing menu
// certificates, after AudioCatalog::match has selected an exact owned block.
EaXmaBlock parseCatalogEaXmaBlock(std::span<const uint8_t> ownedBytes,
                                  uint32_t channels, const AudioCatalog::Block& block);
}
