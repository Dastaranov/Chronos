/**
 * @file test_error_types.cpp
 * @brief Test suite for structured error handling system (error_types.hpp/cpp)
 * 
 * Tests validate:
 * - Result<T> successful construction and value retrieval
 * - Result<T> error construction with typed error codes
 * - ErrorSeverity determination based on error type
 * - Error context tracking and formatting
 * - Recovery hint generation
 */

#include "test_framework.hpp"
#include "util/error_types.hpp"

using namespace chrono_error;

TEST_CASE(ResultBoolSuccess, "Result<bool> can be constructed successfully") {
    Result<bool> result(true);
    ASSERT_TRUE(result.is_success(), "Result should be successful");
    ASSERT_FALSE(result.is_failure(), "Result should not be failure");
    ASSERT_EQ(result.value_or(false), true, "Value should be true");
}

TEST_CASE(ResultBoolFailure, "Result<bool> can be constructed with StorageError") {
    Result<bool> result(StorageErrorCode::IOError, "Disk read failed");
    ASSERT_FALSE(result.is_success(), "Result should be failure");
    ASSERT_TRUE(result.is_failure(), "Result should indicate failure");
    ASSERT_EQ(result.error_code(), static_cast<int>(StorageErrorCode::IOError), "Error code mismatch");
}

TEST_CASE(SeverityTransient, "IOError is classified as Transient severity") {
    Result<bool> result(StorageErrorCode::IOError, "Disk timeout");
    ASSERT_EQ(static_cast<int>(result.error_severity()), 
              static_cast<int>(ErrorSeverity::Transient), 
              "IOError should be Transient");
    ASSERT_TRUE(result.is_retriable(), "IOError should be retriable");
}

TEST_CASE(SeverityRecoverable, "NotFound is classified as Recoverable") {
    Result<bool> result(StorageErrorCode::NotFound, "Block not found");
    ASSERT_EQ(static_cast<int>(result.error_severity()), 
              static_cast<int>(ErrorSeverity::Recoverable), 
              "NotFound should be Recoverable");
    ASSERT_FALSE(result.is_retriable(), "NotFound should not be retriable");
}

TEST_CASE(SeverityCritical, "DiskFull is classified as Critical") {
    Result<bool> result(StorageErrorCode::DiskFull, "No space left on device");
    ASSERT_EQ(static_cast<int>(result.error_severity()), 
              static_cast<int>(ErrorSeverity::Critical), 
              "DiskFull should be Critical");
}

TEST_CASE(SeverityFatal, "ConfigError is Fatal") {
    Result<bool> result(ConfigErrorCode::ParseError, "Invalid TOML syntax");
    ASSERT_EQ(static_cast<int>(result.error_severity()), 
              static_cast<int>(ErrorSeverity::Fatal), 
              "ConfigError should be Fatal");
}

TEST_CASE(NetworkTransient, "ConnectionTimeout is Transient") {
    Result<bool> result(NetworkErrorCode::ConnectionTimeout, "Peer timeout");
    ASSERT_EQ(static_cast<int>(result.error_severity()), 
              static_cast<int>(ErrorSeverity::Transient), 
              "ConnectionTimeout should be Transient");
    ASSERT_TRUE(result.is_retriable(), "ConnectionTimeout should be retriable");
}

TEST_CASE(NetworkRecoverable, "PeerDisconnected is Recoverable") {
    Result<bool> result(NetworkErrorCode::PeerDisconnected, "Peer connection lost");
    ASSERT_EQ(static_cast<int>(result.error_severity()), 
              static_cast<int>(ErrorSeverity::Recoverable), 
              "PeerDisconnected should be Recoverable");
}

TEST_CASE(ConsensusTransient, "RoundTimeout is Transient") {
    Result<bool> result(ConsensusErrorCode::RoundTimeout, "Consensus round timed out");
    ASSERT_EQ(static_cast<int>(result.error_severity()), 
              static_cast<int>(ErrorSeverity::Transient), 
              "RoundTimeout should be Transient");
}

TEST_CASE(ConsensusCritical, "StateInconsistent is Critical") {
    Result<bool> result(ConsensusErrorCode::StateInconsistent, "Consensus state mismatch");
    ASSERT_EQ(static_cast<int>(result.error_severity()), 
              static_cast<int>(ErrorSeverity::Critical), 
              "StateInconsistent should be Critical");
}

TEST_CASE(ResultIntContext, "Result<int> can carry error context") {
    auto context = make_error_context(
        "saveBlock",
        "Storage",
        "height=100",
        "Retry with exponential backoff",
        "node_app.cpp",
        500
    );
    Result<int> result(StorageErrorCode::IOError, "Write failed", context);
    
    ASSERT_FALSE(result.is_success(), "Result should be failure");
    ASSERT_NE(result.get_context().get(), nullptr, "Context should exist");
    ASSERT_EQ(result.get_context()->operation, "saveBlock", "Operation should match");
}

TEST_CASE(ErrorContextFormat, "ErrorContext formats to readable string") {
    auto context = make_error_context(
        "sendMessage",
        "P2P",
        "peer=node5",
        "Reconnect and retry",
        "p2p_client.cpp",
        250
    );
    
    std::string formatted = context->to_string();
    ASSERT_TRUE(formatted.find("Operation: sendMessage") != std::string::npos, "Should contain operation");
    ASSERT_TRUE(formatted.find("Component: P2P") != std::string::npos, "Should contain component");
    ASSERT_TRUE(formatted.find("Details:") != std::string::npos, "Should contain details");
    ASSERT_TRUE(formatted.find("Recovery:") != std::string::npos, "Should contain recovery hint");
}

