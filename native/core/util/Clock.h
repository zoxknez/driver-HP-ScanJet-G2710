// Izvor vremena, zamenljiv u testovima.
//
// MotionGuard prekida kretanje po isteku roka. Test koji bi to proveravao
// stvarnim cekanjem bio bi spor i povremeno nestabilan, pa vreme ide kroz
// ovaj interfejs - u radu sistemski sat, u testu rucno pomeran.

#pragma once

#include <chrono>

namespace g2710 {

using Instant = std::chrono::steady_clock::time_point;
using Duration = std::chrono::milliseconds;

class Clock {
public:
    virtual ~Clock() = default;
    virtual Instant now() const noexcept = 0;

    // Cekanje koje se u testovima moze ubrzati. Podrazumevana implementacija
    // stvarno spava.
    virtual void sleepFor(Duration duration) noexcept;
};

// Sistemski monotoni sat.
class SteadyClock final : public Clock {
public:
    Instant now() const noexcept override { return std::chrono::steady_clock::now(); }
};

// Sat koji se pomera rucno. Ne spava.
class ManualClock final : public Clock {
public:
    Instant now() const noexcept override { return now_; }

    void sleepFor(Duration duration) noexcept override { advance(duration); }

    void advance(Duration duration) noexcept { now_ += duration; }

private:
    Instant now_{};
};

// Zajednicki sistemski sat; pozivaoci koji nemaju razloga za drugaciji koriste
// njega.
Clock& systemClock() noexcept;

}  // namespace g2710
