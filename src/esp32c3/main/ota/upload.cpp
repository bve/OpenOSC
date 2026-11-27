#include "upload.hpp"
#include "boot.hpp"
#include "watchdog.hpp"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>

namespace openosc::companion::ota {
namespace {
constexpr size_t Descriptor = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
constexpr size_t ProbeBytes = Descriptor + sizeof(esp_app_desc_t);
static_assert(ProbeBytes == 288); // The browser sends this prefix before restarting into Recovery.

bool read(httpd_req_t *req, uint8_t *out, size_t bytes, bool feed = true) {
    while (bytes) {
        const int got = httpd_req_recv(req, reinterpret_cast<char *>(out), bytes);
        if (got <= 0) return false;
        bytes -= got;
        out += got;
        if (feed) Watchdog::feed();
    }
    return true;
}

template<size_t N>
bool matches(const uint8_t *prefix, const char (&project)[N]) {
    esp_image_header_t image{};
    esp_app_desc_t app{};
    std::memcpy(&image, prefix, sizeof image);
    std::memcpy(&app, prefix + Descriptor, sizeof app);
    return image.magic == ESP_IMAGE_HEADER_MAGIC && image.chip_id == ESP_CHIP_ID_ESP32C3 &&
           app.magic_word == ESP_APP_DESC_MAGIC_WORD && !std::memcmp(app.project_name, project, N);
}

esp_err_t error(httpd_req_t *req, const char *message, httpd_err_code_t code = HTTPD_400_BAD_REQUEST) {
    // esp_http_server reports whether it sent the error. The caller needs a distinct failure result.
    httpd_resp_send_err(req, code, message);
    return ESP_FAIL;
}
}

bool applicationProbe(httpd_req_t *req) {
    uint8_t prefix[ProbeBytes];
    return req->content_len == sizeof prefix && read(req, prefix, sizeof prefix, false) && matches(prefix, AppProject);
}

esp_err_t uploadApplication(httpd_req_t *req) {
    const auto *partition = companion();
    const auto size = req->content_len;
    if (!partition || size < ProbeBytes || size > partition->size)
        return error(req, "Firmware size is outside the application slot.");
    Watchdog watchdog;
    if (!watchdog.registered())
        return error(req, "Could not start the update watchdog.", HTTPD_500_INTERNAL_SERVER_ERROR);
    uint8_t bytes[1024];
    if (!read(req, bytes, ProbeBytes) || !matches(bytes, AppProject))
        return error(req, "Select the unmerged ESP32-C3 companion application .bin.");
    esp_ota_handle_t upload;
    if (esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &upload) != ESP_OK)
        return error(req, "Could not open the application slot.", HTTPD_500_INTERNAL_SERVER_ERROR);
    auto result = esp_ota_write(upload, bytes, ProbeBytes);
    for (size_t at = ProbeBytes; result == ESP_OK && at < size;) {
        const size_t count = std::min(sizeof bytes, size - at);
        result = read(req, bytes, count) ? esp_ota_write(upload, bytes, count) : ESP_FAIL;
        at += count;
    }
    if (result != ESP_OK) {
        esp_ota_abort(upload);
        return error(req, "Upload failed. Retry from Recovery.", HTTPD_500_INTERNAL_SERVER_ERROR);
    }
    if (esp_ota_end(upload) != ESP_OK) return error(req, "Image verification failed. Retry from Recovery.");
    Watchdog::feed();
    return ESP_OK;
}

esp_err_t uploadRecovery(httpd_req_t *req) {
    const auto *partition = recovery();
    const auto *app = companion();
    const auto size = req->content_len;
    if (!partition || !app || esp_ota_get_running_partition() != app)
        return error(req, "Run the installed companion application before updating Recovery.");
    const size_t sector = partition->erase_size;
    if (!sector || size < std::max(sector, ProbeBytes) || size > partition->size || size % 16)
        return error(req, "Firmware size is outside the Recovery slot.");
    const size_t eraseSize = ((size + sector - 1) / sector) * sector;
    if (eraseSize > partition->size) return error(req, "Firmware size is outside the Recovery slot.");
    Watchdog watchdog;
    std::unique_ptr<uint8_t[]> first(new (std::nothrow) uint8_t[sector]);
    if (!first || !watchdog.registered())
        return error(req, "Could not prepare the Recovery upload.", HTTPD_500_INTERNAL_SERVER_ERROR);
    if (!read(req, first.get(), sector) || !matches(first.get(), RecoveryProject))
        return error(req, "Select the OpenOSC ESP32-C3 web Recovery .bin.");
    if (esp_ota_get_boot_partition() != app && !selectApplication())
        return error(req, "Could not keep the companion application selected.", HTTPD_500_INTERNAL_SERVER_ERROR);

    // The application remains selected throughout. Write the new Recovery header after the body.
    auto result = esp_partition_erase_range(partition, 0, eraseSize);
    uint8_t bytes[1024];
    for (size_t at = sector; result == ESP_OK && at < size;) {
        const size_t count = std::min(sizeof bytes, size - at);
        result = read(req, bytes, count) ? esp_partition_write(partition, at, bytes, count) : ESP_FAIL;
        at += count;
    }
    if (result == ESP_OK) result = esp_partition_write(partition, 0, first.get(), sector);
    if (result != ESP_OK)
        return error(req, "Recovery upload failed. Retry from this page.", HTTPD_500_INTERNAL_SERVER_ERROR);
    esp_partition_pos_t position{partition->address, partition->size};
    esp_image_metadata_t metadata{};
    if (esp_image_verify(ESP_IMAGE_VERIFY, &position, &metadata) != ESP_OK || metadata.image_len != size) {
        esp_partition_erase_range(partition, 0, sector);
        return error(req, "Recovery image verification failed. Retry from this page.");
    }
    return ESP_OK;
}
}
