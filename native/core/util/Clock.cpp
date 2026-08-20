#include "Clock.h"

#include <thread>

namespace g2710 {

void Clock::sleepFor(Duration duration) noexcept {
    if (duration.count() > 0) {
        std::this_thread::sleep_for(duration);
    }
}

Clock& systemClock() noexcept {
    static SteadyClock instance;
    return instance;
}

}  // namespace g2710
