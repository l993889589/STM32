import React, { useEffect, useMemo, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import "./style.css";

const BAUD_RATES = ["9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"];
const DATA_BITS = ["8", "7", "6", "5"];
const STOP_BITS = ["1", "2"];
const PARITIES = [
  ["none", "NONE"],
  ["even", "EVEN"],
  ["odd", "ODD"]
];
const W800_TOPIC_PRESETS = [
  ["leduo/pc/down", "PC下发"],
  ["leduo/w800/up", "W800上报"],
  ["leduo/w800/status", "W800状态"],
  ["leduo/w800/log", "W800日志"]
];
const W800_COMMAND_PRESETS = [
  ["Ping", { cmd: "ping" }],
  ["状态", { cmd: "status" }],
  ["开日志", { cmd: "log", enable: true }],
  ["关日志", { cmd: "log", enable: false }],
  ["重启", { cmd: "reboot" }]
];

function hexBytes(text) {
  return text.replace(/[^0-9a-fA-F]/g, "").match(/.{1,2}/g)?.join(" ").toUpperCase() || "";
}

function parseHexBytes(text) {
  const clean = text.replace(/[^0-9a-fA-F]/g, "");
  const bytes = [];

  for (let i = 0; i + 1 < clean.length; i += 2) {
    bytes.push(Number.parseInt(clean.slice(i, i + 2), 16));
  }

  return bytes;
}

function bytesToHex(bytes) {
  return bytes.map((byte) => byte.toString(16).padStart(2, "0")).join(" ").toUpperCase();
}

function numberFromInput(value) {
  const text = String(value).trim();
  if (text.toLowerCase().startsWith("0x")) return Number.parseInt(text.slice(2), 16);
  return Number.parseInt(text || "0", 10);
}

function modbusCrc(bytes) {
  let crc = 0xffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let i = 0; i < 8; i += 1) {
      crc = (crc & 1) ? ((crc >> 1) ^ 0xa001) : (crc >> 1);
    }
  }
  return crc & 0xffff;
}

function buildReadHoldingFrame(unit, start, count) {
  const frame = [
    unit & 0xff,
    0x03,
    (start >> 8) & 0xff,
    start & 0xff,
    (count >> 8) & 0xff,
    count & 0xff
  ];
  const crc = modbusCrc(frame);
  frame.push(crc & 0xff, (crc >> 8) & 0xff);
  return bytesToHex(frame);
}

function putU16(bytes, value) {
  bytes.push((value >> 8) & 0xff, value & 0xff);
}

function putU32(bytes, value) {
  bytes.push((value >>> 24) & 0xff, (value >>> 16) & 0xff, (value >>> 8) & 0xff, value & 0xff);
}

function getU16(bytes, offset) {
  return ((bytes[offset] << 8) | bytes[offset + 1]) >>> 0;
}

function getU32(bytes, offset) {
  return (((bytes[offset] << 24) >>> 0) | (bytes[offset + 1] << 16) | (bytes[offset + 2] << 8) | bytes[offset + 3]) >>> 0;
}

function parseModbusReadHoldingResponse(bytes, request) {
  if (!request || bytes.length < 5) return null;

  const unit = bytes[0];
  const functionCode = bytes[1];
  const gotCrc = bytes[bytes.length - 2] | (bytes[bytes.length - 1] << 8);
  const calcCrc = modbusCrc(bytes.slice(0, -2));
  const result = {
    at: new Date().toISOString(),
    unit,
    functionCode,
    ok: false,
    crcOk: gotCrc === calcCrc,
    elapsedMs: Math.round(performance.now() - request.at),
    message: "",
    registers: []
  };

  if (unit !== request.unit) {
    result.message = `从机地址不匹配: ${unit}`;
    return result;
  }

  if (!result.crcOk) {
    result.message = `CRC错误: got=${gotCrc.toString(16).toUpperCase()} calc=${calcCrc.toString(16).toUpperCase()}`;
    return result;
  }

  if (functionCode === 0x83) {
    result.message = `异常响应: code=0x${bytes[2].toString(16).padStart(2, "0").toUpperCase()}`;
    return result;
  }

  if (functionCode !== 0x03) {
    result.message = `功能码不匹配: 0x${functionCode.toString(16).padStart(2, "0").toUpperCase()}`;
    return result;
  }

  const byteCount = bytes[2];
  const expectedBytes = request.count * 2;

  if (byteCount !== expectedBytes || bytes.length !== 3 + byteCount + 2) {
    result.message = `长度不匹配: byteCount=${byteCount}, expected=${expectedBytes}`;
    return result;
  }

  for (let i = 0; i < request.count; i += 1) {
    const value = getU16(bytes, 3 + i * 2);
    result.registers.push({
      address: request.start + i,
      value,
      hex: value.toString(16).padStart(4, "0").toUpperCase()
    });
  }

  result.ok = true;
  result.message = `OK，读取 ${result.registers.length} 个寄存器`;
  return result;
}

function createModbusStressStats() {
  return {
    tx: 0,
    ok: 0,
    timeout: 0,
    crcError: 0,
    exception: 0,
    mismatch: 0,
    writeError: 0,
    rttSum: 0,
    rttMax: 0,
    lastError: ""
  };
}

function validateModbusStressResponse(parsed, request) {
  if (!parsed) return { ok: false, bucket: "timeout", message: "响应超时" };
  if (!parsed.crcOk) return { ok: false, bucket: "crcError", message: parsed.message || "CRC错误" };
  if (parsed.functionCode & 0x80) return { ok: false, bucket: "exception", message: parsed.message || "异常响应" };
  if (!parsed.ok) return { ok: false, bucket: "mismatch", message: parsed.message || "响应不匹配" };

  for (const reg of parsed.registers) {
    if (reg.value !== reg.address) {
      return {
        ok: false,
        bucket: "mismatch",
        message: `寄存器${reg.address}值不匹配: got=${reg.value}, expected=${reg.address}`
      };
    }
  }

  if (parsed.registers.length !== request.count) {
    return { ok: false, bucket: "mismatch", message: `寄存器数量不匹配: ${parsed.registers.length}` };
  }

  return { ok: true, bucket: "ok", message: "OK" };
}

function makePayload(seq, length, mode) {
  const payload = [];
  let seed = (seq * 1664525 + 1013904223) >>> 0;

  for (let i = 0; i < length; i += 1) {
    if (mode === "fixed") {
      payload.push(0x55);
    } else if (mode === "random") {
      seed = (seed * 1664525 + 1013904223) >>> 0;
      payload.push((seed >>> 24) & 0xff);
    } else {
      payload.push((seq + i) & 0xff);
    }
  }

  return payload;
}

function buildLdcStressFrame(seq, payloadLength, payloadMode) {
  const body = [];
  putU32(body, seq >>> 0);
  body.push(0x01);
  body.push(...makePayload(seq, payloadLength, payloadMode));

  const frame = [0xaa, 0x55];
  putU16(frame, body.length);
  frame.push(...body);

  const crc = modbusCrc(frame);
  frame.push(crc & 0xff, (crc >> 8) & 0xff);
  return frame;
}

function parseLdcAckFrames(buffer) {
  const frames = [];
  let offset = 0;

  while (offset + 6 <= buffer.length) {
    if (buffer[offset] !== 0x55 || buffer[offset + 1] !== 0xaa) {
      offset += 1;
      continue;
    }

    const len = getU16(buffer, offset + 2);
    const total = 2 + 2 + len + 2;

    if (len < 5) {
      offset += 1;
      continue;
    }

    if (offset + total > buffer.length) break;

    const frame = buffer.slice(offset, offset + total);
    const gotCrc = frame[frame.length - 2] | (frame[frame.length - 1] << 8);
    const calcCrc = modbusCrc(frame.slice(0, -2));

    if (gotCrc === calcCrc) {
      frames.push({
        seq: getU32(frame, 4),
        status: frame[8],
        ok: frame[8] === 0
      });
      offset += total;
    } else {
      frames.push({ seq: 0, status: 0xff, ok: false, crcError: true });
      offset += 1;
    }
  }

  return { frames, rest: buffer.slice(offset) };
}

function normalizeUsbId(value) {
  return String(value || "").replace(/^0x/i, "").toLowerCase().padStart(4, "0");
}

function scoreBoardPort(port) {
  const vendorId = normalizeUsbId(port.vendorId);
  const productId = normalizeUsbId(port.productId);
  const haystack = [
    port.path,
    port.manufacturer,
    port.serialNumber,
    port.pnpId,
    port.friendlyName
  ].filter(Boolean).join(" ").toLowerCase();

  let score = 0;
  if (vendorId === "0483") score += 100;
  if (productId === "5740") score += 80;
  if (haystack.includes("stm")) score += 40;
  if (haystack.includes("virtual comport") || haystack.includes("vcp")) score += 30;
  if (haystack.includes("cdc")) score += 20;
  if (haystack.includes("leduo")) score += 120;
  return score;
}

