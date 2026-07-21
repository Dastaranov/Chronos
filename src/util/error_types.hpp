/**
 * @file error_types.hpp
 * @brief Structured error handling system for Chronos with typed error codes and recovery strategies.
 *
 * This module provides a comprehensive error handling infrastructure with:
 * - Typed error enums for different failure domains (Storage, Network, Consensus, Config)
 * - A Result<T> template for type-safe error propagation
 * - Error severity levels for determining recovery actions
 * - Error context tracking for better logging and debugging
 * 
 * Error codes are designed to be meaningful and traceable, allowing:
 * - Automatic retry decisions based on error type
 * - Circuit breaker implementation for failing services
 * - Graceful degradation with fallback strategies
 * - Context-aware error logging with recovery actions
 */

#pragma once

#include <string>
#include <optional>
#include <stdexcept>
#include <memory>

namespace chrono_error {

/**
 * @enum ErrorSeverity
 * @brief Determines recovery strategy for an error.
 * 
 * - Transient: Temporary failure, retry with backoff (e.g., network timeout)
 * - Recoverable: Operation failed but system continues, fallback available (e.g., peer disconnect)
 * - Critical: Operation failed, manual intervention may be needed (e.g., storage failure)
 * - Fatal: System cannot continue, immediate shutdown required (e.g., config invalid)
 */
enum class ErrorSeverity {
    Transient,      ///< Retry with exponential backoff
    Recoverable,    ///< Use fallback strategy, notify operator
    Critical,       ///< Log detailed error, degrade functionality
    Fatal           ///< Cannot continue, require restart
};

/**
 * @enum StorageErrorCode
 * @brief Error codes specific to storage operations (block save/load, metadata operations).
 * 
 * Recovery strategies per code:
 * - IOError: Transient, retry with exponential backoff (max 3 retries)
 * - NotFound: Recoverable, may indicate chain gap or peer lag
 * - Corrupted: Critical, snapshot/backup recovery needed
 * - DiskFull: Critical, requires operator intervention (cleanup disk space)
 * - PermissionDenied: Critical, requires operator fix (file permissions)
 * - TransactionFailed: Critical, atomic write failed, state inconsistent
 */
enum class StorageErrorCode {
    Success = 0,
    IOError,
    NotFound,
    Corrupted,
    DiskFull,
    PermissionDenied,
    TransactionFailed,
    DeserializationFailed,
    SerializationFailed,
    Unknown
};

/**
 * @enum NetworkErrorCode
 * @brief Error codes for network/P2P operations (message send, peer connection, sync).
 * 
 * Recovery strategies per code:
 * - ConnectionTimeout: Transient, retry after exponential backoff
 * - ConnectionRefused: Recoverable, peer offline, try different peer
 * - MessageSendFailed: Transient, retry sending
 * - PeerDisconnected: Recoverable, reconnect with backoff
 * - InvalidMessage: Recoverable, drop message, penalize peer, continue
 * - PeerNotFound: Recoverable, initiate discovery, try fallback peers
 */
enum class NetworkErrorCode {
    Success = 0,
    ConnectionTimeout,
    ConnectionRefused,
    MessageSendFailed,
    PeerDisconnected,
    InvalidMessage,
    PeerNotFound,
    BufferOverflow,
    SerializationFailed,
    ProtocolViolation,
    Unknown
};

/**
 * @enum ConsensusErrorCode
 * @brief Error codes for consensus operations (block validation, BFT messages, state transitions).
 * 
 * Recovery strategies per code:
 * - RoundTimeout: Transient, increment round, elect new leader
 * - InvalidBlock: Recoverable, reject block, penalize proposer
 * - InvalidSignature: Recoverable, drop message, penalize sender
 * - QuorumNotReached: Transient, continue waiting or timeout
 * - StateInconsistent: Critical, resync from peer or load snapshot
 * - LeaderStuck: Critical, rotate leader, may need full resync
 */
enum class ConsensusErrorCode {
    Success = 0,
    RoundTimeout,
    InvalidBlock,
    InvalidSignature,
    QuorumNotReached,
    StateInconsistent,
    LeaderStuck,
    InvalidProposal,
    MessageOutOfOrder,
    Unknown
};

/**
 * @enum ConfigErrorCode
 * @brief Error codes for configuration operations (load, parse, validation).
 * 
 * Recovery strategies per code:
 * - ParseError: Fatal, cannot start without valid config, show error to operator
 * - InvalidValue: Fatal, show which field is invalid and expected format
 * - MissingRequired: Fatal, indicate which required field is missing
 * - PathNotFound: Fatal, file/directory doesn't exist, check configuration
 * - PermissionDenied: Fatal, cannot read/write config file, check permissions
 */
enum class ConfigErrorCode {
    Success = 0,
    ParseError,
    InvalidValue,
    MissingRequired,
    PathNotFound,
    PermissionDenied,
    TypeMismatch,
    OutOfRange,
    Unknown
};

/**
 * @struct ErrorContext
 * @brief Captures context information for detailed error reporting and recovery.
 * 
 * Used to provide rich error messages with:
 * - What operation was being performed
 * - Where in the code the error occurred
 * - Additional context (IDs, hashes, peer info, etc.)
 * - Suggested recovery action
 */
struct ErrorContext {
    std::string operation;      ///< What operation failed (e.g., "saveBlock", "sendMessage")
    std::string component;      ///< Which component (e.g., "Storage", "P2P", "Consensus")
    std::string details;        ///< Additional context (block hash, peer ID, config field)
    std::string recovery_hint;  ///< Suggested recovery action for operator
    int retry_count;            ///< Number of retries already attempted
    std::string source_file;    ///< Source file name where error occurred
    int source_line;            ///< Line number where error occurred
    
