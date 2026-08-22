// Prenos jedne stranice - odvojen od COM ljuske, iz istog razloga kao
// WiaCapabilities.
//
// Izmereno, ne pretpostavljeno (vidi docs/ROADMAP.md, odeljak 3.1): od svih wiaservc
// pomocnih funkcija, van WIA servisa radi samo `wiasCreateDrvItem`.
// `wiasReadPropLong` i `wiasWritePropLong` vracaju E_INVALIDARG jer skladiste
// osobina pravi servis, a ne drajver. Znaci: sve sto cita WIA osobine ne moze
// se testirati bez uredjaja i servisa.
//
// Zato je granica povucena tacno tu:
//
//   drvAcquireItemData   cita osobine (wiasReadPropLong)   -> ceka H11
//                        sklopi sink i pozove runTransfer
//
//   runTransfer          lampa, planiranje, sesija, redovi,
//                        otkazivanje, napredak, zatvaranje  -> testira se sada
//
// Sve sto se moze pogresiti je ispod te granice. Iznad nje je trideset linija
// pravolinijskog citanja osobina.
//
// MINIDRV_TRANSFER_CONTEXT je obican POD, a IWiaMiniDrvCallBack obican COM
// interfejs - oboje se moze napraviti u testu. Zato i WiaCallbackSink, koji
// barata baferima servisa, ostaje testabilan; servis treba samo osobinama.

#pragma once

#include <windows.h>

#include <wia.h>

#include <wiamindr.h>

#include "WiaCapabilities.h"
#include "WiaItemContext.h"

#include <chrono>
#include <cstdint>
#include <span>

namespace g2710 {
class G2710Device;
}

namespace g2710::wia {

// Geometrija koja se zna tek kada sesija pocne. Sink je dobija pre prvog bajta
// jer WIA trazi da ova polja budu popunjena pre podataka.
struct TransferGeometry {
    int widthInPixels = 0;
    int lines = 0;
    int wiaDepth = 0;
    int xResolution = 0;
    int yResolution = 0;
    std::size_t bytesPerLine = 0;
};

// Gde idu bajtovi i ko javlja napredak.
//
// Svaka metoda sme da vrati S_FALSE - to znaci "aplikacija je odustala", i
// nije greska. Ta razlika je ceo razlog zasto povratna vrednost nije bool.
class ITransferSink {
public:
    virtual ~ITransferSink() = default;

    virtual HRESULT begin(const TransferGeometry& geometry) = 0;
    virtual HRESULT writeLine(std::span<const std::uint8_t> line) = 0;

    // Napredak u procentima. Zove se najvise kProgressSteps puta po stranici.
    virtual HRESULT progress(int percent) = 0;

    // Poslednji bajtovi i kraj stranice. Zove se samo posle uspesnog prenosa.
    virtual HRESULT finish() = 0;
};

// Koliko se ceka posle paljenja lampe pre nego sto prvi red ima smisla.
//
// Referenca meri stabilnost (Lamp_PWM_CheckStable) umesto da ceka slepo, ali
// to trazi GetOneLineInfo koji dolazi sa orkestracijom kalibracije. Do tada
// slepo cekanje - i to je zapisano u izvestaju, ne precutano.
inline constexpr std::chrono::milliseconds kLampWarmup{3000};

// Koliko cesto se javlja napredak. Prijavljivati svaki red znaci hiljade COM
// poziva po slici.
inline constexpr int kProgressSteps = 50;

// Nas ErrorCode -> HRESULT, kako ga vidi aplikacija koja skenira.
//
// Namerno NIJE `toHresult`: G2710Usd.cpp ima istoimenu funkciju sa DRUGACIJIM
// mapiranjem (STIERR_*, ne WIA_ERROR_*), jer STI i WIA nisu isti sloj.
HRESULT toTransferHresult(ErrorCode code) noexcept;

// Odradi ceo prenos: lampa, plan, sesija, redovi, zatvaranje prolaza.
//
// `warmup` je parametar, a ne konstanta, iz jednog razloga: test koji svaki put
// spava tri sekunde je test koji se prestane pokretati.
//
// `deviceError` se puni nasim ErrorCode-om kada nesto padne - WIA to prosledjuje
// aplikaciji kroz drvGetDeviceErrorStr.
HRESULT runTransfer(G2710Device& device, ItemContext& item,
                    const ItemSettings& requested, ITransferSink& sink,
                    LONG* deviceError,
                    std::chrono::milliseconds warmup = kLampWarmup);

// Sink koji pise u bafere WIA servisa.
//
// Dva rezima, oba iz MINIDRV_TRANSFER_CONTEXT:
//   traka po traka  bafer se predaje cim se napuni (bTransferDataCB)
//   ceo bafer       slika se slaze na svoje mesto i predaje na kraju
class WiaCallbackSink final : public ITransferSink {
public:
    explicit WiaCallbackSink(PMINIDRV_TRANSFER_CONTEXT transfer) : transfer_(transfer) {}

    HRESULT begin(const TransferGeometry& geometry) override;
    HRESULT writeLine(std::span<const std::uint8_t> line) override;
    HRESULT progress(int percent) override;
    HRESULT finish() override;

    // Koliko je bajtova predato aplikaciji. Za dijagnostiku i testove.
    LONG deliveredBytes() const noexcept { return delivered_; }

private:
    // Predaj ono sto je u baferu i vrati se na pocetak.
    HRESULT flush(int percent);

    PMINIDRV_TRANSFER_CONTEXT transfer_;
    bool banded_ = false;
    LONG offset_ = 0;
    LONG delivered_ = 0;
    int percent_ = 0;
};

}  // namespace g2710::wia
