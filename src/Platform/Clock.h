#pragma once

#include <chrono>

namespace kosmos::platform
{
class Clock
{
public:
    Clock();

    float Tick();
    float TotalSeconds() const;

private:
    using SteadyClock = std::chrono::steady_clock;

    SteadyClock::time_point start_;
    SteadyClock::time_point lastTick_;
};
}
