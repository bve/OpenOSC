#pragma once
#include "esp_partition.h"

namespace openosc::companion::ota {
inline constexpr char AppProject[] = "openosc_companion";
inline constexpr char RecoveryProject[] = "openosc_companion_recovery_web";
const esp_partition_t *companion();
const esp_partition_t *recovery();
bool recoveryAvailable();
bool webRecoveryAvailable();
bool selectRecovery();
bool selectApplication();
bool validApplication();
}
