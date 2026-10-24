// The Armory's thumbnails, drawn offscreen: headless Chrome runs page.html (three.js), this drives
// it over the DevTools protocol (Node's own WebSocket, no npm packages) and writes each picture.
//
//   node render.js <jobs.json>
//
// jobs.json: {"chrome": path, "vendor": dir, "glb": dir, "out": dir,
//             "jobs": [{"id": hash, "glb": "<file in glb dir>", "view": "item"|"figure", "elite": null|{...}}]}
// Writes <out>/<id>.webp (512 px) and <out>/<id>.96.webp, transparent. Prints one line a job.
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const http = require('http');
const { spawn } = require('child_process');

const spec = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const PAGE = path.join(__dirname, 'page.html');
const TYPES = { '.js': 'text/javascript', '.html': 'text/html', '.glb': 'model/gltf-binary' };

function serve() {
  const server = http.createServer((req, res) => {
    const url = decodeURIComponent(req.url.split('?')[0]);
    let file = null;
    if (url === '/' || url === '/page.html') file = PAGE;
    else if (url.startsWith('/vendor/')) file = path.join(spec.vendor, url.slice(8));
    else if (url.startsWith('/glb/')) file = path.join(spec.glb, path.basename(url));
    if (!file || !fs.existsSync(file)) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream' });
    fs.createReadStream(file).pipe(res);
  });
  return new Promise(ok => server.listen(0, '127.0.0.1', () => ok(server)));
}

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

function client(ws) {
  let next = 1;
  const waiting = new Map();
  ws.addEventListener('message', ev => {
    const msg = JSON.parse(ev.data);
    if (msg.id && waiting.has(msg.id)) {
      const { ok, fail } = waiting.get(msg.id);
      waiting.delete(msg.id);
      if (msg.error) fail(new Error(msg.error.message)); else ok(msg.result);
    }
  });
  return (method, params = {}) => new Promise((ok, fail) => {
    const id = next++;
    waiting.set(id, { ok, fail });
    ws.send(JSON.stringify({ id, method, params }));
  });
}

async function main() {
  const server = await serve();
  const base = `http://127.0.0.1:${server.address().port}`;
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'armory-chrome-'));
  const chrome = spawn('nice', ['-n', '10', spec.chrome, '--headless=new', '--remote-debugging-port=0',
    `--user-data-dir=${profile}`, '--no-first-run', '--no-default-browser-check', '--disable-extensions',
    '--mute-audio', '--hide-scrollbars', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist',
    '--window-size=600,600', 'about:blank'], { stdio: ['ignore', 'ignore', 'pipe'] });
  let errors = '';
  chrome.stderr.on('data', d => { errors += d; });
  let failed = 0;
  try {
    const port = await devtools(profile);
    const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
    const target = list.find(t => t.type === 'page');
    const ws = new WebSocket(target.webSocketDebuggerUrl);
    await new Promise((ok, fail) => { ws.addEventListener('open', ok); ws.addEventListener('error', fail); });
    const send = client(ws);
    await send('Runtime.enable');
    await send('Page.navigate', { url: `${base}/page.html` });
    for (let i = 0; i < 300; i++) {
      const r = await send('Runtime.evaluate', { expression: 'window.ready === true', returnByValue: true });
      if (r.result && r.result.value) break;
      if (i === 299) throw new Error('page.html never became ready (WebGL or the vendor files)');
      await sleep(100);
    }
    for (const job of spec.jobs) {
      const arg = JSON.stringify({ glb: `/glb/${job.glb}`, view: job.view, elite: job.elite || null });
      const r = await send('Runtime.evaluate', { expression: `render(${arg})`, awaitPromise: true, returnByValue: true });
      if (r.exceptionDetails || !r.result || !r.result.value) {
        failed++;
        const why = r.exceptionDetails ? (r.exceptionDetails.exception || {}).description || r.exceptionDetails.text : 'no picture';
        console.log(`FAIL ${job.id} ${String(why).split('\n')[0]}`);
        continue;
      }
      const v = r.result.value;
      fs.writeFileSync(path.join(spec.out, `${job.id}.webp.part`), Buffer.from(v.big, 'base64'));
      fs.writeFileSync(path.join(spec.out, `${job.id}.96.webp`), Buffer.from(v.small, 'base64'));
      fs.renameSync(path.join(spec.out, `${job.id}.webp.part`), path.join(spec.out, `${job.id}.webp`));
      console.log(`OK ${job.id}`);
    }
    ws.close();
  } catch (e) {
    console.log(`FAIL all ${e.message}`);
    if (errors) console.log(errors.split('\n').slice(-5).join('\n'));
    failed = spec.jobs.length || 1;
  } finally {
    chrome.kill();
    server.close();
    await sleep(300);
    fs.rmSync(profile, { recursive: true, force: true });
  }
  process.exit(failed ? 1 : 0);
}

main();
