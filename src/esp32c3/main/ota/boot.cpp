#include "boot.hpp"
#include "esp_ota_ops.h"
#include <cstring>

namespace openosc::companion::ota {
const esp_partition_t *recovery() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "factory");
}
const esp_partition_t *companion() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "companion");
}
bool recoveryAvailable() { return recovery() && companion(); }
bool webRecoveryAvailable() {
    const auto *partition = recovery();
    esp_app_desc_t app{};
    return partition && esp_ota_get_partition_description(partition, &app) == ESP_OK &&
           !std::memcmp(app.project_name, RecoveryProject, sizeof RecoveryProject);
}
bool selectRecovery() {
    const auto *partition = recovery();
    return partition && companion() && esp_ota_set_boot_partition(partition) == ESP_OK;
}
bool selectApplication() {
    const auto *partition = companion();
    esp_app_desc_t app{};
    return partition && esp_ota_get_partition_description(partition, &app) == ESP_OK &&
           !std::memcmp(app.project_name, AppProject, sizeof AppProject) && esp_ota_set_boot_partition(partition) == ESP_OK;
}
bool validApplication() {
    const auto *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state{};
    if (esp_ota_get_state_partition(running, &state) != ESP_OK || state != ESP_OTA_IMG_PENDING_VERIFY) return true;
    return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
}
}
