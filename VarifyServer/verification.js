const crypto = require('crypto')
const { generateVerificationCode } = require('./security')

async function issueAndSend(email, dependencies = {}) {
    const issue = dependencies.issue || require('./redis').IssueVerificationCode
    const rollback = dependencies.rollback || require('./redis').RollbackVerificationCode
    const send = dependencies.send || require('./email').SendMail
    const template = dependencies.template || require('./email').generateVerifyCodeTemplate
    const sender = dependencies.sender || require('./config').email_user
    const code = generateVerificationCode()
    const issuanceId = crypto.randomBytes(16).toString('hex')
    const result = await issue(email, code, issuanceId)
    if (result !== 1) return result
    try {
        await send({from: sender, to: email,
            subject: `🔐 您的验证码：${code} - 请在5分钟内使用`,
            html: template(code, email)})
        return 1
    } catch (error) {
        // 只撤销这次签发；Redis 脚本还会核对签发 ID，避免删除后来的验证码。
        await rollback(email, issuanceId)
        throw error
    }
}

module.exports = { issueAndSend }
