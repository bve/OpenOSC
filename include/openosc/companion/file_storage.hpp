#pragma once
#include "openosc/companion/file_wire.hpp"
#include "ff.h"

namespace openosc::companion {
class FileStorage {
public:
    void admission(bool mayModify) { mayModify_ = mayModify; }
    bool expired(uint32_t now) const { return now - activity_ >= 8000; }
    void close();
    void answer(const Frame &request, Frame &reply, uint32_t now);
private:
    using Status = files::Status;
    Status perform(files::Operation operation, const uint8_t *data, unsigned size, Frame &reply);
    Status next(Frame &reply);
    Status readName(unsigned offset, Frame &reply) const;
    Status beginUpload(uint32_t bytes);
    Status finishUpload();
    bool mayModify_ = false;
    bool openDirectory_ = false, download_ = false, upload_ = false, staged_ = false;
    uint32_t activity_ = 0, left_ = 0;
    unsigned pathSize_[2]{};
    char path_[2][files::PathBytes]{};
    DIR directory_{};
    FILINFO entry_{};
    FIL file_{};
};
void serviceFileStorage(uint32_t now, bool mounted, bool mayModify);
void releaseFileStorage();
void answerFileStorage(const Frame &request, Frame &reply, uint32_t now);
}
