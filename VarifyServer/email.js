const nodemailer = require('nodemailer');
const config_module = require("./config")

/**
 * 创建发送邮件的代理
 */
let transport = nodemailer.createTransport({
    host: config_module.smtp_host,
    port: config_module.smtp_port,
    secure: config_module.smtp_secure,
    auth: {
        user: config_module.email_user, // 发送方邮箱地址
        pass: config_module.email_pass // 邮箱授权码或者密码
    }
});


/**
 * 生成验证码邮件的HTML模板
 * @param {string} verifyCode 验证码
 * @param {string} email 接收邮箱
 * @returns {string} HTML模板
 */
function generateVerifyCodeTemplate(verifyCode, email) {
    return `
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>验证码邮件</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            padding: 20px;
            min-height: 100vh;
        }

        .email-container {
            max-width: 600px;
            margin: 0 auto;
            background: #ffffff;
            border-radius: 20px;
            box-shadow: 0 20px 40px rgba(0, 0, 0, 0.1);
            overflow: hidden;
            position: relative;
        }

        .header {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            padding: 40px 30px;
            text-align: center;
            color: white;
            position: relative;
        }

        .header::before {
            content: '';
            position: absolute;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background: url('data:image/svg+xml,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><defs><pattern id="grain" width="100" height="100" patternUnits="userSpaceOnUse"><circle cx="25" cy="25" r="1" fill="white" opacity="0.1"/><circle cx="75" cy="75" r="1" fill="white" opacity="0.1"/><circle cx="50" cy="10" r="0.5" fill="white" opacity="0.15"/><circle cx="10" cy="60" r="0.5" fill="white" opacity="0.15"/></pattern></defs><rect width="100" height="100" fill="url(%23grain)"/></svg>');
        }

        .logo {
            position: relative;
            z-index: 1;
        }

        .logo h1 {
            font-size: 28px;
            font-weight: 700;
            margin-bottom: 8px;
            letter-spacing: -0.5px;
        }

        .logo p {
            font-size: 16px;
            opacity: 0.9;
            font-weight: 400;
        }

        .content {
            padding: 50px 40px;
            text-align: center;
        }

        .greeting {
            font-size: 24px;
            color: #2d3748;
            margin-bottom: 20px;
            font-weight: 600;
        }

        .message {
            font-size: 16px;
            color: #4a5568;
            line-height: 1.6;
            margin-bottom: 40px;
        }

        .verify-code-container {
            background: linear-gradient(135deg, #f7fafc 0%, #edf2f7 100%);
            border: 2px dashed #cbd5e0;
            border-radius: 15px;
            padding: 30px;
            margin: 30px 0;
            position: relative;
            overflow: hidden;
        }

        .verify-code-container::before {
            content: '';
            position: absolute;
            top: -50%;
            left: -50%;
            width: 200%;
            height: 200%;
            background: linear-gradient(45deg, transparent 30%, rgba(255,255,255,0.3) 50%, transparent 70%);
            animation: shimmer 3s infinite;
        }

        @keyframes shimmer {
            0% { transform: translateX(-100%) translateY(-100%) rotate(45deg); }
            100% { transform: translateX(100%) translateY(100%) rotate(45deg); }
        }

        .verify-code-label {
            font-size: 14px;
            color: #718096;
            margin-bottom: 15px;
            font-weight: 500;
            text-transform: uppercase;
            letter-spacing: 1px;
        }

        .verify-code {
            font-size: 36px;
            font-weight: 700;
            color: #667eea;
            letter-spacing: 8px;
            margin: 0;
            position: relative;
            z-index: 1;
            text-shadow: 0 2px 4px rgba(102, 126, 234, 0.1);
        }

        .expiry-notice {
            background: linear-gradient(135deg, #fed7d7 0%, #feb2b2 100%);
            border-left: 4px solid #e53e3e;
            padding: 20px;
            margin: 30px 0;
            border-radius: 8px;
            font-size: 14px;
            color: #742a2a;
        }

        .expiry-notice strong {
            color: #c53030;
        }

        .security-tips {
            background: #f0fff4;
            border: 1px solid #9ae6b4;
            border-radius: 12px;
            padding: 25px;
            margin: 30px 0;
            text-align: left;
        }

        .security-tips h3 {
            color: #276749;
            font-size: 18px;
            margin-bottom: 15px;
            display: flex;
            align-items: center;
        }

        .security-tips h3::before {
            content: '🔒';
            margin-right: 8px;
        }

        .security-tips ul {
            list-style: none;
            padding: 0;
        }

        .security-tips li {
            color: #2f855a;
            font-size: 14px;
            margin-bottom: 8px;
            padding-left: 20px;
            position: relative;
        }

        .security-tips li::before {
            content: '✓';
            position: absolute;
            left: 0;
            color: #38a169;
            font-weight: bold;
        }

        .footer {
            background: #f7fafc;
            padding: 30px;
            text-align: center;
            border-top: 1px solid #e2e8f0;
        }

        .footer p {
            color: #718096;
            font-size: 14px;
            line-height: 1.5;
            margin-bottom: 10px;
        }

        .footer .company {
            font-weight: 600;
            color: #4a5568;
        }

        .footer .timestamp {
            font-size: 12px;
            color: #a0aec0;
            margin-top: 15px;
        }

        @media (max-width: 600px) {
            .email-container {
                margin: 10px;
                border-radius: 15px;
            }

            .header {
                padding: 30px 20px;
            }

            .content {
                padding: 30px 20px;
            }

            .verify-code {
                font-size: 28px;
                letter-spacing: 4px;
            }

            .greeting {
                font-size: 20px;
            }
        }
    </style>
</head>
<body>
    <div class="email-container">
        <div class="header">
            <div class="logo">
                <h1>🚀 骚话聊天室（暗黑版）</h1>
                <p>安全验证服务</p>
            </div>
        </div>

        <div class="content">
            <h2 class="greeting">验证码已发送！</h2>
            <p class="message">
                亲爱的用户，感谢您使用我们的服务！<br>
                为了确保您的账户安全，请使用以下验证码完成验证：
            </p>

            <div class="verify-code-container">
                <div class="verify-code-label">您的验证码</div>
                <div class="verify-code">${verifyCode}</div>
            </div>

            <div class="expiry-notice">
                <strong>⏰ 重要提醒：</strong> 此验证码将在 <strong>10分钟</strong> 后失效，请尽快使用。
            </div>

            <div class="security-tips">
                <h3>安全提示</h3>
                <ul>
                    <li>请勿将验证码告诉他人</li>
                    <li>我们不会通过电话索要验证码</li>
                    <li>如非本人操作，请忽略此邮件</li>
                    <li>建议使用安全的网络环境进行操作</li>
                </ul>
            </div>
        </div>

        <div class="footer">
            <p class="company">爱你一万年技术团队</p>
            <p>如有疑问，请联系客服：15894062939</p>
            <p class="timestamp">发送时间：${new Date().toLocaleString('zh-CN')}</p>
            <p class="timestamp">发送至：${email}</p>
        </div>
    </div>
</body>
</html>`;
}

/**
 * 发送邮件的函数
 * @param {*} mailOptions_ 发送邮件的参数
 * @returns
 */
function SendMail(mailOptions_){
    return new Promise(function(resolve, reject){
        transport.sendMail(mailOptions_, function(error, info){
            if (error) {
                console.log(error);
                reject(error);
            } else {
                console.log('邮件已成功发送：' + info.response);
                resolve(info.response)
            }
        });
    })

}


module.exports = {
    SendMail,
    generateVerifyCodeTemplate
}
