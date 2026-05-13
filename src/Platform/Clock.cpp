#include "Platform/Clock.h"

namespace kosmos::platform
{
Clock::Clock()
    : start_(SteadyClock::now())
    , lastTick_(start_)
{
}

float Clock::Tick()
{
    const auto now = SteadyClock::now();
    const std::chrono::duration<float> delta = now - lastTick_;
    lastTick_ = now;
    return delta.count();
}

float Clock::TotalSeconds() const
{
    const std::chrono::duration<float> elapsed = SteadyClock::now() - start_;
    return elapsed.count();
}
}
