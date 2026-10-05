#pragma once
#include "native_backend.h"
#include <cstddef>
#include <string_view>

namespace Simpsons::Graphics {
// The original `buttons` atlas uses sixteen 64px cells. Keep its UV layout so
// menu and tutorial text retain their original placement, animation and alpha.
bool isInputPromptAtlas(std::string_view name,uint32_t width,uint32_t height);
std::vector<uint8_t> keyboardMousePromptPixels();
struct Im2DDraw;
// Crop complete atlas-cell quads to the artwork and preserve its proportions.
// Footer prompts grow away from their labels; inline prompts retain their slot.
// Returns the number of recognized quads. Call only for the native prompt atlas.
std::size_t enlargeInputPromptGlyphs(Im2DDraw&);
class NativeInputPrompts {
    std::shared_ptr<Texture> atlas;
public:
    std::shared_ptr<Texture> texture(NativeBackend&);
};
}
