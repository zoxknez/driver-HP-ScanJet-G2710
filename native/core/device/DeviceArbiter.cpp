#include "DeviceArbiter.h"

#include <windows.h>
#include <sddl.h>

#include <algorithm>
#include <cstring>

namespace g2710 {
namespace {

// Dozvoli LocalSystem (WIA servis u Session 0), lokalne administratore i
// interaktivnog korisnika (TWAIN / aplikacija / CLI). Bez ovoga bi objekat
// napravljen u jednoj sesiji bio nedostupan iz druge.
constexpr wchar_t kSddl[] = L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;IU)";

constexpr std::size_t kOwnerBlockSize = 256;

struct SecurityAttributes {
    SECURITY_ATTRIBUTES attributes{};
    PSECURITY_DESCRIPTOR descriptor = nullptr;

    bool build() {
        if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(
                kSddl, SDDL_REVISION_1, &descriptor, nullptr)) {
            return false;
        }
        attributes.nLength = sizeof(attributes);
        attributes.lpSecurityDescriptor = descriptor;
        attributes.bInheritHandle = FALSE;
        return true;
    }

    ~SecurityAttributes() {
        if (descriptor != nullptr) {
            ::LocalFree(descriptor);
        }
    }
};

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                             static_cast<int>(text.size()),
                                             nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                          out.data(), needed);
    return out;
}

}  // namespace

const char* toString(ArbiterScope scope) noexcept {
    switch (scope) {
        case ArbiterScope::Global: return "Global";
        case ArbiterScope::Local:  return "Local (DEGRADIRANO)";
    }
    return "?";
}

// ----------------------------------------------------------- DataSession ----

DataSession::~DataSession() {
    release();
}

DataSession::DataSession(DataSession&& other) noexcept
    : mutex_(other.mutex_), owner_(other.owner_) {
    other.mutex_ = nullptr;
    other.owner_ = nullptr;
}

DataSession& DataSession::operator=(DataSession&& other) noexcept {
    if (this != &other) {
        release();
        mutex_ = other.mutex_;
        owner_ = other.owner_;
        other.mutex_ = nullptr;
        other.owner_ = nullptr;
    }
    return *this;
}

void DataSession::release() noexcept {
    if (mutex_ != nullptr) {
        if (owner_ != nullptr) {
            owner_->onDataReleased();
        }
        ::ReleaseMutex(static_cast<HANDLE>(mutex_));
        mutex_ = nullptr;
        owner_ = nullptr;
    }
}

// ---------------------------------------------------------- DeviceArbiter ----

DeviceArbiter::DeviceArbiter(std::string deviceKey) : deviceKey_(std::move(deviceKey)) {}

DeviceArbiter::~DeviceArbiter() {
    if (ownerView_ != nullptr) {
        ::UnmapViewOfFile(ownerView_);
    }
    if (ownerMapping_ != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(ownerMapping_));
    }
    if (mutex_ != nullptr) {
        ::CloseHandle(static_cast<HANDLE>(mutex_));
    }
}

