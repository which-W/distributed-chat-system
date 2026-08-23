const grpc = require('@grpc/grpc-js')
const message_proto = require('./proto')
const const_module = require('./const')
const config = require('./config')
const emailModule = require('./email')
const redis_module = require('./redis')
const fs = require('fs')
const { generateVerificationCode, normalizeEmail } = require('./security')

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
    const email = normalizeEmail(call.request.email)
    if (!email) {
        callback(null, { email: '', error: const_module.Errors.InvalidEmail })
        return
    }
    try{
        const uniqueId = generateVerificationCode()
        const issueResult = await redis_module.IssueVerificationCode(email, uniqueId)
        if (issueResult === 2 || issueResult === 3) {
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
    var server = new grpc.Server()
    server.addService(message_proto.VarifyService.service, { GetVarifyCode: GetVarifyCode })
    const bindAddress = process.env.VARIFY_BIND_ADDRESS || '0.0.0.0:5000'
    server.bindAsync(bindAddress, serverCredentials(), (error) => {
        if (error) throw error
        console.log(`grpc server started on ${bindAddress}`)
    })
}

main()
