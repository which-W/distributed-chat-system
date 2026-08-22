const fs = require('fs');
const path = require('path');

const configPath = process.env.VARIFY_CONFIG_FILE || path.join(__dirname, 'config.json');
let config = {};
if (fs.existsSync(configPath)) {
  config = JSON.parse(fs.readFileSync(configPath, 'utf8'));
}

const read = (envName, objectName, key, fallback = '') =>
  process.env[envName] ?? config[objectName]?.[key] ?? fallback;

const email_user = read('VARIFY_EMAIL_USER', 'email', 'user');
const email_pass = read('VARIFY_EMAIL_PASSWORD', 'email', 'pass');
const redis_host = read('VARIFY_REDIS_HOST', 'redis', 'host', '127.0.0.1');
const redis_port = Number(read('VARIFY_REDIS_PORT', 'redis', 'port', 6379));
const redis_passwd = read('VARIFY_REDIS_PASSWORD', 'redis', 'passwd');
const smtp_host = read('VARIFY_SMTP_HOST', 'smtp', 'host', 'smtp.163.com');
const smtp_port = Number(read('VARIFY_SMTP_PORT', 'smtp', 'port', 465));
const smtp_secure = String(read('VARIFY_SMTP_SECURE', 'smtp', 'secure', true)).toLowerCase() === 'true';
const code_prefix = 'code_';

module.exports = {
  email_pass,
  email_user,
  redis_host,
  redis_port,
  redis_passwd,
  smtp_host,
  smtp_port,
  smtp_secure,
  code_prefix,
};
