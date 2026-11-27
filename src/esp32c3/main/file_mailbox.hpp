#pragma once
#include "openosc/companion/file_wire.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <atomic>

namespace openosc::companion {
// The HTTP server is the sole requester. The UART loop only exchanges queued frames.
class FileMailbox {
public:
    FileMailbox() : mutex_(xSemaphoreCreateMutexStatic(&storage_)) {}
    bool active() const { return active_.load(); }
    void active(bool value) { active_ = value; }
    void online(bool value) { online_ = value; }
    bool call(files::Operation op, const uint8_t *data, unsigned size, Frame &reply) {
        if (!online_ || size > files::Chunk) return false;
        {
            Lock lock(mutex_);
            if (++id_ == 0) ++id_;
            request_ = {};
            request_.size = size + 3;
            request_.data[1] = id_;
            request_.data[2] = uint8_t(op);
            if (size) std::memcpy(request_.data + 3, data, size);
            reply_.size = 0;
            pending_ = true;
        }
        const auto start = xTaskGetTickCount();
        while (online_ && xTaskGetTickCount() - start < pdMS_TO_TICKS(4500)) {
            {
                Lock lock(mutex_);
                if (reply_.size) { reply = reply_; return true; }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        Lock lock(mutex_);
        pending_ = false;
        return false;
    }
    void exchange(const Frame &incoming, Frame &outgoing) {
        Lock lock(mutex_);
        if (pending_ && incoming.size >= 2 && incoming.data[0] == id_) {
            reply_ = incoming;
            pending_ = false;
        }
        if (pending_) {
            outgoing.size = request_.size;
            std::memcpy(outgoing.data + 1, request_.data + 1, request_.size - 1);
        } else {
            outgoing.size = 2;
            outgoing.data[1] = 0;
        }
    }
private:
    class Lock {
    public:
        explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex) { xSemaphoreTake(mutex_, portMAX_DELAY); }
        ~Lock() { xSemaphoreGive(mutex_); }
    private:
        SemaphoreHandle_t mutex_;
    };
    StaticSemaphore_t storage_{};
    SemaphoreHandle_t mutex_;
    std::atomic<bool> pending_{false}, online_{false}, active_{false};
    Frame request_{}, reply_{};
    uint8_t id_ = 0;
};
}
