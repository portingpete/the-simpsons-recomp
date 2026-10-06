#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace Simpsons {
enum class ControlAction : uint32_t {
    MoveForward,MoveBackward,MoveLeft,MoveRight,Jump,Attack,Special,Action,
    SwitchCharacter,TargetLock,LeftStick,RightStick,CharacterMenu,SpecialPower,LeftTrigger,Pause,Count
};
// Physical Windows virtual keys, including mouse buttons, are persisted as
// numbers. Zero means unbound. The two slots are interchangeable aliases.
struct NativeControlSettings {
    static constexpr uint32_t actionCount=uint32_t(ControlAction::Count),slotCount=2;
    std::array<std::array<uint32_t,slotCount>,actionCount> bindings{{
        {{'W',0}},{{'S',0}},{{'A',0}},{{'D',0}},{{0x20,0x0D}},{{'J',0x01}},{{'K',0x02}},{{'E',0}},
        {{'Q',0}},{{'R',0}},{{'F',0}},{{'G',0x04}},{{0x09,0x08}},{{0xA0,0}},{{0xA2,0xA3}},{{0x1B,0}}
    }};
    uint32_t mouseSensitivity=100; // Percentage: 25..300, in 25% steps.
    bool invertMouseX=false,invertMouseY=false;
    static bool validKey(uint32_t code);
    static bool reservedKey(uint32_t code);
    bool valid() const;
    static NativeControlSettings load(const std::filesystem::path&);
    void save(const std::filesystem::path&) const;
    void reset();
    // If a key is already assigned, exchange its old slot with this one. This
    // makes conflicts deterministic and preserves the displaced action.
    void setBinding(ControlAction action,uint32_t slot,uint32_t code);
    // Mouse options: 0 sensitivity, 1 invert X, 2 invert Y, 3 reset defaults.
    void step(uint32_t row,int direction);
    static std::string actionLabel(ControlAction action);
    static std::string keyLabel(uint32_t code);
    std::string bindingLabel(ControlAction action,uint32_t slot) const;
    bool operator==(const NativeControlSettings&) const=default;
};
}
