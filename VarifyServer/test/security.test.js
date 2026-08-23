const test = require('node:test')
const assert = require('node:assert/strict')
const { generateVerificationCode, normalizeEmail, verificationKeys } = require('../security')

test('verification codes are six decimal digits', () => {
    for (let i = 0; i < 1000; ++i) {
        assert.match(generateVerificationCode(), /^\d{6}$/)
    }
})

test('email normalization is stable and rejects malformed input', () => {
    assert.equal(normalizeEmail(' User@Example.COM '), 'user@example.com')
    assert.equal(normalizeEmail('not-an-email'), null)
    assert.equal(normalizeEmail(''), null)
})

test('verification keys use the normalized email namespace', () => {
    assert.deepEqual(verificationKeys('user@example.com'), {
        code: 'code_user@example.com',
        attempts: 'code_attempts_user@example.com',
        cooldown: 'code_cooldown_user@example.com',
        hourly: 'code_hourly_user@example.com',
    })
})
