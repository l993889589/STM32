import { app, BrowserWindow, dialog, ipcMain } from "electron";
import { SerialPort } from "serialport";
import { Aedes } from "aedes";
import { connectAsync } from "mqtt";
import { spawn } from "node:child_process";
import fs from "node:fs";
import net from "node:net";
import os from "node:os";
import path from "node:path";

let win;
const serialPorts = new Map();
let logFile;
const writableBaseDir = app.isPackaged ? path.dirname(process.execPath) : app.getAppPath();
const workspaceLogDir = path.join(writableBaseDir, "logs");
const latestLogFile = path.join(workspaceLogDir, "latest.log");
const h5ProjectName = "STM32H563_Threadx_usbx_cdc_acm";

let boardProcess;
let otaProcess;
let mqttBroker;
let mqttServer;
let mqttPort = 1883;
let mqttStartedAt;

function send(channel, payload) {
  win?.webContents.send(channel, payload);
}

function ensureLogFiles() {
  fs.mkdirSync(workspaceLogDir, { recursive: true });
  if (!logFile) {
    const dir = path.join(workspaceLogDir, "sessions");
    fs.mkdirSync(dir, { recursive: true });
    logFile = path.join(dir, `serial-${new Date().toISOString().replace(/[:.]/g, "-")}.log`);
  }
}

function appendLog(text) {
  ensureLogFiles();
  fs.appendFileSync(logFile, text);
  fs.appendFileSync(latestLogFile, text);
}

function appendEvent(event) {
  ensureLogFiles();
  fs.appendFileSync(path.join(workspaceLogDir, "latest-events.jsonl"), `${JSON.stringify({ at: new Date().toISOString(), ...event })}\n`);
}

function findH5ProjectRoot() {
  const candidates = [
    path.resolve(app.getAppPath(), "..", h5ProjectName),
    path.resolve(writableBaseDir, "..", "..", "..", h5ProjectName),
    path.resolve(process.cwd(), "..", h5ProjectName),
    path.join("D:\\Embedded\\H5", h5ProjectName)
  ];

  return candidates.find((candidate) => fs.existsSync(path.join(candidate, "flash.ps1"))) || candidates[0];
}

function findH5Root() {
  const appRoot = app.getAppPath();
  const candidates = [
    path.resolve(appRoot, ".."),
    path.resolve(writableBaseDir, "..", "..", ".."),
    process.cwd(),
    "D:\\Embedded\\H5"
  ];

  return candidates.find((candidate) => fs.existsSync(path.join(candidate, "make_ota_package.ps1"))) || candidates[0];
}

function otaPackagePaths() {
  return otaPackagePathsById("default");
}