TEST_CASE(ResultStringConfig, "Result<string> handles ConfigErrorCode") {
    Result<std::string> result(ConfigErrorCode::MissingRequired, 
                              "Missing required field: private_key_id");
    ASSERT_FALSE(result.is_success(), "Result should be failure");
    ASSERT_EQ(result.error_code(), static_cast<int>(ConfigErrorCode::MissingRequired), "Error code mismatch");
    ASSERT_EQ(static_cast<int>(result.error_severity()), 
              static_cast<int>(ErrorSeverity::Fatal), 
              "Config errors should be Fatal");
}

TEST_CASE(ResultUint64Success, "Result<uint64_t> can store numeric result") {
    Result<uint64_t> result(static_cast<uint64_t>(12345));
    ASSERT_TRUE(result.is_success(), "Result should be successful");
    ASSERT_EQ(result.value_or(0), 12345, "Value should match");
}

TEST_CASE(ResultUint64Failure, "Result<uint64_t> can fail with error code") {
    Result<uint64_t> result(StorageErrorCode::NotFound, "Key not found");
    ASSERT_FALSE(result.is_success(), "Result should be failure");
    ASSERT_EQ(result.value_or(999), 999, "Should return default on failure");
}

TEST_CASE(ConvertToBool, "Result<T> converts implicitly to bool for conditionals") {
    Result<bool> success(true);
    Result<bool> failure(StorageErrorCode::IOError, "Failed");
    
    bool success_check = success ? true : false;
    bool failure_check = failure ? true : false;
    
    ASSERT_TRUE(success_check, "Success result should convert to true");
    ASSERT_FALSE(failure_check, "Failure result should convert to false");
}

TEST_CASE(ValueOrThrowSuccess, "value_or_throw returns value on success") {
    Result<int> result(42);
    int value = result.value_or_throw();
    ASSERT_EQ(value, 42, "Should return the actual value");
}

TEST_CASE(ValueOrThrowThrows, "value_or_throw throws on failure") {
    Result<int> result(StorageErrorCode::IOError, "Read failed");
    bool exception_thrown = false;
    try {
        result.value_or_throw();
    } catch (const std::exception&) {
        exception_thrown = true;
    }
    ASSERT_TRUE(exception_thrown, "Should throw std::exception on failure");
}

TEST_CASE(RecoveryHintContext, "recovery_hint returns context recovery hint") {
    auto context = make_error_context(
        "loadSnapshot",
        "Storage",
        "snapshot_height=50",
        "Delete corrupted snapshot and resync"
    );
    Result<bool> result(StorageErrorCode::Corrupted, "Snapshot checksum mismatch", context);
    
    ASSERT_EQ(result.recovery_hint(), 
              "Delete corrupted snapshot and resync",
              "Should return recovery hint from context");
}

TEST_CASE(AllStorageErrorCodesDefined, "All StorageErrorCode values are valid") {
    ASSERT_TRUE(static_cast<int>(StorageErrorCode::Success) == 0, "Success should be 0");
    ASSERT_TRUE(static_cast<int>(StorageErrorCode::IOError) > 0, "IOError should be positive");
    ASSERT_TRUE(static_cast<int>(StorageErrorCode::DiskFull) > 0, "DiskFull should be positive");
    ASSERT_TRUE(static_cast<int>(StorageErrorCode::DiskFull) != static_cast<int>(StorageErrorCode::IOError), 
                "Error codes should be distinct");
}

TEST_CASE(AllNetworkErrorCodesDefined, "All NetworkErrorCode values are valid") {
    ASSERT_TRUE(static_cast<int>(NetworkErrorCode::Success) == 0, "Success should be 0");
    ASSERT_TRUE(static_cast<int>(NetworkErrorCode::ConnectionTimeout) > 0, "ConnectionTimeout should be positive");
    ASSERT_TRUE(static_cast<int>(NetworkErrorCode::ProtocolViolation) > 0, "ProtocolViolation should be positive");
}

TEST_CASE(AllConsensusErrorCodesDefined, "All ConsensusErrorCode values are valid") {
    ASSERT_TRUE(static_cast<int>(ConsensusErrorCode::Success) == 0, "Success should be 0");
    ASSERT_TRUE(static_cast<int>(ConsensusErrorCode::RoundTimeout) > 0, "RoundTimeout should be positive");
    ASSERT_TRUE(static_cast<int>(ConsensusErrorCode::LeaderStuck) > 0, "LeaderStuck should be positive");
}

TEST_CASE(AllConfigErrorCodesDefined, "All ConfigErrorCode values are valid") {
    ASSERT_TRUE(static_cast<int>(ConfigErrorCode::Success) == 0, "Success should be 0");
    ASSERT_TRUE(static_cast<int>(ConfigErrorCode::ParseError) > 0, "ParseError should be positive");
    ASSERT_TRUE(static_cast<int>(ConfigErrorCode::MissingRequired) > 0, "MissingRequired should be positive");
}
