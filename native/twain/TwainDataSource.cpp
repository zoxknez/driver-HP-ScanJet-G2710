#include "TwainDataSource.h"

#include "device/G2710Device.h"
#include "rts8822/Lamp.h"
#include "rts8822/RegisterFile.h"
#include "scan/ScanPlanner.h"
#include "scan/ScanSession.h"

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <memory>
#include <mutex>

namespace {

enum class State : unsigned short { PreSession = 3, Open = 4, Enabled = 5, Transferring = 6, TransferDone = 7 };

struct Source {
    // DSM i aplikacija mogu uci iz razlicitih niti. TWAIN state machine je
    // namerno jedan serijski tok; bez ove brave dve poruke bi mogle obe
    // videti isto stanje i preskociti obavezan prelaz.
    std::mutex mutex;
    State state = State::PreSession;
    TW_UINT16 condition = TWCC_SUCCESS;
    // DS ne pravi paralelni "lazni" lock: G2710Device::begin() je jedini
    // vlasnik globalne arbitraze i isti je put kao WIA i desktop aplikacija.
    std::unique_ptr<g2710::G2710Device> device;
    std::unique_ptr<g2710::rts8822::RegisterFile> registers;
    std::unique_ptr<g2710::scan::ScanSession> session;
    int deliveredLines = 0;
    TW_ENTRYPOINT entryPoint{};
};

Source& source() {
    static Source instance;
    return instance;
}

TW_UINT16 fail(TW_UINT16 condition) {
    source().condition = condition;
    return TWRC_FAILURE;
}

// Zatvori prolaz i pusti uredjaj. Vraca false ako cip NIJE zaustavljen.
//
// Ishod se vraca, a ne odbacuje: zaustavljanje cipa je i samo transfer i moze
// da padne. Prva verzija je pisala `(void)s.session->finish();` - pa bi TWAIN
// prijavio uspeh i kada glava nastavlja da se krece.
//
// Destruktor sesije je i dalje mreza i zatvorice prolaz ako se ovde nije
// stiglo. Zato se ova dva puta spolja ne razlikuju kada sve prodje - razlikuju
// se samo kada NE prodje, i bas to je jedino sto se isplati meriti.
bool endTransfer(Source& s) noexcept {
    bool closed = true;
    if (s.session && s.session->started() && !s.session->finished()) {
        closed = static_cast<bool>(s.session->finish());
    }
    s.session.reset();
    s.registers.reset();
    if (s.device) {
        (void)s.device->end();
        s.device.reset();
    }
    s.deliveredLines = 0;
    return closed;
}

bool beginTransfer(Source& s) {
    g2710::DeviceOptions options;
    options.safety = g2710::SafetyGate{g2710::SafetyLevel::FullScan};
    options.clientName = "TWAIN";
    options.acquireTimeout = std::chrono::milliseconds(0);

    auto opened = g2710::G2710Device::open(g2710::DeviceRef::defaultUsbScan(), options);
    if (!opened) return false;
    s.device = std::move(opened.value());
    if (!s.device->identify() || !s.device->begin()) {
        endTransfer(s);
        return false;
    }

    s.registers = std::make_unique<g2710::rts8822::RegisterFile>(s.device->transport());
    g2710::rts8822::Lamp lamp{*s.registers, s.device->safety()};
    if (!lamp.setLamp(g2710::rts8822::LampKind::Flatbed, true) ||
        !lamp.setupPwm(g2710::rts8822::LampKind::Flatbed)) {
        endTransfer(s);
        return false;
    }

    // Mala fiksna oblast je namerno interna: dok H8 nije hardverski potvrdjen,
    // produkcioni DS ne sme da nudi rezolucije. Harness gradi istu putanju sa
    // eksplicitnim test-kompajlerskim prekidacem i simulatorom.
    g2710::scan::ScanRequest request;
    request.resolution = 300;
    request.colorMode = g2710::image::ColorMode::Color;
    request.depth = 8;
    request.region = {0, 0, 64, 8};
#if defined(G2710_TWAIN_ALLOW_UNQUALIFIED)
    request.allowUnqualified = true;
#endif
    auto planned = g2710::scan::planScan(request);
    if (!planned) {
        endTransfer(s);
        return false;
    }
    s.session = std::make_unique<g2710::scan::ScanSession>(*s.registers, s.device->safety(), planned.value());
    if (!s.session->begin()) {
        endTransfer(s);
        return false;
    }
    return true;
}

TW_UINT16 nativeTransfer(Source& s, TW_MEMREF data) {
    if (!data || !s.entryPoint.DSM_MemAllocate || !s.entryPoint.DSM_MemLock || !s.entryPoint.DSM_MemUnlock) {
        return fail(TWCC_CAPBADOPERATION);
    }
    if (!s.session && !beginTransfer(s)) return fail(TWCC_OPERATIONERROR);

    const std::size_t bytesPerLine = s.session->outputBytesPerLine();
    const int lines = s.session->expectedOutputLines();
    if (bytesPerLine == 0 || lines <= 0) { endTransfer(s); return fail(TWCC_OPERATIONERROR); }

    // Native transfer je Windows DIB. Core daje RGB redove odozgo nadole;
    // 24-bit DIB je BGR odozdo nagore i svaki red mora biti poravnat na 4.
    const std::size_t stride = (bytesPerLine + 3u) & ~std::size_t{3u};
    const std::size_t imageBytes = stride * static_cast<std::size_t>(lines);
    const std::size_t total = sizeof(BITMAPINFOHEADER) + imageBytes;
    if (total > static_cast<std::size_t>(UINT32_MAX)) { endTransfer(s); return fail(TWCC_LOWMEMORY); }
    const TW_HANDLE handle = s.entryPoint.DSM_MemAllocate(static_cast<TW_UINT32>(total));
    if (!handle) { endTransfer(s); return fail(TWCC_LOWMEMORY); }
    auto* raw = static_cast<TW_UINT8*>(s.entryPoint.DSM_MemLock(handle));
    if (!raw) {
        if (s.entryPoint.DSM_MemFree) s.entryPoint.DSM_MemFree(handle);
        endTransfer(s);
        return fail(TWCC_LOWMEMORY);
    }
    std::memset(raw, 0, total);
    auto* header = reinterpret_cast<BITMAPINFOHEADER*>(raw);
    header->biSize = sizeof(BITMAPINFOHEADER);
    header->biWidth = static_cast<LONG>(bytesPerLine / 3u);
    header->biHeight = lines;
    header->biPlanes = 1;
    header->biBitCount = 24;
    header->biCompression = BI_RGB;
    header->biSizeImage = static_cast<DWORD>(imageBytes);

    std::vector<std::uint8_t> line(bytesPerLine);
    bool okay = true;
    for (int y = 0; y < lines; ++y) {
        auto more = s.session->nextLine(line, s.device->cancellation());
        if (!more || !more.value()) { okay = false; break; }
        auto* destination = raw + sizeof(BITMAPINFOHEADER) +
            static_cast<std::size_t>(lines - 1 - y) * stride;
        for (std::size_t x = 0; x < bytesPerLine; x += 3) {
            destination[x] = line[x + 2];
            destination[x + 1] = line[x + 1];
            destination[x + 2] = line[x];
        }
    }
    s.entryPoint.DSM_MemUnlock(handle);
    if (!okay) {
        if (s.entryPoint.DSM_MemFree) s.entryPoint.DSM_MemFree(handle);
        endTransfer(s);
        return fail(TWCC_OPERATIONERROR);
    }
    *static_cast<TW_HANDLE*>(data) = handle;
    s.deliveredLines = lines;
    s.state = State::TransferDone;
    s.condition = TWCC_SUCCESS;
    return TWRC_XFERDONE;
}

void identity(pTW_IDENTITY out) {
    if (!out) return;
    std::memset(out, 0, sizeof(*out));
    // Verzija dolazi iz korenskog VERSION fajla, kao i za sve ostalo.
    //
    // Ovo je broj koji TWAIN aplikacija prikazuje u spisku izvora. Dok je bio
    // otkucan ovde, bio je sedmo mesto na kome je pisala verzija - a takva se
    // mesta razidju cim se jedno zaboravi.
#ifndef G2710_VERSION_MAJOR
#error "G2710_VERSION_MAJOR mora doci iz build sistema"
#endif
    out->Version.MajorNum = G2710_VERSION_MAJOR;
    out->Version.MinorNum = G2710_VERSION_MINOR;
    out->ProtocolMajor = TWON_PROTOCOLMAJOR;
    out->ProtocolMinor = TWON_PROTOCOLMINOR;
    out->SupportedGroups = DG_CONTROL | DG_IMAGE;
    strcpy_s(out->Manufacturer, "G2710 Project");
    strcpy_s(out->ProductFamily, "HP ScanJet");
    strcpy_s(out->ProductName, "HP ScanJet G2710");
}

void imageInfo(pTW_IMAGEINFO out) {
    if (!out) return;
    std::memset(out, 0, sizeof(*out));
    // Ovo je geometrija internog, malog prenosa dok H8 ne otkljuca korisnicki
    // izbor rezolucije. Vrednosti su iste kao ScanRequest u beginTransfer().
    out->XResolution = {300, 0};
    out->YResolution = {300, 0};
    out->ImageWidth = 64;
    out->ImageLength = 8;
    out->SamplesPerPixel = 3;
    out->BitsPerSample[0] = 8;
    out->BitsPerSample[1] = 8;
    out->BitsPerSample[2] = 8;
    out->BitsPerPixel = 24;
    out->PixelType = TWPT_RGB;
    out->Compression = TWCP_NONE;
}

void imageLayout(pTW_IMAGELAYOUT out) {
    if (!out) return;
    std::memset(out, 0, sizeof(*out));
    // TWAIN frame je u inch jedinicama. 64x8 na 300 dpi = 0.2133x0.0266 in.
    out->Frame.Right = {0, static_cast<TW_UINT16>((64u << 16) / 300u)};
    out->Frame.Bottom = {0, static_cast<TW_UINT16>((8u << 16) / 300u)};
    out->DocumentNumber = 1;
    out->PageNumber = 1;
    out->FrameNumber = 1;
}

// Mogucnosti koje zaista odgovaraju na MSG_GET.
//
// Jedan spisak, tri odgovora: CAP_SUPPORTEDCAPS ga vraca aplikaciji,
// MSG_QUERYSUPPORT iz njega izvodi masku operacija, a MSG_GET po njemu odlucuje
// da li uopste ima sta da vrati. Dok su bila tri odvojena mesta, ona su davala
// tri razlicita odgovora o istoj stvari.
constexpr TW_UINT16 kSupportedCapabilities[] = {
    ICAP_XFERMECH, ICAP_UNITS, ICAP_PIXELTYPE, ICAP_BITDEPTH};

bool isSupportedCapability(TW_UINT16 capability) {
    for (TW_UINT16 supported : kSupportedCapabilities) {
        if (supported == capability) {
            return true;
        }
    }
    return false;
}

bool isKnownCapability(TW_UINT16 capability) {
    // Rezolucija se PREPOZNAJE ali se ne podrzava - dok H8 ne prodje nijedna
    // vrednost nije hardverski potvrdjena. Razlika je vazna: na poznatu a
    // nepodrzanu mogucnost odgovaramo maskom bez ijedne operacije, sto je
    // jasnije aplikaciji od "ne znam sta je to".
    return capability == CAP_SUPPORTEDCAPS || isSupportedCapability(capability) ||
           capability == ICAP_XRESOLUTION || capability == ICAP_YRESOLUTION;
}

TW_UINT16 capability(TW_UINT16 message, pTW_CAPABILITY cap) {
    if (!cap || !isKnownCapability(cap->Cap)) return fail(TWCC_BADCAP);

    auto& entry = source().entryPoint;
    if (!entry.DSM_MemAllocate || !entry.DSM_MemLock || !entry.DSM_MemUnlock) {
        source().condition = TWCC_CAPBADOPERATION;
        return TWRC_DATANOTAVAILABLE;
    }

    // MSG_QUERYSUPPORT mora vratiti TW_ONEVALUE sa maskom TWQC_* operacija.
    //
    // Prva verzija je vracala TWRC_SUCCESS sa hContainer = nullptr. Aplikacija
    // koja radi ono sto standard nalaze - proveri kod, pa zakljuca kontejner -
    // zakljucavala bi nulu. To se ne vidi ni u jednom nasem prolazu, nego u
    // tudjem programu, kao pad skenera koji "ne radi sa ovim drajverom".
    //
    // Nula operacija je ISPRAVAN odgovor za rezoluciju: mogucnost postoji u
    // standardu, ovaj build je ne nudi, i aplikacija to sme da procita umesto
    // da pogadja.
    if (message == MSG_QUERYSUPPORT) {
        const TW_HANDLE handle = entry.DSM_MemAllocate(sizeof(TW_ONEVALUE));
        if (!handle) return fail(TWCC_LOWMEMORY);
        auto* one = static_cast<pTW_ONEVALUE>(entry.DSM_MemLock(handle));
        if (!one) { if (entry.DSM_MemFree) entry.DSM_MemFree(handle); return fail(TWCC_LOWMEMORY); }
        one->ItemType = TWTY_INT32;
        one->Item = (cap->Cap == CAP_SUPPORTEDCAPS || isSupportedCapability(cap->Cap))
                        ? (TWQC_GET | TWQC_GETCURRENT | TWQC_GETDEFAULT)
                        : 0;
        entry.DSM_MemUnlock(handle);
        cap->ConType = TWON_ONEVALUE;
        cap->hContainer = handle;
        source().condition = TWCC_SUCCESS;
        return TWRC_SUCCESS;
    }
    if (message != MSG_GET && message != MSG_GETCURRENT && message != MSG_GETDEFAULT) {
        return fail(TWCC_CAPBADOPERATION);
    }
    TW_UINT32 value = 0;
    TW_UINT16 type = TWTY_UINT16;
    if (cap->Cap == CAP_SUPPORTEDCAPS) {
        const auto& supported = kSupportedCapabilities;
        const TW_HANDLE handle = entry.DSM_MemAllocate(
            static_cast<TW_UINT32>(sizeof(TW_ARRAY) + sizeof(supported) - sizeof(TW_UINT8)));
        if (!handle) return fail(TWCC_LOWMEMORY);
        auto* array = static_cast<pTW_ARRAY>(entry.DSM_MemLock(handle));
        if (!array) { if (entry.DSM_MemFree) entry.DSM_MemFree(handle); return fail(TWCC_LOWMEMORY); }
        array->ItemType = TWTY_UINT16;
        array->NumItems = static_cast<TW_UINT32>(std::size(supported));
        std::memcpy(array->ItemList, supported, sizeof(supported));
        entry.DSM_MemUnlock(handle);
        cap->ConType = TWON_ARRAY;
        cap->hContainer = handle;
        source().condition = TWCC_SUCCESS;
        return TWRC_SUCCESS;
    }
    if (cap->Cap == ICAP_XFERMECH && message == MSG_GET) {
        constexpr TW_UINT16 mechanisms[] = {TWSX_NATIVE, TWSX_MEMORY};
        const TW_HANDLE handle = entry.DSM_MemAllocate(
            static_cast<TW_UINT32>(sizeof(TW_ENUMERATION) + sizeof(mechanisms) - sizeof(TW_UINT8)));
        if (!handle) return fail(TWCC_LOWMEMORY);
        auto* enumeration = static_cast<pTW_ENUMERATION>(entry.DSM_MemLock(handle));
        if (!enumeration) { if (entry.DSM_MemFree) entry.DSM_MemFree(handle); return fail(TWCC_LOWMEMORY); }
        enumeration->ItemType = TWTY_UINT16;
        enumeration->NumItems = static_cast<TW_UINT32>(std::size(mechanisms));
        enumeration->CurrentIndex = 1; // memory je podrazumevani, striming put.
        enumeration->DefaultIndex = 1;
        std::memcpy(enumeration->ItemList, mechanisms, sizeof(mechanisms));
        entry.DSM_MemUnlock(handle);
        cap->ConType = TWON_ENUMERATION;
        cap->hContainer = handle;
        source().condition = TWCC_SUCCESS;
        return TWRC_SUCCESS;
    }
    switch (cap->Cap) {
    case ICAP_XFERMECH: value = TWSX_MEMORY; break;
    case ICAP_UNITS: value = TWUN_INCHES; break;
    case ICAP_PIXELTYPE: value = TWPT_RGB; break;
    case ICAP_BITDEPTH: value = 8; break;
    // H8 jos nije prosao: rezoluciju ne smemo ni kontejnerom da obecamo.
    case ICAP_XRESOLUTION:
    case ICAP_YRESOLUTION: return fail(TWCC_CAPUNSUPPORTED);
    default: return fail(TWCC_BADCAP);
    }
    const TW_HANDLE handle = entry.DSM_MemAllocate(sizeof(TW_ONEVALUE));
    if (!handle) return fail(TWCC_LOWMEMORY);
    auto* one = static_cast<pTW_ONEVALUE>(entry.DSM_MemLock(handle));
    if (!one) { if (entry.DSM_MemFree) entry.DSM_MemFree(handle); return fail(TWCC_LOWMEMORY); }
    one->ItemType = type;
    one->Item = value;
    entry.DSM_MemUnlock(handle);
    cap->ConType = TWON_ONEVALUE;
    cap->hContainer = handle;
    source().condition = TWCC_SUCCESS;
    return TWRC_SUCCESS;
}

}  // namespace

