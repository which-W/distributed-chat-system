const crypto = require('crypto')
const ranks = { debug: 0, info: 1, warn: 2, error: 3 }
const minimum = String(process.env.CHAT_LOG_LEVEL || 'info').toLowerCase()

function log(level, event, message, fields = {}) {
  const normalized = String(level).toLowerCase()
  if ((ranks[normalized] ?? ranks.info) < (ranks[minimum] ?? ranks.info)) return
  const clean = {}
  for (const [key, value] of Object.entries(fields)) {
    if (value !== undefined && value !== null && value !== '') clean[key] = value
  }
  process.stdout.write(`${JSON.stringify({
    timestamp: new Date().toISOString(), level: normalized.toUpperCase(),
    service: 'varify_server', pid: process.pid, thread: 'main', event, message, ...clean,
  })}\n`)
}

function emailHash(email) {
  return crypto.createHash('sha256').update(String(email)).digest('hex').slice(0, 12)
}

module.exports = { log, emailHash }
