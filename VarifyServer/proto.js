const path = require("path")
const grpc = require("@grpc/grpc-js")
const protoLoader = require("@grpc/proto-loader")

// All services consume the same protocol source to prevent schema drift.
const PROTO_PATH = path.join(__dirname, "..", "proto", "message.proto")
//定义解析参数
/*
keepCase: 如果为 true，则保留字段名的原始大小写。如果为 false，则将所有字段名转换为驼峰命名法。

longs: 控制如何表示 Protocol Buffers 中的 long 类型。如果设置为 String，则长整数会被转换为字符串，以避免 JavaScript 中的整数溢出问题。

enums: 控制如何表示 Protocol Buffers 中的枚举类型。如果设置为 String，则枚举值会被转换为字符串。

defaults: 如果为 true，则为未明确设置的字段提供默认值。

oneofs: 如果为 true，则支持 Protocol Buffers 中的 oneof 特性。
*/
const packageDefinition = protoLoader.loadSync(PROTO_PATH, { keepCase: true,
    longs: String,
    enums: String,
    defaults: true,
    oneofs: true })
//解析grpc
const protoDescriptor = grpc.loadPackageDefinition(packageDefinition)

const message_proto = protoDescriptor.message
//将message_proto导出给其他模块使用
module.exports = message_proto
