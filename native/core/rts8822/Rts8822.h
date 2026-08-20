// Osnovne RTS8822 operacije nad RegisterFile-om.
//
// Ovde su SAMO one koje su verifikovane protiv reference i koje ne pomeraju
// mehaniku. Motor, DMA i akvizicija dolaze u G2710-4 i G2710-5, iza
// SafetyLevel kapije.

#pragma once

#include "../device/SafetyLevel.h"
#include "RegisterFile.h"
#include "Registers.h"

namespace g2710::rts8822 {

struct LampStatus {
    bool flatbedOn = false;
    bool tmaOn = false;
};

class Rts8822 {
public:
    Rts8822(ITransport& transport, SafetyGate gate)
        : registers_(transport), gate_(gate) {}

    RegisterFile& registers() noexcept { return registers_; }
    const SafetyGate& safety() const noexcept { return gate_; }

    // --- nivo 1: read-only -----------------------------------------------

    // Da li je scan u toku (kControl bit 7). rts8822.c:3832
    Result<bool> isExecuting();

    // Da li je glava na home poziciji (kHeadSensor bit 6). rts8822.c:3805
    //
    // Ovo je CITANJE SENZORA, ne kretanje - zato nivo 1. Samo kretanje je
    // nivo 3 i dolazi u G2710-4.
    Result<bool> isHeadAtHome();

    // Koja lampa gori. rts8822.c:4088
    //
    // RTS8822BL-03A ima granu razlicitu od ostalih chipsetova: TMA zahteva
    // I bit u kLampStatus I selektor u kLampMode. Prepisano doslovno.
    Result<LampStatus> lampStatus();

    // Trenutni PWM duty cycle lampe (donjih 6 bita). rts8822.c:2512
    Result<std::uint8_t> lampPwmDutyCycle();

    // --- nivo 2: konfiguracioni upis --------------------------------------

    // Warm reset: postavi pa obrisi bit 6 u kControl. rts8822.c:3905
    //
    // Ne pomera mehaniku, ali menja stanje cipa, pa je nivo 2.
    Status warmReset();

    // Reset celog cipa preko wIndex 0x0801. rts8822.c:4203 Chipset_Reset
    Status chipsetReset();

    // Ukljuci CCD kanale. rts8822.c:3884 RTS_Enable_CCD
    //
    // Read-modify-write nad 4 bajta: donja tri bita `channels` idu u
    // kCcdChannelsLowMask, cetvrti u kCcdChannelsHighMask.
    Status enableCcdChannels(std::uint8_t channels);

private:
    RegisterFile registers_;
    SafetyGate gate_;
};

}  // namespace g2710::rts8822
