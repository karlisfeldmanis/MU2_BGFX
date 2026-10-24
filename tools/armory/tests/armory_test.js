// The Armory's headless test, against a built release (tools/armory/build.py's out/armory/<commit>/).
//
//   node tools/armory/tests/armory_test.js [out/armory/<commit>]     (the newest build by default)
//
// Grown from the prototype's harness.js: it checks the output's contract (what docs/plans/armory.md
// promises a page), runs the design's page logic (tools/armory/prototype/check.js) on the built
// rows, and holds the spot checks the brief lists. A spot check whose numbers come from the dump is
// SKIPPED, said as such, while meta.json says the dump left that file out. Exit 1 on any failure.
'use strict';
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..', '..');
const ARMORY = path.join(ROOT, 'out', 'armory');
let dir = process.argv[2];
if (!dir) {
  const built = fs.readdirSync(ARMORY).filter(d => fs.existsSync(path.join(ARMORY, d, 'meta.json')));
  built.sort((a, b) => fs.statSync(path.join(ARMORY, b, 'meta.json')).mtimeMs - fs.statSync(path.join(ARMORY, a, 'meta.json')).mtimeMs);
  dir = built.length ? path.join(ARMORY, built[0]) : null;
}
if (!dir || !fs.existsSync(path.join(dir, 'meta.json'))) { console.log('FAIL no built Armory (./mu armory build first)'); process.exit(1); }

const J = f => JSON.parse(fs.readFileSync(path.join(dir, f), 'utf8'));
const has = f => fs.existsSync(path.join(dir, f));
let fails = 0, oks = 0, skips = 0;
const ok = (cond, label, more) => { if (cond) { oks++; } else { fails++; console.log('FAIL', label, more !== undefined ? JSON.stringify(more).slice(0, 300) : ''); } };
const skip = (label, why) => { skips++; console.log('SKIP', label, '--', why); };
const meta = J('meta.json');
const missing = new Set((meta.dump || {}).missing || []);
console.log(`Armory ${meta.commit} protocol ${meta.protocol} (${dir})` + (missing.size ? `; dump missing: ${[...missing].join(', ')}` : ''));

// ---- 1. every JSON file stamped with the release's commit and protocol
const walk = d => fs.readdirSync(path.join(dir, d), { withFileTypes: true }).flatMap(e =>
  e.isDirectory() ? walk(path.join(d, e.name)) : [path.join(d, e.name)]);
const files = walk('.');
let unstamped = [];
for (const f of files.filter(f => f.endsWith('.json'))) {
  const v = J(f);
  if (v.commit !== meta.commit || v.protocol !== meta.protocol) unstamped.push(f);
}
ok(unstamped.length === 0, 'every JSON carries the commit and protocol', unstamped.slice(0, 5));
ok(meta.protocol > 0 && /^[0-9a-f]{7,}$/.test(meta.commit), 'meta names a commit and a protocol', meta);

// ---- 2. no models are published; thumbnails instead
ok(!has('models') && !files.some(f => f.endsWith('.glb')), 'no models/ and no .glb in the output');

// ---- 3. index rows <-> entity files, slugs stable-shaped and unique
const kinds = fs.readdirSync(path.join(dir, 'index')).map(f => f.replace(/\.json$/, ''));
const bySlug = {};
for (const kind of kinds) {
  const rows = J(`index/${kind}.json`).rows;
  const slugs = new Set();
  rows.forEach(r => {
    ok(/^[a-z0-9]+(-[a-z0-9]+)*$/.test(r.slug || ''), `${kind}: slug shape`, r.slug);
    ok(!slugs.has(r.slug), `${kind}: slug unique`, r.slug);
    slugs.add(r.slug);
    ok(has(`${kind}/${r.slug}.json`), `${kind}: row has its page`, r.slug);
  });
  const pages = fs.existsSync(path.join(dir, kind)) ? fs.readdirSync(path.join(dir, kind)).map(f => f.replace(/\.json$/, '')) : [];
  ok(pages.length === rows.length, `${kind}: a page for every row and a row for every page`, [pages.length, rows.length]);
  bySlug[kind] = slugs;
  ok(fs.statSync(path.join(dir, 'index', `${kind}.json`)).size < 1.5e6, `${kind}: the list is small enough to load`);
}