TW_UINT16 TW_CALLINGSTYLE G2710TwainEntry(pTW_IDENTITY,
                                          pTW_IDENTITY destination,
                                          TW_UINT32 dg,
                                          TW_UINT16 dat,
                                          TW_UINT16 msg,
                                          TW_MEMREF data) {
    Source& s = source();
    const std::scoped_lock lock(s.mutex);
    if (dg == DG_CONTROL && dat == DAT_IDENTITY) {
        // Open-source DSM pri ucitavanju .ds prvo salje MSG_GET; aplikacije
        // potom koriste GETFIRST/GETDEFAULT. Sva tri za jedini izvor vracaju
        // istu identity vrednost.
        if (msg == MSG_GET || msg == MSG_GETFIRST || msg == MSG_GETDEFAULT) {
            identity(static_cast<pTW_IDENTITY>(data)); return TWRC_SUCCESS;
        }
        if (msg == MSG_GETNEXT) return TWRC_ENDOFLIST;
        if (msg == MSG_OPENDS && s.state == State::PreSession) {
            s.state = State::Open; identity(destination); return TWRC_SUCCESS;
        }
        if (msg == MSG_CLOSEDS && s.state == State::Open) { endTransfer(s); s.state = State::PreSession; return TWRC_SUCCESS; }
        return fail(TWCC_SEQERROR);
    }
    if (dg == DG_CONTROL && dat == DAT_STATUS && msg == MSG_GET) {
        if (!data) return fail(TWCC_BADVALUE);
        static_cast<pTW_STATUS>(data)->ConditionCode = s.condition;
        return TWRC_SUCCESS;
    }
    if (s.state == State::PreSession) return fail(TWCC_SEQERROR);
    if (dg == DG_CONTROL && dat == DAT_ENTRYPOINT && msg == MSG_GET) {
        if (!data || static_cast<pTW_ENTRYPOINT>(data)->Size < sizeof(TW_ENTRYPOINT)) return fail(TWCC_BADVALUE);
        s.entryPoint = *static_cast<pTW_ENTRYPOINT>(data);
        return TWRC_SUCCESS;
    }
    if (dg == DG_CONTROL && dat == DAT_CAPABILITY) return capability(msg, static_cast<pTW_CAPABILITY>(data));
    if (dg == DG_IMAGE && dat == DAT_IMAGEINFO && msg == MSG_GET &&
        (s.state == State::Enabled || s.state == State::Transferring || s.state == State::TransferDone)) {
        if (!data) return fail(TWCC_BADVALUE);
        imageInfo(static_cast<pTW_IMAGEINFO>(data));
        s.condition = TWCC_SUCCESS;
        return TWRC_SUCCESS;
    }
    if (dg == DG_IMAGE && dat == DAT_IMAGELAYOUT && msg == MSG_GET &&
        (s.state == State::Enabled || s.state == State::Transferring || s.state == State::TransferDone)) {
        if (!data) return fail(TWCC_BADVALUE);
        imageLayout(static_cast<pTW_IMAGELAYOUT>(data));
        s.condition = TWCC_SUCCESS;
        return TWRC_SUCCESS;
    }
    if (dg == DG_IMAGE && dat == DAT_IMAGENATIVEXFER && msg == MSG_GET && s.state == State::Enabled) {
        return nativeTransfer(s, data);
    }
    if (dg == DG_CONTROL && dat == DAT_USERINTERFACE) {
        if (msg == MSG_ENABLEDS && s.state == State::Open) { s.state = State::Enabled; return TWRC_SUCCESS; }
        if (msg == MSG_DISABLEDS && (s.state == State::Enabled || s.state == State::Transferring || s.state == State::TransferDone)) {
            endTransfer(s); s.state = State::Open; return TWRC_SUCCESS;
        }
        return fail(TWCC_SEQERROR);
    }
    if (dg == DG_CONTROL && dat == DAT_SETUPMEMXFER && msg == MSG_GET && s.state == State::Enabled) {
        if (!data) return fail(TWCC_BADVALUE);
        auto* setup = static_cast<pTW_SETUPMEMXFER>(data);
        setup->MinBufSize = 4096; setup->MaxBufSize = 1024 * 1024; setup->Preferred = 64 * 1024;
        return TWRC_SUCCESS;
    }
    if (dg == DG_IMAGE && dat == DAT_IMAGEMEMXFER && msg == MSG_GET &&
        (s.state == State::Enabled || s.state == State::Transferring)) {
        if (!data) return fail(TWCC_BADVALUE);
        auto* transfer = static_cast<pTW_IMAGEMEMXFER>(data);
        if (!transfer->Memory.TheMem || transfer->Memory.Length == 0) return fail(TWCC_BADVALUE);
        if (!s.session && !beginTransfer(s)) return fail(TWCC_OPERATIONERROR);

        const auto bytesPerLine = s.session->outputBytesPerLine();
        if (bytesPerLine == 0 || transfer->Memory.Length < bytesPerLine) return fail(TWCC_BADVALUE);
        auto* output = static_cast<TW_UINT8*>(transfer->Memory.TheMem);
        const int firstLine = s.deliveredLines;
        int rows = 0;
        while ((static_cast<std::size_t>(rows) + 1) * bytesPerLine <= transfer->Memory.Length) {
            auto more = s.session->nextLine(
                std::span<std::uint8_t>{output + static_cast<std::size_t>(rows) * bytesPerLine, bytesPerLine},
                s.device->cancellation());
            if (!more) { endTransfer(s); return fail(TWCC_OPERATIONERROR); }
            if (!more.value()) break;
            ++rows;
            ++s.deliveredLines;
        }
        transfer->Compression = TWCP_NONE;
        transfer->BytesPerRow = static_cast<TW_UINT32>(bytesPerLine);
        transfer->Columns = static_cast<TW_UINT32>(bytesPerLine / 3);
        transfer->Rows = static_cast<TW_UINT32>(rows);
        transfer->XOffset = 0;
        transfer->YOffset = static_cast<TW_UINT32>(firstLine);
        transfer->BytesWritten = static_cast<TW_UINT32>(static_cast<std::size_t>(rows) * bytesPerLine);
        if (s.deliveredLines >= s.session->expectedOutputLines()) {
            s.state = State::TransferDone;
            return TWRC_XFERDONE;
        }
        s.state = State::Transferring;
        return TWRC_SUCCESS;
    }
    // ENDXFER i RESET moraju raditi i IZ STANJA 6, ne samo iz 7.
    //
    // Stanje Transferring je ono u kome aplikacija provede vecinu velikog
    // skeniranja - svaki poziv vraca onoliko redova koliko stane u bafer - i
    // to je tacno stanje u kome korisnik pritiska "Otkazi". TWAIN za prekid
    // propisuje MSG_RESET, a za "dosta mi je ove stranice" MSG_ENDXFER.
    //
    // Prva verzija je oba primala samo iz TransferDone. Prekid usred prenosa
    // je zato vracao TWCC_SEQERROR, endTransfer se nije zvao, i prolaz je
    // ostajao otvoren - cip nastavlja da skenira, glava da se krece. Nijedan
    // test to nije video jer su svi davali bafer veci od cele slike, pa se
    // stanje Transferring nikada nije ni dosezalo.
    if (dg == DG_CONTROL && dat == DAT_PENDINGXFERS && msg == MSG_ENDXFER &&
        (s.state == State::Transferring || s.state == State::TransferDone)) {
        if (data) static_cast<pTW_PENDINGXFERS>(data)->Count = 0;
        const bool closed = endTransfer(s);
        s.state = State::Enabled;
        return closed ? TWRC_SUCCESS : fail(TWCC_OPERATIONERROR);
    }
    if (dg == DG_CONTROL && dat == DAT_PENDINGXFERS && msg == MSG_RESET &&
        (s.state == State::Enabled || s.state == State::Transferring ||
         s.state == State::TransferDone)) {
        if (data) static_cast<pTW_PENDINGXFERS>(data)->Count = 0;
        const bool closed = endTransfer(s);
        s.state = State::Enabled;
        if (!closed) {
            return fail(TWCC_OPERATIONERROR);
        }
        s.condition = TWCC_SUCCESS;
        return TWRC_SUCCESS;
    }
    return fail(TWCC_SEQERROR);
}

TW_UINT16 TW_CALLINGSTYLE DS_Entry(pTW_IDENTITY origin,
                                   TW_UINT32 dg,
                                   TW_UINT16 dat,
                                   TW_UINT16 msg,
                                   TW_MEMREF data) {
    return G2710TwainEntry(origin, nullptr, dg, dat, msg, data);
}
