#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace Simpsons {
// Native audio preferences beside the profile store (<store>.audio.cfg).
// The original Voice/Music/Effects/Chatter sliders and Subtitles selector keep
// their own profile save; this file only holds what the original menu lacks.
struct NativeAudioSettings {
    uint32_t masterVolume=100; // Percent, 0..100 in steps of 5. Live.
    static constexpr uint32_t step=5;
    float gain() const noexcept {return float(masterVolume)/100.0f;}
    void stepMaster(int direction) {
        // Holding a direction stops at the end of the range instead of wrapping
        // from silence to full volume.
        if(direction>0)masterVolume=masterVolume>=100?100u:masterVolume+step;
        else masterVolume=masterVolume<=0?0u:masterVolume-step;
    }
    std::string label() const {return "Master volume: "+std::to_string(masterVolume)+"%";}
    static NativeAudioSettings load(const std::filesystem::path& file) {
        NativeAudioSettings out;
        std::ifstream in(file);uint32_t version{},master{};
        if(!(in>>version>>master)||version!=1||master>100||master%step)return {};
        in>>std::ws;if(!in.eof())return {};
        out.masterVolume=master;return out;
    }
    void save(const std::filesystem::path& file) const {
        if(file.empty())return;
        if(!file.parent_path().empty())std::filesystem::create_directories(file.parent_path());
        std::ofstream out(file,std::ios::trunc);out<<1<<' '<<masterVolume<<'\n';out.close();
        if(!out)throw std::runtime_error("Unable to save native audio settings");
    }
    bool operator==(const NativeAudioSettings&) const=default;
};
}
