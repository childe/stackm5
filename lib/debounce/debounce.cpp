#include "debounce.h"

void Debounce::touch(uint32_t nowMs)
{
    _last = nowMs;
    _pending = true;
}

bool Debounce::due(uint32_t nowMs)
{
    if (!_pending) return false;
    if (static_cast<uint32_t>(nowMs - _last) < _quietMs) return false;
    _pending = false;
    return true;
}
