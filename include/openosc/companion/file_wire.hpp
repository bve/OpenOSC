#pragma once
#include "openosc/companion/protocol.hpp"

namespace openosc::companion::files {
constexpr unsigned PathBytes = 512;
constexpr unsigned Chunk = Payload - 3;
enum class Operation : uint8_t {
    Path, List, Next, Name, Download, Read, Upload, Write, Commit, Close, MakeDirectory, Remove, Rename
};
enum class Status : uint8_t { Ok, End, Invalid, Missing, Exists, Denied, Busy, Full, Io, Skip };
}
