const grpc = require('@grpc/grpc-js')
const message_proto = require('./proto')
const const_module = require('./const')
const config = require('./config')
const emailModule = require('./email')
const redis_module = require('./redis')
const fs = require('fs')
const { generateVerificationCode, normalizeEmail } = require('./security')

const INTERNAL_TOKEN_HEADER = 'x-chat-internal-token'
const DEVELOPMENT_TOKEN = 'local-development-only-change-me'

function isLoopbackBind(bindAddress) {
	return bindAddress.startsWith('127.0.0.1:') || bindAddress.startsWith('[::1]:')
}

function configuredGateToken() {
	if (process.env.CHAT_GATE_VARIFY_TOKEN) return process.env.CHAT_GATE_VARIFY_TOKEN
	const bindAddress = process.env.VARIFY_BIND_ADDRESS || '127.0.0.1:5000'
	// 开发占位令牌只能与回环监听组合，公开绑定必须显式提供高熵凭据。
	return isLoopbackBind(bindAddress)
		? DEVELOPMENT_TOKEN : null
}

function authorizeGate(call) {
	const expected = configuredGateToken()
    if (!expected) return false
    const supplied = call.metadata.get(INTERNAL_TOKEN_HEADER)
    if (supplied.length !== 1) return false
    const actual = Buffer.from(String(supplied[0]))
    const wanted = Buffer.from(expected)
    return actual.length === wanted.length
        && require('crypto').timingSafeEqual(actual, wanted)
}

function readRequiredFile(envName) {
    const file = process.env[envName]
    if (!file) throw new Error(`${envName} is required when gRPC TLS is enabled`)
    return fs.readFileSync(file)
}

function serverCredentials() {
    const mode = (process.env.VARIFY_GRPC_TLS_MODE || 'insecure').toLowerCase()
    if (mode === 'insecure') return grpc.ServerCredentials.createInsecure()
    if (mode !== 'tls' && mode !== 'mtls') {
        throw new Error('VARIFY_GRPC_TLS_MODE must be insecure, tls, or mtls')
    }
    const ca = mode === 'mtls' ? readRequiredFile('VARIFY_GRPC_CA_CERT') : null
    const keyCertPairs = [{
        private_key: readRequiredFile('VARIFY_GRPC_KEY'),
        cert_chain: readRequiredFile('VARIFY_GRPC_CERT'),
    }]
    return grpc.ServerCredentials.createSsl(ca, keyCertPairs, mode === 'mtls')
}

async function GetVarifyCode(call, callback) {
	if (!authorizeGate(call)) {
		callback({ code: grpc.status.UNAUTHENTICATED, message: 'invalid internal RPC identity' })
		return
	}
    const email = normalizeEmail(call.request.email)
    if (!email) {
        callback(null, { email: '', error: const_module.Errors.InvalidEmail })
        return
    }
    try{
        const uniqueId = generateVerificationCode()
        const issueResult = await redis_module.IssueVerificationCode(email, uniqueId)
	        if (issueResult === 2 || issueResult === 3 || issueResult === 4) {
            callback(null, { email, error: const_module.Errors.RateLimited })
            return
        }
        if (issueResult !== 1) {
            callback(null, { email, error: const_module.Errors.RedisErr })
            return
        }

        const htmlContent = emailModule.generateVerifyCodeTemplate(uniqueId, email);
        //发送邮件
        let mailOptions = {
            from: config.email_user,
            to: email,
            subject: `🔐 您的验证码：${uniqueId} - 请在5分钟内使用`,
            html: htmlContent,
        };

        await emailModule.SendMail(mailOptions);

        callback(null, { email,
            error:const_module.Errors.Success
        });


    }catch(error){
        console.log("verification email failed:", error.message)

        callback(null, { email,
            error:const_module.Errors.Exception
        });
    }

}

function main() {
	const bindAddress = process.env.VARIFY_BIND_ADDRESS || '127.0.0.1:5000'
	const tlsMode = (process.env.VARIFY_GRPC_TLS_MODE || 'insecure').toLowerCase()
	// 跨主机 bearer token 禁止走明文 gRPC，配置错误必须在启动阶段失败。
	if (!isLoopbackBind(bindAddress) && tlsMode !== 'tls' && tlsMode !== 'mtls') {
		throw new Error('non-loopback Varify RPC listeners require TLS or mTLS')
	}
	if (!configuredGateToken()) {
		throw new Error('CHAT_GATE_VARIFY_TOKEN is required')
	}
    var server = new grpc.Server()
    server.addService(message_proto.VarifyService.service, { GetVarifyCode: GetVarifyCode })
    server.bindAsync(bindAddress, serverCredentials(), (error) => {
        if (error) throw error
        console.log(`grpc server started on ${bindAddress}`)
    })
}

main()
