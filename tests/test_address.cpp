//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file test_address.cpp
 * @brief This file contains unit tests for the address generation and validation logic in the Chronos project.
 *
 * It utilizes the `test_framework.hpp` to register and run tests. The primary focus
 * is on verifying the correct functionality of `chrono_address::Address` creation,
 * string conversion, and Bech32m encoding/decoding.
 *
 * Key tests include:
 * - `test_address_logic()`: Verifies the end-to-end process of creating an address
 *   from a public key, converting it to a string, parsing it back, and decoding
 *   its Bech32m components.
 */

#include "address/address.hpp"
#include "test_framework.hpp"
#include <cassert>
#include <vector>
#include <string>

namespace {

/**
 * @brief Tests the core logic of address creation, string conversion, and Bech32m decoding.
 *
 * This test case performs the following steps:
 * 1. Creates a fake public key (32 bytes of 0x42).
 * 2. Constructs a `chrono_address::Address` object from the fake public key.
 * 3. Converts the `Address` object to its string representation (Bech32m encoded).
 * 4. Parses the string representation back into an `Address` object.
 * 5. Decodes the Bech32m string directly using `bech32m_decode`.
 * 6. Asserts that the decoded human-readable part (HRP) is "cqc" and the data part
 *    (parsed address bytes) has the expected size (20 bytes).
 */
void test_address_logic() {
  std::vector<uint8_t> fake_pk(32, 0x42); ///< @var fake_pk A 32-byte vector representing a dummy public key.
  auto a = chrono_address::Address(fake_pk).to_string(); ///< @var a The Bech32m encoded string representation of the address.
  auto parsed = chrono_address::Address(a); ///< @var parsed An `Address` object created by parsing the Bech32m string.
  auto decoded = chrono_address::bech32m_decode(a); ///< @var decoded The result of directly decoding the Bech32m string.
  assert(decoded && decoded->first == "cqc" && parsed.get_bytes().size() == 20); // Assertions for correct decoding and address size
}

/**
 * @struct Registrar
 * @brief A helper struct to automatically register test cases with the test framework.
 *
 * This struct's constructor is executed at static initialization time,
 * registering `test_address_logic` with the `test_framework`.
 */
struct Registrar {
    Registrar() {
        test_framework::register_test("Address Logic", test_address_logic);
    }
};

static Registrar registrar; ///< @var registrar Static instance of Registrar to trigger test registration.

} // namespace