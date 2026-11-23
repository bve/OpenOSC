#pragma once
#include "ff.h"
#include "openosc/scpi/backend.hpp"
#include "openosc/acquisition/mailbox.hpp"
#include "openosc/decode/record_key.hpp"
#include "openosc/generator/arbitrary.hpp"

namespace openosc {
class ApplicationController;
class InstrumentBackend final : public scpi::Backend {
public:
    explicit InstrumentBackend(ApplicationController &application);
    bool multimeter() const override;
    const ScopeRequest &scopeRequest() const override;
    const scope::FrameMetadata *configuration() const override;
    bool configureScope(const ScopeRequest &) override;
    bool setTrigger(const ScopeRequest &) override;
    bool channelShown(unsigned channel) const override;
    int16_t showChannel(unsigned channel, bool on) override;
    unsigned meterTraceFunction() const override;
    int16_t meterTraceFunction(unsigned function) override;
    bool run(bool single) override;
    bool autoScale() override;
    bool forceTrigger() override;
    uint32_t frameSequence() const override;
    void stop() override;
    bool held() const override;
    bool waiting() const override;
    bool triggered() const override;
    const scope::FrameView *frame(bool refresh) override;
    void releaseFrame() override;
    void endSession() override;
    bool frameKey(char *output, size_t capacity) const override;
    bool decodeReport(scpi::DecodeReport &) const override;
    uint32_t decodeExport(int16_t &error) override;
    int decodeMeasure(uint32_t ticket, uint32_t &total) override;
    size_t decodeRead(uint32_t ticket, uint8_t *output, size_t capacity) override;
    void decodeEnd(uint32_t ticket) override;
    int16_t decodeSave() override;
    const char *decodeSaved() const override;
    const dmm::Result &meter() const override;
    DataStatus meterStatus() const override;
    bool selectFunction(dmm::Function) override;
    dmm::TemperatureSettings meterTemperature() const override;
    bool meterTemperature(dmm::TemperatureSettings settings) override;
    bool setAuto(bool) override;
    bool setRange(unsigned) override;
    bool holdMeter(bool) override;
    bool relativeMeter(bool) override;
    scpi::MeterHold meterHold() const override;
    bool meterHold(const dmm::HoldSettings &settings) override;
    bool armMeterHold() override;
    dmm::StatisticsView meterStatistics() const override;
    void resetMeterStatistics() override;
    MeasurementMode screen() const override;
    bool screen(MeasurementMode) override;
    bool pressKey(uint32_t key, uint32_t ms) override;
    bool busy() const override;
    uint32_t operationTimeoutMs() const override;
    DataStatus operationStatus() const override;
    generator::Snapshot generator() const override;
    bool configureGenerator(const generator::Settings &) override;
    bool output(bool) override;
    int16_t arbitraryBegin(const void *owner, uint32_t bytes) override;
    void arbitraryByte(const void *owner, uint8_t byte) override;
    int16_t arbitraryCommit(const void *owner) override;
    void arbitraryAbort(const void *owner) override;
    size_t arbitraryRead(uint32_t revision, uint32_t offset, uint8_t *out, size_t size) override;
    bool generatorStartup() const override;
    bool generatorStartup(bool restore) override;
    InterleaveStatus interleave() const override;
    bool interleaveAuto() override;
    bool interleave(bool on) override;
    bool remote() const override;
    bool remote(bool) override;
    bool performance(PerformanceSnapshot &) const override;
    bool memory(MemorySnapshot &) const override;
    size_t stalls(char *out, size_t size) const override;
    size_t bootTimes(char *out, size_t size) const override;
    size_t meterPacket(const uint8_t *&data) const override;
    uint32_t screenCapture() override;
    void screenEnd(uint32_t ticket) override;
    size_t screenRead(uint32_t ticket, uint32_t offset, uint8_t *out, size_t size) override;
    bool factoryCalibration(FactoryCalibration &) const override;
    bool factoryRecordFormat(FactoryRecordReading &) const override;
    bool recovery() override;
    uint32_t coreMhz() const override;
    int coreMhz(uint32_t mhz) override;
    bool counter(unsigned channel,scope::CounterReading &reading) const override;
    bool shortWindowCounter() const override;
    bool idleSettings(IdleSettings &) const override;
    int setIdle(const IdleSettings &) override;
    int fileList(const char *folder, uint32_t start, char *out, size_t size, uint32_t &count, uint32_t &total) override;
    int fileOpen(const char *path, uint32_t &bytes) override;
    size_t fileRead(uint32_t at, uint8_t *out, size_t size) override;
    void fileClose() override;
    int fileDelete(const char *path) override;
    scope::CaptureProgress captureProgress() const override;
    const char *serial() const override { return serial_; }
    const char *version() const override;
private:
    bool configure(const ScopeRequest &request, bool level); // configureScope and setTrigger: LevelIntent.
    // SOUR:ARB:DATA's block, decoded into the generator's stage as it comes (remote/arbitrary.cpp).
    struct ArbitraryStage {
        const void *owner = nullptr; // The session whose block is staged (none: no stage of this backend's).
        generator::ArbitraryBlock block;
    } arbitrary_;
    ApplicationController &application_;
    scope::FrameLease lease_;
    decoding::RecordKey leaseKey_; // The lease's full key, frozen when it was taken (WAV:KEY?).
    bool decodedRecords() const;   // Complete in RUN: only decoded records are served.
    bool record(scope::FrameLease &out);
    unsigned host_; // Its slot in DataAccess::frameHosts().
    void demand() const; // This session reads records: the accepted one on show is kept for it.
    uint64_t previousFrameTime_ = 0;
    uint32_t previousFrameSequence_ = 0;
    bool freshFrame_ = false;
    mutable dmm::Result reading_;
    char serial_[25]{};
    FIL file_{};
    bool fileOpen_ = false, fileShot_ = false, fileCapture_ = false; // The open file counts as a screenshot or a waveform reader.
};
} // namespace openosc
