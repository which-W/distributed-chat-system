const grpc = require('@grpc/grpc-js')
const message_proto = require('./proto')
const const_module = require('./const')
const config = require('./config')
const {v4:uuidv4} = require('uuid')
const emailModule = require('./email')
const redis_module = require('./redis')

async function GetVarifyCode(call, callback) {
    console.log("email is ", call.request.email)
    try{
        let query_res = await redis_module.GetRedis(const_module.code_prefix+call.request.email);
        if(query_res == null){

        }
         let uniqueId = query_res;
        if(query_res == null){
            // 生成4位数字验证码（更常见的格式）
            uniqueId = Math.floor(1000 + Math.random() * 9000).toString();
            let bres = await redis_module.SetRedisExpire(const_module.code_prefix+call.request.email, uniqueId, 600)
            if(!bres){
                callback(null, {
                    email: call.request.email,
                    error: const_module.Errors.RedisErr
                });
                return;
            }
        }

        const htmlContent = emailModule.generateVerifyCodeTemplate(uniqueId, call.request.email);
        //发送邮件
        let mailOptions = {
            from: config.email_user,
            to: call.request.email,
            subject: `🔐 您的验证码：${uniqueId} - 请在10分钟内使用`,
            html: htmlContent,
        };

        let send_res = await emailModule.SendMail(mailOptions);
        console.log("send res is ", send_res)

        callback(null, { email:  call.request.email,
            error:const_module.Errors.Success
        });


    }catch(error){
        console.log("catch error is ", error)

        callback(null, { email:  call.request.email,
            error:const_module.Errors.Exception
        });
    }

}

function main() {
    var server = new grpc.Server()
    server.addService(message_proto.VarifyService.service, { GetVarifyCode: GetVarifyCode })
    const bindAddress = process.env.VARIFY_BIND_ADDRESS || '0.0.0.0:5000'
    server.bindAsync(bindAddress, grpc.ServerCredentials.createInsecure(), () => {
        console.log(`grpc server started on ${bindAddress}`)
    })
}

main()
