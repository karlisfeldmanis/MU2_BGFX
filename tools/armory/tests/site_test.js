// The Armory's pages in headless Chrome, against web/server.py serving a built release.
//
//   node tools/armory/tests/site_test.js [out/armory/<commit>] [--port 8091] [--shots]
//
// Starts web/server.py on a private port (a scratch accounts database, never the box's), then for
// every list the release has and a few entity pages of each kind: the page answers 200 with its own
// head (title, description, og:image), draws its view with no console error and no failed request,
// and shows what it should (rows in a list, the game's card on an item). Comments are tried end to
// end with a scratch account: post, vote, report, delete, the rate limit. With --shots, pictures of
// an item, a monster and the talent calculator at desktop and phone width go to out/armory/shots/.
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');
const chrome = require('./chrome');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const args = process.argv.slice(2);
const flag = (n) => args.includes(n);
const opt = (n, d) => { const i = args.indexOf(n); return i >= 0 ? args[i + 1] : d; };
const PORT = +opt('--port', 8000 + Math.floor(Math.random() * 900) + 100);
let dir = args.find(a => !a.startsWith('--') && a !== String(PORT));
if (!dir) {
  const A = path.join(ROOT, 'out', 'armory');
  const built = fs.readdirSync(A).filter(d => fs.existsSync(path.join(A, d, 'meta.json')));
  built.sort((a, b) => fs.statSync(path.join(A, b, 'meta.json')).mtimeMs - fs.statSync(path.join(A, a, 'meta.json')).mtimeMs);
  dir = built.length ? path.join(A, built[0]) : null;
}
if (!dir || !fs.existsSync(path.join(dir, 'meta.json'))) { console.log('FAIL no built Armory (./mu armory build first)'); process.exit(1); }
const SHOTS = path.join(ROOT, 'out', 'armory', 'shots');
const base = `http://127.0.0.1:${PORT}`;
let fails = 0, oks = 0;
const ok = (cond, label, more) => { if (cond) oks++; else { fails++; console.log('FAIL', label, more !== undefined ? JSON.stringify(more).slice(0, 400) : ''); } };
const J = (f) => JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8'));

async function server() {
  const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'armory-web-'));
  const p = spawn('nice', ['-n', '10', 'python3', '-I', path.join(ROOT, 'web', 'server.py'), '--db', path.join(scratch, 'accounts.db'),
    '--port', String(PORT), '--armory', dir], { stdio: ['ignore', 'pipe', 'pipe'] });
  let log = '';
  p.stdout.on('data', d => { log += d; });
  p.stderr.on('data', d => { log += d; });
  for (let i = 0; i < 100; i++) {
    try { const r = await fetch(base + '/armory/sitemap.xml'); if (r.ok) return { p, scratch, log: () => log }; } catch (e) { /* not yet */ }
    await chrome.sleep(100);
  }
  throw new Error('web/server.py did not start: ' + log);
}

async function api(pathname, body, token) {
  const r = await fetch(base + pathname, { method: body ? 'POST' : 'GET', headers: Object.assign({ 'Content-Type': 'application/json' }, token ? { Authorization: 'Bearer ' + token } : {}),
    body: body ? JSON.stringify(body) : undefined });
  return { status: r.status, said: await r.json().catch(() => ({})) };
}

