#include "crypto/crypto_provider.hpp"
#include "crypto/blake3.hpp"

namespace chrono_crypto {

chrono_util::Bytes Blake3Provider::hash(const chrono_util::Bytes& data) const {
    return blake3(data);
}

chrono_util::Bytes Blake3Provider::keyed_hash(const chrono_util::Bytes& key, const chrono_util::Bytes& data) const {
    return blake3_keyed(key, data);
}

// Global instance
static std::shared_ptr<ICryptoProvider> g_crypto_provider = std::make_shared<Blake3Provider>();

std::shared_ptr<ICryptoProvider> get_crypto_provider() {
    return g_crypto_provider;
}

void set_crypto_provider(std::shared_ptr<ICryptoProvider> provider) {
    g_crypto_provider = provider;
}

} // namespace chrono_crypto