    /**
     * @brief Creates a string representation of error context for logging.
     * @return Formatted context string with all available information
     */
    std::string to_string() const;
};

/**
 * @template Result<T>
 * @brief Type-safe error propagation container for operations that may fail.
 * 
 * Instead of throwing exceptions or returning bool, operations return Result<T> with:
 * - The success/failure status
 * - The result value (if successful)
 * - Typed error code (if failed)
 * - Error message and context
 * - Severity level for recovery decisions
 * 
 * Usage:
 * @code
 * auto result = storage.saveBlock(block);
 * if (result) {
 *     // Success path
 * } else {
 *     if (result.error_severity() == ErrorSeverity::Transient) {
 *         retry_with_backoff();
 *     }
 *     LOG_ERROR(..., "Failed: {}. Recovery: {}",
 *               result.error_message(), result.recovery_hint());
 * }
 * @endcode
 */
template<typename T>
class Result {
public:
    /**
     * @brief Constructs a successful result.
     * @param value The successful result value
     */
    explicit Result(T value) 
        : success_(true), value_(std::make_optional<T>(value)), 
          error_code_(0), error_message_(""), context_(nullptr) {}

    /**
     * @brief Constructs a failed result with StorageErrorCode.
     * @param code Storage error code indicating failure type
     * @param message Human-readable error message
     * @param context Optional error context for debugging
     */
    Result(StorageErrorCode code, const std::string& message, 
           std::shared_ptr<ErrorContext> context = nullptr)
        : success_(false), value_(std::nullopt), storage_error_(code),
          error_code_(static_cast<int>(code)), error_message_(message),
          context_(context) {}

    /**
     * @brief Constructs a failed result with NetworkErrorCode.
     * @param code Network error code indicating failure type
     * @param message Human-readable error message
     * @param context Optional error context for debugging
     */
    Result(NetworkErrorCode code, const std::string& message, 
           std::shared_ptr<ErrorContext> context = nullptr)
        : success_(false), value_(std::nullopt), network_error_(code),
          error_code_(static_cast<int>(code)), error_message_(message),
          context_(context) {}

    /**
     * @brief Constructs a failed result with ConsensusErrorCode.
     * @param code Consensus error code indicating failure type
     * @param message Human-readable error message
     * @param context Optional error context for debugging
     */
    Result(ConsensusErrorCode code, const std::string& message, 
           std::shared_ptr<ErrorContext> context = nullptr)
        : success_(false), value_(std::nullopt), consensus_error_(code),
          error_code_(static_cast<int>(code)), error_message_(message),
          context_(context) {}

    /**
     * @brief Constructs a failed result with ConfigErrorCode.
     * @param code Config error code indicating failure type
     * @param message Human-readable error message
     * @param context Optional error context for debugging
     */
    Result(ConfigErrorCode code, const std::string& message, 
           std::shared_ptr<ErrorContext> context = nullptr)
        : success_(false), value_(std::nullopt), config_error_(code),
          error_code_(static_cast<int>(code)), error_message_(message),
          context_(context) {}

    // Implicit conversion to bool for easy conditional checking
    explicit operator bool() const { return success_; }
    bool is_success() const { return success_; }
    bool is_failure() const { return !success_; }

    /**
     * @brief Returns the result value if successful, throws std::runtime_error if failed.
     * @return The successful result value
     * @throws std::runtime_error if operation failed
     */
    T value_or_throw() const {
        if (!success_) {
            throw std::runtime_error(error_message_);
        }
        return value_.value();
    }

    /**
     * @brief Returns the result value or a default if failed.
     * @param default_value Default value to return on failure
     * @return The successful result value or default_value
     */
    T value_or(const T& default_value) const {
        return success_ ? value_.value() : default_value;
    }

    /**
     * @brief Returns the underlying value (only valid if is_success() == true).
     * @return Optional containing the value if successful, nullopt otherwise
     */
    const std::optional<T>& get_value() const { return value_; }

    // Error information accessors
    int error_code() const { return error_code_; }
    const std::string& error_message() const { return error_message_; }
    
    /**
     * @brief Determines the severity of this error for recovery decisions.
     * @return ErrorSeverity level indicating recovery strategy
     */
    ErrorSeverity error_severity() const;

    /**
     * @brief Returns the error context if available.
     * @return Shared pointer to ErrorContext or nullptr
     */
    const std::shared_ptr<ErrorContext>& get_context() const { return context_; }

    /**
     * @brief Checks if this error is retriable (Transient severity).
     * @return true if operation should be retried with backoff
     */
    bool is_retriable() const {
        return error_severity() == ErrorSeverity::Transient;
    }

    /**
     * @brief Returns suggested recovery action for operator.
     * @return String describing recovery steps
     */
    std::string recovery_hint() const;

private:
    bool success_;
    std::optional<T> value_;
    std::optional<StorageErrorCode> storage_error_;
    std::optional<NetworkErrorCode> network_error_;
    std::optional<ConsensusErrorCode> consensus_error_;
    std::optional<ConfigErrorCode> config_error_;
    int error_code_;
    std::string error_message_;
    std::shared_ptr<ErrorContext> context_;
};

// Helper function to create error context
std::shared_ptr<ErrorContext> make_error_context(
    const std::string& operation,
    const std::string& component,
    const std::string& details = "",
    const std::string& recovery_hint = "",
    const std::string& source_file = "",
    int source_line = 0);

// Explicit template instantiation declarations for common types.
// Definitions are provided in error_types.cpp after specializations.
extern template class Result<bool>;
extern template class Result<int>;
extern template class Result<std::string>;
extern template class Result<uint64_t>;

} // namespace chrono_error