async function main() {
  const meta = J('meta.json');
  const kinds = fs.readdirSync(path.join(dir, 'index')).map(f => f.replace(/\.json$/, ''));
  const ONE = { items: 'item', monsters: 'monster', npcs: 'npc', quests: 'quest', events: 'event', runes: 'rune', skills: 'skill', talents: 'talents', mixes: 'mix', elites: 'elite', worlds: 'world', boxes: 'box' };
  const srv = await server();
  const page = await chrome.open({ width: 1440, height: 1000 });
  const failed = [];
  page.send('Network.enable').catch(() => {});
  try {
    // ---- the served head, without the script
    const sod = J('index/items.json').rows.find(r => r.n === 'Sword of Destruction') || J('index/items.json').rows[0];
    const html = await (await fetch(`${base}/armory/item/${sod.slug}`)).text();
    ok(html.includes(`<title>${sod.n} — MU Classic Armory</title>`), 'item page: its own title', html.slice(0, 200));
    ok(/<meta property="og:image" content="https:\/\/mu-classic\.com\/armory\/data\/[0-9a-f]+\/thumbs\/[0-9a-f]+\.webp">/.test(html) || !sod.t, 'item page: og:image is its thumbnail');
    ok(/<meta name="description" content="[^"]{30,}">/.test(html), 'item page: a description');
    ok(html.includes(`<h1 class="h1 big">${sod.n}</h1>`), 'item page: its name in the served body');
    ok(html.includes('"route":{"view":"entity","kind":"items"'), 'item page: its entity inlined');
    ok((await fetch(`${base}/armory/item/no-such-thing`)).status === 404, 'a missing entity is 404');
    const data = await fetch(`${base}/armory/data/${meta.commit}/index/items.json`);
    ok(data.ok && /immutable/.test(data.headers.get('cache-control') || ''), 'release files are immutable');
    ok((await fetch(`${base}/armory/data/${meta.commit}/../meta.json`)).status === 404, 'no path out of the release');
    const sm = await (await fetch(`${base}/armory/sitemap.xml`)).text();
    ok(sm.includes(`/armory/item/${sod.slug}<`), 'the sitemap lists the item');
    const pt = await fetch(`${base}/pt/armory/item/${sod.slug}`);
    ok(pt.ok && (await pt.text()).includes('<html lang="pt-BR">'), 'the page in Portuguese');
    const r = await fetch(`${base}/armory`, { headers: { 'Accept-Language': 'ru,en;q=0.5' }, redirect: 'manual' });
    ok(r.status === 302 && r.headers.get('location') === '/ru/armory', 'a Russian browser is sent to /ru/armory', r.headers.get('location'));

    // ---- every list and a few pages of each kind, drawn in the browser
    const visit = async (url, check, label) => {
      await page.go(base + url, 500);
      for (let i = 0; i < 40; i++) { const busy = await page.eval(`!!document.querySelector('#aview .loading')`); if (!busy) break; await chrome.sleep(150); }
      const errs = page.errors.filter(e => !/nostalgic\.gg/.test(e));
      ok(errs.length === 0, `${label}: no console errors`, errs);
      const got = await page.eval(`(${check.toString()})()`).catch(e => 'threw ' + e.message);
      ok(got === true, `${label}: draws`, got);
    };
    for (const kind of kinds) {
      await visit(`/armory/${kind === 'mixes' ? 'mixes' : kind}`, () => document.querySelectorAll('#aview tbody tr, #aview article, #aview .tcol').length > 0 || document.querySelector('#aview .absent') !== null, `list ${kind}`);
      const rows = J(`index/${kind}.json`).rows;
      const pick = kind === 'items' ? [sod].concat(rows.filter(x => x.src && x.src.includes('c')).slice(0, 1), rows.filter(x => x.live === false).slice(0, 1)) : rows.slice(0, 2);
      for (const row of pick) {
        await visit(`/armory/${ONE[kind]}/${row.slug}`, () => !!document.querySelector('#aview h1, #aview .tcol') && !document.querySelector('#aview .loading'), `${kind}/${row.slug}`);
      }
    }
    // The item page: the game's card, its tabs, a hover card in the list.
    await visit(`/armory/item/${sod.slug}`, () => !!document.querySelector('.side .gcard .gtitle') && document.querySelectorAll('.tab').length >= 6, 'item page: the game card and tabs');
    await visit(`/armory/item/${sod.slug}?tab=drops`, () => document.querySelectorAll('#aview tbody tr').length > 0, 'item page: dropped by');
    await visit(`/armory`, () => document.querySelectorAll('#aview tbody tr').length === 25, 'items list: a page of 25');
    await page.eval(`document.querySelector('.nm[data-card]').dispatchEvent(new MouseEvent('mouseover', { bubbles: true }))`);
    await chrome.sleep(900);
    ok(await page.eval(`!!document.querySelector('.nm[data-card] .pop .gcard')`), 'items list: the hover card is the game\'s');
    await visit(`/armory?cat=Weapons&sub=Swords&sort=lv&dir=-1`, () => [...document.querySelectorAll('#aview tbody tr td:nth-child(3)')].every(td => td.textContent === 'Swords'), 'items list: a category');
    // Following a link stays in the page (no reload) and changes the head's title.
    await visit('/armory', () => document.querySelectorAll('#aview a.open').length > 0, 'items list again');
    await page.eval(`window.__kept = 1; document.querySelector('#aview a.open').click()`);
    await chrome.sleep(900);
    ok(await page.eval('window.__kept === 1 && /MU Classic Armory/.test(document.title) && location.pathname.startsWith("/armory/item/")'), 'a link is followed without a reload');
    // Search.
    await page.go(base + '/armory', 400);
    await page.eval(`(() => { const i = document.getElementById('aq'); i.focus(); i.value = 'sword of d'; i.dispatchEvent(new Event('input')); })()`);
    await chrome.sleep(900);
    await page.eval(`(() => { const i = document.getElementById('aq'); i.dispatchEvent(new Event('input')); })()`);
    await chrome.sleep(300);
    ok(await page.eval(`[...document.querySelectorAll('#ahits a')].some(a => a.textContent.includes('Sword of Destruction'))`), 'search finds the sword');
    // The talent calculator: points from the dump, a point taken and the link keeps it.
    if (kinds.includes('talents')) {
      await visit('/armory/talents/dk?lv=400', () => document.querySelectorAll('.tcol').length >= 4, 'talents: columns');
      const left = await page.eval(`document.querySelector('.tbar b.num:last-of-type') && [...document.querySelectorAll('.tbar b.num')].pop().textContent`);
      const want = (J('talents/dk.json').points || {})['400'];
      ok(!want || String(want[0]) === String(left), 'talents: DK at 400 has the dump\'s points', { left, want: want && want[0] });
      await page.eval(`document.querySelector('.tcol a.tb[href]:not([aria-disabled])') && [...document.querySelectorAll('.tcol a.tb[href]')].find(a => a.textContent === '+').click()`);
      await chrome.sleep(500);
      ok(/[?&]p=1/.test(await page.eval('location.search')), 'talents: a point taken is in the address', await page.eval('location.search'));
    }

    // ---- comments, end to end, with a scratch account
    const email = `t${Date.now()}@example.com`;
    await api('/api/register', { email, password: 'correct horse battery', name: `T${String(Date.now()).slice(-8)}` });
    const login = await api('/api/login', { email, password: 'correct horse battery' });
    const tok = login.said.token;
    ok(!!tok, 'scratch account signs in', login);
    const other = `u${Date.now()}@example.com`;
    await api('/api/register', { email: other, password: 'correct horse battery', name: `U${String(Date.now()).slice(-8)}` });
    const tok2 = (await api('/api/login', { email: other, password: 'correct horse battery' })).said.token;
    const where = { kind: 'items', slug: sod.slug };
    ok((await api('/api/armory/comments', { ...where, text: 'hi' })).status === 401, 'comments: signed out is refused');
    ok((await api('/api/armory/comments', { kind: 'items', slug: 'nope', text: 'x' }, tok)).status === 400, 'comments: an unknown page is refused');
    ok((await api('/api/armory/comments', { ...where, about: 'deadbeef', text: 'x' }, tok)).status === 400, 'comments: an unknown update is refused');
    const hist = fs.existsSync(path.join(dir, 'history', 'items', `${sod.slug}.json`)) ? J(`history/items/${sod.slug}.json`).releases || [] : [];
    if (hist.length) ok((await api('/api/armory/comments', { ...where, about: hist[0].commit, text: 'about the update' }, tok)).status === 201, 'comments: on an update');
    const posted = await api('/api/armory/comments', { ...where, text: 'Dropped for me in Blood Castle 4.' }, tok);
    ok(posted.status === 201 && posted.said.id, 'comments: posted', posted);
    const v = await api('/api/armory/vote', { id: posted.said.id }, tok2);
    ok(v.status === 200 && v.said.votes === 1 && v.said.mine, 'comments: an upvote', v);
    ok((await api('/api/armory/vote', { id: posted.said.id }, tok)).status === 400, 'comments: no vote on your own');
    ok((await api('/api/armory/report', { id: posted.said.id, why: 'test' }, tok2)).status === 200, 'comments: reported');
    const list = await api(`/api/armory/comments?kind=items&slug=${sod.slug}`, null, tok2);
    const mine = (list.said.comments || []).find(c => c.id === posted.said.id);
    ok(mine && mine.votes === 1 && mine.mine && mine.reported && !mine.own, 'comments: listed with votes and the report', mine);
    ok((await api('/api/armory/delete', { id: posted.said.id }, tok2)).status === 404, 'comments: someone else\'s is not deleted');
    ok((await api('/api/armory/reports', null, tok)).status === 404, 'comments: reports are an admin\'s');
    // The page draws it.
    await page.go(`${base}/armory/item/${sod.slug}?tab=comments`, 300);
    await page.eval(`localStorage.setItem('mu-token', '${tok2}')`);
    await page.go(`${base}/armory/item/${sod.slug}?tab=comments`, 1200);
    ok(await page.eval(`[...document.querySelectorAll('.cm p')].some(p => p.textContent.includes('Blood Castle 4'))`), 'comments: drawn on the page');
    ok(await page.eval(`!!document.querySelector('#cmt')`), 'comments: the box for a signed-in account');
    ok((await api('/api/armory/delete', { id: posted.said.id }, tok)).status === 200, 'comments: its writer deletes it');
    let limited = false;
    for (let i = 0; i < 8; i++) { const r2 = await api('/api/armory/comments', { ...where, text: 'flood ' + i }, tok2); if (r2.status === 429) limited = true; }
    ok(limited, 'comments: the rate limit holds');
    await page.eval(`localStorage.removeItem('mu-token')`);

    // ---- pictures
    if (flag('--shots')) {
      fs.mkdirSync(SHOTS, { recursive: true });
      const mon = J('index/monsters.json').rows.find(m => /red-skeleton-knight/.test(m.slug)) || J('index/monsters.json').rows[0];
      const pages = [['item', `/armory/item/${sod.slug}?state=${encodeURIComponent('+11 excellent (bits 0 and 3) 3 sockets')}`], ['item-drops', `/armory/item/${sod.slug}?tab=drops`],
        ['items', '/armory'], ['monster', `/armory/monster/${mon.slug}`], ['talents', '/armory/talents/dk?lv=400&p=aa1a'], ['event', '/armory/event/blood-castle-4'],
        ['npc', '/armory/npc/' + (J('index/npcs.json').rows.find(n => n.role === 'shop') || J('index/npcs.json').rows[0]).slug]];
      for (const [w, h, tag] of [[1440, 1000, 'desktop'], [390, 844, 'phone']]) {
        const shotPage = await chrome.open({ width: w, height: h, mobile: tag === 'phone', scale: tag === 'phone' ? 2 : 1 });
        for (const [name, url] of pages) {
          await shotPage.go(base + url, 1500);
          await shotPage.shot(path.join(SHOTS, `${name}-${tag}.png`), { full: true });
          const errs = shotPage.errors.filter(e => !/nostalgic\.gg/.test(e));
          ok(errs.length === 0, `${name} (${tag}): no console errors`, errs);
          const wide = await shotPage.eval('document.documentElement.scrollWidth <= window.innerWidth + 1');
          ok(wide, `${name} (${tag}): no sideways scroll`, await shotPage.eval('[document.documentElement.scrollWidth, window.innerWidth]'));
        }
        await shotPage.close();
      }
      console.log(`shots in ${path.relative(ROOT, SHOTS)}/`);
    }
  } finally {
    await page.close();
    srv.p.kill();
    fs.rmSync(srv.scratch, { recursive: true, force: true });
  }
  console.log(`site ${fails ? 'FAILED' : 'passed'}: ${oks} ok, ${fails} failed`);
  process.exit(fails ? 1 : 0);
}

main().catch(e => { console.log('FAIL site test:', e.stack || e); process.exit(1); });
