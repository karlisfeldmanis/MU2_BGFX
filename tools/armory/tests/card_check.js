// The Armory's card check: the site's item card (web/site/armory/card.js + armory.css) drawn in
// headless Chrome beside the game's own offscreen renders (tools/armory/prototype/cards_ref/), as
// card_compare.png was made, and measured against them. Fails on drift.
//
//   node tools/armory/tests/card_check.js            (./mu armory test runs it)
//
// Each reference has a fixture in tests/cards/<name>.json: the Sheet the game drew it from (the
// dump's describe() rows, edited only where the reference's reader or state differed, said in its
// note). What is compared, card by card:
//   * the card's size between its black seams: the width exactly (346 + iron + seam), the height
//     within HEIGHT_TOL px;
//   * every line of type, found as a band of bright rows: as many lines, each one's middle within
//     LINE_TOL px of the game's, its left edge within LEFT_TOL px, and its ink near the game's
//     (INK_TOL in RGB distance).
// The head's picture is left out of both (the site shows the item's 96 px thumbnail there).
// Side by side pictures (site | game) go to out/armory/shots/cards/<name>.png.
'use strict';
const fs = require('fs');
const path = require('path');
const http = require('http');
const chrome = require('./chrome');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const SITE = path.join(ROOT, 'web', 'site', 'armory');
const REFS = path.join(ROOT, 'tools', 'armory', 'prototype', 'cards_ref');
const FIX = path.join(__dirname, 'cards');
const OUT = path.join(ROOT, 'out', 'armory', 'shots', 'cards');
const HEIGHT_TOL = 6, LINE_TOL = 4, LEFT_TOL = 4, INK_TOL = 60;

const PAGE = `<!doctype html><meta charset=utf-8>
<link rel=stylesheet href="https://fonts.googleapis.com/css2?family=Cinzel:wght@500;700&family=Alegreya+Sans+SC:wght@700&family=Alegreya+Sans:wght@400;500;700&display=swap">
<link rel=stylesheet href="/armory/armory.css">
<style>body{margin:0;padding:20px;background:#3a3a3a}</style>
<link rel=icon href="data:,"><div id=card></div><script src="/armory/card.js"></script>
<script>
const BLANK = 'data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7';
window.draw = sheet => { document.getElementById('card').innerHTML = MUCard.render(sheet, BLANK); const r = document.querySelector('.gcard').getBoundingClientRect(); return { x: r.left, y: r.top, w: r.width, h: r.height }; };
// Both pictures measured alike: the seams, then each line of type as a band of bright rows.
const load = src => new Promise((ok, fail) => { const i = new Image(); i.onload = () => ok(i); i.onerror = fail; i.src = src; });
window.measure = async (src, art) => {
  const img = await load(src);
  const c = document.createElement('canvas'); c.width = img.width; c.height = img.height;
  const g = c.getContext('2d'); g.drawImage(img, 0, 0);
  const d = g.getImageData(0, 0, c.width, c.height).data, W = c.width, H = c.height;
  const px = (x, y) => { const i = (y * W + x) * 4; const a = d[i + 3] / 255; return [d[i] * a, d[i + 1] * a, d[i + 2] * a, d[i + 3]]; };
  const seam = (x, y) => { const i = (y * W + x) * 4; return d[i] === 0 && d[i + 1] === 0 && d[i + 2] === 0 && d[i + 3] === 255; };
  const rows = [], cols = [];
  for (let y = 0; y < H; y++) { let n = 0, m = 0; for (let x = Math.floor(W * .3); x < W * .7; x++) { m++; if (seam(x, y)) n++; } if (n / m > .9) rows.push(y); }
  for (let x = 0; x < W; x++) { let n = 0, m = 0; for (let y = Math.floor(H * .3); y < H * .7; y++) { m++; if (seam(x, y)) n++; } if (n / m > .9) cols.push(x); }
  if (!rows.length || !cols.length) return { error: 'no seams found' };
  const top = rows[0], bottom = rows[rows.length - 1], left = cols[0], right = cols[cols.length - 1];
  const bl = left + 2, br = right - 2, bt = top + 2, bb = bottom - 2;
  const picL = br - 6 - 96 * art - 3, picB = bt + 6 + 96 * art + 3;
  const lines = [];
  let cur = null, gapRun = 0;
  for (let y = bt + 3; y < bb - 2; y++) {
    let n = 0, wsum = 0, sum = [0, 0, 0], lx = 1e9;
    for (let x = bl + 4; x < br - 4; x++) {
      if (y < picB && x > picL) continue;
      const p = px(x, y);
      const hi = Math.max(p[0], p[1], p[2]);
      if (hi >= 100) {
        n++; if (x < lx) lx = x;
        // The ink is read from the words' first stretch, past the marks' gutter: a mark is drawn
        // larger in the game, and a line's right end may be another colour (the foot's wear).
        if (x - left >= 30 && x - left < 200) { const w = Math.pow(hi - 90, 4); wsum += w; sum[0] += p[0] * w; sum[1] += p[1] * w; sum[2] += p[2] * w; }
      }
    }
    if (n >= 2) {
      if (!cur) cur = { from: y, to: y, n: 0, w: 0, sum: [0, 0, 0], left: lx };
      cur.to = y; cur.n += n; cur.w += wsum; cur.sum[0] += sum[0]; cur.sum[1] += sum[1]; cur.sum[2] += sum[2]; cur.left = Math.min(cur.left, lx);
      gapRun = 0;
    } else if (cur) {
      if (++gapRun > 2) { lines.push(cur); cur = null; gapRun = 0; }
    }
  }
  if (cur) lines.push(cur);
  return { width: right - left, height: bottom - top,
    lines: lines.filter(l => l.to - l.from >= 4).map(l => ({ mid: (l.from + l.to) / 2 - top, left: l.left - left,
      ink: l.w ? l.sum.map(v => Math.round(v / l.w)) : null })) };
};
window.pair = async (a, b, file) => {
  const A = await load(a), B = await load(b);
  const c = document.createElement('canvas'); c.width = A.width + B.width + 30; c.height = Math.max(A.height, B.height);
  const g = c.getContext('2d'); g.fillStyle = '#3a3a3a'; g.fillRect(0, 0, c.width, c.height); g.drawImage(A, 0, 0); g.drawImage(B, A.width + 30, 0);
  return c.toDataURL('image/png').split(',')[1];
};
</script>`;

