const crypto = require('crypto')

const EMAIL_PATTERN = /^[^\s@]+@[^\s@]+\.[^\s@]+$/

function normalizeEmail(value) {
    if (typeof value !== 'string') return null
    const email = value.trim().toLowerCase()
    if (email.length === 0 || email.length > 254 || !EMAIL_PATTERN.test(email)) return null
    return email
}

function generateVerificationCode() {
    return crypto.randomInt(0, 1_000_000).toString().padStart(6, '0')
}

function verificationKeys(email) {
    return {
        code: `code_${email}`,
        attempts: `code_attempts_${email}`,
        cooldown: `code_cooldown_${email}`,
        hourly: `code_hourly_${email}`,
    }
}

module.exports = { generateVerificationCode, normalizeEmail, verificationKeys }
