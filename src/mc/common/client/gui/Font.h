#include <string>

#include <mc/Addresses.h>

namespace SDK {
    class Font {
    private:
        uintptr_t* vtable;

    public:
        int getLineLength(std::string_view str, float fontSize, bool showColorSymbol = false) {
            using Fn = int(__thiscall*)(Font*, std::string_view, float, bool);
            return reinterpret_cast<Fn>(vtable[Signatures::VtableIndex::Font::getLineLength])(
                this, str, fontSize, showColorSymbol);
        }

        float getLineHeight() {
            return reinterpret_cast<float (*)(Font*)>(vtable[Signatures::VtableIndex::Font::getLineHeight])(this);
        }
    };
}
