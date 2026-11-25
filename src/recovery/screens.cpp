#include "openosc/recovery/screens.hpp"
#include "openosc/storage/layout.hpp"
#include <cstdio>
#include "openosc/calibration/record.hpp"
#include "openosc/recovery/action.hpp"

namespace openosc {
void RecoveryScreens::frame(const char* title, const char* footer) {
    screen_.clear();
    screen_.text(8, 10, title, Display::Cyan);
    screen_.rectangle(8, 24, 304, 1, Display::Selected);
    screen_.text(8, 226, footer, Display::Cyan);
}
void RecoveryScreens::menu(unsigned selected, uint32_t jedec, uint32_t bytes, const char* status) {
    frame("OPENOSC RECOVERY", "UP/DOWN  MENU=OK  F4=BACK");
    for (unsigned i = 0; i < RecoveryItemCount; ++i) {
        if (i == selected) screen_.rectangle(8, 32 + i * 18, 304, 16, Display::Selected);
        screen_.text(16, 36 + i * 18, RecoveryItems[i]);
    }
    screen_.text(8, 164, "NOR:"); screen_.number(38, 164, jedec, 6, 16);
    screen_.number(92, 164, bytes / (1024 * 1024)); screen_.text(110, 164, "MIB");
    screen_.text(8, 184, status);
    char images[48];
    std::snprintf(images, sizeof images, "IMAGES: %s/*.OSC, *.BIN", storage::Layout::Update);
    screen_.text(8, 204, images);
    screen_.present();
}
void RecoveryScreens::files(const ImageCatalog& catalog, unsigned selected) {
    char title[40];
    std::snprintf(title, sizeof title, "SELECT FIRMWARE %s", storage::Layout::Update);
    frame(title, "LEFT/RIGHT=PAGE  MENU=OK  F4=BACK");
    screen_.text(8, 34, "PAGE:"); screen_.number(44, 34, catalog.page() + 1);
    screen_.text(110, 34, "FILES:"); screen_.number(152, 34, catalog.total());
    if (!catalog.count()) screen_.text(8, 66, "NO .OSC/.BIN FILES. COPY FROM PC.");
    for (unsigned i = 0; i < catalog.count(); ++i) {
        if (i == selected) screen_.rectangle(8, 56 + i * 28, 304, 22, Display::Selected);
        screen_.text(12, 64 + i * 28, catalog.name(i));
    }
    screen_.present();
}
void RecoveryScreens::confirm(const char* title, const char* detail, bool yes,
                              const ImageHeader* image, bool rawBinary) {
    frame(title, "F2=CANCEL  F3=CONFIRM  MENU=OK");
    screen_.text(8, 40, detail);
    if (image) {
        screen_.text(8, 64, "VERSION:"); screen_.text(62, 64, image->version);
        screen_.text(8, 82, "BYTES:"); screen_.number(50, 82, image->payloadBytes);
        screen_.text(8, 98, "CRC32:"); screen_.number(50, 98, image->payloadCrc, 8, 16);
        screen_.text(8, 116, rawBinary ? "RAW BIN: BOARD MARKER OK" : "APPLICATION WILL BE REPLACED.",
                     rawBinary ? Display::Green : Display::White);
    }
    screen_.rectangle(yes ? 168 : 16, 130, 136, 28, Display::Selected);
    screen_.text(32, 141, "CANCEL"); screen_.text(184, 141, "CONFIRM");
    screen_.present();
}
void RecoveryScreens::message(const char* title, const char* detail) {
    frame(title, "MENU=OK  F4=BACK");
    screen_.text(8, 48, detail); screen_.present();
}
void RecoveryScreens::disconnect(bool yes) {
    confirm("DISCONNECT USB DISK?", "USB DISK WILL DISCONNECT FROM THE PC.", yes);
    screen_.text(8, 64, "FINISH COPYING FILES BEFORE CONTINUING.");
    screen_.text(8, 86, "RECONNECTS WHEN YOU RETURN TO MENU.");
    screen_.present();
}
void RecoveryScreens::backupResult(uint32_t crc) {
    frame("BACKUP COMPLETE", "MENU=RETURN TO USB DISK  F4=BACK");
    screen_.text(8, 48, storage::Layout::FactoryBackup);
    screen_.text(8, 72, "SIZE: 131072 BYTES");
    screen_.text(8, 94, "CRC32:"); screen_.number(56, 94, crc, 8, 16);
    screen_.present();
}
void RecoveryScreens::calibration(unsigned channel, bool hex) {
    CalibrationRecord record;
    CalibrationData decoded; // The application's decoder, always Auto here (no settings in Recovery).
    const bool valid = record.load(decoded);
    frame("FACTORY CALIBRATION (READ ONLY)", "L/R=CHANNEL  MENU=HEX/DEC  F4=BACK");
    screen_.text(8, 32, "CH:"); screen_.number(32, 32, channel + 1);
    screen_.text(62, 32, hex ? "HEX" : "DEC");
    screen_.text(104, 32, valid ? factoryRecordName(decoded.format()) : "CRC FAILED",
                 !valid ? Display::Red : decoded.format() == FactoryRecordFormat::Ambiguous ? Display::Cyan : Display::Green);
    if (valid) screen_.text(196, 32, "AUTO");
    for (unsigned row = 0; row < CalibrationRecord::Rows; ++row) {
        screen_.number(8, 48 + row * 12, row, 2);
        for (unsigned col = 0; col < CalibrationRecord::Columns; ++col)
            screen_.number(30 + col * 41, 48 + row * 12, record.value(channel, row, col), hex ? 4 : 5, hex ? 16 : 10);
    }
    // The identity calibration files are bound to (a compact record's own CRC; Ambiguous reads as long).
    screen_.text(8, 198, "ID CRC:");
    if (valid) screen_.number(62, 198, decoded.crc(), 4, 16); else screen_.text(62, 198, "----");
    screen_.text(168, 198, "BASE:0801C000");
    screen_.text(8, 210, "346B EXTRA:");
    for (unsigned i = 0; i < 4; ++i) screen_.number(80 + i * 42, 210, record.word(336 + i * 2), 4, 16);
    screen_.present();
}
}
