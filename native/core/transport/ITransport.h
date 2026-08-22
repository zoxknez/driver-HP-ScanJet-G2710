// Granica koja ceo projekat cini testabilnim bez hardvera.
//
// Oblik je diktiran hp3900 protokolom (vidi docs/PROTOCOL-RTS8822.md):
//
//   control  bmRequestType 0x40 (out) / 0xC0 (in), bRequest 0x04,
//            wValue = adresa, wIndex = KOMANDA, wLength = velicina
//   bulk     EP 0x81 IN / 0x02 OUT, iskljucivo DMA payload
//   event    interrupt pipe -> fizicka dugmad
//
// wIndex NIJE prosirenje adrese. Zato ga API prima kao Command enum, a ne kao
// goli int - da se adresa i komanda ne mogu zameniti mestima.

#pragma once

#include "../util/Result.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

namespace g2710 {

// Vrednosti su doslovno wIndex iz reference. Vidi PROTOCOL-RTS8822.md, 3.
enum class Command : std::uint16_t {
    RegisterWrite  = 0x0000,
    RegisterRead   = 0x0100,
    Eeprom         = 0x0200,
    DmaEnableRead  = 0x0400,
    DmaEnableWrite = 0x0401,
    DmaCancel      = 0x0600,
    DmaOpType      = 0x0800,
    ChipsetReset   = 0x0801,
};

const char* toString(Command command) noexcept;

enum class PipeKind {
    BulkIn,
    BulkOut,
    Interrupt,
};

struct Timeouts {
    std::chrono::milliseconds read{1000};   // TIMEOUT iz reference
    std::chrono::milliseconds write{1000};
    std::chrono::milliseconds event{0};     // 0 = blokira do dogadjaja
};

// USB identitet procitan SA UREDJAJA. Svaki transport mora umeti da odgovori
// - provera "da li je ovo stvarno G2710" ne sme zavisiti od toga koji je
// transport u igri, jer bi inace neka putanja ostala neproverena.
struct DeviceIdentity {
    std::uint16_t vendorId = 0;
    std::uint16_t productId = 0;
    std::uint16_t bcdDevice = 0;
};

// Konfiguracija endpointa procitana sa uredjaja, ne pretpostavljena.
// Referenca hardkoduje 0x81 / 0x02; mi to VERIFIKUJEMO na pravom uredjaju
// (H1) umesto da verujemo na rec.
struct PipeConfiguration {
    std::uint8_t bulkIn = 0;
    std::uint8_t bulkOut = 0;
    std::uint8_t interrupt = 0;
    bool hasInterrupt = false;
};

class ITransport {
public:
    virtual ~ITransport() = default;

    ITransport(const ITransport&) = delete;
    ITransport& operator=(const ITransport&) = delete;

    // --- control -------------------------------------------------------
    // `address` je wValue, `command` je wIndex. Implementacija sme interno
    // deliti transfer na komade; pozivalac vidi jedan logicki prenos.
    virtual Status controlIn(std::uint16_t address, Command command,
                             std::span<std::byte> buffer) = 0;
    virtual Status controlOut(std::uint16_t address, Command command,
                              std::span<const std::byte> buffer) = 0;

    // Najveci payload koji ide u JEDNOM control transferu bez deljenja.
    // Register bank je 1818 bajtova (RT_BUFFER_LEN) i referenca ga salje
    // odjednom; da li usbscan.sys to dozvoljava proverava se u H2.
    virtual std::size_t maxControlChunk() const noexcept = 0;

    // --- bulk ----------------------------------------------------------
    virtual Result<std::size_t> bulkRead(std::span<std::byte> buffer) = 0;
    virtual Status bulkWrite(std::span<const std::byte> buffer) = 0;

    // --- interrupt / dugmad --------------------------------------------
    // Blokira do pritiska dugmeta ili isteka Timeouts::event. Vraca sirovu
    // masku; mapiranje maska -> dugme pripada sloju iznad.
    virtual Result<std::uint32_t> waitEvent() = 0;

    // --- upravljanje ---------------------------------------------------
    virtual Status resetPipe(PipeKind pipe) = 0;
    virtual Status setTimeouts(const Timeouts& timeouts) = 0;
    virtual Result<PipeConfiguration> pipeConfiguration() = 0;

    // Ko je zaista na drugom kraju. Cita se sa uredjaja, ne pretpostavlja iz
    // INF-a ni iz imena porta.
    virtual Result<DeviceIdentity> identity() = 0;

    // Prekida sve operacije u letu. Mora biti bezbedno pozvati iz drugog
    // thread-a dok transfer traje - to je jedini nacin da cancel radi.
    //
    // Otkazivanje je LEPLJIVO: svaki sledeci transfer pada dok se ne pozove
    // clearCancel(). Namerno - transfer koji je krenuo pre otkazivanja ne sme
    // tiho uspeti posle njega.
    virtual void cancel() noexcept = 0;

    // Otkazivanje je zavrseno; sledeci transferi su CISCENJE.
    //
    // Bez ovoga otkazivanje ostavlja uredjaj u stanju u kome se ne moze
    // zaustaviti: zaustavljanje cipa je i samo transfer, pa ga lepljivi cancel
    // odbija. Na simulatoru to izgleda kao greska pri zatvaranju prolaza; na
    // pravom skeneru glava nastavlja da se krece posle "Prekini".
    //
    // Zove ga sloj koji zna da je otkazivanje gotovo - ne sam transport, koji
    // ne moze znati da li je transfer koji stize ciscenje ili zakasneli posao.
    virtual void clearCancel() noexcept = 0;

    // Zatvara i ponovo otvara uredjaj. Posle TransportLost pozicija glave je
    // NEPOZNATA i sloj iznad mora izvrsiti HOME pre bilo cega drugog;
    // reopen() sam po sebi to ne resava. Vidi docs/SAFETY.md.
    virtual Status reopen() = 0;

    virtual bool isOpen() const noexcept = 0;

    // Kratak opis za dijagnostiku ("usbscan", "sim", "replay").
    virtual const char* name() const noexcept = 0;

protected:
    ITransport() = default;
};

}  // namespace g2710
