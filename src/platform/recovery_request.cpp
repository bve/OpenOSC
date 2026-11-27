#include "openosc/platform/recovery_request.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/handoff.hpp"
#include "openosc/update/crc32.hpp"
#include "openosc/update/layout.hpp"
#include <cstring>

namespace openosc {
namespace {
constexpr uint32_t KindMenu = 0, KindInstall = 1;
struct Record { uint32_t magic, inverse, kind, crc; char name[RecoveryRequest::NameBytes]; };
static_assert(sizeof(Record) == 0x40 && RecoveryRequest::Address + sizeof(Record) == 0x20020000,
              "RecoveryRequest must fill DTCM from 0x2001FFC0 to its end");
volatile Record &record() { return *reinterpret_cast<volatile Record*>(RecoveryRequest::Address); }
uint32_t check(uint32_t kind, const char (&name)[RecoveryRequest::NameBytes]) {
    Crc32 crc;
    crc.update(&kind, sizeof kind);
    crc.update(name, sizeof name);
    return crc.value();
}
}
RecoveryRequest::Request RecoveryRequest::take() {
    volatile Record &r = record();
    Request request;
    if (r.magic != Magic || r.inverse != ~Magic) return request;
    const uint32_t kind = r.kind, crc = r.crc;
    for (unsigned i = 0; i < NameBytes; ++i) request.name[i] = r.name[i];
    r.magic = r.inverse = r.kind = r.crc = 0;
    for (unsigned i = 0; i < NameBytes; ++i) r.name[i] = 0;
    __DSB();
    request.recovery = true;
    request.name[NameBytes - 1] = 0;
    request.install = kind == KindInstall && request.name[0] && crc == check(kind, request.name);
    return request;
}
[[noreturn]] void RecoveryRequest::restart(const char *install) {
    char name[NameBytes] = {};
    if (install) std::strncpy(name, install, NameBytes - 1);
    const uint32_t kind = install ? KindInstall : KindMenu;
    volatile Record &r = record();
    r.kind = kind; r.crc = check(kind, name);
    for (unsigned i = 0; i < NameBytes; ++i) r.name[i] = name[i];
    r.magic = Magic; r.inverse = ~Magic; // Last: a record is a request only whole.
    __DSB();
    // Return to our loader directly: the factory's reset startup may clear
    // the retained request before our loader gets to inspect it.
    Handoff::jump(layout::LoaderBase);
}
}
