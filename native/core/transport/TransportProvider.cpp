#include "ITransportProvider.h"
#include "UsbScanTransport.h"

namespace g2710::TransportProvider {
namespace {

UsbScanTransportProvider& productionProvider() {
    static UsbScanTransportProvider instance;
    return instance;
}

// Aktivni provider. Nije atomic namerno: zamena se dogadja samo u testovima,
// pre pokretanja radnih thread-ova, kroz ScopedTestProvider.
ITransportProvider*& active() {
    static ITransportProvider* provider = &productionProvider();
    return provider;
}

}  // namespace

Result<std::unique_ptr<ITransport>> create(const DeviceRef& ref) {
    return active()->create(ref);
}

const char* activeProviderName() noexcept {
    return active()->name();
}

ScopedTestProvider::ScopedTestProvider(std::unique_ptr<ITransportProvider> provider)
    : previous_(active()), owned_(std::move(provider)) {
    if (owned_) {
        active() = owned_.get();
    }
}

ScopedTestProvider::~ScopedTestProvider() {
    active() = previous_;
}

}  // namespace g2710::TransportProvider
