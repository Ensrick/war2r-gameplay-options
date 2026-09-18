#include "units.h"

#include <cstring>

namespace units {

const Entry* FindByName(const char* name) {
    for (const Entry& e : kUnits)
        if (_stricmp(e.name, name) == 0) return &e;
    return nullptr;
}

const Entry* FindById(uint8_t id) {
    for (const Entry& e : kUnits)
        if (e.id == id) return &e;
    return nullptr;
}

}  // namespace units