function otaPackagePathsById(packageId = "default") {
  const root = findH5Root();
  const packageRoot = path.join(root, "ota_package");

  if (packageId && packageId !== "default") {
    const safeId = String(packageId).replace(/[<>:"/\\|?*]/g, "");
    const candidateDir = path.join(packageRoot, safeId);
    const namedAppBin = path.join(packageRoot, `${safeId}.bin`);
    const namedManifestBin = path.join(packageRoot, `${safeId}.manifest.bin`);
    const namedManifestJson = path.join(packageRoot, `${safeId}.manifest.json`);

    if (fs.existsSync(path.join(candidateDir, "app.bin")) && fs.existsSync(path.join(candidateDir, "manifest.bin"))) {
      return {
        root,
        packageId: safeId,
        packageDir: candidateDir,
        appBin: path.join(candidateDir, "app.bin"),
        manifestBin: path.join(candidateDir, "manifest.bin"),
        manifestJson: path.join(candidateDir, "manifest.json")
      };
    }

    if (fs.existsSync(namedAppBin) && fs.existsSync(namedManifestBin)) {
      return {
        root,
        packageId: safeId,
        packageDir: packageRoot,
        appBin: namedAppBin,
        manifestBin: namedManifestBin,
        manifestJson: namedManifestJson
      };
    }
  }

  return {
    root,
    packageId: "default",
    packageDir: packageRoot,
    appBin: path.join(packageRoot, "app.bin"),
    manifestBin: path.join(packageRoot, "manifest.bin"),
    manifestJson: path.join(packageRoot, "manifest.json")
  };
}

function readOtaManifestJson(manifestJson) {
  if (!fs.existsSync(manifestJson)) return null;
  return JSON.parse(fs.readFileSync(manifestJson, "utf8").replace(/^\uFEFF/, ""));
}

function otaPackageInfo(paths) {
  const manifest = readOtaManifestJson(paths.manifestJson);

  return {
    ...paths,
    exists: fs.existsSync(paths.appBin) && fs.existsSync(paths.manifestBin),
    appSize: fs.existsSync(paths.appBin) ? fs.statSync(paths.appBin).size : 0,
    manifestSize: fs.existsSync(paths.manifestBin) ? fs.statSync(paths.manifestBin).size : 0,
    manifest
  };
}

function listOtaPackages() {
  const root = findH5Root();
  const packageRoot = path.join(root, "ota_package");
  const packages = [];
  const seen = new Set();

  function push(paths, label) {
    const info = otaPackageInfo(paths);
    if (!info.exists || seen.has(info.packageId)) return;
    seen.add(info.packageId);
    packages.push({ ...info, label });
  }

  push(otaPackagePathsById("default"), "app.bin / manifest.bin");

  if (!fs.existsSync(packageRoot)) return packages;

  for (const entry of fs.readdirSync(packageRoot, { withFileTypes: true })) {
    if (entry.isDirectory()) {
      push(otaPackagePathsById(entry.name), entry.name);
    } else if (entry.isFile() && entry.name.toLowerCase().endsWith(".bin") && !entry.name.toLowerCase().endsWith(".manifest.bin")) {
      const id = path.basename(entry.name, ".bin");
      push(otaPackagePathsById(id), entry.name);
    }
  }

  return packages;
}

function crc16Modbus(buffer) {
  let crc = 0xffff;
  for (const byte of buffer) {
    crc ^= byte;
    for (let i = 0; i < 8; i += 1) {
      crc = (crc & 1) ? ((crc >> 1) ^ 0xa001) : (crc >> 1);
    }
  }
  return crc & 0xffff;
}

function writeU16LE(buffer, offset, value) {
  buffer[offset] = value & 0xff;
  buffer[offset + 1] = (value >>> 8) & 0xff;
}

function writeU32LE(buffer, offset, value) {
  buffer[offset] = value & 0xff;
  buffer[offset + 1] = (value >>> 8) & 0xff;
  buffer[offset + 2] = (value >>> 16) & 0xff;
  buffer[offset + 3] = (value >>> 24) & 0xff;
}

function buildOtaFrame(cmd, seq, address, payload = Buffer.alloc(0)) {
  const header = Buffer.alloc(16);
  header[0] = 0x4c;
  header[1] = 0x44;
  header[2] = 0x4f;
  header[3] = 0x54;
  header[4] = cmd & 0xff;
  header[5] = 0;
  writeU16LE(header, 6, seq & 0xffff);
  writeU32LE(header, 8, address >>> 0);
  writeU16LE(header, 12, payload.length);
  writeU16LE(header, 14, 0);

  const body = Buffer.concat([header, payload]);
  const crc = crc16Modbus(body);
  const frame = Buffer.alloc(body.length + 2);
  body.copy(frame, 0);
  writeU16LE(frame, body.length, crc);
  return frame;
}

function writeSerialBuffer(serialPort, buffer) {
  return new Promise((resolve, reject) => {
    serialPort.write(buffer, (error) => {
      if (error) {
        reject(error);
        return;
      }
      serialPort.drain((drainError) => drainError ? reject(drainError) : resolve());
    });
  });
}

function waitForOtaAck(serialPort, cmd, seq, timeoutMs = 5000) {
  return new Promise((resolve, reject) => {
    let text = "";
    const timeout = setTimeout(() => {
      cleanup();
      reject(new Error(`OTA ACK timeout cmd=${cmd} seq=${seq}`));
    }, timeoutMs);

    const cleanup = () => {
      clearTimeout(timeout);
      serialPort.off("data", onData);
    };

    const onData = (data) => {
      text += data.toString("utf8");
      const lines = text.split(/\r?\n/);
      text = lines.pop() || "";

      for (const line of lines) {
        const match = line.match(/ota ack\s+(\d+)\s+(\d+)\s+(\d+)/i);
        if (!match) continue;

        const gotCmd = Number(match[1]);
        const gotSeq = Number(match[2]);
        const status = Number(match[3]);

        if (gotCmd === cmd && gotSeq === seq) {
          cleanup();
          if (status === 0) resolve({ cmd: gotCmd, seq: gotSeq, status });
          else reject(new Error(`OTA ACK error cmd=${cmd} seq=${seq} status=${status}`));
          return;
        }
      }
    };

    serialPort.on("data", onData);
  });
}

function getLanAddresses() {
  return Object.values(os.networkInterfaces())
    .flat()
    .filter((item) => item && item.family === "IPv4" && !item.internal)
    .map((item) => item.address);
}

function mqttSnapshot() {
  return {
    running: !!mqttServer?.listening,
    port: mqttPort,
    addresses: getLanAddresses(),
    startedAt: mqttStartedAt
  };
}

function appendMqttEvent(event) {
  appendEvent({ type: "mqtt", ...event });
  send("mqtt:event", { at: new Date().toISOString(), ...event });
}

async function startMqttBroker(options = {}) {
  if (mqttServer?.listening) return mqttSnapshot();

  mqttPort = Number(options.port || 1883);
  mqttBroker = await Aedes.createBroker({ id: options.clientId || "leduo-debug-assistant" });
  mqttServer = net.createServer(mqttBroker.handle);

  mqttBroker.on("client", (client) => {
    appendMqttEvent({ event: "client", clientId: client?.id || "" });
  });
  mqttBroker.on("clientDisconnect", (client) => {
    appendMqttEvent({ event: "disconnect", clientId: client?.id || "" });
  });
  mqttBroker.on("subscribe", (subscriptions, client) => {
    appendMqttEvent({
      event: "subscribe",
      clientId: client?.id || "",
      topics: subscriptions.map((item) => item.topic)
    });
  });
  mqttBroker.on("publish", (packet, client) => {
    if (packet.topic?.startsWith("$SYS")) return;

    appendMqttEvent({
      event: "publish",
      clientId: client?.id || "broker",
      topic: packet.topic || "",
      payload: packet.payload?.toString("utf8") || "",
      bytes: packet.payload?.length || 0
    });
  });

  await new Promise((resolve, reject) => {
    mqttServer.once("error", reject);
    mqttServer.listen(mqttPort, "0.0.0.0", () => {
      mqttServer.off("error", reject);
      resolve();
    });
  });

  mqttStartedAt = new Date().toISOString();
  appendMqttEvent({ event: "start", port: mqttPort, addresses: getLanAddresses() });
  return mqttSnapshot();
}

function createWindow() {
  win = new BrowserWindow({
    width: 1580,
    height: 960,
    minWidth: 1280,
    minHeight: 820,
    webPreferences: {
      preload: path.join(app.getAppPath(), "electron", "preload.cjs")
    }
  });
  if (app.isPackaged) {
    win.loadFile(path.join(app.getAppPath(), "dist", "index.html"));
  } else {
    win.loadURL(process.env.VITE_DEV_SERVER_URL || "http://127.0.0.1:5173");
  }
}

app.whenReady().then(createWindow);
app.on("window-all-closed", () => app.quit());

ipcMain.handle("serial:list", async () => SerialPort.list());

ipcMain.handle("serial:open", async (_event, options) => {
  const serialChannel = options.channel || "log";
  const oldPort = serialPorts.get(serialChannel);

  if (oldPort?.isOpen) await new Promise((resolve) => oldPort.close(resolve));
  if (serialChannel === "log") logFile = null;
  ensureLogFiles();
  fs.appendFileSync(latestLogFile, `[${new Date().toISOString()}] OPEN ${serialChannel} ${options.path} ${options.baudRate || 115200}\n`);
  const openedPath = options.path;
  const serialPort = new SerialPort({
    path: openedPath,
    baudRate: Number(options.baudRate || 115200),
    dataBits: Number(options.dataBits || 8),
    stopBits: Number(options.stopBits || 1),
    parity: options.parity || "none",
    autoOpen: false
  });
  serialPorts.set(serialChannel, serialPort);

  serialPort.on("data", (data) => {
    const entry = { channel: serialChannel, at: new Date().toISOString(), text: data.toString("utf8"), hex: data.toString("hex").toUpperCase() };
    appendLog(`[${entry.at}] ${serialChannel.toUpperCase()} RX ${entry.text || `<${entry.hex}>`}`);
    appendEvent({ type: "rx", channel: serialChannel, port: openedPath, hex: entry.hex, text: entry.text });
    send("serial:data", entry);
  });
  serialPort.on("error", (error) => {
    appendLog(`[${new Date().toISOString()}] ${serialChannel.toUpperCase()} ERROR ${error.message}\n`);
    send("serial:error", { channel: serialChannel, message: error.message });
  });
  serialPort.on("close", () => {
    if (serialPorts.get(serialChannel) === serialPort) serialPorts.delete(serialChannel);
    appendLog(`[${new Date().toISOString()}] CLOSED ${serialChannel} ${openedPath}\n`);
    send("serial:closed", { channel: serialChannel, path: openedPath });
  });
  await new Promise((resolve, reject) => serialPort.open((error) => error ? reject(error) : resolve()));
  return { ok: true, channel: serialChannel, logFile, latestLogFile };
});

ipcMain.handle("serial:close", async (_event, options = {}) => {
  const serialChannel = options.channel || "log";
  const serialPort = serialPorts.get(serialChannel);

  if (serialPort?.isOpen) await new Promise((resolve) => serialPort.close(resolve));
  appendLog(`[${new Date().toISOString()}] CLOSE ${serialChannel}\n`);
  return { ok: true };
});

ipcMain.handle("serial:write", async (_event, payload) => {
  const serialChannel = payload.channel || "log";
  const serialPort = serialPorts.get(serialChannel);

  if (!serialPort?.isOpen) throw new Error(`${serialChannel} serial port is not open`);
  const text = payload.appendNewline && !payload.hex ? `${payload.data}\r\n` : payload.data;
  const data = payload.hex
    ? Buffer.from(payload.data.replace(/\s+/g, ""), "hex")
    : Buffer.from(text, "utf8");
  await new Promise((resolve, reject) => serialPort.write(data, (error) => error ? reject(error) : resolve()));
  if (!payload.silent) {
    appendLog(`[${new Date().toISOString()}] ${serialChannel.toUpperCase()} TX ${payload.hex ? data.toString("hex").toUpperCase() : payload.data}\n`);
    appendEvent({ type: "tx", channel: serialChannel, hex: data.toString("hex").toUpperCase(), text: payload.hex ? "" : payload.data });
  }
  return { ok: true };
});

ipcMain.handle("logs:paths", async () => {
  ensureLogFiles();
  return { latestLogFile, logFile, eventsFile: path.join(workspaceLogDir, "latest-events.jsonl") };
});

ipcMain.handle("ota:info", async () => {
  return otaPackageInfo(otaPackagePathsById("default"));
});

ipcMain.handle("ota:list", async () => {
  return listOtaPackages();
});

ipcMain.handle("ota:pick-input", async () => {
  const result = await dialog.showOpenDialog(win, {
    title: "Select app image",
    properties: ["openFile"],
    filters: [
      { name: "Firmware image", extensions: ["axf", "elf", "hex", "bin"] },
      { name: "All files", extensions: ["*"] }
    ]
  });

  return {
    canceled: result.canceled,
    filePath: result.filePaths?.[0] || ""
  };
});

ipcMain.handle("ota:convert", async (_event, options = {}) => {
  const inputFile = String(options.inputFile || "").trim();
  if (!inputFile || !fs.existsSync(inputFile)) {
    throw new Error(`Input file not found: ${inputFile}`);
  }

  const root = findH5Root();
  const script = path.join(root, "make_ota_package.ps1");
  if (!fs.existsSync(script)) {
    throw new Error(`make_ota_package.ps1 not found: ${script}`);
  }

  const imageVersion = Math.max(1, Number(options.imageVersion || 1));
  const packageName = String(options.packageName || "").trim();
  const appBase = String(options.appBase || "0x08020000").trim();
  const args = [
    "-NoProfile",
    "-ExecutionPolicy",
    "Bypass",
    "-File",
    script,
    "-ImageVersion",
    String(imageVersion),
    "-InputFile",
    inputFile,
    "-AppBase",
    appBase
  ];

  if (packageName) {
    args.push("-PackageName", packageName);
  }

  appendEvent({ type: "ota-convert-start", inputFile, imageVersion, packageName, appBase });
  appendLog(`[${new Date().toISOString()}] OTA CONVERT ${inputFile}\n`);

  const output = await new Promise((resolve, reject) => {
    const child = spawn("powershell.exe", args, {
      cwd: root,
      windowsHide: true
    });
    let stdout = "";
    let stderr = "";

    child.stdout.on("data", (chunk) => {
      const text = chunk.toString();
      stdout += text;
      appendLog(text);
    });
    child.stderr.on("data", (chunk) => {
      const text = chunk.toString();
      stderr += text;
      appendLog(text);
    });
    child.on("error", reject);
    child.on("close", (code) => {
      if (code === 0) resolve({ stdout, stderr });
      else reject(new Error(`OTA convert failed, exit code ${code}\n${stderr || stdout}`));
    });
  });

  const packages = listOtaPackages();
  const selectedPackageId = packageName || "default";
  const selectedPackage = otaPackageInfo(otaPackagePathsById(selectedPackageId));
  appendEvent({ type: "ota-convert-done", inputFile, selectedPackageId });

  return {
    ok: true,
    output: `${output.stdout}${output.stderr}`,
    packageId: selectedPackageId,
    package: selectedPackage,
    packages
  };
});

ipcMain.handle("ota:start", async (_event, options = {}) => {
  if (otaProcess) throw new Error("OTA workflow is already running");

  const serialPort = serialPorts.get("log");
  if (!serialPort?.isOpen) throw new Error("USB log serial port is not open");

  const paths = otaPackagePathsById(options.packageId || "default");
  if (!fs.existsSync(paths.appBin) || !fs.existsSync(paths.manifestBin)) {
    throw new Error(`OTA package not found: ${path.dirname(paths.appBin)}`);
  }

  otaProcess = { cancelled: false };
  const appBin = fs.readFileSync(paths.appBin);
  const manifestBin = fs.readFileSync(paths.manifestBin);
  const manifest = fs.existsSync(paths.manifestJson)
    ? JSON.parse(fs.readFileSync(paths.manifestJson, "utf8"))
    : {};
  const baseAddress = Number.parseInt(String(manifest.package_address || "0x00100000").replace(/^0x/i, ""), 16) || 0x00100000;
  const chunkSize = Math.min(Math.max(Number(options.chunkSize || 224), 32), 224);
  let seq = 0;

  const sendProgress = (stage, sent = 0, total = appBin.length, detail = "") => {
    const payload = { stage, sent, total, percent: total ? Math.round((sent / total) * 100) : 0, detail };
    send("ota:progress", payload);
    appendEvent({ type: "ota-progress", ...payload });
  };

  async function sendFrame(cmd, frameSeq, address, payload, timeoutMs) {
    const waitAck = waitForOtaAck(serialPort, cmd, frameSeq, timeoutMs);
    await writeSerialBuffer(serialPort, buildOtaFrame(cmd, frameSeq, address, payload));
    return waitAck;
  }

  try {
    appendEvent({ type: "ota-start", appSize: appBin.length, chunkSize, paths });
    appendLog(`[${new Date().toISOString()}] OTA START app=${appBin.length} chunk=${chunkSize}\n`);
    sendProgress("begin", 0, appBin.length, "erase download slot");

    const beginPayload = Buffer.alloc(8);
    writeU32LE(beginPayload, 0, appBin.length);
    writeU32LE(beginPayload, 4, Number.parseInt(String(manifest.image_crc32 || "0").replace(/^0x/i, ""), 16) || 0);
    await sendFrame(1, seq, baseAddress, beginPayload, 90000);
    seq += 1;

    for (let offset = 0; offset < appBin.length; offset += chunkSize) {
      if (otaProcess.cancelled) throw new Error("OTA cancelled");

      const chunk = appBin.subarray(offset, Math.min(offset + chunkSize, appBin.length));
      await sendFrame(2, seq, baseAddress + offset, chunk, 8000);
      seq += 1;

      if ((offset % (chunkSize * 16)) === 0 || offset + chunk.length >= appBin.length) {
        sendProgress("data", offset + chunk.length, appBin.length, `seq=${seq - 1}`);
      }
    }

    sendProgress("manifest-a", appBin.length, appBin.length, "write manifest A");
    await sendFrame(3, seq, 0x00000000, manifestBin, 15000);
    seq += 1;

    sendProgress("manifest-b", appBin.length, appBin.length, "write manifest B");
    await sendFrame(3, seq, 0x00001000, manifestBin, 15000);
    seq += 1;

    sendProgress("end", appBin.length, appBin.length, "finalize");
    await sendFrame(4, seq, baseAddress, Buffer.alloc(0), 5000);
    seq += 1;

    if (options.reset) {
      sendProgress("reset", appBin.length, appBin.length, "software reset");
      await sendFrame(5, seq, 0, Buffer.alloc(0), 3000).catch(() => undefined);
    }

    sendProgress("done", appBin.length, appBin.length, "OTA package written");
    appendLog(`[${new Date().toISOString()}] OTA DONE\n`);
    return { ok: true, appSize: appBin.length, chunkSize, seq };
  } catch (error) {
    send("ota:progress", { stage: "error", sent: 0, total: appBin.length, percent: 0, detail: error.message });
    appendLog(`[${new Date().toISOString()}] OTA ERROR ${error.message}\n`);
    appendEvent({ type: "ota-error", message: error.message });
    throw error;
  } finally {
    otaProcess = null;
  }
});

ipcMain.handle("ota:stop", async () => {
  if (otaProcess) otaProcess.cancelled = true;
  return { ok: true };
});

ipcMain.handle("mqtt:status", async () => mqttSnapshot());

ipcMain.handle("mqtt:start", async (_event, options = {}) => {
  return startMqttBroker(options);
});

ipcMain.handle("mqtt:stop", async () => {
  const broker = mqttBroker;
  const server = mqttServer;

  mqttBroker = null;
  mqttServer = null;
  mqttStartedAt = null;

  if (server) {
    await new Promise((resolve) => server.close(() => resolve()));
  }
  if (broker) {
    await new Promise((resolve) => broker.close(() => resolve()));
  }

  appendMqttEvent({ event: "stop" });
  return mqttSnapshot();
});

ipcMain.handle("mqtt:publish", async (_event, payload = {}) => {
  if (!mqttBroker) throw new Error("MQTT broker is not running");

  const topic = payload.topic || "leduo/pc/down";
  const message = String(payload.message || "");

  await new Promise((resolve, reject) => {
    mqttBroker.publish({
      cmd: "publish",
      topic,
      payload: Buffer.from(message, "utf8"),
      qos: 0,
      retain: false
    }, (error) => error ? reject(error) : resolve());
  });

  appendMqttEvent({ event: "publish-local", topic, payload: message, bytes: Buffer.byteLength(message) });
  return { ok: true };
});

ipcMain.handle("mqtt:simulate-w800", async (_event, options = {}) => {
  if (!mqttServer?.listening) {
    await startMqttBroker({ port: mqttPort || 1883 });
  }

  const port = mqttServer?.listening ? mqttPort : Number(options.port || 1883);
  const deviceId = options.deviceId || "w800-sim";
  const client = await connectAsync(`mqtt://127.0.0.1:${port}`, {
    clientId: deviceId,
    clean: true,
    connectTimeout: 3000
  });

  const status = {
    deviceId,
    online: true,
    ip: "192.168.1.88",
    rssi: -42,
    fw: "sim-0.1.0",
    uptime: Math.floor(process.uptime()),
    mode: "mqtt-sim"
  };

  await client.publishAsync("leduo/w800/status", JSON.stringify(status));
  await client.publishAsync("leduo/w800/up", JSON.stringify({ deviceId, type: "hello", value: 1 }));
  await client.publishAsync("leduo/w800/log", JSON.stringify({ deviceId, level: "info", msg: "hello from simulated W800" }));
  await client.endAsync(true);

  return { ok: true, deviceId };
});

ipcMain.handle("board:flash", async (_event, options = {}) => {
  if (boardProcess) throw new Error("Board workflow is already running");

  const projectRoot = findH5Root();
  const script = path.join(projectRoot, "flash_ota_all.ps1");

  if (!fs.existsSync(script)) {
    throw new Error(`flash_ota_all.ps1 not found: ${script}`);
  }

  const logPort = serialPorts.get("log");
  if (logPort?.isOpen) {
    await new Promise((resolve) => logPort.close(resolve));
  }

  const args = ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script];
  if (!options.build) args.push("-NoBuild");
  if (options.noVerify) args.push("-NoVerify");

  appendEvent({ type: "board-workflow-start", build: !!options.build, projectRoot });
  appendLog(`[${new Date().toISOString()}] BOARD START build=${!!options.build} project=${projectRoot} script=${script}\n`);
  send("board:flash-output", { stream: "info", text: `Workspace: ${projectRoot}\nScript: ${script}\n` });

  boardProcess = spawn("powershell.exe", args, {
    cwd: projectRoot,
    windowsHide: true
  });

  boardProcess.stdout.on("data", (chunk) => {
    const text = chunk.toString("utf8");
    appendLog(`[${new Date().toISOString()}] BOARD STDOUT ${text}`);
    appendEvent({ type: "board-workflow-output", stream: "stdout", text });
    send("board:flash-output", { stream: "stdout", text });
  });

  boardProcess.stderr.on("data", (chunk) => {
    const text = chunk.toString("utf8");
    appendLog(`[${new Date().toISOString()}] BOARD STDERR ${text}`);
    appendEvent({ type: "board-workflow-output", stream: "stderr", text });
    send("board:flash-output", { stream: "stderr", text });
  });

  return await new Promise((resolve) => {
    boardProcess.on("close", (code) => {
      boardProcess = null;
      appendLog(`[${new Date().toISOString()}] BOARD DONE code=${code}\n`);
      appendEvent({ type: "board-workflow-done", code });
      send("board:flash-done", { code });
      resolve({ ok: code === 0, code, projectRoot });
    });
    boardProcess.on("error", (error) => {
      boardProcess = null;
      appendLog(`[${new Date().toISOString()}] BOARD ERROR ${error.message}\n`);
      appendEvent({ type: "board-workflow-error", message: error.message });
      send("board:flash-output", { stream: "stderr", text: `${error.message}\n` });
      send("board:flash-done", { code: -1 });
      resolve({ ok: false, code: -1, projectRoot, error: error.message });
    });
  });
});

ipcMain.handle("board:flash-stop", async () => {
  if (boardProcess) {
    boardProcess.kill();
    boardProcess = null;
    appendEvent({ type: "board-workflow-stopped" });
  }

  return { ok: true };
});
