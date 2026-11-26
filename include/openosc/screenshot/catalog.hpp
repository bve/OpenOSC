#pragma once
#include "openosc/storage/filesystem.hpp"
#include "openosc/storage/layout.hpp"
#include <cstddef>
#include <string>
#include <strings.h>

namespace openosc::screenshot {
class ImageCatalog {
public:
    static constexpr unsigned PageSize=5, NameBytes=256;
    static constexpr const char *Directory=storage::Layout::Screenshots;
    // Directory, '/', a name of up to NameBytes-1 characters and the terminator.
    static constexpr unsigned PathBytes=unsigned(std::char_traits<char>::length(Directory))+1+NameBytes;
    static bool supportedName(const char *name) {
        const size_t length=std::char_traits<char>::length(name);
        if (length<5 || length>=NameBytes) return false;
        const char *extension=name+length-4;
        return !strcasecmp(extension,".PNG") || !strcasecmp(extension,".BMP");
    }
    struct Entry { char name[NameBytes]{}; uint32_t bytes=0; };
    explicit ImageCatalog(Filesystem &fs) : fs_(fs) {}
    ~ImageCatalog() { cancelClear(); }
    bool refresh();
    bool move(int direction);
    bool eraseSelected();
    bool beginClear();
    bool clearStep(); // One file per foreground turn; stops on the first error.
    void cancelClear();
    bool clearing() const { return clearing_; }
    unsigned erased() const { return erased_; }
    unsigned count() const { return count_; }
    unsigned selected() const { return selected_; }
    unsigned first() const { return first_; }
    unsigned visible() const { return visible_; }
    const Entry &entry(unsigned row) const { return entries_[row<PageSize ? row : 0]; }
    const char *name() const { return count_ && selected_-first_<visible_ ? entries_[selected_-first_].name : ""; }
    bool path(char *output,size_t capacity) const;
    const char *error() const { return error_; }
private:
    static bool image(const FILINFO &info);
    bool scan(unsigned first);
    bool fail(const char *message);
    Filesystem &fs_;
    Entry entries_[PageSize];
    unsigned count_=0,selected_=0,first_=0,visible_=0,erased_=0;
    DIR directory_{};
    bool clearing_=false;
    const char *error_=nullptr;
};
} // namespace openosc::screenshot
