#pragma once

#include <chrono>
#include <grpcpp/client_context.h>

namespace chat::grpc_client {

inline constexpr auto kInternalRpcTimeout = std::chrono::seconds(3);
inline constexpr auto kVerificationRpcTimeout = std::chrono::seconds(10);

template <typename Rep, typename Period>
void setDeadline(grpc::ClientContext& context,
    const std::chrono::duration<Rep, Period>& timeout)
{
    context.set_deadline(std::chrono::system_clock::now() + timeout);
}

} // namespace chat::grpc_client
