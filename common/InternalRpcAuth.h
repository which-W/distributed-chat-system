#pragma once

#include <grpcpp/client_context.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/status.h>

#include <cstddef>
#include <stdexcept>
#include <string>

namespace chat::internal_rpc {

inline constexpr char kMetadataKey[] = "x-chat-internal-token";
inline constexpr char kDevelopmentToken[] = "local-development-only-change-me";

// 比较令牌时不根据首个不同字节提前返回，降低远程时序侧信道。
inline bool constant_time_equal(const grpc::string_ref& supplied, const std::string& expected) {
    if (supplied.size() != expected.size()) return false;
    unsigned char difference = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        difference |= static_cast<unsigned char>(supplied.data()[i])
            ^ static_cast<unsigned char>(expected[i]);
    }
    return difference == 0;
}

inline grpc::Status authorize(const grpc::ServerContext& context, const std::string& expected) {
	// 服务端配置缺失时必须失败关闭，不能退化为“只要网络可达即可调用”。
    if (expected.empty()) {
        return {grpc::StatusCode::UNAUTHENTICATED, "internal RPC authentication is not configured"};
    }
    const auto& metadata = context.client_metadata();
    const auto supplied = metadata.find(kMetadataKey);
    if (supplied == metadata.end() || !constant_time_equal(supplied->second, expected)) {
        return {grpc::StatusCode::UNAUTHENTICATED, "invalid internal RPC identity"};
    }
    return grpc::Status::OK;
}

inline void authenticate(grpc::ClientContext& context, const std::string& token) {
    if (token.empty()) throw std::runtime_error("internal RPC token is required");
    context.AddMetadata(kMetadataKey, token);
}

inline bool is_loopback(const std::string& host) {
    return host == "127.0.0.1" || host == "::1" || host == "localhost";
}

inline void validate_server_configuration(
    const std::string& host, const std::string& token, const std::string& transport_mode) {
    if (token.empty()) throw std::runtime_error("internal RPC server token is required");
	// Bearer 令牌离开回环地址时必须由 TLS 保护，公开明文监听直接拒绝启动。
	if (!is_loopback(host) && transport_mode != "tls" && transport_mode != "mtls") {
		throw std::runtime_error("non-loopback internal RPC listeners require TLS or mTLS");
	}
	// 仓库内开发令牌只允许回环联调；跨主机部署必须由环境变量注入高熵值。
    if (!is_loopback(host) && token == kDevelopmentToken) {
        throw std::runtime_error(
            "the development internal RPC token may only be used on a loopback listener");
    }
}

} // namespace chat::internal_rpc
