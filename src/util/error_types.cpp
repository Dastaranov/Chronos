/**
 * @file error_types.cpp
 * @brief Implementation of structured error handling system.
 */

#include "error_types.hpp"
#include <sstream>

namespace chrono_error {

std::string ErrorContext::to_string() const {
    std::ostringstream oss;
    oss << "Operation: " << operation
        << " | Component: " << component;
    if (!details.empty()) {
        oss << " | Details: " << details;
    }
    if (!source_file.empty()) {
        oss << " | Source: " << source_file << ":" << source_line;
    }
    if (!recovery_hint.empty()) {
        oss << " | Recovery: " << recovery_hint;
    }
    return oss.str();
}

std::shared_ptr<ErrorContext> make_error_context(
    const std::string& operation,
    const std::string& component,
    const std::string& details,
    const std::string& recovery_hint,
    const std::string& source_file,
    int source_line) {
    
    auto context = std::make_shared<ErrorContext>();
    context->operation = operation;
    context->component = component;
    context->details = details;
    context->recovery_hint = recovery_hint;
    context->source_file = source_file;
    context->source_line = source_line;
    context->retry_count = 0;
    return context;
}

// Explicit specializations for Result<T>

template<>
ErrorSeverity Result<bool>::error_severity() const {
    if (success_) return ErrorSeverity::Transient;  // Meaningless, but satisfy compiler
    
    if (storage_error_) {
        switch (storage_error_.value()) {
            case StorageErrorCode::IOError:
            case StorageErrorCode::DeserializationFailed:
                return ErrorSeverity::Transient;
            
            case StorageErrorCode::NotFound:
                return ErrorSeverity::Recoverable;
            
            case StorageErrorCode::Corrupted:
            case StorageErrorCode::TransactionFailed:
                return ErrorSeverity::Critical;
            
            case StorageErrorCode::DiskFull:
            case StorageErrorCode::PermissionDenied:
                return ErrorSeverity::Critical;
            
            default:
                return ErrorSeverity::Critical;
        }
    }
    
    if (network_error_) {
        switch (network_error_.value()) {
            case NetworkErrorCode::ConnectionTimeout:
            case NetworkErrorCode::MessageSendFailed:
                return ErrorSeverity::Transient;
            
            case NetworkErrorCode::ConnectionRefused:
            case NetworkErrorCode::PeerDisconnected:
            case NetworkErrorCode::InvalidMessage:
            case NetworkErrorCode::PeerNotFound:
                return ErrorSeverity::Recoverable;
            
            case NetworkErrorCode::ProtocolViolation:
            case NetworkErrorCode::BufferOverflow:
                return ErrorSeverity::Critical;
            
            default:
                return ErrorSeverity::Recoverable;
        }
    }
    
    if (consensus_error_) {
        switch (consensus_error_.value()) {
            case ConsensusErrorCode::RoundTimeout:
            case ConsensusErrorCode::QuorumNotReached:
                return ErrorSeverity::Transient;
            
            case ConsensusErrorCode::InvalidBlock:
            case ConsensusErrorCode::InvalidSignature:
            case ConsensusErrorCode::InvalidProposal:
                return ErrorSeverity::Recoverable;
            
            case ConsensusErrorCode::StateInconsistent:
            case ConsensusErrorCode::LeaderStuck:
                return ErrorSeverity::Critical;
            
            default:
                return ErrorSeverity::Recoverable;
        }
    }
    
    if (config_error_) {
        // Config errors are generally Fatal
        return ErrorSeverity::Fatal;
    }
    
    return ErrorSeverity::Critical;
}

template<>
std::string Result<bool>::recovery_hint() const {
    if (!context_) {
        return "Check logs for details";
    }
    return context_->recovery_hint;
}

template<>
ErrorSeverity Result<int>::error_severity() const {
    if (success_) return ErrorSeverity::Transient;
    
    if (storage_error_) {
        switch (storage_error_.value()) {
            case StorageErrorCode::IOError:
            case StorageErrorCode::DeserializationFailed:
                return ErrorSeverity::Transient;
            case StorageErrorCode::NotFound:
                return ErrorSeverity::Recoverable;
            default:
                return ErrorSeverity::Critical;
        }
    }
    
    if (network_error_) {
        switch (network_error_.value()) {
            case NetworkErrorCode::ConnectionTimeout:
            case NetworkErrorCode::MessageSendFailed:
                return ErrorSeverity::Transient;
            case NetworkErrorCode::ConnectionRefused:
            case NetworkErrorCode::PeerDisconnected:
                return ErrorSeverity::Recoverable;
            default:
                return ErrorSeverity::Critical;
        }
    }
    
    if (consensus_error_) {
        switch (consensus_error_.value()) {
            case ConsensusErrorCode::RoundTimeout:
                return ErrorSeverity::Transient;
            case ConsensusErrorCode::InvalidBlock:
                return ErrorSeverity::Recoverable;
            default:
                return ErrorSeverity::Critical;
        }
    }
    
    return ErrorSeverity::Critical;
}

template<>
std::string Result<int>::recovery_hint() const {
    if (!context_) {
        return "Check logs for details";
    }
    return context_->recovery_hint;
}

template<>
ErrorSeverity Result<std::string>::error_severity() const {
    if (success_) return ErrorSeverity::Transient;
    
    if (config_error_) {
        return ErrorSeverity::Fatal;
    }
    
    if (storage_error_) {
        return ErrorSeverity::Critical;
    }
    
    if (network_error_) {
        return ErrorSeverity::Recoverable;
    }
    
    return ErrorSeverity::Critical;
}

template<>
std::string Result<std::string>::recovery_hint() const {
    if (!context_) {
        return "Check logs for details";
    }
    return context_->recovery_hint;
}

template<>
ErrorSeverity Result<uint64_t>::error_severity() const {
    if (success_) return ErrorSeverity::Transient;
    
    if (storage_error_) {
        switch (storage_error_.value()) {
            case StorageErrorCode::IOError:
            case StorageErrorCode::DeserializationFailed:
                return ErrorSeverity::Transient;
            case StorageErrorCode::NotFound:
                return ErrorSeverity::Recoverable;
            default:
                return ErrorSeverity::Critical;
        }
    }
    
    if (network_error_) {
        switch (network_error_.value()) {
            case NetworkErrorCode::ConnectionTimeout:
            case NetworkErrorCode::MessageSendFailed:
                return ErrorSeverity::Transient;
            case NetworkErrorCode::ConnectionRefused:
            case NetworkErrorCode::PeerDisconnected:
                return ErrorSeverity::Recoverable;
            default:
                return ErrorSeverity::Critical;
        }
    }
    
    if (consensus_error_) {
        switch (consensus_error_.value()) {
            case ConsensusErrorCode::RoundTimeout:
                return ErrorSeverity::Transient;
            default:
                return ErrorSeverity::Critical;
        }
    }
    
    return ErrorSeverity::Critical;
}

template<>
std::string Result<uint64_t>::recovery_hint() const {
    if (!context_) {
        return "Check logs for details";
    }
    return context_->recovery_hint;
}

} // namespace chrono_error
