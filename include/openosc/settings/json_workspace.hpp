#pragma once
#include "openosc/settings/theme_document.hpp"
#include <ArduinoJson.h>
#include <cstddef>
#include <new>

namespace openosc {
// The scratch of the foreground JSON files: settings, theme and scope calibration. Their text
// (up to the theme's 10 KiB), ArduinoJson tree (up to the theme's document) and whole themes live here, in
// SRAM4 on the MCU, never on the DTCM stack: the application controller and LVGL already take most
// of it, and an 11 KiB settings frame below Reload settings once ran the stack into the end of
// .bss (scope calibrator review, Reload settings HardFault). One file operation at a time: each
// resource is leased by one owner; a second lease while it is held gets nothing instead of the first
// owner's memory. A file operation starts only when the whole workspace is idle(): otherwise it is
// unreadable/unsaved before it changes anything, never a refused parse taken for an invalid file.
class JsonWorkspace {
public:
    static constexpr size_t TextCapacity = ThemeParser::MaxBytes + 1;
    static constexpr size_t DocumentBytes = sizeof(ThemeDocument);
    // The file text, while held. Empty when another owner holds it.
    class Text {
    public:
        Text() : data_(acquireText()) {}
        ~Text() { if (data_) releaseText(); }
        Text(const Text &) = delete;
        Text &operator=(const Text &) = delete;
        explicit operator bool() const { return data_ != nullptr; }
        char *data() const { return data_; }
        static constexpr size_t capacity() { return TextCapacity; }
    private:
        char *data_;
    };
    // An empty document of Capacity bytes, while held. Empty when another owner holds it.
    template <size_t Capacity> class Document {
    public:
        using Type = StaticJsonDocument<Capacity>;
        static_assert(sizeof(Type) <= DocumentBytes && alignof(Type) <= alignof(std::max_align_t),
                      "a foreground JSON document must fit the workspace");
        Document() {
            if (void *storage = acquireDocument()) document_ = new (storage) Type;
        }
        ~Document() {
            if (!document_) return;
            document_->~Type();
            releaseDocument();
        }
        Document(const Document &) = delete;
        Document &operator=(const Document &) = delete;
        explicit operator bool() const { return document_ != nullptr; }
        Type &operator*() const { return *document_; }
        Type *operator->() const { return document_; }
    private:
        Type *document_ = nullptr;
    };
    // A whole theme of a theme file operation (2.3 KiB each): ThemeStore's candidate, and
    // ThemeParser's candidate and stock reference. Made in place from a prvalue (no stack copy).
    enum class ThemeSlot : unsigned { Store, Candidate, Reference, Count };
    class ThemeLease {
    public:
        explicit ThemeLease(ThemeSlot slot) : slot_(slot), storage_(acquireTheme(slot)) {}
        ~ThemeLease() {
            if (!storage_) return;
            destroy();
            releaseTheme(slot_);
        }
        ThemeLease(const ThemeLease &) = delete;
        ThemeLease &operator=(const ThemeLease &) = delete;
        explicit operator bool() const { return storage_ != nullptr; }
        // The theme `make()` returns, built directly in the slot; an earlier one there ends first.
        template <class Make> Theme &make(Make make) {
            destroy();
            theme_ = new (storage_) Theme(make());
            return *theme_;
        }
    private:
        void destroy() { if (theme_) { theme_->~Theme(); theme_ = nullptr; } }
        ThemeSlot slot_;
        void *storage_;
        Theme *theme_ = nullptr;
    };
    // Nothing leased: a file operation may start (its own leases then follow one another).
    static bool idle();
    // Held now (tests and diagnostics).
    static bool textHeld();
    static bool documentHeld();
    static bool themeHeld(ThemeSlot slot);
    // A DMA user may borrow unused text storage. Text has priority and calls reclaim before use.
    static char *borrowText(void (*reclaim)());
    static void returnText();
private:
    static char *acquireText();
    static void releaseText();
    static void *acquireDocument();
    static void releaseDocument();
    static void *acquireTheme(ThemeSlot slot);
    static void releaseTheme(ThemeSlot slot);
};
} // namespace openosc
