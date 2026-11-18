#pragma once
#include "util.h"

namespace eth::trainer {
struct Field {
    const char* name;
    uintptr_t offset;
    bool floating;
    double minimum, maximum;
};
const Field* FindField(const char* name);
bool Supported();
// Address is a short-lived identity token, not permission to write arbitrary memory.
bool Read(const char* name, double& value, uintptr_t& address);
bool Write(const char* name, double value, uintptr_t expectedAddress);
}