// ---- 4. a page needs only itself: small, and every link it carries resolves
const big = files.filter(f => /^(items|monsters|npcs|worlds|quests|skills)\//.test(f) && fs.statSync(path.join(dir, f)).size > 400e3);
ok(big.length === 0, 'no entity page over 400 KB', big);
const linkKind = { monster_slug: 'monsters', world_slug: 'worlds' };
const linksOf = (v, kind, out) => {
  if (Array.isArray(v)) v.forEach(x => linksOf(x, kind, out));
  else if (v && typeof v === 'object') {
    for (const [k, x] of Object.entries(v)) {
      if (linkKind[k] && typeof x === 'string') out.push([linkKind[k], x]);
      else if (k === 'slug' && typeof x === 'string' && v.kind && bySlug[v.kind]) out.push([v.kind, x]);
      else linksOf(x, kind, out);
    }
  }
  return out;
};
for (const kind of ['items', 'monsters', 'npcs', 'worlds']) {
  if (!bySlug[kind]) continue;
  for (const s of bySlug[kind]) {
    const e = J(`${kind}/${s}.json`);
    for (const [k, x] of linksOf(e, kind, [])) ok(bySlug[k] && bySlug[k].has(x), `${kind}/${s}: link to ${k}/${x}`);
    if (kind === 'items' && Array.isArray(e.drops)) e.drops.forEach(d => ok(bySlug.monsters.has(d.monster_slug), `${s}: drop source exists`, d));
    if (kind === 'monsters') (e.drops || []).forEach(p => (p.items || []).forEach(i => ok(bySlug.items.has(i.slug), `${s}: dropped item exists`, i)));
    if (kind === 'worlds') (e.monsters || []).forEach(m => ok(bySlug.monsters.has(m.slug), `${s}: nest monster exists`, m));
    for (const pic of ['thumb', 'elite_thumb']) if (e[pic]) for (const f of [e[pic].page, e[pic].card]) ok(has(f), `${kind}/${s}: ${pic} file`, f);
    if (e.chart) ok(has(e.chart.png), `${kind}/${s}: chart file`, e.chart.png);
    if (e.card) ok(has(e.card), `${kind}/${s}: card file`, e.card);
  }
}

// ---- 5. pictures are what they say
const head = (f, n) => { const b = Buffer.alloc(n); const fd = fs.openSync(path.join(dir, f), 'r'); fs.readSync(fd, b, 0, n, 0); fs.closeSync(fd); return b; };
const webps = files.filter(f => f.endsWith('.webp'));
webps.forEach(f => { const b = head(f, 12); ok(b.toString('ascii', 0, 4) === 'RIFF' && b.toString('ascii', 8, 12) === 'WEBP', `webp: ${f}`); });
files.filter(f => f.endsWith('.png')).forEach(f => ok(head(f, 8).toString('hex') === '89504e470d0a1a0a', `png: ${f}`));
const thumbs = webps.filter(f => f.startsWith('thumbs/') && !f.endsWith('.96.webp'));
thumbs.forEach(f => ok(webps.includes(f.replace(/\.webp$/, '.96.webp')), `${f}: its 96 px card picture`));
if (!thumbs.length) skip('thumbnails', 'this build has none (--no-thumbs)');

// ---- 6. search covers every row
const search = J('search.json').rows;
const inSearch = new Set(search.map(r => r.s + '|' + r.n));
for (const kind of ['items', 'monsters', 'npcs', 'worlds']) {
  if (!bySlug[kind]) continue;
  J(`index/${kind}.json`).rows.forEach(r => ok(inSearch.has(r.slug + '|' + r.n), `search has ${kind}/${r.slug}`));
}

// ---- 7. the design's page logic (prototype check.js) on the built rows
(function pageLogic() {
  const proto = path.join(__dirname, '..', 'prototype', 'check.js');
  if (!fs.existsSync(proto)) { skip('page logic', 'no prototype check.js'); return; }
  global.window = { devicePixelRatio: 1 }; global.THREE = undefined;
  global.fetch = () => new Promise(() => {});
  eval(fs.readFileSync(proto, 'utf8') + '\n;global.Component=Component;');
  const c = new global.Component({}); c.props = {};
  c.setState = function (p) { Object.assign(this.state, typeof p === 'function' ? p(this.state) : p); };
  // The design's rows carried two fields the build leaves to history.py and the dump: when it was
  // added and what it affords. Filled neutral here, so the list logic runs on the real rows.
  const items = J('index/items.json').rows.map(r => Object.assign({ aff: [], add: 'base' }, r));
  const monsters = J('index/monsters.json').rows.map(r => ({ k: r.k, n: r.n, num: r.num, lv: r.lv, hp: r.hp, dmg: r.dmg, def: r.def,
    home: r.home, where: (r.maps || []).map(m => ({ map: m, count: 0 })) }));
  Object.assign(c.state, { items, monsters, npcs: [], quests: [], src: {}, sysA: null, sysB: null, icons: null, cards: {}, loading: false, modelIdx: { npcs: {} } });
  const run = (label, st, check) => {
    Object.assign(c.state, st);
    try { const v = c.renderVals(); ok(!check || check(v), `page logic: ${label}`); }
    catch (e) { ok(false, `page logic: ${label}`, e.stack.split('\n').slice(0, 2).join(' | ')); }
  };
  run('items list', { view: 'list', browse: 'items', cat: 'All', sub: '', page: 1 }, v => v.rows && v.rows.length > 0);
  run('items list, swords', { view: 'list', browse: 'items', cat: 'Weapons', sub: 'Swords', page: 1 }, v => v.rows && v.rows.length > 0);
  const sword = items.find(i => i.n === 'Sword of Destruction');
  if (sword) run('item page', { view: 'item', sel: sword.k, tab: '' });
  run('monsters list', { view: 'list', browse: 'monsters', monMap: '' });
})();

// ---- 8. the brief's spot checks
const itemNamed = n => { const r = J('index/items.json').rows.find(x => x.n === n); return r ? J(`items/${r.slug}.json`) : null; };
const sod = itemNamed('Sword of Destruction');
ok(sod && sod.stats.drop_level > 0 && sod.stats.minimum_damage > 0, 'Sword of Destruction: its row is read from content/items', sod && sod.stats);
if (missing.has('cards')) skip('Sword of Destruction +11 excellent: 148 ~ 156, Strength 692, Agility 272, sells 604,100', 'waiting on the dump (cards)');
else {
  const t = JSON.stringify(sod && has(sod.card || '') ? J(sod.card) : {});
  ok(/148\s*~\s*156/.test(t) && t.includes('692') && t.includes('272') && /604,?100/.test(t), 'Sword of Destruction +11 excellent card', t.slice(0, 200));
}
const near = (x, want, tol) => x > 0 && Math.abs(1 / x - want) / want <= tol;
if (missing.has('drops')) {
  skip('Sword of Destruction from a Blood Castle 4 Red Skeleton Knight, ~1 in 263', 'waiting on the dump (drops)');
  skip("Loch's Feather in Tarkan, ~1 in 520", 'waiting on the dump (drops)');
} else {
  const bc4 = (sod.drops || []).filter(d => d.monster_name === 'Red Skeleton Knight' && /4/.test(JSON.stringify(d)) && /castle/i.test(JSON.stringify(d)));
  ok(bc4.some(d => near(d.chance, 263, 0.05)), 'Sword of Destruction, Blood Castle 4 Red Skeleton Knight ~1/263', bc4.map(d => d.chance));
  const loch = itemNamed("Loch's Feather");
  const tk = loch ? (loch.drops || []).filter(d => /tarkan/i.test(JSON.stringify(d))) : [];
  ok(tk.some(d => near(d.chance, 520, 0.08)), "Loch's Feather in Tarkan ~1/520", tk.map(d => d.chance));
}
if (missing.has('talents')) skip('Talents at level 400: 79, +80 a rebirth (MG 111/+112)', 'waiting on the dump (talents)');
else {
  const t = kinds.includes('talents') ? JSON.stringify(J('index/talents.json').rows.map(r => J(`talents/${r.slug}.json`))) : '';
  ok(/\b79\b/.test(t) && /\b111\b/.test(t), 'talent points at 400 (79; MG 111)');
}
if (missing.has('rules')) skip('Lorencia Elite x16 life, x2.5 damage', 'waiting on the dump (rules)');
else {
  const e = kinds.includes('elites') ? JSON.stringify(J('index/elites.json').rows.map(r => J(`elites/${r.slug}.json`)).filter(x => /lorencia/i.test(JSON.stringify(x)))) : '';
  ok(/\b16(\.0)?\b/.test(e) && /2\.5/.test(e), 'Lorencia Elite x16 life x2.5 damage', e.slice(0, 200));
}

console.log(`${fails ? 'FAILED' : 'passed'}: ${oks} ok, ${fails} failed, ${skips} skipped`);
process.exit(fails ? 1 : 0);