function serve() {
  const server = http.createServer((req, res) => {
    const url = decodeURIComponent(req.url.split('?')[0]);
    let file = null, type = 'text/html';
    if (url === '/') { res.writeHead(200, { 'Content-Type': type }); res.end(PAGE); return; }
    if (url.startsWith('/armory/')) file = path.join(SITE, path.basename(url));
    else if (url.startsWith('/ref/')) file = path.join(REFS, path.basename(url));
    if (!file || !fs.existsSync(file)) { res.writeHead(404); res.end(); return; }
    type = { '.css': 'text/css', '.js': 'text/javascript', '.png': 'image/png' }[path.extname(file)] || 'application/octet-stream';
    res.writeHead(200, { 'Content-Type': type });
    fs.createReadStream(file).pipe(res);
  });
  return new Promise(ok => server.listen(0, '127.0.0.1', () => ok(server)));
}

async function main() {
  const server = await serve();
  const base = `http://127.0.0.1:${server.address().port}`;
  const page = await chrome.open({ width: 460, height: 760 });
  let fails = 0, oks = 0;
  const ok = (cond, label, more) => { if (cond) oks++; else { fails++; console.log('FAIL', label, more !== undefined ? JSON.stringify(more) : ''); } };
  try {
    await page.go(base + '/', 800);
    for (const f of fs.readdirSync(FIX).filter(f => f.endsWith('.json')).sort()) {
      const fix = JSON.parse(fs.readFileSync(path.join(FIX, f), 'utf8'));
      const name = f.replace(/\.json$/, '');
      const box = await page.eval(`draw(${JSON.stringify(fix.sheet)})`);
      await page.eval('document.fonts.ready.then(() => 1)');
      const pad = 9;
      const shot = await page.shot(null, { clip: { x: box.x - pad, y: box.y - pad, width: box.w + 2 * pad, height: box.h + 2 * pad } });
      const art = fix.sheet.artScale || 1;
      const web = await page.eval(`measure('data:image/png;base64,${shot}', ${art})`);
      const game = await page.eval(`measure('/ref/${fix.ref}', ${art})`);
      const side = await page.eval(`pair('data:image/png;base64,${shot}', '/ref/${fix.ref}')`);
      fs.mkdirSync(OUT, { recursive: true });
      fs.writeFileSync(path.join(OUT, `${name}.png`), Buffer.from(side, 'base64'));
      if (web.error || game.error) { ok(false, `${name}: measured`, web.error || game.error); continue; }
      ok(web.width === game.width, `${name}: width between the seams`, { site: web.width, game: game.width });
      ok(Math.abs(web.height - game.height) <= HEIGHT_TOL, `${name}: height between the seams`, { site: web.height, game: game.height });
      ok(web.lines.length === game.lines.length, `${name}: lines of type`, { site: web.lines.length, game: game.lines.length,
        siteAt: web.lines.map(l => Math.round(l.mid)), gameAt: game.lines.map(l => Math.round(l.mid)) });
      const n = Math.min(web.lines.length, game.lines.length);
      for (let i = 0; i < n; i++) {
        const a = web.lines[i], b = game.lines[i];
        ok(Math.abs(a.mid - b.mid) <= LINE_TOL, `${name}: line ${i + 1} height`, { site: a.mid, game: b.mid });
        // A right-aligned line (the wear, the price) and the sell strip have a quiet label at its left that one side's
        // threshold may catch and the other's not: its left edge is not compared.
        if (a.left < 150 && b.left < 150 && !(fix.sheet.sell && i === n - 1)) ok(Math.abs(a.left - b.left) <= LEFT_TOL, `${name}: line ${i + 1} left edge`, { site: a.left, game: b.left });
        // The hue, not the brightness: the game's type is thinner than the browser's at these sizes.
        const hue = v => { const m = Math.max(1, ...v); return v.map(x => 255 * x / m); };
        if (!a.ink || !b.ink) continue;
        const A = hue(a.ink), B = hue(b.ink);
        const dist = Math.hypot(A[0] - B[0], A[1] - B[1], A[2] - B[2]);
        ok(dist <= INK_TOL, `${name}: line ${i + 1} ink`, { site: a.ink, game: b.ink, hueDistance: Math.round(dist) });
      }
      console.log(`${name}: ${web.width}x${web.height} (game ${game.width}x${game.height}), ${web.lines.length} lines (game ${game.lines.length})`);
    }
    ok(page.errors.length === 0, 'no console errors drawing the cards', page.errors);
  } finally {
    await page.close();
    server.close();
  }
  console.log(`card check ${fails ? 'FAILED' : 'passed'}: ${oks} ok, ${fails} failed (side by side in ${path.relative(ROOT, OUT)}/)`);
  process.exit(fails ? 1 : 0);
}

main().catch(e => { console.log('FAIL card check:', e.stack || e); process.exit(1); });
