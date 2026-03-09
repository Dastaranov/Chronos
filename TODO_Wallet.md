# Wallet and Validator Key Generation Plan

This file outlines the plan to create a command-line wallet utility for Chronos. This utility is essential for generating the cryptographic keys required for validators.

## Phase 1: Create the Wallet Command-Line Tool

-   [X] **1.1: Create basic file structure and build system integration.**
    -   Create `src/wallet/cli/main.cpp` for the wallet's entry point.
    -   Create `src/wallet/cli/CMakeLists.txt` to define the `wallet_cli` executable.
    -   Update the root `CMakeLists.txt` to include the `src/wallet/cli` directory.

-   [X] **1.2: Implement Key Generation Logic.**
    -   Create a new `Signer` class (e.g., `SignerDilithium`) that uses a real cryptographic library to generate key pairs.
    -   Add a `generate-keys` command to `wallet_cli/main.cpp`.
    -   This command will:
        -   Instantiate the new `Signer`.
        -   Generate a new private and public key pair.
        -   Print the **public key** (for the `validators` list) and the **private key** (to be stored securely by the user) to the console.

-   [X] **1.3: Refactor Node to Use Private Keys.**
    -   Remove the hardcoded `SignerHMAC` from `node_app.cpp`.
    -   Add a new configuration parameter in `config/default.toml`, e.g., `private_key` in the `[crypto]` section.
    -   The `chronos_node` will now start by loading the private key from its configuration and initializing its `Signer` with it.

-   [X] **1.4: Update Documentation.**
    -   Briefly document the new `wallet_cli` tool and its usage in `README.md` or a similar file.
