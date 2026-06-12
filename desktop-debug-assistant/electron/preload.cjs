const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("debugAssistant", {
  listPorts: () => ipcRenderer.invoke("serial:list"),
  openPort: (options) => ipcRenderer.invoke("serial:open", options),
  closePort: (options) => ipcRenderer.invoke("serial:close", options),
  writePort: (payload) => ipcRenderer.invoke("serial:write", payload),
  getLogPaths: () => ipcRenderer.invoke("logs:paths"),
  getMqttStatus: () => ipcRenderer.invoke("mqtt:status"),
  startMqtt: (options) => ipcRenderer.invoke("mqtt:start", options),
  stopMqtt: () => ipcRenderer.invoke("mqtt:stop"),
  publishMqtt: (payload) => ipcRenderer.invoke("mqtt:publish", payload),
  simulateW800: (options) => ipcRenderer.invoke("mqtt:simulate-w800", options),
  getOtaInfo: () => ipcRenderer.invoke("ota:info"),
  listOtaPackages: () => ipcRenderer.invoke("ota:list"),
  pickOtaInput: () => ipcRenderer.invoke("ota:pick-input"),
  convertOtaPackage: (options) => ipcRenderer.invoke("ota:convert", options),
  startOta: (options) => ipcRenderer.invoke("ota:start", options),
  stopOta: () => ipcRenderer.invoke("ota:stop"),
  runBoardFlash: (options) => ipcRenderer.invoke("board:flash", options),
  stopBoardFlash: () => ipcRenderer.invoke("board:flash-stop"),
  onData: (callback) => ipcRenderer.on("serial:data", (_event, data) => callback(data)),
  onError: (callback) => ipcRenderer.on("serial:error", (_event, message) => callback(message)),
  onClosed: (callback) => ipcRenderer.on("serial:closed", (_event, payload) => callback(payload)),
  onMqttEvent: (callback) => ipcRenderer.on("mqtt:event", (_event, payload) => callback(payload)),
  onOtaProgress: (callback) => ipcRenderer.on("ota:progress", (_event, payload) => callback(payload)),
  onBoardFlashOutput: (callback) => ipcRenderer.on("board:flash-output", (_event, payload) => callback(payload)),
  onBoardFlashDone: (callback) => ipcRenderer.on("board:flash-done", (_event, payload) => callback(payload))
});
