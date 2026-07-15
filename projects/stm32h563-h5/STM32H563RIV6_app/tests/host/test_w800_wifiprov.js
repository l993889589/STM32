'use strict'

const assert = require('assert')
const protocol = require('../../companion/w800_ble_miniprogram/utils/wifiprov')

assert(protocol.uuidMatches('1824', '1824'))
assert(protocol.uuidMatches('00001824-0000-1000-8000-00805F9B34FB', '1824'))
assert(protocol.uuidMatches('00002ABC-0000-1000-8000-00805F9B34FB', '2ABC'))
assert(!protocol.uuidMatches('00001825-0000-1000-8000-00805F9B34FB', '1824'))

assert.strictEqual(protocol.crc8([0x00]), 0x00)
assert.strictEqual(protocol.crc8([0x01]), 0x91)
assert.strictEqual(protocol.crc8([0x02]), 0xe3)

const frames = protocol.buildConfigFrames('factory-network', '12345678', 20)
assert(frames.length > 1)
let payload = []
frames.forEach((buffer, index) => {
  const frame = protocol.parseFrame(buffer)
  assert.strictEqual(frame.command, 0x0a)
  assert.strictEqual(frame.sequence, index)
  assert.strictEqual(frame.fragment, index)
  assert.strictEqual(frame.more, index !== frames.length - 1)
  payload.push(...frame.payload)
})
assert.strictEqual(payload[0], 0x01)
assert.strictEqual(payload[1], 'factory-network'.length)
assert(payload.includes(0x02))

const result = protocol.parseConfigResult([
  0x81, 0x01, 0x00,
  0x82, 0x04, 192, 168, 1, 20,
  0x83, 0x06, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60
])
assert.deepStrictEqual(result, {
  status: 0,
  ip: '192.168.1.20',
  mac: '10:20:30:40:50:60'
})

console.log('W800 WIFIPROV codec tests passed')
