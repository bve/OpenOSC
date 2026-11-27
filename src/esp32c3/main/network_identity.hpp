#pragma once
#include "openosc/companion/protocol.hpp"
#include "nvs.h"

namespace openosc::companion {
inline constexpr char SettingsNamespace[] = "companion";
inline constexpr char ApPasswordKey[] = "ap_password";
bool networkName(char (&name)[ApNameBytes + 1]);
bool validApPassword(const char *password);
void loadApPassword(nvs_handle_t nvs, char (&password)[PasswordBytes + 1]);
bool savedStation(nvs_handle_t nvs, char (&ssid)[SsidBytes + 1], char (&password)[PasswordBytes + 1]);
}