Status DeviceArbiter::initialize() {
    if (deviceKey_.empty()) {
        return fail(ErrorCode::InvalidArgument, "DeviceArbiter: prazan deviceKey");
    }

    SecurityAttributes security;
    const bool haveSecurity = security.build();
    SECURITY_ATTRIBUTES* attributes = haveSecurity ? &security.attributes : nullptr;

    const std::wstring key = widen(deviceKey_);
    const std::wstring globalMutex = L"Global\\G2710-" + key + L"-data";
    const std::wstring globalMap = L"Global\\G2710-" + key + L"-owner";

    // Global\ trazi SeCreateGlobalPrivilege. Ako ga nema, pad na Local\ je
    // vidljiv preko scope() - nikada tih.
    mutex_ = ::CreateMutexW(attributes, FALSE, globalMutex.c_str());
    scope_ = ArbiterScope::Global;
    mutexName_ = globalMutex;

    std::wstring mapName = globalMap;
    if (mutex_ == nullptr && ::GetLastError() == ERROR_ACCESS_DENIED) {
        mutexName_ = L"Local\\G2710-" + key + L"-data";
        mapName = L"Local\\G2710-" + key + L"-owner";
        scope_ = ArbiterScope::Local;
        mutex_ = ::CreateMutexW(attributes, FALSE, mutexName_.c_str());
    }

    if (mutex_ == nullptr) {
        return fail(ErrorCode::Internal, "CreateMutexW",
                    static_cast<std::uint32_t>(::GetLastError()));
    }

    // Kreiranje SECTION objekta u Global\ trazi SeCreateGlobalPrivilege, koji
    // obican korisnik nema - za razliku od muteksa, koji prolazi. Zato ide
    // tri koraka: kreiraj Global (uspeva servisu), otvori postojeci Global
    // (uspeva klijentu kada ga je servis vec napravio), pa tek onda Local.
    ownerScope_ = ArbiterScope::Global;
    ownerMapping_ = ::CreateFileMappingW(INVALID_HANDLE_VALUE, attributes, PAGE_READWRITE,
                                         0, kOwnerBlockSize, mapName.c_str());

    if (ownerMapping_ == nullptr && ::GetLastError() == ERROR_ACCESS_DENIED) {
        ownerMapping_ = ::OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mapName.c_str());
    }

    if (ownerMapping_ == nullptr) {
        const std::wstring localMap = L"Local\\G2710-" + key + L"-owner";
        ownerMapping_ = ::CreateFileMappingW(INVALID_HANDLE_VALUE, attributes,
                                             PAGE_READWRITE, 0, kOwnerBlockSize,
                                             localMap.c_str());
        ownerScope_ = ArbiterScope::Local;
    }

    if (ownerMapping_ != nullptr) {
        ownerView_ = ::MapViewOfFile(static_cast<HANDLE>(ownerMapping_),
                                     FILE_MAP_ALL_ACCESS, 0, 0, kOwnerBlockSize);
    }

    // Neuspeh deljenog bloka nije fatalan: gubi se ime vlasnika u poruci, ne i
    // sama arbitraza. Zato ovo NIKADA ne obara scope_.
    return ok();
}

Result<DataSession> DeviceArbiter::acquireData(std::chrono::milliseconds deadline,
                                               const char* clientName) {
    if (mutex_ == nullptr) {
        return fail(ErrorCode::InvalidState, "acquireData: initialize() nije pozvan");
    }

    const auto millis = deadline.count() < 0 ? 0LL : deadline.count();
    const DWORD timeout = static_cast<DWORD>(
        (std::min)(millis, static_cast<long long>(INFINITE - 1)));

    const DWORD result = ::WaitForSingleObject(static_cast<HANDLE>(mutex_), timeout);

    switch (result) {
        case WAIT_OBJECT_0:
        case WAIT_ABANDONED:
            // WAIT_ABANDONED znaci da je prethodni drzalac pao bez oslobadjanja.
            // Bravu dobijamo, ali uredjaj je u nepoznatom stanju - sloj iznad
            // mora izvrsiti HOME pre bilo cega drugog.
            break;

        case WAIT_TIMEOUT: {
            return fail(ErrorCode::Busy, "acquireData: uredjaj koristi drugi klijent");
        }

        default:
            return fail(ErrorCode::Internal, "WaitForSingleObject",
                        static_cast<std::uint32_t>(::GetLastError()));
    }

    if (ownerView_ != nullptr && clientName != nullptr) {
        auto* text = static_cast<char*>(ownerView_);
        const std::size_t length = (std::min)(std::strlen(clientName), kOwnerBlockSize - 1);
        std::memcpy(text, clientName, length);
        text[length] = '\0';
    }

    return DataSession(mutex_, this);
}

std::string DeviceArbiter::currentOwner() const {
    if (ownerView_ == nullptr) {
        return {};
    }
    const auto* text = static_cast<const char*>(ownerView_);
    const std::size_t length = ::strnlen(text, kOwnerBlockSize - 1);
    return std::string(text, length);
}

void DeviceArbiter::onDataReleased() noexcept {
    if (ownerView_ != nullptr) {
        *static_cast<char*>(ownerView_) = '\0';
    }
}

}  // namespace g2710
