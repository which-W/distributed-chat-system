const config_module = require("./config")
const redis = require("ioredis")
const { verificationKeys } = require('./security')
//建立对象
console.log('Redis host:', config_module.redis_host);
console.log('Redis port:', config_module.redis_port);

// 创建Redis客户端实例
const RedisCli = new redis({
  host: config_module.redis_host,       // Redis服务器主机名
  port: config_module.redis_port,        // Redis服务器端口号
  password: config_module.redis_passwd, // Redis密码
});
/**
 * 监听错误信息
 */
RedisCli.on("error", function (err) {
  console.log("RedisCli connect error:" , err);
  RedisCli.quit();
});

/**
 * 根据key获取value
 * @param {*} key
 * @returns
 */
async function GetRedis(key) {

    try {
        const result = await RedisCli.get(key)
        if (result === null) {
            console.log('result:', '<' + result + '>', 'This key cannot be find...')
            return null
        }
        console.log('Result:', '<' + result + '>', 'Get key success!...');
        return result
    } catch (error) {
        console.log('GetRedis error is', error);
        return null
    }

}

/**
* 根据key查询redis中是否存在key
* @param {*} key
* @returns
*/
async function QueryRedis(key) {
    try {
        const result = await RedisCli.exists(key)
        //  判断该值是否为空 如果为空返回null
        if (result === 0) {
            console.log('result:<', '<' + result + '>', 'This key is null...');
            return null
        }
        console.log('Result:', '<' + result + '>', 'With this value!...');
        return result
    } catch (error) {
        console.log('QueryRedis error is', error);
        return null
    }

}

/**
* 设置key和value，并过期时间
* @param {*} key
* @param {*} value
* @param {*} exptime
* @returns
*/
async function SetRedisExpire(key, value, exptime) {
    try {
        // 设置键和值
        await RedisCli.set(key, value)
        // 设置过期时间（以秒为单位）
        await RedisCli.expire(key, exptime);
        return true;
    } catch (error) {
        console.log('SetRedisExpire error is', error);
        return false;
    }
}

const ISSUE_CODE_SCRIPT = `
if redis.call('EXISTS', KEYS[2]) == 1 then
  return 2
end
local hourly = redis.call('INCR', KEYS[3])
if hourly == 1 then
  redis.call('EXPIRE', KEYS[3], ARGV[3])
end
if hourly > tonumber(ARGV[4]) then
  return 3
end
local global_hourly = redis.call('INCR', KEYS[5])
if global_hourly == 1 then
  redis.call('EXPIRE', KEYS[5], ARGV[3])
end
if global_hourly > tonumber(ARGV[6]) then
  return 4
end
redis.call('SET', KEYS[2], '1', 'EX', ARGV[2])
redis.call('SET', KEYS[1], ARGV[1], 'EX', ARGV[5])
redis.call('DEL', KEYS[4])
return 1
`

async function IssueVerificationCode(email, code, options = {}) {
    const cooldownSeconds = options.cooldownSeconds ?? 60
    const hourlyWindowSeconds = options.hourlyWindowSeconds ?? 3600
    const hourlyLimit = options.hourlyLimit ?? 10
    const codeTtlSeconds = options.codeTtlSeconds ?? 300
	// 单邮箱限制不能阻止轮换地址；全局预算保护 SMTP 配额和发件信誉。
	const globalHourlyLimit = options.globalHourlyLimit
		?? Number(process.env.VARIFY_GLOBAL_HOURLY_LIMIT || 500)
    const keys = verificationKeys(email)
    try {
        const result = await RedisCli.eval(
            ISSUE_CODE_SCRIPT,
			5,
            keys.code,
            keys.cooldown,
            keys.hourly,
            keys.attempts,
			'verification_global_hourly',
            code,
            cooldownSeconds,
            hourlyWindowSeconds,
            hourlyLimit,
            codeTtlSeconds,
			globalHourlyLimit,
        )
        return Number(result)
    } catch (error) {
        console.log('IssueVerificationCode error:', error.message)
        return 0
    }
}

/**
 * 退出函数
 */
function Quit() {
    RedisCli.quit();
}

module.exports = { GetRedis, QueryRedis, Quit, SetRedisExpire, IssueVerificationCode }
