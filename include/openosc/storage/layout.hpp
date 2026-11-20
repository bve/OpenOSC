#pragma once

namespace openosc::storage {
// Every OpenOSC file on the SPI flash lives under /openosc (application, Recovery, formatter, host tools).
// No file is read from or written to the volume root; nothing here falls back to it. Creating the
// tree: storage/directories.hpp.
struct Layout {
    static constexpr const char *Root = "/openosc";
    static constexpr const char *Update = "/openosc/UPDATE";           // Firmware images (.OSC) for Recovery.
    static constexpr const char *Backup = "/openosc/BACKUP";           // The manual factory boot backup.
    static constexpr const char *Settings = "/openosc/SETTINGS";
    static constexpr const char *Ui = "/openosc/UI";                   // Theme and its icons.
    static constexpr const char *Calibration = "/openosc/CALIBRATION";
    static constexpr const char *Screenshots = "/openosc/SCREENSHOTS";
    static constexpr const char *Captures = "/openosc/CAPTURES";
    static constexpr const char *Logs = "/openosc/LOGS";
    static constexpr const char *SettingsCopyDirectory = "/openosc/SETCOPY";
    // Decoded events (CSV, docs/SCOPE_PROTOCOL_DECODERS_CSV.md): created by the application when it first
    // saves one (DecodeCsvFile), not by prepare(): Recovery and the formatter never need it.
    static constexpr const char *Decode = "/openosc/DECODE";
    // Parents before children: the order prepare() creates them in.
    static constexpr const char *Directories[] = {Root, Update, Backup, Settings, SettingsCopyDirectory, Ui, Calibration, Screenshots, Captures, Logs};
    // Empty directories the factory startup creates in the root after every reset. Not OpenOSC data:
    // prepare() removes exactly these, only while empty, so the root holds only /openosc.
    static constexpr const char *FactoryRootDirectories[] = {"/firmware", "/Application", "/log", "/pic", "/data"};

    static constexpr const char *SettingsFile = "/openosc/SETTINGS/settings.json";
    // Its own directory (own 4 KiB block): a cut erase spares SETTINGS and BACKUP's factory boot copy.
    static constexpr const char *SettingsCopy = "/openosc/SETCOPY/settings.json";
    static constexpr const char *SettingsCopyOld = "/openosc/BACKUP/settings.json"; // Test builds 9-12: read only.
    // The 360 MHz start guard (BootGuard): "starting" before that profile runs, "ok" once confirmed.
    static constexpr const char *BootGuardFile = "/openosc/SETTINGS/boot.json";
    static constexpr const char *BootGuardPrevious = "/openosc/SETTINGS/boot.previous.json";
    static constexpr const char *ThemeFile = "/openosc/UI/theme.json";
    static constexpr const char *IconPrefix = "/openosc/UI/";            // Theme icons must lie here.
    static constexpr const char *ZeroFile = "/openosc/CALIBRATION/scope_zero.json";
    static constexpr const char *ZeroPrevious = "/openosc/CALIBRATION/scope_zero.previous.json";
    static constexpr const char *PwmFile = "/openosc/CALIBRATION/scope_pwm.json"; // Hardware zero.
    static constexpr const char *PwmPrevious = "/openosc/CALIBRATION/scope_pwm.previous.json";
    static constexpr const char *GainFile = "/openosc/CALIBRATION/scope_gain.json"; // User gain per range.
    static constexpr const char *GainPrevious = "/openosc/CALIBRATION/scope_gain.previous.json";
    static constexpr const char *InterleaveFile = "/openosc/CALIBRATION/scope_interleave.json"; // ZT706S service offset.
    static constexpr const char *InterleavePrevious = "/openosc/CALIBRATION/scope_interleave.previous.json";
    static constexpr const char *InterleaveMatchFile = "/openosc/CALIBRATION/scope_interleave_match.json";
    static constexpr const char *FactoryBackup = "/openosc/BACKUP/FACTORY_BOOT_08000000.bin";
    static constexpr const char *FactoryBackupTemporary = "/openosc/BACKUP/FACTORY_BOOT_08000000.tmp";
    static constexpr const char *FactoryBackupMetadata = "/openosc/BACKUP/FACTORY_BOOT_08000000.txt";
};
} // namespace openosc::storage
