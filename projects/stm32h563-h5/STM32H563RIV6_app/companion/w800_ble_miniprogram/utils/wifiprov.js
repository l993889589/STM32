const FLAG_MORE = 0x20
const CMD_CONFIG_STA = 0x0a
const CMD_CONFIG_STA_RESULT = 0x8a

function uuidMatches(uuid, shortUuid) {
  const normalized = String(uuid || '').replace(/-/g, '').toUpperCase()
  const short = String(shortUuid || '').replace(/^0X/i, '').toUpperCase()
  return normalized === short || normalized.startsWith(`0000${short}`)
}

function crc8(bytes) {
  let crc = 0
  for (const value of bytes) {
    crc ^= value
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc & 1) ? ((crc >>> 1) ^ 0xe0) : (crc >>> 1)
    }
  }
  return crc & 0xff
}

function utf8Bytes(text) {
  const escaped = unescape(encodeURIComponent(text))
  const bytes = []
  for (let i = 0; i < escaped.length; i += 1) bytes.push(escaped.charCodeAt(i))
  return bytes
}

function tlv(type, value) {
  if (value.length > 255) throw new Error('TLV is too long')
  return [type, value.length, ...value]
}

function buildConfigFrames(ssid, password, mtu = 20) {
  const ssidBytes = utf8Bytes(ssid)
  const passwordBytes = utf8Bytes(password)
  if (ssidBytes.length < 1 || ssidBytes.length > 32) throw new Error('SSID 必须为 1..32 字节')
  if (passwordBytes.length !== 0 && (passwordBytes.length < 8 || passwordBytes.length > 63)) {
    throw new Error('密码必须为空或 8..63 字节')
  }

  const payload = [...tlv(0x01, ssidBytes)]
  if (passwordBytes.length) payload.push(...tlv(0x02, passwordBytes))
  const chunkSize = Math.max(1, mtu - 5)
  const frames = []
  let offset = 0
  let sequence = 0
  let fragment = 0
  while (offset < payload.length) {
    const chunk = payload.slice(offset, offset + chunkSize)
    offset += chunk.length
    const flag = offset < payload.length ? FLAG_MORE : 0
    const frame = [CMD_CONFIG_STA, sequence & 0xff, flag, fragment & 0xff, ...chunk]
    frame.push(crc8(frame))
    frames.push(Uint8Array.from(frame).buffer)
    sequence += 1
    fragment += 1
  }
  return frames
}

function parseFrame(buffer) {
  const bytes = Array.from(new Uint8Array(buffer))
  if (bytes.length < 5) throw new Error('BLE frame is too short')
  const expected = bytes[bytes.length - 1]
  if (crc8(bytes.slice(0, -1)) !== expected) throw new Error('BLE CRC8 mismatch')
  return {
    command: bytes[0],
    sequence: bytes[1],
    more: (bytes[2] & FLAG_MORE) !== 0,
    fragment: bytes[3],
    payload: bytes.slice(4, -1)
  }
}

function parseTlvs(payload) {
  const values = {}
  let offset = 0
  while (offset + 2 <= payload.length) {
    const type = payload[offset]
    const length = payload[offset + 1]
    offset += 2
    if (offset + length > payload.length) throw new Error('invalid result TLV')
    values[type] = payload.slice(offset, offset + length)
    offset += length
  }
  return values
}

function parseConfigResult(payload) {
  const values = parseTlvs(payload)
  const status = values[0x81] ? values[0x81][0] : 1
  const ip = values[0x82] || values[0x02] || []
  const mac = values[0x83] || values[0x03] || []
  return {
    status,
    ip: ip.length === 4 ? ip.join('.') : '',
    mac: mac.length === 6 ? mac.map(v => v.toString(16).padStart(2, '0')).join(':') : ''
  }
}

module.exports = {
  uuidMatches,
  CMD_CONFIG_STA_RESULT,
  buildConfigFrames,
  crc8,
  parseConfigResult,
  parseFrame
}