function scoreModbusPort(port) {
  if (scoreBoardPort(port) > 0) return -1;

  const vendorId = normalizeUsbId(port.vendorId);
  const haystack = [
    port.path,
    port.manufacturer,
    port.serialNumber,
    port.pnpId,
    port.friendlyName
  ].filter(Boolean).join(" ").toLowerCase();

  let score = 0;
  if (vendorId === "0403") score += 100;
  if (vendorId === "1a86") score += 90;
  if (vendorId === "10c4") score += 80;
  if (haystack.includes("ftdi")) score += 80;
  if (haystack.includes("ch340") || haystack.includes("ch341")) score += 70;
  if (haystack.includes("cp210")) score += 70;
  if (haystack.includes("usb serial")) score += 50;
  if (haystack.includes("rs485") || haystack.includes("485")) score += 50;
  return score;
}

function findBoardPort(list) {
  return [...list]
    .map((port) => ({ port, score: scoreBoardPort(port) }))
    .filter((item) => item.score > 0)
    .sort((a, b) => b.score - a.score)[0]?.port || null;
}

function findModbusPort(list) {
  return [...list]
    .map((port) => ({ port, score: scoreModbusPort(port) }))
    .filter((item) => item.score > 0)
    .sort((a, b) => b.score - a.score)[0]?.port || null;
}

function channelName(channel) {
  return channel === "modbus" ? "485" : "USB";
}

function createMqttStats(events) {
  return events.reduce((stats, event) => {
    if (event.event === "client") stats.clients += 1;
    if (event.event === "disconnect") stats.disconnects += 1;
    if (event.event === "subscribe") stats.subscribes += 1;
    if (event.event === "publish") stats.rx += 1;
    if (event.event === "publish-local") stats.tx += 1;
    return stats;
  }, { clients: 0, disconnects: 0, subscribes: 0, rx: 0, tx: 0 });
}

function buildW800Config({ host, port }) {
  return [
    `broker=mqtt://${host}:${port}`,
    `#define APP_W800_MQTT_HOST "${host}"`,
    `#define APP_W800_MQTT_PORT ${port}U`,
    "clientId=w800-<chip-id>",
    "topicUp=leduo/w800/up",
    "topicStatus=leduo/w800/status",
    "topicLog=leduo/w800/log",
    "topicDown=leduo/pc/down",
    "auth=none"
  ].join("\n");
}

function parseW800DeviceMessage(event) {
  if (event.event !== "publish" || !event.topic?.startsWith("leduo/w800/") || !event.payload) return null;

  let payload;
  try {
    payload = JSON.parse(event.payload);
  } catch {
    return null;
  }

  return {
    deviceId: payload.deviceId || payload.id || event.clientId || "w800",
    online: payload.online !== false,
    ip: payload.ip || "",
    rssi: payload.rssi ?? "",
    fw: payload.fw || payload.version || "",
    uptime: payload.uptime ?? "",
    mode: payload.mode || "",
    lastTopic: event.topic,
    lastSeen: event.at,
    lastPayload: event.payload
  };
}

