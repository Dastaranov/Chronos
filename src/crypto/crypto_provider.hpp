#pragma once
#include <vector>
#include <cstdint>
#include <memory>
#include "util/bytes.hpp"

namespace chrono_crypto {

/**
 * @interface ICryptoProvider
 * @brief Abstract interface for cryptographic operations.
 * 
 * This interface allows the application to switch between different cryptographic
 * implementations (e.g., BLAKE3 vs SHA3) without changing the core logic.
 * This is crucial for future-proofing and upgradability.
 */
class ICryptoProvider {
public:
    virtual ~ICryptoProvider() = default;

    /**
     * @brief Computes a cryptographic hash of the input data.
     * @param data The input data to hash.
     * @return The computed hash.
     */
    virtual chrono_util::Bytes hash(const chrono_util::Bytes& data) const = 0;

    /**
     * @brief Computes a keyed cryptographic hash (MAC) of the input data.
     * @param key The secret key.
     * @param data The input data to hash.
     * @return The computed keyed hash.
     */
    virtual chrono_util::Bytes keyed_hash(const chrono_util::Bytes& key, const chrono_util::Bytes& data) const = 0;
};

/**
 * @class Blake3Provider
 * @brief Implementation of ICryptoProvider using BLAKE3.
 */
class Blake3Provider : public ICryptoProvider {
public:
    chrono_util::Bytes hash(const chrono_util::Bytes& data) const override;
    chrono_util::Bytes keyed_hash(const chrono_util::Bytes& key, const chrono_util::Bytes& data) const override;
};

/**
 * @brief Gets the global crypto provider instance.
 * @return Shared pointer to the current crypto provider.
 */
std::shared_ptr<ICryptoProvider> get_crypto_provider();

/**
 * @brief Sets the global crypto provider instance.
 * @param provider Shared pointer to the new crypto provider.
 */
void set_crypto_provider(std::shared_ptr<ICryptoProvider> provider);

} // namespace chrono_crypto
