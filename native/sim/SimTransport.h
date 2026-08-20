// Minimalni RTS8822 simulator.
//
// OBIM: dovoljno da se dokaze transportni sloj i CLI bez hardvera. Pun
// simulator - virtuelni motor sa mehanickim limitima, home senzor, lampe sa
// warmup krivom, CCD koji renderuje test-metu, FailureInjector - dolazi u
// G2710-3.
//
// Vazno: register bank ima STVARNU semantiku (write -> stanje), ne echo. Bez
// toga bi Write_Byte read-modify-write ciklus iz reference "prosao" i na
// neispravnoj implementaciji.

#pragma once

#include "FailureInjector.h"
#include "VirtualCcd.h"
#include "VirtualLamp.h"
#include "VirtualMotor.h"
#include "transport/ITransport.h"
#include "transport/ITransportProvider.h"

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

namespace g2710::sim {

class SimTransport final : public ITransport {
public:
    SimTransport();

    Status controlIn(std::uint16_t address, Command command,
                     std::span<std::byte> buffer) override;
    Status controlOut(std::uint16_t address, Command command,
                      std::span<const std::byte> buffer) override;
    std::size_t maxControlChunk() const noexcept override { return maxControlChunk_; }

    Result<std::size_t> bulkRead(std::span<std::byte> buffer) override;
    Status bulkWrite(std::span<const std::byte> buffer) override;

    Result<std::uint32_t> waitEvent() override;

    Status resetPipe(PipeKind pipe) override;
    Status setTimeouts(const Timeouts& timeouts) override;
    Result<PipeConfiguration> pipeConfiguration() override;

    void cancel() noexcept override;
    Status reopen() override;
    bool isOpen() const noexcept override { return open_; }
    const char* name() const noexcept override { return "sim"; }

    // --- kontrola simulacije, samo za testove --------------------------
    void setMaxControlChunk(std::size_t bytes) noexcept { maxControlChunk_ = bytes; }
    void pressButton(std::uint32_t mask) noexcept { pendingEvent_ = mask; }

    // Pokvari sledecih N read-back operacija. Sluzi da se dokaze da
    // verifikaciona petlja iz RTS_DMA_Write stvarno ponavlja upis, a ne da
    // slucajno prolazi iz prvog pokusaja.
    void corruptNextDmaReadBacks(int count) noexcept { corruptReadBacks_ = count; }

    std::uint16_t dmaOperationType() const noexcept { return dmaOperationType_; }
    std::size_t dmaLength() const noexcept { return dmaLength_; }

    // --- simulirani hardver --------------------------------------------
    //
    // Stanje ovih komponenti se PRESLIKAVA u registre pri svakom citanju, pa
    // ga engine vidi kroz iste adrese kao na pravom uredjaju - ne kroz neki
    // poseban testni kanal koji u produkciji ne postoji.

    VirtualMotor& motor() noexcept { return motor_; }
    const VirtualMotor& motor() const noexcept { return motor_; }

    VirtualLamp& flatbedLamp() noexcept { return flatbedLamp_; }
    const VirtualLamp& flatbedLamp() const noexcept { return flatbedLamp_; }

    VirtualLamp& tmaLamp() noexcept { return tmaLamp_; }
    const VirtualLamp& tmaLamp() const noexcept { return tmaLamp_; }

    VirtualCcd& ccd() noexcept { return ccd_; }
    const VirtualCcd& ccd() const noexcept { return ccd_; }

    FailureInjector& faults() noexcept { return faults_; }

    // Pomeri simulirano vreme. Zagrevanje lampe zavisi iskljucivo od ovoga,
    // ne od sistemskog sata - testovi su zato trenutni i deterministicki.
    void advanceTime(std::uint32_t milliseconds) noexcept;

    std::uint8_t peekRegister(std::uint16_t address) const noexcept;
    void pokeRegister(std::uint16_t address, std::uint8_t value) noexcept;

    // Da li registar odrazava STANJE HARDVERA umesto da bude obicna memorija.
    //
    // Home senzor i status lampi se ne mogu proizvoljno upisati ni na pravom
    // uredjaju - vrednost dolazi iz mehanike, ne iz onoga sto je upisano.
    // Preslikavanje ih prepisuje pri svakom citanju, pa test koji ocekuje
    // round-trip kroz njih testira pogresnu stvar.
    static bool isHardwareBackedRegister(std::uint16_t address) noexcept;

    int chipsetResetCount() const noexcept { return chipsetResets_; }
    int controlInCount() const noexcept { return controlIns_; }
    int controlOutCount() const noexcept { return controlOuts_; }

private:
    Status checkOpen(const char* context) const;
    std::size_t registerIndex(std::uint16_t address) const noexcept;

    // Prepisi stanje motora i lampi u odgovarajuce registre pre citanja.
    void mirrorHardwareIntoRegisters() noexcept;

    // Primeni zakazani otkaz, ako ga ima.
    Status applyFault(TransferKind kind, const char* context);

    VirtualMotor motor_;
    VirtualLamp flatbedLamp_;
    VirtualLamp tmaLamp_;
    VirtualCcd ccd_;
    FailureInjector faults_;

    bool open_ = true;
    bool cancelled_ = false;
    std::size_t maxControlChunk_ = 0;
    Timeouts timeouts_{};

    std::vector<std::uint8_t> registers_;
    std::vector<std::uint8_t> eeprom_;

    // DMA memorija ZIVI izmedju enable poziva. Brisanje pri svakom enable-u
    // bi ucinilo write-verify petlju iz RTS_DMA_Write besmislenom: citanje
    // nazad bi uvek vracalo nule.
    std::vector<std::uint8_t> dmaMemory_;
    std::size_t dmaLength_ = 0;
    std::uint16_t dmaOperationType_ = 0;
    int corruptReadBacks_ = 0;

    std::uint32_t pendingEvent_ = 0;
    int chipsetResets_ = 0;
    int controlIns_ = 0;
    int controlOuts_ = 0;
};

class SimTransportProvider final : public ITransportProvider {
public:
    Result<std::unique_ptr<ITransport>> create(const DeviceRef& ref) override;
    const char* name() const noexcept override { return "sim"; }
};

}  // namespace g2710::sim
