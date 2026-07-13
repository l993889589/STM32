const protocol = require('../../utils/wifiprov')

function call(api, options = {}) {
  return new Promise((resolve, reject) => api({...options, success: resolve, fail: reject}))
}

Page({
  data: {
    devices: [], scanning: false, connected: false, configuring: false,
    ssid: '', password: '', status: '请先扫描并连接 W800', statusKind: ''
  },

  onLoad() {
    this.rxPayload = []
    wx.onBluetoothDeviceFound(result => {
      const merged = [...this.data.devices]
      for (const device of result.devices || []) {
        const index = merged.findIndex(item => item.deviceId === device.deviceId)
        if (index >= 0) merged[index] = device
        else merged.push(device)
      }
      merged.sort((a, b) => (b.RSSI || -127) - (a.RSSI || -127))
      this.setData({devices: merged})
    })
    wx.onBLECharacteristicValueChange(event => this.onValue(event))
  },

  async scan() {
    try {
      await call(wx.openBluetoothAdapter)
      await call(wx.startBluetoothDevicesDiscovery, {allowDuplicatesKey: false, interval: 0})
      this.setData({devices: [], scanning: true, status: '正在扫描附近 BLE 设备…', statusKind: ''})
      setTimeout(() => {
        wx.stopBluetoothDevicesDiscovery()
        this.setData({scanning: false})
      }, 8000)
    } catch (error) {
      this.setData({scanning: false, status: `蓝牙启动失败：${error.errMsg || error}`, statusKind: 'error'})
    }
  },

  async connect(event) {
    const deviceId = event.currentTarget.dataset.id
    try {
      wx.stopBluetoothDevicesDiscovery()
      await call(wx.createBLEConnection, {deviceId, timeout: 10000})
      const serviceResult = await call(wx.getBLEDeviceServices, {deviceId})
      const service = serviceResult.services.find(item => protocol.uuidMatches(item.uuid, '1824'))
      if (!service) throw new Error('设备没有 WIFIPROV 0x1824 服务')
      const characteristicResult = await call(wx.getBLEDeviceCharacteristics, {deviceId, serviceId: service.uuid})
      const characteristic = characteristicResult.characteristics.find(item => protocol.uuidMatches(item.uuid, '2ABC'))
      if (!characteristic) throw new Error('设备没有 WIFIPROV 0x2ABC 特征')
      await call(wx.notifyBLECharacteristicValueChange, {
        deviceId, serviceId: service.uuid, characteristicId: characteristic.uuid, state: true
      })
      this.deviceId = deviceId
      this.serviceId = service.uuid
      this.characteristicId = characteristic.uuid
      this.rxPayload = []
      this.setData({connected: true, scanning: false, status: 'W800 已连接，可以写入 Wi-Fi 参数', statusKind: 'success'})
    } catch (error) {
      this.setData({connected: false, status: `连接失败：${error.errMsg || error.message || error}`, statusKind: 'error'})
    }
  },

  onSsid(event) { this.setData({ssid: event.detail.value}) },
  onPassword(event) { this.setData({password: event.detail.value}) },

  async configure() {
    try {
      this.setData({configuring: true, status: '正在发送 Wi-Fi 参数…', statusKind: ''})
      const frames = protocol.buildConfigFrames(this.data.ssid, this.data.password, 20)
      for (const value of frames) {
        await call(wx.writeBLECharacteristicValue, {
          deviceId: this.deviceId,
          serviceId: this.serviceId,
          characteristicId: this.characteristicId,
          value
        })
      }
      this.setData({status: '参数已发送，等待 W800 返回连接结果…'})
    } catch (error) {
      this.setData({configuring: false, status: `配网发送失败：${error.errMsg || error.message || error}`, statusKind: 'error'})
    }
  },

  onValue(event) {
    if (!protocol.uuidMatches(event.characteristicId, '2ABC')) return
    try {
      const frame = protocol.parseFrame(event.value)
      if (frame.fragment === 0) this.rxPayload = []
      this.rxPayload.push(...frame.payload)
      if (frame.more || frame.command !== protocol.CMD_CONFIG_STA_RESULT) return
      const result = protocol.parseConfigResult(this.rxPayload)
      if (result.status === 0) {
        this.setData({
          configuring: false,
          status: `配网成功${result.ip ? `，IP ${result.ip}` : ''}${result.mac ? `，MAC ${result.mac}` : ''}`,
          statusKind: 'success',
          password: ''
        })
      } else {
        const reasons = {2: '密码错误', 3: 'DHCP 获取 IP 失败', 4: 'Wi-Fi 扫描失败'}
        this.setData({configuring: false, status: `配网失败：${reasons[result.status] || `状态码 ${result.status}`}`, statusKind: 'error'})
      }
    } catch (error) {
      this.setData({configuring: false, status: `结果解析失败：${error.message || error}`, statusKind: 'error'})
    }
  },

  onUnload() {
    wx.stopBluetoothDevicesDiscovery()
    if (this.deviceId) wx.closeBLEConnection({deviceId: this.deviceId})
    wx.closeBluetoothAdapter()
  }
})
