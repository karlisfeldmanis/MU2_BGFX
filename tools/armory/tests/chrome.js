// Headless Chrome over the DevTools protocol, with Node's own WebSocket and no npm packages (as
// tools/armory/thumbs/render.js): the Armory's page checks and its card check drive it.
//
//   const page = await require('./chrome').open({ width, height });
//   await page.go(url); await page.eval('1 + 1'); await page.shot(file, { full: true }); page.errors; await page.close();
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');

const CHROME = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const sleep = ms => new Promise(ok => setTimeout(ok, ms));

async function devtools(profile) {
  const file = path.join(profile, 'DevToolsActivePort');
  for (let i = 0; i < 300; i++) {
    if (fs.existsSync(file)) {
      const port = fs.readFileSync(file, 'utf8').split('\n')[0].trim();
      if (port) return port;
    }
    await sleep(100);
  }
  throw new Error('Chrome did not open its DevTools port');
}

async function open({ width = 1440, height = 900, mobile = false, scale = 1 } = {}) {
  if (!fs.existsSync(CHROME)) throw new Error(`no Chrome at ${CHROME}`);
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'armory-site-'));
  const chrome = spawn('nice', ['-n', '10', CHROME, '--headless=new', '--remote-debugging-port=0', `--user-data-dir=${profile}`,
    '--no-first-run', '--no-default-browser-check', '--disable-extensions', '--mute-audio', '--hide-scrollbars',
    '--font-render-hinting=none', '--lang=en-US', '--accept-lang=en-US', `--window-size=${width},${height}`, 'about:blank'], { stdio: ['ignore', 'ignore', 'ignore'] });
  const port = await devtools(profile);
  const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const target = list.find(t => t.type === 'page');
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((ok, fail) => { ws.onopen = ok; ws.onerror = fail; });
  let next = 1;
  const waiting = new Map(), listeners = [];
  ws.addEventListener('message', ev => {
    const msg = JSON.parse(ev.data);
    if (msg.id && waiting.has(msg.id)) {
      const { ok, fail } = waiting.get(msg.id);
      waiting.delete(msg.id);
      if (msg.error) fail(new Error(msg.error.message)); else ok(msg.result);
    } else if (msg.method) listeners.forEach(l => l(msg));
  });
  const send = (method, params = {}) => new Promise((ok, fail) => {
    const id = next++;
    waiting.set(id, { ok, fail });
    ws.send(JSON.stringify({ id, method, params }));
  });
  const page = { errors: [], logs: [], send };
  listeners.push(msg => {
    if (msg.method === 'Runtime.exceptionThrown') page.errors.push(msg.params.exceptionDetails.exception ? msg.params.exceptionDetails.exception.description : msg.params.exceptionDetails.text);
    if (msg.method === 'Runtime.consoleAPICalled' && msg.params.type === 'error') page.errors.push(msg.params.args.map(a => a.value || a.description).join(' '));
    if (msg.method === 'Log.entryAdded' && msg.params.entry.level === 'error') page.errors.push(`${msg.params.entry.text} ${msg.params.entry.url || ''}`.trim());
  });
  await send('Page.enable');
  await send('Runtime.enable');
  await send('Log.enable');
  await send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: scale, mobile });
  page.go = async (url, settle = 400) => {
    page.errors.length = 0;
    const loaded = new Promise(ok => { const l = m => { if (m.method === 'Page.loadEventFired') { listeners.splice(listeners.indexOf(l), 1); ok(); } }; listeners.push(l); });
    await send('Page.navigate', { url });
    await Promise.race([loaded, sleep(15000)]);
    await page.eval('document.fonts ? document.fonts.ready.then(() => 1) : 1');
    await sleep(settle);
  };
  page.eval = async (expression) => {
    const r = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception ? r.exceptionDetails.exception.description : r.exceptionDetails.text);
    return r.result.value;
  };
  page.shot = async (file, { full = false, clip = null } = {}) => {
    const params = { format: 'png' };
    if (clip) params.clip = Object.assign({ scale: 1 }, clip);
    else if (full) {
      const m = await send('Page.getLayoutMetrics');
      const s = m.cssContentSize || m.contentSize;
      params.clip = { x: 0, y: 0, width: width, height: Math.ceil(s.height), scale: 1 };
      params.captureBeyondViewport = true;
    }
    const r = await send('Page.captureScreenshot', params);
    if (file) { fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, Buffer.from(r.data, 'base64')); }
    return r.data;
  };
  page.close = async () => {
    try { ws.close(); } catch (e) { /* gone */ }
    chrome.kill();
    await sleep(200);
    fs.rmSync(profile, { recursive: true, force: true });
  };
  return page;
}

module.exports = { open, sleep, CHROME };
