#pragma once

namespace openosc {
enum class RecoveryAction { Update, Calibration, Backup, PowerHold, CheckFat, Launch, PowerOff, Format, Count };
inline constexpr const char* RecoveryItems[] = {
    "UPDATE FIRMWARE", "VIEW CALIBRATION", "BACKUP FACTORY BOOT", "POWER-ON HOLD",
    "CHECK / REPAIR FAT", "START APPLICATION", "POWER OFF", "FORMAT EXTERNAL DISK"
};
inline constexpr unsigned RecoveryItemCount = static_cast<unsigned>(RecoveryAction::Count);
static_assert(sizeof RecoveryItems / sizeof *RecoveryItems == RecoveryItemCount);
}
