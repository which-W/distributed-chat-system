const test = require('node:test')
const assert = require('node:assert/strict')
const { issueAndSend } = require('../verification')

test('邮件失败只回滚当前随机签发标识', async () => {
    let issued
    let rolledBack
    await assert.rejects(issueAndSend('alice@example.test', {
        issue: async (email, code, id) => {
            issued = {email, code, id}
            return 1
        },
        rollback: async (email, id) => { rolledBack = {email, id}; return true },
        send: async () => { throw new Error('smtp unavailable') },
        template: () => '<p>code</p>',
        sender: 'demo@example.test',
    }), /smtp unavailable/)
    assert.match(issued.code, /^[0-9]{6}$/)
    assert.match(issued.id, /^[a-f0-9]{32}$/)
    assert.deepEqual(rolledBack, {email: issued.email, id: issued.id})
})

test('冷却或预算拒绝签发时不发送邮件也不回滚', async () => {
    let sends = 0
    let rollbacks = 0
    const result = await issueAndSend('alice@example.test', {
        issue: async () => 3,
        rollback: async () => { ++rollbacks },
        send: async () => { ++sends },
        template: () => '',
        sender: 'demo@example.test',
    })
    assert.equal(result, 3)
    assert.equal(sends, 0)
    assert.equal(rollbacks, 0)
})
