// Virtuelna lampa sa krivom zagrevanja.
//
// Postoji zato sto Lamp_Warmup ne ceka fiksno vreme nego meri: cita nivo
// svakih `interval` ms i staje kada se dva uzastopna citanja razlikuju za
// manje od `diff`, ili kada istekne `tottime`. Za G2710 su te vrednosti
// 100.0 / 200 ms / 10000 ms (hp3800_checkstable).
//
// Lampa koja bi odmah bila na punom nivou ucinila bi taj kod besmislenim -
// warmup bi uvek prosao iz prvog pokusaja i niko ne bi primetio da merenje ne
// radi.
//
// Vreme se NE uzima iz sistemskog sata. Simulacija napreduje eksplicitno
// (advance), pa su testovi determinisicki i trenutni.

#pragma once

#include <cstdint>

namespace g2710::sim {

enum class LampKind {
    Flatbed,
    Tma,
};

const char* toString(LampKind kind) noexcept;

struct LampProfile {
    // Nivo koji lampa dostigne kada se potpuno zagreje, u istim jedinicama u
    // kojima CCD meri (0..65535).
    double stableLevel = 52000.0;

    // Nivo odmah po paljenju, kao udeo stabilnog.
    double coldFraction = 0.55;

    // Vremenska konstanta zagrevanja u milisekundama. Nivo prilazi stabilnom
    // eksponencijalno, kao pravi luminiscentni izvor.
    double timeConstantMs = 1500.0;
};

class VirtualLamp {
public:
    explicit VirtualLamp(LampProfile profile = {}) : profile_(profile) {}

    bool isOn() const noexcept { return on_; }

    void turnOn() noexcept;
    void turnOff() noexcept;

    // Pomeri simulirano vreme. Sve sto zavisi od zagrevanja ide kroz ovo.
    void advance(std::uint32_t milliseconds) noexcept;

    std::uint32_t onTimeMs() const noexcept { return onTimeMs_; }

    // Trenutni nivo. Ugasena lampa daje nulu.
    double level() const noexcept;

    // Udeo puta do stabilnog nivoa, 0..1. Dijagnostika za testove.
    double warmFraction() const noexcept;

    // PWM duty cycle koji je engine postavio; nivo se skalira njime.
    void setDutyCycle(std::uint8_t duty) noexcept { duty_ = duty; }
    std::uint8_t dutyCycle() const noexcept { return duty_; }

    const LampProfile& profile() const noexcept { return profile_; }
    void setProfile(const LampProfile& profile) noexcept { profile_ = profile; }

private:
    LampProfile profile_;
    bool on_ = false;
    std::uint32_t onTimeMs_ = 0;
    std::uint8_t duty_ = 0x3F;  // pun opseg je 6 bita
};

}  // namespace g2710::sim
