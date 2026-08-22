#pragma once

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

#include <grpcpp/grpcpp.h>

namespace chat::grpc_tls {

struct Options {
    std::string mode{"insecure"};
    std::string ca_cert;
    std::string cert;
    std::string key;
};

inline std::string env_or(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : fallback;
}

inline std::string read_file(const std::string& path, const char* description) {
    if (path.empty()) {
        throw std::runtime_error(std::string("Missing gRPC TLS ") + description + " path");
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error(std::string("Cannot open gRPC TLS ") + description + ": " + path);
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

template <typename Config>
Options from_config(Config& cfg) {
    Options options;
    options.mode = env_or("CHAT_GRPC_TLS_MODE", cfg["GrpcTLS"]["Mode"]);
    if (options.mode.empty()) {
        options.mode = "insecure";
    }
    options.ca_cert = env_or("CHAT_GRPC_CA_CERT", cfg["GrpcTLS"]["CACert"]);
    options.cert = env_or("CHAT_GRPC_CERT", cfg["GrpcTLS"]["Cert"]);
    options.key = env_or("CHAT_GRPC_KEY", cfg["GrpcTLS"]["Key"]);
    if (options.mode != "insecure" && options.mode != "tls" && options.mode != "mtls") {
        throw std::runtime_error("GrpcTLS.Mode must be insecure, tls, or mtls");
    }
    return options;
}

inline std::shared_ptr<grpc::ChannelCredentials> channel_credentials(const Options& options) {
    if (options.mode == "insecure") {
        return grpc::InsecureChannelCredentials();
    }
    grpc::SslCredentialsOptions ssl;
    ssl.pem_root_certs = read_file(options.ca_cert, "CA certificate");
    if (options.mode == "mtls") {
        ssl.pem_cert_chain = read_file(options.cert, "client certificate");
        ssl.pem_private_key = read_file(options.key, "client private key");
    }
    return grpc::SslCredentials(ssl);
}

inline std::shared_ptr<grpc::ServerCredentials> server_credentials(const Options& options) {
    if (options.mode == "insecure") {
        return grpc::InsecureServerCredentials();
    }
    grpc::SslServerCredentialsOptions ssl;
    ssl.pem_key_cert_pairs.push_back({read_file(options.key, "server private key"),
                                      read_file(options.cert, "server certificate")});
    if (options.mode == "mtls") {
        ssl.pem_root_certs = read_file(options.ca_cert, "CA certificate");
        ssl.client_certificate_request =
            GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;
    }
    return grpc::SslServerCredentials(ssl);
}

inline std::shared_ptr<grpc::Channel> make_channel(const std::string& host,
                                                   const std::string& port,
                                                   const Options& options,
                                                   const std::string& tls_name = {}) {
    grpc::ChannelArguments arguments;
    if (!tls_name.empty()) {
        arguments.SetSslTargetNameOverride(tls_name);
    }
    return grpc::CreateCustomChannel(host + ":" + port, channel_credentials(options), arguments);
}

}  // namespace chat::grpc_tls