function App() {
  const [ports, setPorts] = useState([]);
  const [logSelected, setLogSelected] = useState("");
  const [modbusSelected, setModbusSelected] = useState("");
  const [baudRate, setBaudRate] = useState("115200");
  const [dataBits, setDataBits] = useState("8");
  const [stopBits, setStopBits] = useState("1");
  const [parity, setParity] = useState("none");
  const [logConnected, setLogConnected] = useState(false);
  const [modbusConnected, setModbusConnected] = useState(false);
  const [rxHex, setRxHex] = useState(false);
  const [lineMode, setLineMode] = useState(true);
  const [showTimestamp, setShowTimestamp] = useState(true);
  const [autoScroll, setAutoScroll] = useState(true);
  const [autoConnect, setAutoConnect] = useState(true);
  const [logPaused, setLogPaused] = useState(false);
  const [logFilter, setLogFilter] = useState("");
  const [showRx, setShowRx] = useState(true);
  const [showTx, setShowTx] = useState(true);
  const [sendHex, setSendHex] = useState(true);
  const [appendNewline, setAppendNewline] = useState(false);
  const [sendText, setSendText] = useState("01 03 00 00 00 03 05 CB");
  const [logs, setLogs] = useState([]);
  const [status, setStatus] = useState("准备好了");
  const [latestLogFile, setLatestLogFile] = useState("");
  const [eventsFile, setEventsFile] = useState("");
  const [modbusUnit, setModbusUnit] = useState("1");
  const [modbusFunction, setModbusFunction] = useState("03");
  const [modbusAddress, setModbusAddress] = useState("0x0000");
  const [modbusCount, setModbusCount] = useState("3");
  const [modbusResult, setModbusResult] = useState(null);
  const [modbusStressCount, setModbusStressCount] = useState("10000");
  const [modbusStressInterval, setModbusStressInterval] = useState("1");
  const [modbusStressTimeout, setModbusStressTimeout] = useState("120");
  const [modbusStressRunning, setModbusStressRunning] = useState(false);
  const [modbusStressStats, setModbusStressStats] = useState(createModbusStressStats);
  const [combinedRunning, setCombinedRunning] = useState(false);
  const [rightTool, setRightTool] = useState("modbus");
  const [stressPayloadLength, setStressPayloadLength] = useState("240");
  const [stressFrameCount, setStressFrameCount] = useState("10000");
  const [stressInterval, setStressInterval] = useState("0");
  const [stressChunkSize, setStressChunkSize] = useState("0");
  const [stressChunkGap, setStressChunkGap] = useState("0");
  const [stressPayloadMode, setStressPayloadMode] = useState("counter");
  const [stressRunning, setStressRunning] = useState(false);
  const [boardBusy, setBoardBusy] = useState(false);
  const [boardBuild, setBoardBuild] = useState(true);
  const [boardNoVerify, setBoardNoVerify] = useState(false);
  const [boardAutoReconnect, setBoardAutoReconnect] = useState(true);
  const [boardOutput, setBoardOutput] = useState([]);
  const [otaInfo, setOtaInfo] = useState(null);
  const [otaPackages, setOtaPackages] = useState([]);
  const [otaPackageId, setOtaPackageId] = useState("default");
  const [otaBusy, setOtaBusy] = useState(false);
  const [otaConvertBusy, setOtaConvertBusy] = useState(false);
  const [otaInputFile, setOtaInputFile] = useState("");
  const [otaImageVersion, setOtaImageVersion] = useState("1");
  const [otaConvertName, setOtaConvertName] = useState("");
  const [otaAppBase, setOtaAppBase] = useState("0x08020000");
  const [otaSendAfterConvert, setOtaSendAfterConvert] = useState(false);
  const [otaConvertOutput, setOtaConvertOutput] = useState("");
  const [otaChunkSize, setOtaChunkSize] = useState("224");
  const [otaResetAfter, setOtaResetAfter] = useState(true);
  const [otaProgress, setOtaProgress] = useState({ stage: "idle", sent: 0, total: 0, percent: 0, detail: "" });
  const [mqttPort, setMqttPort] = useState("1883");
  const [mqttHost, setMqttHost] = useState("");
  const [mqttStatus, setMqttStatus] = useState({ running: false, port: 1883, addresses: [], startedAt: "" });
  const [mqttTopic, setMqttTopic] = useState("leduo/pc/down");
  const [mqttMessage, setMqttMessage] = useState("{\"cmd\":\"ping\"}");
  const [mqttEvents, setMqttEvents] = useState([]);
  const [w800Devices, setW800Devices] = useState({});
  const [stressStats, setStressStats] = useState({
    tx: 0,
    ack: 0,
    timeout: 0,
    crcError: 0,
    statusError: 0,
    missing: 0,
    rttSum: 0,
    rttMax: 0
  });
  const logRef = useRef(null);
  const logQueueRef = useRef([]);
  const logFlushTimerRef = useRef(0);
  const stressStopRef = useRef(false);
  const stressPendingRef = useRef(new Map());
  const stressAckBufferRef = useRef([]);
  const stressNextAckRef = useRef(0);
  const stressStatsRef = useRef({
    tx: 0,
    ack: 0,
    timeout: 0,
    crcError: 0,
    statusError: 0,
    missing: 0,
    rttSum: 0,
    rttMax: 0
  });
  const stressStatsFlushRef = useRef(0);
  const boardOutputRef = useRef(null);
  const mqttEventsRef = useRef(null);
  const modbusRequestRef = useRef(null);
  const modbusRxBufferRef = useRef([]);
  const modbusWaiterRef = useRef(null);
  const modbusStressStopRef = useRef(false);
  const modbusStressStatsRef = useRef(createModbusStressStats());

  const generatedFrame = useMemo(() => {
    if (modbusFunction !== "03") return "";
    return buildReadHoldingFrame(
      numberFromInput(modbusUnit),
      numberFromInput(modbusAddress),
      numberFromInput(modbusCount)
    );
  }, [modbusUnit, modbusFunction, modbusAddress, modbusCount]);

  const visibleLogs = useMemo(() => {
    const keyword = logFilter.trim().toLowerCase();

    return logs.filter((item) => {
      if (item.dir === "SEND" && !showTx) return false;
      if (item.dir !== "SEND" && !showRx) return false;
      if (!keyword) return true;

      return [
        item.at,
        item.dir,
        item.channel,
        item.text,
        item.hex
      ].filter(Boolean).join(" ").toLowerCase().includes(keyword);
    });
  }, [logs, logFilter, showRx, showTx]);

  async function refresh(autoOpenLog = false) {
    try {
      const list = await window.debugAssistant.listPorts();
      const board = findBoardPort(list);
      const modbus = findModbusPort(list);

      setPorts(list);
      if (board) {
        setLogSelected(board.path);
        setStatus(`发现板子 ${board.path}${autoOpenLog ? "，正在打开USB日志" : ""}`);
        if (autoOpenLog && !logConnected) await openLog(board.path);
      } else {
        if (!logSelected && list[0]) setLogSelected(list[0].path);
        setStatus(`发现 ${list.length} 个串口，未识别到板子`);
      }

      if (modbus && !modbusConnected) {
        setModbusSelected((old) => old || modbus.path);
      }
    } catch (error) {
      setStatus(`串口枚举失败: ${error.message}`);
    }
  }

  async function openLog(path = logSelected) {
    if (!path) return;

    try {
      const result = await window.debugAssistant.openPort({ channel: "log", path, baudRate, dataBits, stopBits, parity });
      setLogConnected(true);
      setLogSelected(path);
      setLatestLogFile(result.latestLogFile || "");
      setStatus(`USB日志已打开: ${path}`);
    } catch (error) {
      setStatus(`USB日志打开失败: ${error.message}`);
    }
  }

  async function openModbus(path = modbusSelected) {
    if (!path) return;

    try {
      await window.debugAssistant.openPort({ channel: "modbus", path, baudRate, dataBits, stopBits, parity });
      setModbusConnected(true);
      setModbusSelected(path);
      setStatus(`485 Modbus已打开: ${path}`);
    } catch (error) {
      setStatus(`485 Modbus打开失败: ${error.message}`);
    }
  }

  async function autoOpenBoard() {
    await refresh(true);
  }

  async function refreshMqttStatus() {
    try {
      const snapshot = await window.debugAssistant.getMqttStatus();
      setMqttStatus(snapshot);
      setMqttPort(String(snapshot.port || 1883));
      setMqttHost((old) => old || snapshot.addresses?.[0] || "");
    } catch (error) {
      setStatus(`MQTT状态读取失败: ${error.message}`);
    }
  }

  async function startMqttBroker() {
    try {
      const snapshot = await window.debugAssistant.startMqtt({ port: numberFromInput(mqttPort) || 1883 });
      setMqttStatus(snapshot);
      setMqttPort(String(snapshot.port || 1883));
      setMqttHost((old) => old || snapshot.addresses?.[0] || "");
      setStatus(`MQTT Broker已启动: ${snapshot.port}`);
    } catch (error) {
      setStatus(`MQTT启动失败: ${error.message}`);
    }
  }

  async function stopMqttBroker() {
    try {
      const snapshot = await window.debugAssistant.stopMqtt();
      setMqttStatus(snapshot);
      setStatus("MQTT Broker已停止");
    } catch (error) {
      setStatus(`MQTT停止失败: ${error.message}`);
    }
  }

  async function publishMqttMessage() {
    if (!mqttTopic.trim()) return;

    try {
      await window.debugAssistant.publishMqtt({ topic: mqttTopic.trim(), message: mqttMessage });
      setStatus(`MQTT已发布: ${mqttTopic.trim()}`);
    } catch (error) {
      setStatus(`MQTT发布失败: ${error.message}`);
    }
  }

  function formatMqttMessage() {
    try {
      setMqttMessage(JSON.stringify(JSON.parse(mqttMessage), null, 2));
    } catch {
      setStatus("MQTT消息不是合法JSON，先按原文发送也可以");
    }
  }

  function clearMqttEvents() {
    setMqttEvents([]);
    setW800Devices({});
  }

  function applyW800Command(command) {
    setMqttTopic("leduo/pc/down");
    setMqttMessage(JSON.stringify(command, null, 2));
  }

  async function simulateW800Device() {
    try {
      const result = await window.debugAssistant.simulateW800({ port: numberFromInput(mqttPort) || 1883 });
      await refreshMqttStatus();
      setStatus(`已模拟W800上报: ${result.deviceId}`);
    } catch (error) {
      setStatus(`模拟W800失败: ${error.message}`);
    }
  }

  async function refreshOtaInfo(preferredPackageId = otaPackageId) {
    try {
      const packages = await window.debugAssistant.listOtaPackages();
      const nextPackageId = packages.some((item) => item.packageId === preferredPackageId)
        ? preferredPackageId
        : (packages[0]?.packageId || "default");
      const info = packages.find((item) => item.packageId === nextPackageId) || await window.debugAssistant.getOtaInfo();
      const maxVersion = packages.reduce((max, item) => Math.max(max, Number(item.manifest?.image_version || 0)), 0);

      setOtaPackages(packages);
      setOtaPackageId(nextPackageId);
      setOtaInfo(info);
      setOtaImageVersion((old) => {
        const current = numberFromInput(old) || 0;
        return current <= maxVersion ? String(maxVersion + 1) : old;
      });
      setStatus(info.exists ? `OTA package ready: ${nextPackageId}` : "OTA package not found");
    } catch (error) {
      setStatus(`OTA info failed: ${error.message}`);
    }
  }

  async function startOtaUpdate(packageIdOverride = otaPackageId) {
    if (otaBusy) return;
    if (!logConnected) {
      setStatus("OTA blocked: USB log serial is not open");
      return;
    }
    if (!otaInfo?.exists) {
      setStatus("OTA blocked: package is missing, refresh or convert a package first");
      return;
    }

    setOtaBusy(true);
    setOtaProgress({ stage: "start", sent: 0, total: otaInfo?.appSize || 0, percent: 0, detail: "starting" });

    try {
      await refreshOtaInfo(packageIdOverride);
      const result = await window.debugAssistant.startOta({
        packageId: packageIdOverride,
        chunkSize: numberFromInput(otaChunkSize) || 224,
        reset: otaResetAfter
      });
      setStatus(`OTA done: ${result.appSize} bytes`);
      if (otaResetAfter && boardAutoReconnect) {
        await waitForBoardReconnect(15000);
      }
    } catch (error) {
      setStatus(`OTA failed: ${error.message}`);
    } finally {
      setOtaBusy(false);
    }
  }

  async function stopOtaUpdate() {
    await window.debugAssistant.stopOta();
    setStatus("Stopping OTA...");
  }

  async function pickOtaInputFile() {
    try {
      const result = await window.debugAssistant.pickOtaInput();
      if (!result.canceled && result.filePath) {
        setOtaInputFile(result.filePath);
        if (!otaConvertName.trim()) {
          const name = result.filePath.split(/[\\/]/).pop()?.replace(/\.[^.]+$/, "") || "";
          setOtaConvertName(name);
        }
      }
    } catch (error) {
      setStatus(`Select OTA input failed: ${error.message}`);
    }
  }

  async function convertOtaPackage() {
    if (otaConvertBusy) return;

    setOtaConvertBusy(true);
    setOtaConvertOutput("");
    setStatus("Converting OTA package...");

    try {
      const result = await window.debugAssistant.convertOtaPackage({
        inputFile: otaInputFile,
        imageVersion: numberFromInput(otaImageVersion) || 1,
        packageName: otaConvertName.trim(),
        appBase: otaAppBase.trim() || "0x08020000"
      });
      setOtaConvertOutput(result.output || "done");
      setOtaPackages(result.packages || []);
      const nextPackageId = result.packageId || "default";
      setOtaPackageId(nextPackageId);
      setOtaInfo(result.package || (result.packages || []).find((item) => item.packageId === nextPackageId) || null);
      await refreshOtaInfo(nextPackageId);
      setStatus(`OTA package converted: ${nextPackageId}`);
      if (otaSendAfterConvert) {
        if (!logConnected) {
          setStatus(`OTA package converted: ${nextPackageId}, USB log serial is not open`);
        } else {
          await startOtaUpdate(nextPackageId);
        }
      }
    } catch (error) {
      setOtaConvertOutput(error.message);
      setStatus(`OTA convert failed: ${error.message}`);
    } finally {
      setOtaConvertBusy(false);
    }
  }

  function selectOtaPackage(packageId) {
    setOtaPackageId(packageId);
    const info = otaPackages.find((item) => item.packageId === packageId);
    if (info) setOtaInfo(info);
  }

  function resetStressStats() {
    stressPendingRef.current.clear();
    stressAckBufferRef.current = [];
    stressNextAckRef.current = 0;
    stressStatsRef.current = {
      tx: 0,
      ack: 0,
      timeout: 0,
      crcError: 0,
      statusError: 0,
      missing: 0,
      rttSum: 0,
      rttMax: 0
    };
    setStressStats({ ...stressStatsRef.current });
  }

  function sleep(ms) {
    return new Promise((resolve) => window.setTimeout(resolve, ms));
  }

  function enqueueLog(item) {
    logQueueRef.current.push(item);

    if (logFlushTimerRef.current) return;

    logFlushTimerRef.current = window.setTimeout(() => {
      const pending = logQueueRef.current.splice(0);
      logFlushTimerRef.current = 0;
      if (pending.length) {
        setLogs((old) => [...old, ...pending].slice(-1500));
      }
    }, 80);
  }

  function flushStressStats(force = false) {
    const now = performance.now();
    if (!force && now - stressStatsFlushRef.current < 100) return;
    stressStatsFlushRef.current = now;
    setStressStats({ ...stressStatsRef.current });
  }

  function pruneStressTimeouts(timeoutMs = 1000) {
    const now = performance.now();
    let timeout = 0;

    for (const [seq, item] of stressPendingRef.current.entries()) {
      if (now - item.at >= timeoutMs) {
        stressPendingRef.current.delete(seq);
        timeout += 1;
      }
    }

    if (timeout) {
      stressStatsRef.current.timeout += timeout;
      flushStressStats();
    }
  }

  async function writeStressFrame(frame, options = {}) {
    const chunkSize = Math.max(0, numberFromInput(options.chunkSize ?? stressChunkSize) || 0);
    const chunkGap = Math.max(0, numberFromInput(options.chunkGap ?? stressChunkGap) || 0);

    if (chunkSize <= 0 || chunkSize >= frame.length) {
      await window.debugAssistant.writePort({ channel: "log", data: bytesToHex(frame), hex: true, silent: true });
      return;
    }

    for (let offset = 0; offset < frame.length; offset += chunkSize) {
      if (stressStopRef.current) return;

      const chunk = frame.slice(offset, offset + chunkSize);
      await window.debugAssistant.writePort({ channel: "log", data: bytesToHex(chunk), hex: true, silent: true });
      if (chunkGap > 0) await sleep(chunkGap);
    }
  }

  function handleStressAckBytes(bytes) {
    if (bytes.length === 0) return;

    const parsed = parseLdcAckFrames([...stressAckBufferRef.current, ...bytes]);
    stressAckBufferRef.current = parsed.rest.slice(-512);

    if (parsed.frames.length === 0) return;

    const now = performance.now();

    {
      const next = { ...stressStatsRef.current };

      for (const frame of parsed.frames) {
        if (frame.crcError) {
          next.crcError += 1;
          continue;
        }

        const pending = stressPendingRef.current.get(frame.seq);
        if (pending) {
          const rtt = now - pending.at;
          next.rttSum += rtt;
          next.rttMax = Math.max(next.rttMax, rtt);
          stressPendingRef.current.delete(frame.seq);
        }

        if (!frame.ok) {
          next.statusError += 1;
        } else {
          next.ack += 1;
        }

        if (frame.seq !== stressNextAckRef.current) {
          next.missing += frame.seq > stressNextAckRef.current ? frame.seq - stressNextAckRef.current : 0;
          stressNextAckRef.current = frame.seq + 1;
        } else {
          stressNextAckRef.current += 1;
        }
      }

      stressStatsRef.current = next;
      flushStressStats();
    }
  }

  async function startStress(options = {}) {
    if (!logConnected || stressRunning) return;

    const payloadLength = Math.max(0, Math.min(4096, numberFromInput(options.payloadLength ?? stressPayloadLength) || 0));
    const frameCount = Math.max(1, numberFromInput(options.frameCount ?? stressFrameCount) || 1);
    const interval = Math.max(0, numberFromInput(options.interval ?? stressInterval) || 0);
    const payloadMode = options.payloadMode ?? stressPayloadMode;

    resetStressStats();
    stressStopRef.current = false;
    setStressRunning(true);
    setStatus("LDC压测运行中");

    try {
      for (let seq = 0; seq < frameCount && !stressStopRef.current; seq += 1) {
        const frame = buildLdcStressFrame(seq, payloadLength, payloadMode);
        stressPendingRef.current.set(seq, { at: performance.now() });
        await writeStressFrame(frame, options);
        stressStatsRef.current.tx += 1;
        flushStressStats();
        pruneStressTimeouts();

        if (interval > 0) await sleep(interval);
      }

      await sleep(300);
      pruneStressTimeouts(300);
      flushStressStats(true);
      setStatus(stressStopRef.current ? "LDC压测已停止" : "LDC压测完成");
    } catch (error) {
      setStatus(`LDC压测失败: ${error.message}`);
    } finally {
      setStressRunning(false);
      stressStopRef.current = false;
      flushStressStats(true);
    }
  }

  function stopStress() {
    stressStopRef.current = true;
    setStatus("正在停止LDC压测");
  }

  function resetModbusStressStats() {
    const stats = createModbusStressStats();
    modbusStressStatsRef.current = stats;
    setModbusStressStats(stats);
  }

  function updateModbusStressStats(mutator, publish = true) {
    const next = { ...modbusStressStatsRef.current };
    mutator(next);
    modbusStressStatsRef.current = next;
    if (publish) setModbusStressStats(next);
  }

  function waitForModbusResponse(request, timeoutMs) {
    return new Promise((resolve) => {
      let timeoutId = 0;
      const waiter = {
        resolve: (parsed) => {
          window.clearTimeout(timeoutId);
          if (modbusWaiterRef.current === waiter) modbusWaiterRef.current = null;
          resolve(parsed);
        }
      };

      modbusWaiterRef.current = waiter;
      timeoutId = window.setTimeout(() => {
        if (modbusWaiterRef.current === waiter) modbusWaiterRef.current = null;
        if (modbusRequestRef.current === request) modbusRequestRef.current = null;
        resolve(null);
      }, timeoutMs);
    });
  }

  async function runOneModbusStressRequest(frame, request, timeoutMs) {
    modbusRequestRef.current = request;
    modbusRxBufferRef.current = [];
    const responsePromise = waitForModbusResponse(request, timeoutMs);

    await window.debugAssistant.writePort({ channel: "modbus", data: frame, hex: true, silent: true });
    return responsePromise;
  }

  async function startModbusStress(options = {}) {
    if (!modbusConnected || modbusStressRunning) return;

    const unit = numberFromInput(options.unit ?? modbusUnit) & 0xff;
    const start = numberFromInput(options.address ?? modbusAddress);
    const count = Math.max(1, numberFromInput(options.count ?? modbusCount) || 1);
    const requestTotal = Math.max(1, numberFromInput(options.requestTotal ?? modbusStressCount) || 1);
    const interval = Math.max(0, numberFromInput(options.interval ?? modbusStressInterval) || 0);
    const timeoutMs = Math.max(10, numberFromInput(options.timeoutMs ?? modbusStressTimeout) || 120);
    const frame = buildReadHoldingFrame(unit, start, count);

    resetModbusStressStats();
    modbusStressStopRef.current = false;
    setModbusStressRunning(true);
    setStatus("Modbus高压测试运行中");

    try {
      for (let seq = 0; seq < requestTotal && !modbusStressStopRef.current; seq += 1) {
        const request = { unit, start, count, at: performance.now() };

        updateModbusStressStats((stats) => {
          stats.tx += 1;
        }, seq % 25 === 0);

        let parsed = null;

        try {
          parsed = await runOneModbusStressRequest(frame, request, timeoutMs);
        } catch (error) {
          updateModbusStressStats((stats) => {
            stats.writeError += 1;
            stats.lastError = error.message;
          }, true);
          break;
        }

        const verdict = validateModbusStressResponse(parsed, request);
        updateModbusStressStats((stats) => {
          if (verdict.ok) {
            stats.ok += 1;
            stats.rttSum += parsed.elapsedMs;
            stats.rttMax = Math.max(stats.rttMax, parsed.elapsedMs);
          } else {
            stats[verdict.bucket] += 1;
            stats.lastError = verdict.message;
          }
        }, seq % 25 === 0 || !verdict.ok);

        if (interval > 0) await sleep(interval);
      }

      setModbusStressStats({ ...modbusStressStatsRef.current });
      setStatus(modbusStressStopRef.current ? "Modbus高压测试已停止" : "Modbus高压测试完成");
    } finally {
      modbusWaiterRef.current = null;
      modbusRequestRef.current = null;
      setModbusStressRunning(false);
      modbusStressStopRef.current = false;
    }
  }

  async function startCombinedStress() {
    if (!logConnected || !modbusConnected || stressRunning || modbusStressRunning || combinedRunning) return;

    setCombinedRunning(true);
    setStatus("USB LDC + 485 Modbus 同时高压运行中");

    try {
      await Promise.all([
        startStress({
          payloadLength: 240,
          frameCount: 10000,
          interval: 0,
          chunkSize: 0,
          chunkGap: 0,
          payloadMode: "counter"
        }),
        startModbusStress({
          unit: 1,
          address: "0x0000",
          count: 3,
          requestTotal: 10000,
          interval: 1,
          timeoutMs: 120
        })
      ]);
      setStatus("USB LDC + 485 Modbus 同时高压完成");
    } finally {
      setCombinedRunning(false);
    }
  }

  function stopAllStress() {
    stressStopRef.current = true;
    modbusStressStopRef.current = true;
    setStatus("正在停止全部压力测试");
  }

  async function waitForBoardReconnect(timeoutMs = 15000) {
    const deadline = Date.now() + timeoutMs;

    setStatus("等待板子CDC重新枚举");

    while (Date.now() < deadline) {
      const list = await window.debugAssistant.listPorts();
      const board = findBoardPort(list);
      setPorts(list);

      if (board) {
        await openLog(board.path);
        setStatus(`板子已重连: ${board.path}`);
        return true;
      }

      await sleep(700);
    }

    setStatus("等待板子重连超时");
    return false;
  }

  async function runBoardWorkflow(build = boardBuild) {
    if (boardBusy) return;

    stopAllStress();
    setBoardBusy(true);
    setBoardOutput([]);
    setLogConnected(false);
    setStatus(build ? "正在编译并烧录" : "正在烧录并重启");

    try {
      const result = await window.debugAssistant.runBoardFlash({ build, noVerify: boardNoVerify });

      if (!result.ok) {
        setStatus(`板子流程失败，退出码 ${result.code}`);
        if (boardAutoReconnect) {
          await waitForBoardReconnect(8000);
        }
        return;
      }

      setStatus("烧录完成，已请求软件复位");

      if (boardAutoReconnect) {
        await waitForBoardReconnect();
      }
    } catch (error) {
      setStatus(`板子流程失败: ${error.message}`);
    } finally {
      setBoardBusy(false);
    }
  }

  async function stopBoardWorkflow() {
    await window.debugAssistant.stopBoardFlash();
    setBoardBusy(false);
    setStatus("已停止板子流程");
  }

  async function closeLog() {
    try {
      stopAllStress();
      await window.debugAssistant.closePort({ channel: "log" });
      setLogConnected(false);
      setStatus("USB日志已关闭");
    } catch (error) {
      setStatus(`USB日志关闭失败: ${error.message}`);
    }
  }

  async function closeModbus() {
    try {
      modbusStressStopRef.current = true;
      await window.debugAssistant.closePort({ channel: "modbus" });
      setModbusConnected(false);
      setStatus("485 Modbus已关闭");
    } catch (error) {
      setStatus(`485 Modbus关闭失败: ${error.message}`);
    }
  }

  function addTxLog(data, hex, channel = "log") {
    const cleanHex = hex ? hexBytes(data) : Array.from(new TextEncoder().encode(data))
      .map((byte) => byte.toString(16).padStart(2, "0"))
      .join(" ")
      .toUpperCase();

    enqueueLog({
      at: new Date().toISOString(),
      dir: "SEND",
      channel,
      text: hex ? cleanHex : data,
      hex: cleanHex
    });
  }

  async function send(data = sendText, hex = sendHex, channel = "log") {
    if (!data.trim()) return;

    try {
      await window.debugAssistant.writePort({ channel, data, hex, appendNewline });
      addTxLog(data, hex, channel);
      if (data === sendText) setSendText("");
      setStatus(`${channelName(channel)}已发送 ${hex ? hexBytes(data).split(" ").filter(Boolean).length : data.length} 字节`);
    } catch (error) {
      setStatus(`发送失败: ${error.message}`);
    }
  }

  async function sendModbusRequest(data = generatedFrame) {
    const unit = numberFromInput(modbusUnit) & 0xff;
    const start = numberFromInput(modbusAddress);
    const count = numberFromInput(modbusCount);

    modbusRequestRef.current = {
      unit,
      start,
      count,
      at: performance.now()
    };
    modbusRxBufferRef.current = [];
    setModbusResult(null);
    await send(data, true, "modbus");
  }

  function handleModbusRxBytes(bytes) {
    const request = modbusRequestRef.current;
    if (!request || bytes.length === 0) return;

    const buffer = [...modbusRxBufferRef.current, ...bytes].slice(-512);
    const candidates = [];

    for (let offset = 0; offset < buffer.length; offset += 1) {
      if (buffer[offset] !== request.unit) continue;

      const functionCode = buffer[offset + 1];

      if (functionCode === 0x03 && buffer.length >= offset + 5) {
        const byteCount = buffer[offset + 2];
        const total = 3 + byteCount + 2;
        if (buffer.length >= offset + total) candidates.push(buffer.slice(offset, offset + total));
      } else if (functionCode === 0x83 && buffer.length >= offset + 5) {
        candidates.push(buffer.slice(offset, offset + 5));
      }
    }

    for (const candidate of candidates) {
      const parsed = parseModbusReadHoldingResponse(candidate, request);
      if (parsed) {
        setModbusResult(parsed);
        modbusWaiterRef.current?.resolve(parsed);
        modbusRequestRef.current = null;
        modbusRxBufferRef.current = [];
        return;
      }
    }

    modbusRxBufferRef.current = buffer;
  }

  useEffect(() => {
    refresh(true);
    window.debugAssistant.getLogPaths().then((paths) => {
      setLatestLogFile(paths.latestLogFile || "");
      setEventsFile(paths.eventsFile || "");
    });
    refreshMqttStatus();
    refreshOtaInfo();
    window.debugAssistant.onData((data) => {
      enqueueLog({ ...data, dir: "RECV" });
      const bytes = parseHexBytes(data.hex || "");
      if ((data.channel || "log") === "log") handleStressAckBytes(bytes);
      if (data.channel === "modbus") handleModbusRxBytes(bytes);
    });
    window.debugAssistant.onError((payload) => {
      const source = payload?.channel ? `${channelName(payload.channel)} ` : "";
      const message = payload?.message || String(payload);
      setStatus(`${source}错误: ${message}`);
    });
    window.debugAssistant.onClosed((payload) => {
      if (payload?.channel === "modbus") {
        setModbusConnected(false);
      } else {
        setLogConnected(false);
      }
      setStatus(`${channelName(payload?.channel)}已断开 ${payload?.path || ""}`);
    });
    window.debugAssistant.onMqttEvent((payload) => {
      setMqttEvents((old) => [...old.slice(-300), payload]);
      const device = parseW800DeviceMessage(payload);
      if (device) {
        setW800Devices((old) => ({
          ...old,
          [device.deviceId]: {
            ...(old[device.deviceId] || {}),
            ...device
          }
        }));
      }
      if (payload.event === "start" || payload.event === "stop") refreshMqttStatus();
    });
    window.debugAssistant.onOtaProgress((payload) => {
      setOtaProgress(payload);
    });
    window.debugAssistant.onBoardFlashOutput((payload) => {
      setBoardOutput((old) => [...old.slice(-400), { at: new Date().toISOString(), ...payload }]);
    });
    window.debugAssistant.onBoardFlashDone((payload) => {
      setBoardOutput((old) => [...old.slice(-400), { at: new Date().toISOString(), stream: "info", text: `流程结束，退出码 ${payload.code}\n` }]);
    });
  }, []);

  useEffect(() => () => {
    if (logFlushTimerRef.current) window.clearTimeout(logFlushTimerRef.current);
  }, []);

  useEffect(() => {
    if (!autoConnect || logConnected) return undefined;

    const timer = window.setInterval(() => {
      refresh(true);
    }, 2500);

    return () => window.clearInterval(timer);
  }, [autoConnect, logConnected, logSelected, baudRate, dataBits, stopBits, parity]);

  useEffect(() => {
    if (autoScroll && !logPaused && logRef.current) logRef.current.scrollTop = logRef.current.scrollHeight;
  }, [visibleLogs, autoScroll, logPaused]);

  useEffect(() => {
    if (boardOutputRef.current) boardOutputRef.current.scrollTop = boardOutputRef.current.scrollHeight;
  }, [boardOutput]);

  useEffect(() => {
    if (mqttEventsRef.current) mqttEventsRef.current.scrollTop = mqttEventsRef.current.scrollHeight;
  }, [mqttEvents]);

  const avgRtt = stressStats.ack ? `${(stressStats.rttSum / stressStats.ack).toFixed(1)} ms` : "0 ms";
  const modbusAvgRtt = modbusStressStats.ok ? `${(modbusStressStats.rttSum / modbusStressStats.ok).toFixed(1)} ms` : "0 ms";
  const mqttStats = createMqttStats(mqttEvents);
  const w800DeviceList = Object.values(w800Devices);
  const selectedMqttHost = mqttHost || mqttStatus.addresses?.[0] || "PC_IP";
  const otaStartBlockedReason = !logConnected
    ? "先打开 USB CDC 日志串口"
    : (!otaInfo?.exists ? "当前 OTA 包不存在，先刷新或转换" : "");
  const shortPath = (value) => value ? value.split(/[\\/]/).pop() : "";
  const w800Config = buildW800Config({
    host: selectedMqttHost,
    port: mqttStatus.port || mqttPort || 1883
  });

  return (
    <main className="app-shell">
      <header className="titlebar">
        <div>
          <strong>LeduO Debug Assistant</strong>
          <span>双通道调试台</span>
        </div>
        <div className="status-pills">
          <span className={logConnected ? "pill on" : "pill"}>USB {logConnected ? "ON" : "OFF"}</span>
          <span className={modbusConnected ? "pill on" : "pill"}>485 {modbusConnected ? "ON" : "OFF"}</span>
          <span className={mqttStatus.running ? "pill on" : "pill"}>MQTT {mqttStatus.running ? "ON" : "OFF"}</span>
        </div>
      </header>

      <aside className="left-panel">
        <section className="group channel-card usb-card">
          <h2>USB日志</h2>
          <label>端口
            <select value={logSelected} onChange={(event) => setLogSelected(event.target.value)}>
              {ports.length === 0 && <option value="">无串口</option>}
              {ports.map((port) => (
                <option key={port.path} value={port.path}>
                  {scoreBoardPort(port) > 0 ? "板子 " : ""}{port.path} {port.manufacturer || ""}
                </option>
              ))}
            </select>
          </label>
          <div className="button-row">
            <button onClick={() => refresh(false)}>刷新</button>
            <button onClick={autoOpenBoard} disabled={logConnected}>识别板子</button>
            <button className="primary wide" onClick={logConnected ? closeLog : () => openLog()} disabled={!logSelected}>
              {logConnected ? "关闭USB" : "打开USB"}
            </button>
          </div>
          <label className="check"><input type="checkbox" checked={autoConnect} onChange={(event) => setAutoConnect(event.target.checked)} /> 自动连接板子</label>
        </section>

        <section className="group channel-card modbus-card">
          <h2>485 Modbus</h2>
          <label>端口
            <select value={modbusSelected} onChange={(event) => setModbusSelected(event.target.value)}>
              {ports.length === 0 && <option value="">无串口</option>}
              {ports.map((port) => (
                <option key={port.path} value={port.path}>
                  {scoreModbusPort(port) > 0 ? "485 " : ""}{port.path} {port.manufacturer || ""}
                </option>
              ))}
            </select>
          </label>
          <div className="button-row">
            <button onClick={() => refresh(false)}>刷新</button>
            <button className="primary" onClick={modbusConnected ? closeModbus : () => openModbus()} disabled={!modbusSelected}>
              {modbusConnected ? "关闭485" : "打开485"}
            </button>
          </div>
          <p className="mini-hint">Modbus请求只从这里发送，不会占用USB日志口。</p>
        </section>

        <section className="group settings-card">
          <h2>串口参数</h2>
          <label>波特率
            <select value={baudRate} onChange={(event) => setBaudRate(event.target.value)}>
              {BAUD_RATES.map((rate) => <option key={rate} value={rate}>{rate}</option>)}
            </select>
          </label>
          <label>数据位
            <select value={dataBits} onChange={(event) => setDataBits(event.target.value)}>
              {DATA_BITS.map((bits) => <option key={bits} value={bits}>{bits}</option>)}
            </select>
          </label>
          <label>停止位
            <select value={stopBits} onChange={(event) => setStopBits(event.target.value)}>
              {STOP_BITS.map((bits) => <option key={bits} value={bits}>{bits}</option>)}
            </select>
          </label>
          <label>校验位
            <select value={parity} onChange={(event) => setParity(event.target.value)}>
              {PARITIES.map(([value, label]) => <option key={value} value={value}>{label}</option>)}
            </select>
          </label>
        </section>

        <section className="group receive-card">
          <h2>接收显示</h2>
          <div className="segmented">
            <button className={!rxHex ? "active" : ""} onClick={() => setRxHex(false)}>ASCII</button>
            <button className={rxHex ? "active" : ""} onClick={() => setRxHex(true)}>HEX</button>
          </div>
          <label className="check"><input type="checkbox" checked={lineMode} onChange={(event) => setLineMode(event.target.checked)} /> 日志模式</label>
          <label className="check"><input type="checkbox" checked={showTimestamp} onChange={(event) => setShowTimestamp(event.target.checked)} /> 时间戳</label>
          <label className="check"><input type="checkbox" checked={autoScroll} onChange={(event) => setAutoScroll(event.target.checked)} /> 自动滚屏</label>
          <button onClick={() => setLogs([])}>清空日志</button>
        </section>
      </aside>

      <section className="center-panel">
        <div className="log-header">
          <span>数据日志</span>
          <div>
            <label><input type="checkbox" checked={showRx} onChange={(event) => setShowRx(event.target.checked)} /> RX</label>
            <label><input type="checkbox" checked={showTx} onChange={(event) => setShowTx(event.target.checked)} /> TX</label>
            <label><input type="checkbox" checked={logPaused} onChange={(event) => setLogPaused(event.target.checked)} /> 暂停</label>
          </div>
        </div>
        <div className="log-toolbar">
          <input value={logFilter} onChange={(event) => setLogFilter(event.target.value)} placeholder="过滤关键字 / HEX / 通道" />
          <button onClick={() => navigator.clipboard?.writeText(visibleLogs.map((item) => `${item.at} ${channelName(item.channel)} ${item.dir} ${rxHex ? item.hex : item.text || item.hex}`).join("\n"))}>复制</button>
        </div>
        <div className="log-view" ref={logRef}>
          {visibleLogs.map((item, index) => {
            const stamp = showTimestamp ? `[${item.at.replace("T", " ").slice(0, 23)}] ` : "";
            const body = rxHex ? item.hex : item.text || item.hex;
            const channel = channelName(item.channel);
            return (
              <div key={`${item.at}-${index}`} className={`log-line ${item.dir === "SEND" ? "tx" : "rx"}`}>
                {lineMode && <span className="log-meta">{stamp}{channel} {item.dir} {rxHex ? "HEX" : "ASCII"} /{body.length} &gt;&gt;&gt;</span>}
                <span>{body}</span>
              </div>
            );
          })}
        </div>
      </section>

      <aside className="right-panel">
        <div className="tool-tabs">
          <button className={rightTool === "modbus" ? "active" : ""} onClick={() => setRightTool("modbus")}>Modbus指令</button>
          <button className={rightTool === "stress" ? "active" : ""} onClick={() => setRightTool("stress")}>LDC压测</button>
          <button className={rightTool === "w800" ? "active" : ""} onClick={() => setRightTool("w800")}>W800联网</button>
          <button className={rightTool === "ota" ? "active" : ""} onClick={() => setRightTool("ota")}>OTA</button>
          <button className={rightTool === "board" ? "active" : ""} onClick={() => setRightTool("board")}>板子流程</button>
        </div>

        {rightTool === "modbus" && (
          <>
            <section className="group modbus">
              <h2>Modbus RTU</h2>
              <label>从站号
                <input value={modbusUnit} onChange={(event) => setModbusUnit(event.target.value)} />
              </label>
              <label>功能码
                <select value={modbusFunction} onChange={(event) => setModbusFunction(event.target.value)}>
                  <option value="03">03H 读保持寄存器</option>
                </select>
              </label>
              <label>寄存器
                <input value={modbusAddress} onChange={(event) => setModbusAddress(event.target.value)} />
              </label>
              <label>数量
                <input value={modbusCount} onChange={(event) => setModbusCount(event.target.value)} />
              </label>
              <p className="hint">地址和数量支持十进制或 0x 开头的十六进制。</p>
            </section>

            <section className="group generated">
              <h2>生成报文</h2>
              <textarea readOnly value={generatedFrame} />
              <div className="right-actions">
                <button onClick={() => setSendText(generatedFrame)}>放到底部</button>
                <button onClick={() => navigator.clipboard?.writeText(generatedFrame)}>复制</button>
                <button onClick={() => setSendText("")}>清除</button>
                <button className="primary" onClick={() => sendModbusRequest(generatedFrame)} disabled={!modbusConnected}>发送485</button>
              </div>
            </section>

            <section className="group modbus-result">
              <h2>响应解析</h2>
              {!modbusResult && <p className="empty-result">等待 03H 响应...</p>}
              {modbusResult && (
                <>
                  <div className={`result-summary ${modbusResult.ok ? "ok" : "bad"}`}>
                    <span>{modbusResult.ok ? "CRC OK" : "解析异常"}</span>
                    <span>{modbusResult.elapsedMs} ms</span>
                    <span>{modbusResult.message}</span>
                  </div>
                  {modbusResult.registers.length > 0 && (
                    <table className="register-table">
                      <thead>
                        <tr>
                          <th>地址</th>
                          <th>HEX</th>
                          <th>DEC</th>
                        </tr>
                      </thead>
                      <tbody>
                        {modbusResult.registers.map((reg) => (
                          <tr key={reg.address}>
                            <td>{reg.address}</td>
                            <td>{reg.hex}</td>
                            <td>{reg.value}</td>
                          </tr>
                        ))}
                      </tbody>
                    </table>
                  )}
                </>
              )}
            </section>

            <section className="group modbus-stress">
              <h2>Modbus高压</h2>
              <label>次数
                <input value={modbusStressCount} onChange={(event) => setModbusStressCount(event.target.value)} />
              </label>
              <label>间隔ms
                <input value={modbusStressInterval} onChange={(event) => setModbusStressInterval(event.target.value)} />
              </label>
              <label>超时ms
                <input value={modbusStressTimeout} onChange={(event) => setModbusStressTimeout(event.target.value)} />
              </label>
              <div className="meter-grid compact">
                <span>TX</span><strong>{modbusStressStats.tx}</strong>
                <span>OK</span><strong>{modbusStressStats.ok}</strong>
                <span>Timeout</span><strong>{modbusStressStats.timeout}</strong>
                <span>CRC</span><strong>{modbusStressStats.crcError}</strong>
                <span>Exception</span><strong>{modbusStressStats.exception}</strong>
                <span>Mismatch</span><strong>{modbusStressStats.mismatch}</strong>
                <span>Avg RTT</span><strong>{modbusAvgRtt}</strong>
                <span>Max RTT</span><strong>{modbusStressStats.rttMax.toFixed(1)} ms</strong>
              </div>
              {modbusStressStats.lastError && <p className="error-hint">{modbusStressStats.lastError}</p>}
              <div className="right-actions">
                <button onClick={resetModbusStressStats} disabled={modbusStressRunning}>清零</button>
                <button className="primary" onClick={modbusStressRunning ? stopAllStress : () => startModbusStress()} disabled={!modbusConnected}>
                  {modbusStressRunning ? "停止" : "开始高压"}
                </button>
              </div>
            </section>
          </>
        )}

        {rightTool === "stress" && (
          <>
            <section className="group stress">
              <h2>LDC压测</h2>
              <label>Payload
                <input value={stressPayloadLength} onChange={(event) => setStressPayloadLength(event.target.value)} />
              </label>
              <label>帧数量
                <input value={stressFrameCount} onChange={(event) => setStressFrameCount(event.target.value)} />
              </label>
              <label>帧间隔
                <input value={stressInterval} onChange={(event) => setStressInterval(event.target.value)} />
              </label>
              <label>分包大小
                <input value={stressChunkSize} onChange={(event) => setStressChunkSize(event.target.value)} />
              </label>
              <label>分包间隔
                <input value={stressChunkGap} onChange={(event) => setStressChunkGap(event.target.value)} />
              </label>
              <label>数据模式
                <select value={stressPayloadMode} onChange={(event) => setStressPayloadMode(event.target.value)}>
                  <option value="counter">递增</option>
                  <option value="random">伪随机</option>
                  <option value="fixed">固定55</option>
                </select>
              </label>
              <p className="hint">压测走USB日志通道，发送时默认静默写入，避免日志刷屏拖慢吞吐。</p>
            </section>

            <section className="group stress-meter">
              <h2>压测统计</h2>
              <div className="meter-grid">
                <span>TX</span><strong>{stressStats.tx}</strong>
                <span>ACK</span><strong>{stressStats.ack}</strong>
                <span>Timeout</span><strong>{stressStats.timeout}</strong>
                <span>CRC</span><strong>{stressStats.crcError}</strong>
                <span>Status</span><strong>{stressStats.statusError}</strong>
                <span>Missing</span><strong>{stressStats.missing}</strong>
                <span>Avg RTT</span><strong>{avgRtt}</strong>
                <span>Max RTT</span><strong>{stressStats.rttMax.toFixed(1)} ms</strong>
              </div>
              <div className="right-actions">
                <button onClick={resetStressStats} disabled={stressRunning}>清零</button>
                <button onClick={combinedRunning ? stopAllStress : startCombinedStress} disabled={!logConnected || !modbusConnected}>
                  {combinedRunning ? "停止同时压" : "USB+485同时压"}
                </button>
                <button className="primary" onClick={stressRunning ? stopStress : startStress} disabled={!logConnected}>
                  {stressRunning ? "停止" : "开始"}
                </button>
              </div>
            </section>
          </>
        )}

        {rightTool === "w800" && (
          <>
            <section className="group w800-panel">
              <h2>W800 MQTT Broker</h2>
              <label>端口
                <input value={mqttPort} onChange={(event) => setMqttPort(event.target.value)} disabled={mqttStatus.running} />
              </label>
              <label>Broker IP
                <input value={mqttHost} onChange={(event) => setMqttHost(event.target.value.trim())} placeholder={mqttStatus.addresses?.[0] || "192.168.x.x"} />
              </label>
              <div className="meter-grid compact mqtt-stats">
                <span>Client</span><strong>{mqttStats.clients}</strong>
                <span>Offline</span><strong>{mqttStats.disconnects}</strong>
                <span>Sub</span><strong>{mqttStats.subscribes}</strong>
                <span>RX</span><strong>{mqttStats.rx}</strong>
                <span>TX</span><strong>{mqttStats.tx}</strong>
                <span>Port</span><strong>{mqttStatus.port || mqttPort}</strong>
              </div>
              <div className="mqtt-addresses">
                {(mqttStatus.addresses || []).length === 0 && <span>未检测到可用局域网IP</span>}
                {(mqttStatus.addresses || []).map((address) => (
                  <button key={address} onClick={() => setMqttHost(address)}>
                    使用 {address}
                  </button>
                ))}
              </div>
              <div className="right-actions">
                <button onClick={refreshMqttStatus}>刷新</button>
                <button onClick={simulateW800Device}>模拟W800</button>
                <button className="primary" onClick={mqttStatus.running ? stopMqttBroker : startMqttBroker}>
                  {mqttStatus.running ? "停止Broker" : "启动Broker"}
                </button>
              </div>
              <p className="hint">W800 先作为 MQTT client 连接电脑 IP 和端口，topic 可先约定为 leduo/w800/up 与 leduo/pc/down。</p>
            </section>

            <section className="group w800-config-panel">
              <div className="section-title-row">
                <h2>固件连接配置</h2>
                <button onClick={() => navigator.clipboard?.writeText(w800Config)}>复制</button>
              </div>
              <pre>{w800Config}</pre>
            </section>

            <section className="group mqtt-publish">
              <h2>电脑下发</h2>
              <div className="topic-presets">
                {W800_TOPIC_PRESETS.map(([topic, label]) => (
                  <button key={topic} onClick={() => setMqttTopic(topic)} className={mqttTopic === topic ? "active" : ""}>
                    {label}
                  </button>
                ))}
              </div>
              <div className="command-presets">
                {W800_COMMAND_PRESETS.map(([label, command]) => (
                  <button key={label} onClick={() => applyW800Command(command)}>
                    {label}
                  </button>
                ))}
              </div>
              <label>Topic
                <input value={mqttTopic} onChange={(event) => setMqttTopic(event.target.value)} />
              </label>
              <textarea value={mqttMessage} onChange={(event) => setMqttMessage(event.target.value)} />
              <div className="right-actions">
                <button onClick={() => setMqttMessage("{\"cmd\":\"ping\"}")}>Ping</button>
                <button onClick={formatMqttMessage}>格式化</button>
                <button className="primary" onClick={publishMqttMessage} disabled={!mqttStatus.running || !mqttTopic.trim()}>发布</button>
              </div>
            </section>

            <section className="group w800-devices-panel">
              <h2>设备状态</h2>
              {w800DeviceList.length === 0 && <p className="empty-result">等待 W800 上报 JSON 状态...</p>}
              {w800DeviceList.map((device) => (
                <div className="w800-device-card" key={device.deviceId}>
                  <div className="device-title">
                    <strong>{device.deviceId}</strong>
                    <span className={device.online ? "device-online" : "device-offline"}>{device.online ? "online" : "offline"}</span>
                  </div>
                  <div className="device-grid">
                    <span>IP</span><code>{device.ip || "-"}</code>
                    <span>RSSI</span><code>{device.rssi === "" ? "-" : device.rssi}</code>
                    <span>FW</span><code>{device.fw || "-"}</code>
                    <span>Uptime</span><code>{device.uptime === "" ? "-" : device.uptime}</code>
                    <span>Mode</span><code>{device.mode || "-"}</code>
                    <span>Seen</span><code>{device.lastSeen?.replace("T", " ").slice(0, 19) || "-"}</code>
                  </div>
                  <em>{device.lastTopic}</em>
                  <p>{device.lastPayload}</p>
                </div>
              ))}
            </section>

            <section className="group mqtt-events-panel">
              <div className="section-title-row">
                <h2>联网日志</h2>
                <button onClick={clearMqttEvents}>清空</button>
              </div>
              <div className="mqtt-events" ref={mqttEventsRef}>
                {mqttEvents.length === 0 && <div className="empty-result">等待 W800 连接或消息...</div>}
                {mqttEvents.map((event, index) => (
                  <div key={`${event.at}-${index}`} className={`mqtt-event ${event.event}`}>
                    <span>{event.at?.replace("T", " ").slice(0, 19)}</span>
                    <strong>{event.event}</strong>
                    <code>{event.clientId || ""}</code>
                    {event.topic && <em>{event.topic}</em>}
                    {event.payload && <p>{event.payload}</p>}
                    {event.addresses && <p>{event.addresses.join(", ")}</p>}
                  </div>
                ))}
              </div>
            </section>
          </>
        )}

        {rightTool === "ota" && (
          <>
            <section className="group ota-convert-panel">
              <h2>OTA包转换</h2>
              <label>编译产物
                <div className="file-picker-row">
                  <input value={otaInputFile} onChange={(event) => setOtaInputFile(event.target.value)} placeholder=".axf / .elf / .hex / .bin" />
                  <button onClick={pickOtaInputFile} disabled={otaConvertBusy}>选择</button>
                </div>
              </label>
              <div className="ota-convert-grid">
                <label>版本号
                  <input value={otaImageVersion} onChange={(event) => setOtaImageVersion(event.target.value)} />
                </label>
                <label>包名
                  <input value={otaConvertName} onChange={(event) => setOtaConvertName(event.target.value)} placeholder="my_app_v1" />
                </label>
                <label>AppBase
                  <input value={otaAppBase} onChange={(event) => setOtaAppBase(event.target.value)} />
                </label>
              </div>
              <label className="check"><input type="checkbox" checked={otaSendAfterConvert} onChange={(event) => setOtaSendAfterConvert(event.target.checked)} /> 转换后直接下发到当前 USB</label>
              <div className="right-actions">
                <button onClick={() => setOtaConvertOutput("")} disabled={otaConvertBusy}>清空</button>
                <button className="primary" onClick={convertOtaPackage} disabled={otaConvertBusy || !otaInputFile.trim()}>
                  {otaConvertBusy ? "处理中" : (otaSendAfterConvert ? "转换并下发" : "一键转换")}
                </button>
              </div>
              {otaConvertOutput && <pre className="ota-convert-output">{otaConvertOutput}</pre>}
              <p className="hint">包名为空时覆盖默认 app.bin；填写包名会生成可在下方选择的命名 OTA 包。</p>
            </section>

            <section className="group ota-panel">
              <h2>USB OTA</h2>
              <label>升级包
                <select value={otaPackageId} onChange={(event) => selectOtaPackage(event.target.value)}>
                  {otaPackages.length === 0 && <option value="default">app.bin / manifest.bin</option>}
                  {otaPackages.map((item) => (
                    <option key={item.packageId} value={item.packageId}>
                      {item.label || item.packageId}
                    </option>
                  ))}
                </select>
              </label>
              <label>分包大小
                <input value={otaChunkSize} onChange={(event) => setOtaChunkSize(event.target.value)} />
              </label>
              <label className="check"><input type="checkbox" checked={otaResetAfter} onChange={(event) => setOtaResetAfter(event.target.checked)} /> 写入后复位并等待 CDC 重连</label>
              <div className="ota-info">
                <span>状态</span><strong>{otaInfo?.exists ? "ready" : "missing"}</strong>
                <span>App</span><code>{otaInfo?.appSize || 0} bytes</code>
                <span>Manifest</span><code>{otaInfo?.manifestSize || 0} bytes</code>
                <span>Version</span><code>{otaInfo?.manifest?.image_version ?? "-"}</code>
                <span>CRC</span><code>{otaInfo?.manifest?.image_crc32 || "-"}</code>
              </div>
              <div className="ota-paths">
                <code title={otaInfo?.root || ""}>{otaInfo?.root || "H5 root not resolved"}</code>
                <code title={otaInfo?.appBin || ""}>{shortPath(otaInfo?.appBin) || "app.bin"}</code>
                <code title={otaInfo?.manifestBin || ""}>{shortPath(otaInfo?.manifestBin) || "manifest.bin"}</code>
              </div>
              <div className="right-actions">
                <button onClick={refreshOtaInfo} disabled={otaBusy}>刷新</button>
                <button onClick={stopOtaUpdate} disabled={!otaBusy}>停止</button>
                <button className="primary" onClick={startOtaUpdate} disabled={otaBusy}>
                  {otaBusy ? "写入中" : "开始OTA"}
                </button>
              </div>
              <p className="hint">OTA 走当前 USB 日志 CDC 通道，发送前先打开板子的 USB 串口。</p>
            </section>

            <section className="group ota-progress-panel">
              <h2>OTA进度</h2>
              <div className="ota-progress">
                <div style={{ width: `${Math.max(0, Math.min(100, otaProgress.percent || 0))}%` }} />
              </div>
              <div className="meter-grid compact">
                <span>Stage</span><strong>{otaProgress.stage || "idle"}</strong>
                <span>Progress</span><strong>{otaProgress.percent || 0}%</strong>
                <span>Sent</span><strong>{otaProgress.sent || 0}</strong>
                <span>Total</span><strong>{otaProgress.total || 0}</strong>
              </div>
              <p className="hint">{otaProgress.detail || "等待开始"}</p>
            </section>
          </>
        )}

        {rightTool === "board" && (
          <>
            <section className="group board-flow">
              <h2>编译 / 烧录 / 重启</h2>
              <label className="check"><input type="checkbox" checked={boardBuild} onChange={(event) => setBoardBuild(event.target.checked)} /> 执行 Keil 编译</label>
              <label className="check"><input type="checkbox" checked={!boardNoVerify} onChange={(event) => setBoardNoVerify(!event.target.checked)} /> 烧录后校验</label>
              <label className="check"><input type="checkbox" checked={boardAutoReconnect} onChange={(event) => setBoardAutoReconnect(event.target.checked)} /> 完成后等待 CDC 重连</label>
              <div className="board-buttons">
                <button className="primary" onClick={() => runBoardWorkflow(true)} disabled={boardBusy}>编译+烧录+重启</button>
                <button onClick={() => runBoardWorkflow(false)} disabled={boardBusy}>仅烧录+重启</button>
                <button onClick={stopBoardWorkflow} disabled={!boardBusy}>停止</button>
                <button onClick={() => setBoardOutput([])} disabled={boardBusy}>清空输出</button>
              </div>
              <p className="hint">只会关闭USB日志通道，485 Modbus通道保持独立。</p>
            </section>

            <section className="group board-output-panel">
              <h2>流程输出</h2>
              <div className="board-output" ref={boardOutputRef}>
                {boardOutput.map((line, index) => (
                  <div key={`${line.at}-${index}`} className={line.stream === "stderr" ? "err" : ""}>
                    {line.text}
                  </div>
                ))}
              </div>
            </section>
          </>
        )}
      </aside>

      <section className="bottom-send">
        <div className="send-options">
          <span>底部发送走USB日志通道</span>
          <label><input type="radio" checked={!sendHex} onChange={() => setSendHex(false)} /> ASCII</label>
          <label><input type="radio" checked={sendHex} onChange={() => setSendHex(true)} /> HEX</label>
          <label><input type="checkbox" checked={appendNewline} onChange={(event) => setAppendNewline(event.target.checked)} disabled={sendHex} /> 自动换行</label>
        </div>
        <textarea value={sendText} onChange={(event) => setSendText(sendHex ? hexBytes(event.target.value) : event.target.value)} />
        <button className="send-button" onClick={() => send()} disabled={!logConnected || !sendText.trim()}>发送USB</button>
      </section>

      <footer className="statusbar">
        <span>{status}</span>
        <span>{latestLogFile || "latest.log 未创建"}</span>
        <span>{eventsFile || "events 未创建"}</span>
        <span>RX: {logs.filter((item) => item.dir !== "SEND").length}</span>
        <span>TX: {logs.filter((item) => item.dir === "SEND").length}</span>
      </footer>
    </main>
  );
}

createRoot(document.getElementById("root")).render(<App />);
