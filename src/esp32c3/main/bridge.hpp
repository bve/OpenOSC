#pragma once
#include "openosc/companion/protocol.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace openosc::companion {
// Wireless transports share one SCPI byte stream. Replacing the owner invalidates queued bytes.
class Bridge {
public:
    enum class Owner { Tcp, Ble, WebSocket };
    Bridge() : mutex_(xSemaphoreCreateMutexStatic(&storage_)) {}
    bool open(Owner owner, uint32_t now) {
        Lock lock(mutex_);
        if (session_) return false;
        if (++generation_ == 0) ++generation_;
        inbound_.clear(); outbound_.clear();
        owner_ = owner; session_ = generation_; lastActivity_ = now;
        return true;
    }
    void close(Owner owner) {
        Lock lock(mutex_);
        if (session_ && owner == owner_) reset();
    }
    void close() { Lock lock(mutex_); reset(); }
    bool active(Owner owner) {
        Lock lock(mutex_);
        return session_ && owner == owner_;
    }
    size_t input(Owner owner, const uint8_t *bytes, size_t size, uint32_t now) {
        Lock lock(mutex_);
        if (!session_ || owner != owner_) return 0;
        const size_t result = inbound_.append(bytes, size);
        if (result) lastActivity_ = now;
        return result;
    }
    size_t space(Owner owner) {
        Lock lock(mutex_);
        return session_ && owner == owner_ ? inbound_.free() : 0;
    }
    size_t output(Owner owner, uint8_t *bytes, size_t limit) {
        Lock lock(mutex_);
        return session_ && owner == owner_ ? outbound_.peek(bytes, limit) : 0;
    }
    bool hasOutput(Owner owner) {
        Lock lock(mutex_);
        return session_ && owner == owner_ && !outbound_.empty();
    }
    void sent(Owner owner, size_t size, uint32_t now) {
        Lock lock(mutex_);
        if (session_ && owner == owner_) { outbound_.consume(size); lastActivity_ = now; }
    }
    uint32_t activity() const { return lastActivity_.load(); }
    uint8_t session() {
        Lock lock(mutex_);
        return session_;
    }
    size_t read(uint8_t session, uint8_t *bytes, size_t size) {
        Lock lock(mutex_);
        if (!session || session != session_) return 0;
        const size_t got = inbound_.peek(bytes, size);
        inbound_.consume(got);
        return got;
    }
    size_t write(uint8_t session, const uint8_t *bytes, size_t size) {
        Lock lock(mutex_);
        return session && session == session_ ? outbound_.append(bytes, size) : 0;
    }
    void drop(uint8_t session) {
        Lock lock(mutex_);
        if (session && session == session_) reset();
    }
    // Request: session, receive allowance, flags, output. Reply: result, session, credit, input.
    void exchange(const Frame &request, Frame &response) {
        if (request.size < 3) { response.data[0] = uint8_t(Result::Invalid); return; }
        Lock lock(mutex_);
        const auto seen = request.data[0];
        if (seen && seen == session_) {
            if (request.data[2] || outbound_.append(request.data + 3, request.size - 3) != request.size - 3u)
                reset();
        }
        response.data[1] = session_;
        response.data[2] = uint8_t(std::min<size_t>(Stream, outbound_.free()));
        response.size = 3;
        if (session_ && seen == session_) {
            const auto got = inbound_.peek(response.data + 3, std::min<unsigned>(Stream, request.data[1]));
            inbound_.consume(got);
            response.size += got;
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
    class Queue {
    public:
        void clear() { head_ = count_ = 0; }
        bool empty() const { return count_ == 0; }
        size_t free() const { return bytes_.size() - count_; }
        size_t append(const uint8_t *bytes, size_t count) {
            const auto length = std::min(count, free());
            for (size_t i = 0; i < length; ++i) bytes_[(head_ + count_ + i) % bytes_.size()] = bytes[i];
            count_ += length;
            return length;
        }
        size_t peek(uint8_t *bytes, size_t limit) const {
            const auto length = std::min(limit, count_);
            for (size_t i = 0; i < length; ++i) bytes[i] = bytes_[(head_ + i) % bytes_.size()];
            return length;
        }
        void consume(size_t size) { size = std::min(size, count_); head_ = (head_ + size) % bytes_.size(); count_ -= size; }
    private:
        std::array<uint8_t, 2048> bytes_{};
        size_t head_ = 0, count_ = 0;
    };
    void reset() { session_ = 0; inbound_.clear(); outbound_.clear(); }
    StaticSemaphore_t storage_{};
    SemaphoreHandle_t mutex_;
    Queue inbound_, outbound_;
    Owner owner_ = Owner::Tcp;
    uint8_t session_ = 0, generation_ = 0;
    std::atomic<uint32_t> lastActivity_{0};
};
}
