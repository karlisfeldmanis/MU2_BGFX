// The Armory's pages (web/armory.py serves them): one script draws every list and entity page from
// the release's files under /armory/data/<commit>/ (tools/armory/build.py), as the design canvas
// tools/armory/prototype/design/Main.dc.html lays them out. A page fetches its own entity file
// (or has it inline) and the small indexes it needs, never the whole data set. Links are real
// URLs; inside the Armory they are followed without a reload. Words go through t() (the site's
// i18n, web/i18n.py); the game's names stay English.
(() => {
  'use strict';
  const B = JSON.parse(document.getElementById('armory-boot').textContent);
  const MAIN = document.getElementById('armory');
  const P = B.prefix || '';
  const t = (s, fill) => (window.MU && MU.t ? MU.t(s, fill) : s);
  const esc = MUCard.esc;
  const N = (n) => (n == null || n === '' ? '—' : typeof n === 'number' ? n.toLocaleString('en') : esc(n));
  const missing = new Set(B.missing || []);

  // ---- data: the release's files, each fetched once
  const files = {};
  const waiting = {};
  function have(rel, quiet) {
    if (rel in files) return files[rel];
    if (!waiting[rel] && B.data) {
      waiting[rel] = fetch(B.data + rel).then(r => (r.ok ? r.json() : null)).catch(() => null)
        .then(v => { files[rel] = v; delete waiting[rel]; if (!quiet) draw(); });
    }
    return undefined;
  }
  const loading = (...vals) => vals.some(v => v === undefined);
  if (B.entity && B.route && B.route.view === 'entity') files[`${B.route.kind}/${B.route.slug}.json`] = B.entity;
  if (B.card && B.entity && B.entity.card) files[B.entity.card] = B.card;
  const pic = (p) => (p ? B.data + p : '');

  // ---- names the pages say
  const LIST = { items: t('Items'), monsters: t('Monsters'), npcs: t('NPCs'), quests: t('Quests'), events: t('Events'), runes: t('Runes'),
    skills: t('Skills'), talents: t('Talent calculator'), mixes: t('Chaos Machine'), elites: t('Elite monsters'), worlds: t('Maps'), boxes: t('Boxes') };
  const BROWSE = { talents: t('Talents') };
  const ONE = { items: 'item', monsters: 'monster', npcs: 'npc', quests: 'quest', events: 'event', runes: 'rune', skills: 'skill',
    talents: 'talents', mixes: 'mix', elites: 'elite', worlds: 'world', boxes: 'box' };
  const KIND_OF = Object.fromEntries(Object.entries(ONE).map(([k, v]) => [v, k]));
  const CLS = { DK: 'Dark Knight', DW: 'Dark Wizard', FE: 'Fairy Elf', Elf: 'Fairy Elf', MG: 'Magic Gladiator' };
  const SRC = { d: [t('Drop'), t('Dropped by monsters')], s: [t('Shop'), t('Sold by a townsperson')], c: [t('Craft'), t('Made in the Chaos Machine')],
    q: [t('Quest'), t('A quest reward')], e: [t('Event'), t('An event or raid')], b: [t('Box'), t('Found in a box')] };
  const ORDER = ['Weapons', 'Armour', 'Shields', 'Wings', 'Jewellery', 'Pets', 'Other'];
  const RARITY_TONE = { common: 'White', rare: 'Rare', epic: 'Epic', legendary: 'RuneLegendary', ancient: 'RuneAncient' };
  const href = (kind, slug) => `${P}/armory/${ONE[kind] || kind}/${slug}`;
  const listHref = (kind, q) => `${P}/armory${kind === 'items' && !q ? '' : '/' + kind}${q ? '?' + new URLSearchParams(q) : ''}`;
  const a = (kind, slug, text, cls) => (slug ? `<a class="${cls || 'open'}" href="${href(kind, slug)}">${esc(text)}</a>` : `<span>${esc(text)}</span>`);
  const pretty = (k) => { k = String(k).replace(/_/g, ' ').replace(/([a-z])([A-Z])/g, '$1 $2').toLowerCase(); return k.charAt(0).toUpperCase() + k.slice(1); };
  const pct = (x) => (x == null ? '—' : (x * 100 >= 10 ? (x * 100).toFixed(0) : (x * 100).toFixed(1)) + '%');
  // A per-kill chance as players say it: "1 in 263", the percent beside.
  function odds(c) {
    if (!c) return { one: '—', pct: '' };
    if (c >= 0.999) return { one: t('Always'), pct: '' };
    const p = c * 100;
    const ps = p >= 10 ? p.toFixed(0) : p >= 1 ? p.toFixed(1) : p >= 0.01 ? p.toFixed(2) : p.toPrecision(2);
    return c >= 0.2 ? { one: p.toFixed(0) + '%', pct: '' } : { one: t('1 in {n}', { n: Math.round(1 / c).toLocaleString('en') }), pct: ps + '%' };
  }
  const oddsCell = (c) => { const o = odds(c); return `<b class="num" style="font-weight:500">${esc(o.one)}</b>${o.pct ? `<span class="sub2">${esc(o.pct)}</span>` : ''}`; };
  const ROLL = { item: t('Plain'), excellent: t('Excellent'), elite_piece: t('Elite spoils'), elite_excellent: t('Elite spoils, excellent'), rune: t('Rune'),
    jewel: t('Jewel'), jewel_group: t('Jewel'), first_wings: t('Your class only'), scroll: t('Ticket'), bone: t('Ticket'), eye: t('Ticket'), key: t('Ticket'),
    hoard: t('Hoard'), minion: t('Spoils'), while_quest: t('While its quest asks') };
  const rollWords = (rolls) => [...new Set((rolls || []).map(r => ROLL[r.roll] || pretty(r.roll || '')))].filter(Boolean).join(' · ');
  const absent = (what) => `<div class="absent">${esc(what)}</div>`;
  const waitDump = (name) => absent(missing.has(name) ? t('Waiting on the game\'s data for this release.') : t('Nothing here in this release.'));
  const ld = `<p class="loading">${esc(t('Loading…'))}</p>`;

  // ---- the address: route and its query
  function routeOf() {
    let path = location.pathname;
    if (P && path.startsWith(P + '/')) path = path.slice(P.length);
    const parts = path.split('/').filter(Boolean).slice(1);
    const q = Object.fromEntries(new URLSearchParams(location.search));
    if (!parts.length) return { view: 'list', kind: 'items', q };
    if (parts.length === 1) return { view: 'list', kind: parts[0] === 'chaos-machine' ? 'mixes' : parts[0], q };
    return { view: 'entity', kind: KIND_OF[parts[0]] || parts[0], slug: parts[1], q };
  }
  let R = routeOf();
  function go(url, replace) {
    history[replace ? 'replaceState' : 'pushState'](null, '', url);
    const was = R;
    R = routeOf();
    draw();
    if (!replace && (was.kind !== R.kind || was.slug !== R.slug || was.view !== R.view)) window.scrollTo(0, 0);
  }
  const setQ = (changes, replace) => {
    const q = Object.assign({}, R.q, changes);
    Object.keys(q).forEach(k => { if (q[k] === '' || q[k] == null) delete q[k]; });
    const s = new URLSearchParams(q).toString();
    go(location.pathname + (s ? '?' + s : ''), replace);
  };
  const qLink = (changes) => {
    const q = Object.assign({}, R.q, changes);
    Object.keys(q).forEach(k => { if (q[k] === '' || q[k] == null) delete q[k]; });
    const s = new URLSearchParams(q).toString();
    return location.pathname + (s ? '?' + s : '');
  };
  const chip = (label, on, changes, extra) => `<a class="chip" href="${esc(qLink(changes))}" data-q aria-pressed="${on ? 'true' : 'false'}"${extra || ''}>${label}</a>`;

  // ---- the frame: trail and search stay; the view is drawn under them
  MAIN.innerHTML = `<div class="awrap"><div class="ahead"><nav aria-label="${esc(t('Breadcrumb'))}"><ol class="crumbs" id="acrumbs"></ol></nav>
<div class="asearch" role="search"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true"><circle cx="11" cy="11" r="7"></circle><path d="M20 20l-4-4"></path></svg>
<label for="aq" class="sr">${esc(t('Search the Armory'))}</label><input id="aq" type="search" autocomplete="off" placeholder="${esc(t('Search items, monsters, NPCs, quests, runes, skills'))}" role="combobox" aria-expanded="false" aria-controls="ahits"><div class="hits" id="ahits" role="listbox" hidden></div></div></div>
<div id="aview"></div></div>`;
  const VIEW = document.getElementById('aview');
  const CRUMBS = document.getElementById('acrumbs');

  function crumbs(list) {
    CRUMBS.innerHTML = [`<li><a class="crumb" href="${P}/armory">${esc(t('Armory'))}</a></li>`].concat(list.map((c, i) =>
      i === list.length - 1 || !c[1] ? `<li><span class="here" aria-current="page">${esc(c[0])}</span></li>` : `<li><a class="crumb" href="${esc(c[1])}">${esc(c[0])}</a></li>`)).join('');
  }

  // ---- the rail: what there is to browse, and a list's filters
  function rail(extra) {
    const kinds = (B.kinds || []).filter(k => k !== 'talents' && k !== 'boxes' && k !== 'worlds');
    const browse = kinds.map(k => `<a class="ci" href="${listHref(k)}" aria-current="${R.kind === k ? 'true' : 'false'}">${esc(LIST[k])}<span>${N(B.counts[k])}</span></a>`);
    if ((B.kinds || []).includes('talents')) browse.push(`<a class="ci" href="${listHref('talents')}" aria-current="${R.kind === 'talents' ? 'true' : 'false'}">${esc(BROWSE.talents)}<span></span></a>`);
    ['worlds', 'boxes'].forEach(k => { if ((B.kinds || []).includes(k)) browse.push(`<a class="ci" href="${listHref(k)}" aria-current="${R.kind === k ? 'true' : 'false'}">${esc(LIST[k])}<span>${N(B.counts[k])}</span></a>`); });
    return `<aside class="rail" aria-label="${esc(t('Browse'))}"><div><h2 class="rh">${esc(t('Browse'))}</h2>${browse.join('')}</div>${extra || ''}</aside>`;
  }
  const titled = (title, count) => `<div class="ttl"><h1>${esc(title)}</h1><span class="mute" style="font-size:15px">${esc(count || '')}</span></div>`;
  const listPage = (title, count, body, extra) => `<div class="cols">${rail(extra)}<section class="main">${titled(title, count)}${body}</section></div>`;
  const table = (head, rows, cls) => `<div class="tbox${cls && cls.includes('pops') ? ' pops' : ''}"><table class="tbl ${cls || ''}"><thead><tr>${head.map(h => `<th scope="col" class="plain${h[1] ? ' r' : ''}">${esc(h[0])}</th>`).join('')}</tr></thead><tbody>${rows.join('')}</tbody></table></div>`;
  const kv = (pairs) => pairs.filter(p => p && p[1] != null && p[1] !== '').map(p => `<div class="kv"><span>${esc(p[0])}</span><b>${p[2] ? p[1] : esc(p[1])}</b></div>`).join('');
  const facts = (pairs) => `<div class="facts">${pairs.filter(p => p && p[1] != null && p[1] !== '').map(p => `<span class="fact"${p[2] ? ` style="color:${p[2]}"` : ''}>${esc(p[0])} <b>${esc(p[1])}</b></span>`).join('')}</div>`;
  const more = (shown, total, key) => (shown < total ? `<a class="chip" style="margin-top:12px" data-q href="${esc(qLink({ [key]: 'all' }))}">${esc(t('Show all {n}', { n: total }))}</a>` : '');
  const shot = (thumb, name) => (thumb && thumb.page ? `<div class="shot"><img src="${pic(thumb.page)}" alt="${esc(t('{name}, as the game draws it', { name }))}" width="300" height="300"></div>` : `<div class="shot none">${esc(t('No picture'))}</div>`);
  const notLive = (live) => (live === false ? `<span class="kan">${esc(t('Not dropping yet'))}</span>` : '');

  // ---- the indexes, by slug and by key
  const byOf = {};
  function by(kind, field) {
    const idx = have(`index/${kind}.json`);
    if (!idx) return idx === null ? {} : undefined;
    const k = kind + '|' + field;
    if (!byOf[k]) { byOf[k] = {}; (idx.rows || []).forEach(r => { if (r[field] != null) byOf[k][r[field]] = r; }); }
    return byOf[k];
  }
  const rowsOf = (kind) => { const v = have(`index/${kind}.json`); return v === undefined ? undefined : ((v && v.rows) || []); };
  function worldName(w) { const W = by('worlds', 'k'); return (W && W[w] && W[w].n) || (w ? w.charAt(0).toUpperCase() + w.slice(1) : ''); }
  function placeName(d) {
    const ev = d.kind === 'castle' ? `castle-${d.castle}` : d.kind === 'square' ? `square-${d.square}` : d.kind === 'raid' ? 'raid' : null;
    const E = ev ? by('events', 'k') : null;
    if (ev && E && E[ev]) return { name: E[ev].n + (d.kind === 'raid' ? ' · ' + worldName(d.world) : ''), href: href('events', E[ev].slug) };
    return { name: worldName(d.world || d.place) + (d.elite ? '' : ''), href: d.world ? href('worlds', (by('worlds', 'k') || {})[d.world] ? by('worlds', 'k')[d.world].slug : d.world) : '' };
  }

  // ---- items
  function itemsList() {
    const rows = rowsOf('items');
    crumbs([[LIST.items, R.q.cat ? listHref('items') : ''], ...(R.q.cat ? [[R.q.cat, R.q.sub ? listHref('items', { cat: R.q.cat }) : '']] : []), ...(R.q.sub ? [[R.q.sub]] : [])]);
    if (rows === undefined) return listPage(LIST.items, '', ld);
    const q = (R.q.q || '').trim().toLowerCase();
    const base = rows.filter(i => (!q || i.n.toLowerCase().includes(q)) && (!R.q.cls || (i.cls || []).includes(R.q.cls)) &&
      (!R.q.src || (R.q.src === 'none' ? i.live === false : (i.src || '').includes(R.q.src))));
    const cats = ORDER.filter(c => rows.some(i => i.c === c)).concat([...new Set(rows.map(i => i.c))].filter(c => !ORDER.includes(c)));
    const tree = [`<a class="ci" href="${esc(qLink({ cat: '', sub: '', page: '' }))}" data-q aria-current="${!R.q.cat ? 'true' : 'false'}">${esc(t('All items'))}<span>${base.length}</span></a>`];
    cats.forEach(c => {
      const inC = base.filter(i => i.c === c);
      tree.push(`<a class="ci" href="${esc(qLink({ cat: c, sub: '', page: '' }))}" data-q aria-current="${R.q.cat === c && !R.q.sub ? 'true' : 'false'}">${esc(t(c))}<span>${inC.length}</span></a>`);
      if (R.q.cat === c) {
        const subs = [...new Set(inC.map(i => i.s))];
        if (subs.length > 1) subs.forEach(s => tree.push(`<a class="ci sub" href="${esc(qLink({ cat: c, sub: s, page: '' }))}" data-q aria-current="${R.q.sub === s ? 'true' : 'false'}">${esc(s)}<span>${inC.filter(i => i.s === s).length}</span></a>`));
      }
    });
    const classes = ['DK', 'DW', 'FE', 'MG'].filter(c => rows.some(i => (i.cls || []).includes(c)));
    const extra = `<div><h2 class="rh">${esc(t('Category'))}</h2>${tree.join('')}</div>
<div><h2 class="rh">${esc(t('Class'))}</h2><div class="chips">${classes.map(c => chip(esc(c), R.q.cls === c, { cls: R.q.cls === c ? '' : c, page: '' }, ` title="${esc(CLS[c])}"`)).join('')}</div></div>
<div><h2 class="rh">${esc(t('Source'))}</h2><div class="chips">${Object.keys(SRC).map(s => chip(esc(SRC[s][0]), R.q.src === s, { src: R.q.src === s ? '' : s, page: '' }, ` title="${esc(SRC[s][1])}"`)).join('')}${chip(esc(t('Not dropping yet')), R.q.src === 'none', { src: R.q.src === 'none' ? '' : 'none', page: '' })}</div></div>`;
    let list = base.filter(i => (!R.q.cat || i.c === R.q.cat) && (!R.q.sub || i.s === R.q.sub));
    const sort = R.q.sort || 'lv', dir = R.q.dir === '-1' ? -1 : 1;
    const need = (i) => i.need || {};
    const KEY = { n: i => i.n, s: i => i.s, lv: i => i.lv || i.dl || 0, st: i => (i.dmg ? i.dmg[1] : i.def || 0), rq: i => (need(i).strength || 0) + (need(i).agility || 0) + (need(i).energy || 0) + (need(i).level || 0), cl: i => (i.cls || []).join('') };
    const key = KEY[sort] || KEY.lv;
    list = list.slice().sort((x, y) => { const p = key(x), r = key(y); return (p > r ? 1 : p < r ? -1 : x.n > y.n ? 1 : -1) * dir; });
    const per = 25, pages = Math.max(1, Math.ceil(list.length / per)), page = Math.min(Math.max(1, +R.q.page || 1), pages);
    const cols = [['n', t('Name')], ['s', t('Type')], ['lv', t('Level'), 1], ['st', t('Damage / Def'), 1], ['rq', t('Requires')], ['cl', t('Class')]];
    const head = `<th scope="col" style="width:68px"><span class="sr">${esc(t('Picture'))}</span></th>` + cols.map(c => {
      const on = sort === c[0];
      return `<th scope="col"${c[2] ? ' class="r"' : ''}${on ? ` aria-sort="${dir > 0 ? 'ascending' : 'descending'}"` : ''}><a class="sh" data-q href="${esc(qLink({ sort: c[0], dir: on ? String(-dir) : '', page: '' }))}"${on ? ' aria-sort="x"' : ''}>${esc(c[1])}<span aria-hidden="true">${on ? (dir > 0 ? ' ▲' : ' ▼') : ''}</span></a></th>`;
    }).join('') + `<th scope="col" class="plain">${esc(t('Source'))}</th>`;
    const body = list.slice((page - 1) * per, page * per).map(i => {
      const n = need(i);
      const req = [n.level ? `Lv ${n.level}` : '', n.strength ? `Str ${n.strength}` : '', n.agility ? `Agi ${n.agility}` : '', n.energy ? `Ene ${n.energy}` : ''].filter(Boolean).join(' · ');
      const stat = i.dmg ? `${i.dmg[0]} ~ ${i.dmg[1]}` : i.def ? String(i.def) : '—';
      return `<tr><td><span class="ico">${i.t ? `<img src="${pic(i.t)}" alt="" loading="lazy" width="40" height="40">` : ''}</span></td>
<td><span class="nm" data-card="${esc(i.slug)}">${a('items', i.slug, i.n)}${notLive(i.live)}<span class="pop" role="tooltip">${simpleCard(i)}</span></span></td>
<td class="mute">${esc(i.s)}</td><td class="r num">${i.lv ? 'Lv ' + i.lv : i.dl ? t('Drop {n}', { n: i.dl }) : '—'}</td><td class="r num">${esc(stat)}</td>
<td class="mute">${esc(req || '—')}</td><td class="mute">${esc((i.cls || []).length >= 4 ? t('All') : (i.cls || []).join(' ') || '—')}</td>
<td><span class="srcs">${(i.src || '').split('').filter(c => SRC[c]).map(c => `<span class="sb" title="${esc(SRC[c][1])}">${esc(SRC[c][0])}</span>`).join('')}</span></td></tr>`;
    });
    const from = list.length ? (page - 1) * per + 1 : 0, to = Math.min(page * per, list.length);
    const title = R.q.sub || (R.q.cat ? t(R.q.cat) : q ? t('Items matching “{q}”', { q: R.q.q }) : t('All items'));
    return listPage(title, t('{from}–{to} of {n}', { from, to, n: list.length }),
      `<div class="tbox pops"><table class="tbl"><thead><tr>${head}</tr></thead><tbody>${body.join('')}</tbody></table></div>` +
      (list.length ? '' : `<p class="mute" style="padding:20px 0">${esc(t('No item matches.'))} <a class="crumb" style="text-decoration:underline" href="${listHref('items')}">${esc(t('Clear the filters'))}</a></p>`) +
      `<div class="pager"><span class="mute" style="margin-right:6px">${esc(t('Page {page} of {pages}', { page, pages }))}</span>
<a class="pb" data-q role="button" aria-label="${esc(t('Previous page'))}" ${page <= 1 ? 'aria-disabled="true" style="pointer-events:none;color:#3A322B"' : ''} href="${esc(qLink({ page: page - 1 }))}">‹</a>
<a class="pb" data-q role="button" aria-label="${esc(t('Next page'))}" ${page >= pages ? 'aria-disabled="true" style="pointer-events:none;color:#3A322B"' : ''} href="${esc(qLink({ page: page + 1 }))}">›</a></div>`, extra);
  }
  // The list's own card while the game's loads (and for an item with none).
  function simpleCard(i) {
    const n = i.need || {};
    const req = [n.level ? t('Level {n}', { n: n.level }) : '', n.strength ? t('Strength {n}', { n: n.strength }) : '', n.agility ? t('Agility {n}', { n: n.agility }) : '', n.energy ? t('Energy {n}', { n: n.energy }) : ''].filter(Boolean).join(', ');
    return `<span class="scard"><span class="c-name">${esc(i.n)}</span><span class="mute small">${esc(i.s)}</span><span class="c-rule"></span>
${i.dmg ? `<span><b style="color:#F2C14E;font-size:17px">${i.dmg[0]} ~ ${i.dmg[1]}</b> ${esc(t('Damage'))}</span>` : i.def ? `<span><b style="color:#F2C14E;font-size:17px">${i.def}</b> ${esc(t('Defense'))}</span>` : ''}
${req ? `<span>${esc(t('Requires {what}', { what: req }))}</span>` : ''}<span style="color:#7FD66A">${esc((i.cls || []).map(c => CLS[c] || c).join(', '))}</span></span>`;
  }
  // The item's list hover: the game's own card once its rows are here.
  document.addEventListener('mouseover', (ev) => {
    const nm = ev.target.closest && ev.target.closest('.nm[data-card]');
    if (!nm || nm.dataset.done) return;
    nm.dataset.done = '1';
    const slug = nm.dataset.card;
    const cards = have(`cards/${slug}.json`, true);
    const put = (c) => {
      if (!c || !c.cards) return;
      const st = MUCard.states(c.cards)[0];
      const I = by('items', 'slug');
      const row = I && I[slug];
      const html = MUCard.render(c.cards[st].sheet, row && row.t ? pic(row.t) : '');
      const pop = nm.querySelector('.pop');
      if (pop && html) pop.innerHTML = html;
    };
    if (cards) put(cards);
    else if (waiting[`cards/${slug}.json`]) waiting[`cards/${slug}.json`].then(() => { const el = document.querySelector(`.nm[data-card="${CSS.escape(slug)}"]`); if (el) { el.dataset.done = ''; el.dispatchEvent(new MouseEvent('mouseover', { bubbles: true })); } });
  });

  function itemPage(e) {
    const I = by('items', 'slug');
    const row = I ? I[e.slug] : null;
    crumbs([[LIST.items, listHref('items')], [t(e.category), listHref('items', { cat: e.category })], ...(e.sub && e.sub !== e.category ? [[e.sub, listHref('items', { cat: e.category, sub: e.sub })]] : []), [e.name]]);
    const cardFile = e.card ? have(e.card) : null;
    const hist = have(`history/items/${e.slug}.json`);
    const st = e.stats || {};
    const F = e.from || {};
    const gives = (k) => (F[k] || []).filter(r => r.role === 'gives');
    const needs = Object.values(F).flat().filter(r => r.role === 'needs');
    const drops = e.drops || [];
    const releases = (hist && hist.releases) || [];
    const changes = releases.filter(r => (r.changes || []).length || r.added);
    const tabs = [['stats', t('Stats & scaling'), ''], ['drops', t('Dropped by'), drops.length], ['shops', t('Sold by'), gives('shops').length + gives('npcs').length],
      ['crafts', t('Created by'), gives('mixes').length], ['used', t('Used in'), needs.length], ['rewards', t('Reward from'), gives('quests').length + gives('events').length + gives('boxes').length],
      ['updates', t('Updates'), changes.length], ['comments', t('Comments'), '']];
    const tab = tabs.some(x => x[0] === R.q.tab) ? R.q.tab : 'stats';
    // The card, in each state the dump drew it.
    let card = '';
    if (cardFile === undefined) card = ld;
    else if (cardFile && cardFile.cards) {
      const states = MUCard.states(cardFile.cards);
      const s = states.includes(R.q.state) ? R.q.state : states[0];
      card = (states.length > 1 ? `<div class="chips">${states.map(k => chip(esc(MUCard.stateLabel(k)), k === s, { state: k === states[0] ? '' : k })).join('')}</div>` : '') +
        MUCard.render(cardFile.cards[s].sheet, e.thumb ? pic(e.thumb.card) : '') +
        `<span class="mute small">${esc(t('The card as the game draws it, from its own rows.'))}</span>`;
    } else card = waitDump('cards');
    const similar = I ? Object.values(I).filter(i => i.s === e.sub && i.slug !== e.slug)
      .sort((x, y) => Math.abs((x.lv || x.dl || 0) - (st.drop_level || 0)) - Math.abs((y.lv || y.dl || 0) - (st.drop_level || 0))).slice(0, 6) : [];
    const kind = [e.two_handed ? t('Two-handed') : '', e.sub || e.category].filter(Boolean).join(' ');
    const top = `<div class="top2"><span class="ico lg">${e.thumb ? `<img src="${pic(e.thumb.card)}" alt="">` : ''}</span><div><h1 class="h1 big">${esc(e.name)}</h1>
<div class="mute" style="font-size:15px;margin-top:4px">${esc(kind)}${(e.classes || []).length ? ' · ' + esc((e.classes || []).length >= 4 ? t('Every class') : e.classes.map(c => CLS[c] || c).join(', ')) : ''}</div></div>
${facts([st.drop_level ? [t('Drop level'), st.drop_level] : null, row && row.src ? [t('From'), row.src.split('').filter(c => SRC[c]).map(c => SRC[c][0]).join(' · ')] : null,
      e.live === false ? [t('Kanturu'), t('not dropping yet'), '#7FD7F0'] : null, changes.length ? [t('Updates'), changes.length, '#F2C14E'] : null])}</div>`;
    const side = `<aside class="side">${shot(e.thumb, e.name)}${card}
<div><h2 class="rh" style="padding:0">${esc(t('More {what}', { what: (e.sub || '').toLowerCase() }))}</h2>${similar.map(i => `<a class="sim" href="${href('items', i.slug)}"><span style="flex:1">${esc(i.n)}</span><span class="mute num small">${i.lv ? 'Lv ' + i.lv : i.dl ? t('Drop {n}', { n: i.dl }) : ''}</span></a>`).join('')}</div></aside>`;
    let body = '';
    if (tab === 'stats') body = itemStats(e, cardFile);
    else if (tab === 'drops') body = itemDrops(drops);
    else if (tab === 'shops') body = refsTable(gives('shops').concat(gives('npcs')), t('Sold by'), 'shops');
    else if (tab === 'crafts') body = refsTable(gives('mixes'), t('Mix'), 'mixes');
    else if (tab === 'used') body = refsTable(needs, t('Asked by'), 'needs');
    else if (tab === 'rewards') body = refsTable(gives('quests').concat(gives('events'), gives('boxes')), t('Reward from'), 'rewards');
    else if (tab === 'updates') body = updates(hist, 'items', e.slug);
    else body = comments('items', e.slug, releases);
    return `${top}<div class="split">${side}<div class="body" style="gap:0"><div class="tabs" role="tablist" aria-label="${esc(t('About this item'))}">${tabs.map(x =>
      `<a class="tab${x[2] === 0 ? ' none' : ''}" role="tab" data-q data-keep href="${esc(qLink({ tab: x[0] === 'stats' ? '' : x[0], all: '', map: '' }))}" aria-selected="${x[0] === tab ? 'true' : 'false'}">${esc(x[1])}${x[2] !== '' && x[2] != null ? `<span>${x[2]}</span>` : ''}</a>`).join('')}</div>${body}</div></div>`;
  }
  function itemStats(e, cardFile) {
    const st = e.stats || {}, req = st.requires || {};
    const base = [[t('Damage'), st.minimum_damage ? `${st.minimum_damage} ~ ${st.maximum_damage}` : null], [t('Defense'), st.defense || null], [t('Defense rate'), st.defense_rate || null],
      [t('Wizardry'), st.magic_power || null], [t('Attack speed'), st.attack_speed || null], [t('Walk speed'), st.walk_speed || null], [t('Durability'), st.durability || null],
      [t('Drop level'), st.drop_level || null], [t('Size'), st.width ? `${st.width} × ${st.height}` : null], [t('Drops from monsters'), st.drops_from_monsters === false ? t('No') : null]];
    let out = `<div class="roll" style="margin-bottom:24px"><div class="rbox"><b>${esc(t('Its row'))}</b>${kv(base)}</div>`;
    const p = e.powers;
    if (p && p.needs) out += `<div class="rbox"><b>${esc(t('Asks at +0'))}</b>${kv([[t('Level'), p.needs.level || null], [t('Strength'), p.needs.strength || null], [t('Agility'), p.needs.agility || null], [t('Energy'), p.needs.energy || null], [t('Vitality'), p.needs.vitality || null]]) || `<span class="mute">${esc(t('Nothing'))}</span>`}</div>`;
    else if (req) out += `<div class="rbox"><b>${esc(t('Requires'))}</b>${kv(Object.entries(req).filter(x => x[1]).map(x => [pretty(x[0]), x[1]])) || `<span class="mute">${esc(t('Nothing'))}</span>`}</div>`;
    if (p && p.tells) out += `<div class="rbox"><b>${esc(t('What it does'))}</b><p style="margin:0">${esc(p.tells)}</p></div>`;
    out += '</div>';
    // The card's states side by side: what the game's describe() says at each.
    if (cardFile && cardFile.cards) {
      const states = MUCard.states(cardFile.cards);
      const cell = (c, f) => { try { return f(c) ?? '—'; } catch (x) { return '—'; } };
      const lines = [[t('Headline'), c => c.sheet.hero && c.sheet.hero.value ? `${c.sheet.hero.value} ${c.sheet.hero.word || ''}` : null],
        [t('Level'), c => c.needs && c.needs.level || null], [t('Strength'), c => c.needs && c.needs.strength || null], [t('Agility'), c => c.needs && c.needs.agility || null],
        [t('Energy'), c => c.needs && c.needs.energy || null], [t('Sells for'), c => c.sells != null ? c.sells.toLocaleString('en') : null]]
        .filter(l => states.some(s => cell(cardFile.cards[s], l[1]) !== '—'));
      out += `<h2 class="h3">${esc(t('As the game describes it'))}</h2><div class="tbox" style="margin-bottom:24px"><table class="tbl"><thead><tr><th class="plain"></th>${states.map(s => `<th class="plain r">${esc(MUCard.stateLabel(s))}</th>`).join('')}</tr></thead><tbody>${
        lines.map(l => `<tr><th scope="row" class="plain rowh">${esc(l[0])}</th>${states.map(s => `<td class="r num">${esc(cell(cardFile.cards[s], l[1]))}</td>`).join('')}</tr>`).join('')}</tbody></table></div>`;
    }
    // Any by-plus table the dump gives (wings, jewellery), and the rest of its powers.
    if (p) {
      Object.entries(p).forEach(([k, v]) => {
        if (v && typeof v === 'object' && Array.isArray(v.by_plus) && v.by_plus.length && typeof v.by_plus[0] === 'object') {
          const fields = Object.keys(v.by_plus[0]).filter(f => f !== 'plus');
          out += `<h2 class="h3">${esc(t('{what} by plus', { what: pretty(k) }))}</h2><div class="tbox" style="margin-bottom:24px"><table class="tbl"><thead><tr><th class="plain"></th>${v.by_plus.map(r => `<th class="plain r">+${esc(r.plus)}</th>`).join('')}</tr></thead><tbody>${
            fields.map(f => `<tr><th scope="row" class="plain rowh">${esc(pretty(f))}</th>${v.by_plus.map(r => `<td class="r num">${esc(r[f])}</td>`).join('')}</tr>`).join('')}</tbody></table></div>`;
        }
      });
    }
    if (!cardFile && !p) out += waitDump('gear_powers');
    return out;
  }
  function itemDrops(drops) {
    if (!drops.length) return missing.has('drops') ? waitDump('drops') : absent(t('No monster drops it.'));
    const places = [...new Set(drops.map(d => d.place))];
    const sel = places.includes(R.q.map) ? R.q.map : '';
    const shown = drops.filter(d => !sel || d.place === sel).sort((x, y) => (y.chance || 0) - (x.chance || 0));
    const all = R.q.all === 'all';
    const chips = places.length > 1 ? `<div class="chips" style="margin-bottom:14px">${chip(esc(t('All places · {n}', { n: drops.length })), !sel, { map: '' })}${places.map(p => {
      const d = drops.find(x => x.place === p);
      return chip(esc(placeName(d).name), sel === p, { map: p, all: 'all' });
    }).join('')}</div>` : '';
    return chips + table([[t('Monster')], [t('Level'), 1], [t('Where')], [t('How')], [t('Chance a kill'), 1]], (all ? shown : shown.slice(0, 30)).map(d => {
      const pl = placeName(d);
      return `<tr><td>${a('monsters', d.monster_slug, d.monster_name)}${d.elite ? ` <span class="sb gold">${esc(t('Elite'))}</span>` : ''}</td><td class="r num">${N(d.level)}</td>
<td class="mute">${pl.href ? `<a href="${pl.href}">${esc(pl.name)}</a>` : esc(pl.name)}</td><td class="wrap" style="color:${(d.rolls || []).some(r => r.excellent) ? '#7FD66A' : '#C9BFAE'}">${esc(rollWords(d.rolls))}</td><td class="r">${oddsCell(d.chance)}</td></tr>`;
    }), 'sm') + more(all ? shown.length : Math.min(30, shown.length), shown.length, 'all') +
      `<p class="note">${esc(t('Best chance first. Chances are per kill for one player, before Item Find, talents and party bonuses, as the game works them out.'))}</p>`;
  }
  function refsTable(refs, head, what) {
    if (!refs.length) return missing.has('rules') ? waitDump('rules') : absent(t('Nothing in this release.'));
    const name = { shops: t('Shop'), npcs: t('Townsperson'), mixes: t('Chaos Machine'), quests: t('Quest'), events: t('Event'), boxes: t('Box'), runes: t('Rune'), elites: t('Elites') };
    return table([[head], [t('What')]], refs.map(r => `<tr><td>${a(r.kind === 'shops' ? 'npcs' : r.kind, r.slug, r.name)}</td><td class="mute">${esc(name[r.kind] || pretty(r.kind))}</td></tr>`), 'sm');
  }

  // ---- updates and comments
  function updates(hist, kind, slug) {
    if (hist === undefined) return ld;
    const rel = (hist && hist.releases) || [];
    if (!rel.length) return absent(t('No update has changed it since the Armory began keeping track.'));
    return `<ol class="tl">${rel.map(r => `<li><span class="pip" aria-hidden="true"></span>
<div style="display:flex;flex-wrap:wrap;gap:4px 14px;align-items:baseline"><span style="font:700 15px/1.3 'Alegreya Sans SC',sans-serif;color:#F2C14E">${esc((r.built || '').slice(0, 10))}</span><span class="num">${esc(r.commit)}</span>${r.added ? `<span>${esc(t('Added to the game'))}</span>` : ''}</div>
${(r.changes || []).length ? `<div class="diff"><span class="k">${esc(t('What'))}</span><span class="k">${esc(t('Was'))}</span><span class="k">${esc(t('Now'))}</span>${r.changes.slice(0, 40).map(c => `<span class="mute">${esc(c.field)}</span><span class="was">${esc(JSON.stringify(c.was))}</span><span>${esc(JSON.stringify(c.now))}</span>`).join('')}</div>` : ''}
${r.note ? `<p class="mute" style="margin:6px 0 0">${esc(r.note)}</p>` : ''}
<a class="chip" style="margin-top:10px" data-q data-keep href="${esc(qLink({ tab: 'comments', about: r.commit }))}">${esc(t('Comment on this update'))}</a></li>`).join('')}</ol>`;
  }
  const talk = {};  // kind|slug -> {comments, me} | null
  function loadTalk(kind, slug, again) {
    const k = kind + '|' + slug;
    if (talk[k] !== undefined && !again) return talk[k];
    if (!talk['w' + k] || again) {
      talk['w' + k] = (window.MU ? MU.api(`/api/armory/comments?kind=${encodeURIComponent(kind)}&slug=${encodeURIComponent(slug)}`) : Promise.reject())
        .then(r => { talk[k] = r.status === 200 ? r.said : null; draw(); }).catch(() => { talk[k] = null; draw(); });
    }
    return talk[k];
  }
  let draft = '', postErr = '';
  function comments(kind, slug, releases) {
    const got = loadTalk(kind, slug);
    if (got === undefined) return ld;
    if (got === null) return absent(t('Comments can’t be reached right now.'));
    const abouts = [['', t('The thing itself')]].concat((releases || []).map(r => [r.commit, t('The update of {day}', { day: (r.built || '').slice(0, 10) })]));
    const about = abouts.some(x => x[0] === R.q.about) ? R.q.about : '';
    const filt = R.q.cf || 'all';
    const list = (got.comments || []).filter(c => filt === 'all' || c.about === (filt === 'thing' ? '' : filt))
      .sort((x, y) => (R.q.cs === 'new' ? y.at - x.at : (y.votes - x.votes) || (y.at - x.at)));
    const me = got.me;
    const form = me ? (me.name ? `<div class="cbox"><label for="cmt" style="font-size:15px">${esc(t('Comment as'))} <b style="color:#F3E8D4">${esc(me.name)}</b></label>
<textarea id="cmt" class="afield" rows="3" maxlength="1000" placeholder="${esc(t('How does it play? Where did it drop?'))}">${esc(draft)}</textarea>
<div style="display:flex;flex-wrap:wrap;gap:10px;align-items:center;justify-content:space-between"><div class="chips" style="align-items:center"><span class="mute small">${esc(t('About'))}</span>${abouts.map(x => chip(esc(x[1]), x[0] === about, { about: x[0] })).join('')}</div>
<button class="abtn red" type="button" data-post="${esc(kind)}|${esc(slug)}|${esc(about)}">${esc(t('Post'))}</button></div>${postErr ? `<div class="err2">${esc(postErr)}</div>` : ''}</div>`
      : `<div class="cbox">${esc(t('Pick a display name on your account page to comment.'))} <a href="${P}/account" style="text-decoration:underline">${esc(t('Account'))}</a></div>`)
      : `<div class="cbox">${esc(t('Sign in to comment and to vote.'))} <a href="${P}/account" style="text-decoration:underline">${esc(t('Sign in'))}</a></div>`;
    const aboutName = Object.fromEntries(abouts);
    return form + `<div class="chips" style="margin-bottom:4px">${chip(esc(t('All · {n}', { n: (got.comments || []).length })), filt === 'all', { cf: '' })}${(releases || []).length ? chip(esc(t('On the thing')), filt === 'thing', { cf: 'thing' }) + releases.map(r => chip(esc(aboutName[r.commit]), filt === r.commit, { cf: r.commit })).join('') : ''}
<span style="flex:1"></span>${chip(esc(t('Top')), R.q.cs !== 'new', { cs: '' })}${chip(esc(t('Newest')), R.q.cs === 'new', { cs: 'new' })}</div>` +
      (list.length ? list.map(c => `<article class="cm"><button class="vote" type="button" data-vote="${c.id}" aria-pressed="${c.mine ? 'true' : 'false'}" aria-label="${esc(t('Agree'))}" ${!me || c.own ? 'disabled' : ''}><svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" aria-hidden="true"><path d="M5 15l7-7 7 7"></path></svg>${c.votes}</button>
<div style="min-width:0;flex:1;display:flex;flex-direction:column;gap:4px"><div style="display:flex;flex-wrap:wrap;gap:4px 12px;align-items:baseline;font-size:14px"><b style="color:#F3E8D4;font-size:15px">${esc(c.name)}</b>${c.about ? `<span style="color:#F2C14E">${esc(t('on {what}', { what: (aboutName[c.about] || c.about).toLowerCase() }))}</span>` : ''}<span class="mute">${esc(window.MU ? MU.when(c.at) : '')}</span>${c.reports ? `<span class="sb" style="color:#E07A6C">${esc(t('{n} reports', { n: c.reports }))}</span>` : ''}</div>
<p>${esc(c.text)}</p><div class="acts">${me && !c.own && !c.reported ? `<button type="button" data-report="${c.id}">${esc(t('Report'))}</button>` : ''}${c.reported ? `<span class="mute">${esc(t('Reported'))}</span>` : ''}${me && (c.own || me.admin) ? `<button type="button" data-delete="${c.id}">${esc(t('Delete'))}</button>` : ''}</div></div></article>`).join('')
        : `<p class="mute" style="padding:16px 0;margin:0;border-top:1px solid #1E1814">${esc(t('No comments yet.'))}</p>`);
  }
  async function act(path, body) {
    const r = await MU.api(path, body);
    return r;
  }

  // ---- monsters
  function monstersList() {
    const rows = rowsOf('monsters');
    crumbs([[LIST.monsters, R.q.map ? listHref('monsters') : ''], ...(R.q.map ? [[R.q.map]] : [])]);
    if (rows === undefined) return listPage(LIST.monsters, '', ld);
    const q = (R.q.q || '').trim().toLowerCase();
    const maps = {};
    rows.forEach(m => (m.maps || []).forEach(x => { maps[x] = (maps[x] || 0) + 1; }));
    const list = rows.filter(m => (!R.q.map || (m.maps || []).includes(R.q.map) || (!(m.maps || []).length && m.home === R.q.map)) && (!q || m.n.toLowerCase().includes(q)))
      .sort((x, y) => (x.lv || 0) - (y.lv || 0) || (x.n > y.n ? 1 : -1));
    const extra = `<div><h2 class="rh">${esc(t('Map'))}</h2><a class="ci" data-q href="${esc(qLink({ map: '' }))}" aria-current="${!R.q.map ? 'true' : 'false'}">${esc(t('Every map'))}<span>${rows.length}</span></a>${Object.keys(maps).sort().map(x =>
      `<a class="ci" data-q href="${esc(qLink({ map: x }))}" aria-current="${R.q.map === x ? 'true' : 'false'}">${esc(x)}<span>${maps[x]}</span></a>`).join('')}</div>`;
    return listPage(R.q.map ? t('Monsters in {map}', { map: R.q.map }) : LIST.monsters, t('{n} monsters', { n: list.length }), table(
      [[''], [t('Monster')], [t('Level'), 1], [t('Life'), 1], [t('Damage'), 1], [t('Defense'), 1], [t('Lives in')]],
      list.map(m => `<tr><td style="width:60px"><span class="ico">${m.t ? `<img src="${pic(m.t)}" alt="" loading="lazy">` : ''}</span></td><td>${a('monsters', m.slug, m.n)}${notLive(m.live)}</td><td class="r num">${N(m.lv)}</td><td class="r num">${N(m.hp)}</td>
<td class="r num">${m.dmg && m.dmg[0] != null ? `${m.dmg[0]} ~ ${m.dmg[1]}` : '—'}</td><td class="r num">${N(m.def)}</td><td class="mute wrap">${esc((m.maps || []).join(', ') || m.home || '—')}</td></tr>`)), extra);
  }
  function chart(world, marks) {
    // A world's chart as the minimap bakes it: tile (x, y) at px = (x + 0.5) * 2 on its 512 picture.
    const W = by('worlds', 'k');
    const row = W && W[world];
    if (!row || !row.chart) return '';
    const size = 512, at = (v) => ((v + 0.5) * 2 / size * 100).toFixed(3) + '%';
    return `<div class="chart"><img src="${pic(row.chart)}" alt="${esc(t('Map of {map}', { map: row.n }))}" width="512" height="512">${marks.map(m => m.box
      ? `<span class="box" aria-hidden="true" style="left:${at(m.box[0])};top:${at(m.box[1])};width:${((m.box[2] - m.box[0] + 1) * 2 / size * 100).toFixed(3)}%;height:${((m.box[3] - m.box[1] + 1) * 2 / size * 100).toFixed(3)}%"></span>`
      : `<span class="dot" aria-hidden="true" style="left:${at(m.x)};top:${at(m.y)}"></span>${m.label ? `<span class="lbl" style="left:${at(m.x)};top:${at(m.y)}">${esc(m.label)}</span>` : ''}`).join('')}</div>`;
  }
  function monsterPage(e) {
    crumbs([[LIST.monsters, listHref('monsters')], [e.name]]);
    const c = e.combat || {};
    const I = by('items', 'slug');
    const E = rowsOf('elites');
    const places = e.drops || [];
    const keys = places.map((p, i) => i);
    const pick = keys.includes(+R.q.at) ? +R.q.at : (places.length ? 0 : -1);
    const elite = R.q.elite === '1' && e.elite_thumb;
    const nests = e.nests || [];
    const kvs = [[t('Level'), c.level], [t('Life'), N(c.health), 1], [t('Damage'), c.minimum_damage != null ? `${c.minimum_damage} ~ ${c.maximum_damage}` : null], [t('Defense'), c.defense],
      [t('Attack rate'), c.attack_rate], [t('Defense rate'), c.defense_rate], [t('Attack range'), c.attack_range != null ? t('{n} tiles', { n: c.attack_range }) : null],
      [t('Sees'), c.view_range != null ? t('{n} tiles', { n: c.view_range }) : null], [t('Attacks every'), c.attack_delay ? (c.attack_delay / 1000).toFixed(1) + ' s' : null],
      [t('Moves every'), c.move_delay ? (c.move_delay / 1000).toFixed(1) + ' s' : null]].concat(Object.entries(e.resist || {}).map(([k, v]) => [t('{what} resistance', { what: pretty(k) }), v]));
    const eliteMaps = (E || []).filter(x => places.some(p => p.elite && p.world === x.world));
    const side = `<aside class="side">${shot(elite ? e.elite_thumb : e.thumb, e.name)}${e.elite_thumb ? `<div class="chips">${chip(esc(t('Ordinary')), !elite, { elite: '' })}${chip(esc(t('Elite')), !!elite, { elite: '1' })}</div>` : ''}
<div class="icard"><div class="t">${esc(e.name)}</div><span class="mute small">${esc(t('Level {n}', { n: c.level }))}</span><div class="c-rule"></div>${kv(kvs)}
${eliteMaps.length ? `<div class="c-rule"></div><span style="font-size:14px;color:#F2C14E">${esc(t('Rises as an Elite in {maps}: ×{life} life, ×{dmg} damage, ×{def} defense, {up} levels higher.', { maps: eliteMaps.map(x => worldName(x.world)).join(', '), life: eliteMaps.map(x => x.life).join('/'), dmg: eliteMaps[0].damage, def: eliteMaps[0].defense, up: eliteMaps[0].levels_up }))}</span>` : ''}</div>
<div><h2 class="rh" style="padding:0">${esc(t('Lives in'))}</h2>${nests.map(n => `<a class="sim" href="${href('worlds', n.world_slug)}"><span style="flex:1">${esc(n.map)}</span><span class="mute small">${esc(t('{n} in its nests', { n: n.count }))}</span></a>`).join('') || `<span class="mute">${esc(t('Only in events'))}</span>`}</div>
${nests.length ? chart(nests[0].world, (nests[0].boxes || []).map(b => ({ box: b }))) : ''}</aside>`;
    let body;
    if (!places.length) body = missing.has('drops') ? waitDump('drops') : absent(t('It drops nothing.'));
    else {
      const pl = places[pick];
      const items = (pl.items || []).filter(i => (i.chance || 0) > 0);
      const cats = I ? [...new Set(items.map(i => (I[i.slug] || {}).c).filter(Boolean))] : [];
      const cat = cats.includes(R.q.cat) ? R.q.cat : '';
      const shown = items.filter(i => !cat || (I && I[i.slug] && I[i.slug].c === cat));
      const all = R.q.all === 'all';
      body = `${places.length > 1 ? `<div class="chips" style="margin-bottom:14px">${places.map((p, i) => chip(esc(placeName(p).name + (p.elite ? ' · ' + t('Elite') : '') + ' · ' + t('Lv {n}', { n: p.level })), i === pick, { at: i ? String(i) : '', cat: '', all: '' })).join('')}</div>` : ''}
${pl.zen ? `<p class="mute" style="margin:0 0 12px">${esc(t('Pays Zen {pct} of kills.', { pct: pct(pl.zen) }))}</p>` : ''}
${cats.length > 1 ? `<div class="chips" style="margin-bottom:14px">${chip(esc(t('All · {n}', { n: items.length })), !cat, { cat: '' })}${cats.map(x => chip(esc(t(x)) + ' · ' + items.filter(i => I[i.slug] && I[i.slug].c === x).length, cat === x, { cat: x, all: 'all' })).join('')}</div>` : ''}
${table([[t('Item')], [t('Type')], [t('Drop level'), 1], [t('How')], [t('Chance a kill'), 1]], (all ? shown : shown.slice(0, 30)).map(i => {
        const r = I && I[i.slug];
        return `<tr><td><span class="nm" data-card="${esc(i.slug)}">${a('items', i.slug, i.n)}<span class="pop" role="tooltip">${r ? simpleCard(r) : ''}</span></span></td><td class="mute">${esc(r ? r.s : '')}</td><td class="r num">${N(r && r.dl)}</td>
<td class="wrap" style="color:${(i.rolls || []).some(x => x.excellent) ? '#7FD66A' : '#C9BFAE'}">${esc(rollWords(i.rolls))}</td><td class="r">${oddsCell(i.chance)}</td></tr>`;
      }), 'sm pops')}${more(all ? shown.length : Math.min(30, shown.length), shown.length, 'all')}
${(pl.while_quest || []).length ? `<h2 class="h3" style="margin-top:20px">${esc(t('While a quest asks for it'))}</h2>${table([[t('Item')], [t('Who')], [t('Chance a kill'), 1]], pl.while_quest.map(w => {
        const r = I && Object.values(I).find(x => x.k === w.item);
        return `<tr><td>${r ? a('items', r.slug, r.n) : esc(w.item)}</td><td class="mute">${esc((w.classes || []).join(', '))}</td><td class="r">${oddsCell(w.chance)}</td></tr>`;
      }), 'sm')}` : ''}
<p class="note">${esc(t('Best chance first, per kill for one player before Item Find, talents and party bonuses.'))}</p>`;
    }
    const n = places.reduce((s, p) => s + (p.items || []).length, 0);
    return `<div class="top2"><div class="grow"><h1 class="h1 big">${esc(e.name)}</h1><div class="mute" style="font-size:15px;margin-top:4px">${esc(t('Level {n}', { n: c.level }))}${nests.length ? ' · ' + esc(nests.map(x => x.map).join(', ')) : ''}</div></div>
${facts([[t('Level'), c.level], [t('Life'), (c.health || 0).toLocaleString('en')], places.length ? [t('Drops'), t('{n} items', { n: (places[pick].items || []).length })] : null])}${notLive(e.live)}</div>
<div class="split">${side}<div class="body"><section><div class="ttl"><h2 class="h2" style="margin:0">${esc(t('Drops'))}</h2><span class="mute">${esc(n ? t('{n} item rows in {p} places', { n, p: places.length }) : '')}</span></div>${body}</section>
<section><h2 class="h2">${esc(t('Comments'))}</h2>${comments('monsters', e.slug, [])}</section></div></div>`;
  }

  // ---- townsfolk and worlds
  const ROLE = (r) => (r ? pretty(String(r)) : '');
  function npcsList() {
    const rows = rowsOf('npcs');
    crumbs([[LIST.npcs]]);
    if (rows === undefined) return listPage(LIST.npcs, '', ld);
    const q = (R.q.q || '').trim().toLowerCase();
    const list = rows.filter(n => !q || n.n.toLowerCase().includes(q));
    return listPage(LIST.npcs, t('{n} townsfolk', { n: list.length }), table([[''], [t('Name')], [t('Role')], [t('Where')]], list.map(n =>
      `<tr><td style="width:60px"><span class="ico">${n.t ? `<img src="${pic(n.t)}" alt="" loading="lazy">` : ''}</span></td><td>${a('npcs', n.slug, n.n)}</td><td class="mute">${esc(ROLE(n.role) || '—')}</td><td class="mute wrap">${esc((n.maps || []).join(', '))}</td></tr>`)));
  }
  function npcPage(e) {
    crumbs([[LIST.npcs, listHref('npcs')], [e.name]]);
    const where = e.where || [];
    const ix = Math.min(+R.q.at || 0, Math.max(0, where.length - 1));
    const w = where[ix];
    const I = by('items', 'k');
    const Q = rowsOf('quests');
    const M = rowsOf('mixes');
    const quests = (Q || []).filter(q => q.giver === e.npc || q.receiver === e.npc);
    const mixes = (M || []).filter(m => m.npc === e.npc && m.listed !== false);
    const shelf = e.shelf || [];
    const side = `<aside class="side">${shot(e.thumb, e.name)}<div class="icard"><div class="t">${esc(e.name)}</div><span class="mute small">${esc(ROLE(e.role))}</span>
${(e.services || []).length ? `<div class="c-rule"></div>${e.services.map(s => `<span style="font-size:15px">· ${esc(pretty(s))}</span>`).join('')}` : ''}${e.repairs ? `<span style="font-size:15px">· ${esc(t('Repairs'))}</span>` : ''}</div></aside>`;
    const W = by('worlds', 'k');
    return `<div class="top2"><div class="grow"><h1 class="h1 big">${esc(e.name)}</h1><div class="mute" style="font-size:15px;margin-top:4px">${esc(ROLE(e.role))}</div></div>${facts([[t('Where'), [...new Set(where.map(x => worldName(x.world)))].join(', ')], shelf.length ? [t('Sells'), shelf.length] : null])}</div>
<div class="split">${side}<div class="body">
<section><div class="ttl"><h2 class="h2" style="margin:0">${esc(t('Where to find {name}', { name: e.name }))}</h2><span class="mute">${w ? esc(`${worldName(w.world)} · ${w.x}, ${w.y}`) : ''}</span></div>
${where.length > 1 ? `<div class="chips" style="margin-bottom:12px">${where.map((x, i) => chip(esc(`${worldName(x.world)} ${x.x}, ${x.y}`), i === ix, { at: i ? String(i) : '' })).join('')}</div>` : ''}
${w ? (W === undefined ? ld : chart(w.world, [{ x: w.x, y: w.y, label: e.name }])) : absent(t('Not standing anywhere in this release.'))}</section>
<section><h2 class="h2">${esc(t('Sells'))}</h2>${shelf.length ? (I === undefined ? ld : table([[''], [t('Item')], [t('Type')], [t('Plus'), 1], [t('Price'), 1]], shelf.map(s => {
      const r = I[s.item];
      return `<tr><td style="width:56px"><span class="ico">${r && r.t ? `<img src="${pic(r.t)}" alt="" loading="lazy">` : ''}</span></td><td>${r ? `<span class="nm" data-card="${esc(r.slug)}">${a('items', r.slug, r.n)}<span class="pop" role="tooltip">${simpleCard(r)}</span></span>` : esc(s.item)}</td>
<td class="mute">${esc(r ? r.s : '')}</td><td class="r num">${s.plus ? '+' + s.plus : '—'}</td><td class="r num" style="color:#F2C14E">${N(s.price)} Zen${s.pieces > 1 ? ' ' + esc(t('for {n}', { n: s.pieces })) : ''}</td></tr>`;
    }), 'sm pops')) : (missing.has('rules') ? waitDump('rules') : absent(t('Sells nothing.')))}</section>
${quests.length ? `<section><h2 class="h2">${esc(t('Quests'))}</h2>${quests.map(q => `<a class="sim" href="${href('quests', q.slug)}"><span style="flex:1;color:#F3E8D4">${esc(q.n)}</span><span class="mute small">${q.minLevel ? esc(t('Level {n}', { n: q.minLevel })) : ''}</span></a>`).join('')}</section>` : ''}
${mixes.length ? `<section><h2 class="h2">${esc(t('Mixes'))}</h2>${mixes.map(m => `<a class="sim" href="${href('mixes', m.slug)}"><span style="flex:1;color:#F3E8D4">${esc(m.n)}</span><span class="mute small">${esc(m.success || '')}</span></a>`).join('')}</section>` : ''}
<section><h2 class="h2">${esc(t('Comments'))}</h2>${comments('npcs', e.slug, [])}</section></div></div>`;
  }
  function worldsList() {
    const rows = rowsOf('worlds');
    crumbs([[LIST.worlds]]);
    if (rows === undefined) return listPage(LIST.worlds, '', ld);
    return listPage(LIST.worlds, t('{n} maps', { n: rows.length }), table([[t('Map')], [t('Monsters'), 1], [t('In this release')]], rows.map(w =>
      `<tr><td>${a('worlds', w.slug, w.n)}${notLive(w.live)}</td><td class="r num">${N(w.monsters)}</td><td class="mute">${w.in_release ? esc(t('Yes')) : esc(t('No'))}</td></tr>`), 'sm'));
  }
  function worldPage(e) {
    crumbs([[LIST.worlds, listHref('worlds')], [e.name]]);
    const sel = (e.monsters || []).find(m => m.slug === R.q.m);
    const marks = sel ? (sel.boxes || []).map(b => ({ box: b })) : (e.npcs || []).filter(n => n.x != null).map(n => ({ x: n.x, y: n.y }));
    return `<div class="top2"><div class="grow"><h1 class="h1 big">${esc(e.name)}</h1></div>${facts([[t('Monsters'), (e.monsters || []).length], [t('Townsfolk'), (e.npcs || []).length]])}${notLive(e.live)}</div>
<div class="split"><aside class="side" style="max-width:560px;flex-basis:480px">${chart(e.key, marks)}<span class="mute small">${esc(sel ? t('Where {name} nests', { name: sel.name }) : t('The dots are its townsfolk'))}</span></aside>
<div class="body"><section><h2 class="h2">${esc(t('Monsters'))}</h2>${table([[t('Monster')], [t('Level'), 1], [t('In its nests'), 1], ['']], (e.monsters || []).slice().sort((x, y) => (x.level || 0) - (y.level || 0)).map(m =>
      `<tr><td>${a('monsters', m.slug, m.name)}</td><td class="r num">${N(m.level)}</td><td class="r num">${N(m.count)}</td><td>${chip(esc(t('On the map')), R.q.m === m.slug, { m: R.q.m === m.slug ? '' : m.slug })}</td></tr>`), 'sm')}</section>
<section><h2 class="h2">${esc(t('Townsfolk'))}</h2>${[...new Map((e.npcs || []).map(n => [n.slug, n])).values()].map(n => `<a class="sim" href="${href('npcs', n.slug)}"><span style="flex:1">${esc(n.name)}</span><span class="mute small">${n.x != null ? `${n.x}, ${n.y}` : ''}</span></a>`).join('')}</section></div></div>`;
  }

  // ---- quests
  const repeats = (s) => (!s ? t('Once') : s % 3600 === 0 ? t('Every {n} h', { n: s / 3600 }) : t('Every {n} min', { n: Math.round(s / 60) }));
  function questsList() {
    const rows = rowsOf('quests');
    const F = by('npcs', 'npc');
    crumbs([[LIST.quests]]);
    if (rows === undefined) return listPage(LIST.quests, '', missing.has('rules') ? waitDump('rules') : ld);
    const list = rows.slice().sort((x, y) => (x.index ?? 0) - (y.index ?? 0));
    return listPage(LIST.quests, t('{n} quests', { n: list.length }), table([[t('Quest')], [t('Given by')], [t('Where')], [t('Level'), 1], [t('Experience'), 1], [t('Zen'), 1], [t('Repeats')]], list.map(q => {
      const g = F && F[q.giver];
      return `<tr><td>${a('quests', q.slug, q.n)}</td><td>${g ? a('npcs', g.slug, q.giverName || g.n, 'crumb') : esc(q.giverName || '')}</td><td class="mute wrap">${esc(q.place || '')}</td><td class="r num">${q.minLevel ? N(q.minLevel) : '—'}</td>
<td class="r"><span class="num" style="color:#B9A2F2">${N(q.experience)}</span>${q.firstExperience ? `<span class="sub2">${esc(t('first time {n}', { n: q.firstExperience.toLocaleString('en') }))}</span>` : ''}</td><td class="r num" style="color:#F2C14E">${N(q.zen)}</td><td class="mute">${esc(repeats(q.repeatSeconds))}</td></tr>`;
    })));
  }
  function questPage(e) {
    crumbs([[LIST.quests, listHref('quests')], [e.title || e.name]]);
    const F = by('npcs', 'npc'), I = by('items', 'k'), M = by('monsters', 'num'), Q = by('quests', 'k');
    const g = F && F[e.giver], rcv = F && F[e.receiver];
    const quote = (lines) => (lines || []).length ? `<blockquote class="say">${(Array.isArray(lines) ? lines : [lines]).map(l => `<p>${esc(l)}</p>`).join('')}</blockquote>` : '';
    const steps = (e.steps || []).map(s => {
      const m = M && M[s.target];
      const what = s.kind === 'clear' || s.kind === 'kill' ? `${esc(t('Kill'))} <b class="num">${N(s.count)}</b> ${m ? a('monsters', m.slug, s.line || m.n) : esc(s.line || '')}`
        : s.item && I && I[s.item] ? `${esc(pretty(s.kind))} ${s.count > 1 ? `<b class="num">${N(s.count)}</b> ` : ''}${a('items', I[s.item].slug, I[s.item].n)}` : esc(s.line || pretty(s.kind || ''));
      return `<div style="display:flex;gap:12px;align-items:center;padding:10px 0;border-top:1px solid #16110E"><span style="width:18px;height:18px;flex:0 0 18px;border:1px solid #6A5426;border-radius:3px"></span><span>${what}</span></div>`;
    }).join('');
    const paid = (e.paid || []).map(p => {
      const r = I && I[p.item];
      const meta = [p.count > 1 ? '×' + p.count : '', p.plus ? '+' + p.plus : '', p.class ? t('for a {cls}', { cls: CLS[p.class] || p.class }) : '', p.first_only ? t('the first time') : '',
        p.sockets ? t('{n} sockets', { n: p.sockets }) : '', p.power ? t('with a rune') : ''].filter(Boolean).join(' · ');
      return `<div style="padding:3px 0">${r ? `<span class="nm" data-card="${esc(r.slug)}">${a('items', r.slug, r.n)}<span class="pop" role="tooltip">${simpleCard(r)}</span></span>` : esc(p.item)} <span class="mute small">${esc(meta)}</span></div>`;
    }).join('');
    const after = (e.afterAny || []).map(k => Q && Q[k] ? `<a class="sim" style="padding-left:0" href="${href('quests', Q[k].slug)}">${esc(Q[k].n)}</a>` : '').join('');
    return `<div class="top2"><div class="grow"><h1 class="h1 big">${esc(e.title || e.name)}</h1><div class="mute" style="font-size:15px;margin-top:4px">${esc(t('Given by'))} ${g ? a('npcs', g.slug, e.giverName || g.n, 'crumb') : esc(e.giverName || '')}${e.place ? ' · ' + esc(e.place) : ''}</div></div>
${facts([e.minLevel ? [t('Level'), e.minLevel] : null, [t('Repeats'), repeats(e.repeatSeconds)], (e.natives || []).length ? [t('For'), e.natives.join(', ')] : null])}</div>
<div class="split"><aside class="side">${g && g.t ? shot({ page: g.t.replace('.96.webp', '.webp') }, g.n) : ''}<div class="icard"><span class="h3" style="margin:0">${esc(t('Requirements'))}</span>${kv([[t('Level'), e.minLevel || t('Any')], [t('Classes'), (e.natives || []).map(c => CLS[c] || c).join(', ') || t('Any')], e.second ? [t('Second class'), t('Yes')] : null])}
${after ? `<div class="c-rule"></div><span class="mute small">${esc(t('After any of'))}</span>${after}` : ''}${rcv && e.receiver !== e.giver ? `<div class="c-rule"></div><span class="mute small">${esc(t('Hand it in to'))}</span>${a('npcs', rcv.slug, e.receiverName || rcv.n, 'crumb')}` : ''}</div></aside>
<div class="body">${quote(e.offer)}<section><h2 class="h2">${esc(t('Objectives'))}</h2>${steps || absent(t('No steps in this release\'s data.'))}</section>
<section><h2 class="h2">${esc(t('Rewards'))}</h2><div class="pay">${e.experience ? `<div><div class="k">${esc(t('Experience'))}</div><div class="v" style="color:#B9A2F2">${N(e.experience)}</div>${e.firstExperience ? `<div class="mute small">${esc(t('first time {n}', { n: e.firstExperience.toLocaleString('en') }))}</div>` : ''}</div>` : ''}
${e.zen ? `<div><div class="k">Zen</div><div class="v" style="color:#F2C14E">${N(e.zen)}</div></div>` : ''}</div>${paid ? `<div class="rbox"><b>${esc(t('Items'))}</b>${paid}</div>` : ''}${e.boon ? `<p>${esc(e.boon)}</p>` : ''}</section>
${(e.handIn || []).length ? `<section><h2 class="h3">${esc(t('When you return'))}</h2>${quote(e.handIn)}</section>` : ''}${e.next ? `<p class="mute">${esc(e.next)}</p>` : ''}
<section><h2 class="h2">${esc(t('Comments'))}</h2>${comments('quests', e.slug, [])}</section></div></div>`;
  }

  // ---- events
  function eventsList() {
    const rows = rowsOf('events');
    crumbs([[LIST.events]]);
    if (rows === undefined) return listPage(LIST.events, '', ld);
    if (rows === null || !rows.length) return listPage(LIST.events, '', waitDump('drops'));
    return listPage(LIST.events, t('Each castle and square has its own monsters and drops'), table([[t('Event')], [t('Monster levels'), 1], [t('Monsters'), 1], [t('Items drop'), 1]], rows.map(v =>
      `<tr><td>${a('events', v.slug, v.n)}${notLive(v.live)}</td><td class="r num">${v.levels ? `${v.levels[0]}–${v.levels[1]}` : '—'}</td><td class="r num">${N(v.monsters)}</td><td class="r num">${N(v.items)}</td></tr>`), 'sm'));
  }
  function eventPage(e) {
    crumbs([[LIST.events, listHref('events')], [e.name]]);
    const rows = rowsOf('events') || [];
    const sib = rows.filter(r => r.type === e.type);
    const cats = [...new Set((e.drops || []).map(d => d.cat).filter(Boolean))];
    const cat = cats.includes(R.q.cat) ? R.q.cat : '';
    const shown = (e.drops || []).filter(d => !cat || d.cat === cat);
    const all = R.q.all === 'all';
    const levels = (e.monsters || []).map(m => m.level).filter(x => x != null);
    return `<div class="top2"><div class="grow"><h1 class="h1 big"${e.type === 'raid' ? ' style="color:#F2C14E"' : ''}>${esc(e.name)}</h1>${(e.towns || []).length ? `<div class="mute" style="font-size:15px;margin-top:4px">${esc(t('Lands in {towns}', { towns: e.towns.map(x => x.name).join(', ') }))}</div>` : ''}</div>
${facts([levels.length ? [t('Monster levels'), `${Math.min(...levels)}–${Math.max(...levels)}`] : null, [t('Monsters'), (e.monsters || []).length], [t('Items drop'), (e.drops || []).length]])}${notLive(e.live)}</div>
${sib.length > 1 ? `<div class="chips" style="margin-bottom:32px">${sib.map(r => `<a class="chip" href="${href('events', r.slug)}" aria-pressed="${r.slug === e.slug ? 'true' : 'false'}">${esc(r.n)}</a>`).join('')}</div>` : ''}
<div class="split"><aside class="side"><section><h2 class="h2">${esc(t('Monsters'))}</h2>${(e.monsters || []).map(m => `<a class="sim" href="${href('monsters', m.slug)}"><span style="flex:1;color:#F3E8D4">${esc(m.name)}</span><span class="mute num small">${esc(t('Lv {n}', { n: m.level }))} · ${esc(t('{n} items', { n: m.items }))}</span></a>`).join('')}</section>
${(e.towns || []).length ? `<section><h2 class="h2">${esc(t('Where it lands'))}</h2>${e.towns.map(x => `<a class="sim" href="${href('worlds', x.world_slug)}">${esc(x.name)}</a>`).join('')}</section>` : ''}</aside>
<section class="body" style="gap:0"><div class="ttl"><h2 class="h2" style="margin:0">${esc(t('Drops here'))}</h2><span class="mute">${esc(t('{n} items', { n: shown.length }))}</span></div>
${cats.length > 1 ? `<div class="chips" style="margin-bottom:16px">${chip(esc(t('All · {n}', { n: (e.drops || []).length })), !cat, { cat: '' })}${cats.map(x => chip(esc(t(x)), cat === x, { cat: x, all: 'all' })).join('')}</div>` : ''}
${table([[t('Item')], [t('Type')], [t('Best from')], [t('Chance a kill'), 1]], (all ? shown : shown.slice(0, 30)).map(d => `<tr><td><span class="nm" data-card="${esc(d.slug)}">${a('items', d.slug, d.n)}<span class="pop" role="tooltip"></span></span>${d.excellent ? `<span class="sub2" style="color:#7FD66A">${esc(t('can be excellent'))}</span>` : ''}</td>
<td class="mute">${esc(d.sub || '')}</td><td>${a('monsters', d.from_slug, d.from_name, 'crumb')}</td><td class="r">${oddsCell(d.chance)}</td></tr>`), 'sm pops')}${more(all ? shown.length : Math.min(30, shown.length), shown.length, 'all')}
<p class="note">${esc(t('Chances are per kill of the monster named, for one player, before Item Find, talents and party bonuses.'))}</p>
<section style="margin-top:28px"><h2 class="h2">${esc(t('Comments'))}</h2>${comments('events', e.slug, [])}</section></section></div>`;
  }

  // ---- runes
  const runeTone = (r) => MUCard.ink(RARITY_TONE[r.rarity] || 'White');
  function runesList() {
    const rows = rowsOf('runes');
    crumbs([[LIST.runes]]);
    if (rows === undefined) return listPage(LIST.runes, '', missing.has('rules') ? waitDump('rules') : ld);
    const rar = [...new Set(rows.map(r => r.rarity_name).filter(Boolean))];
    const list = rows.filter(r => !R.q.r || r.rarity_name === R.q.r).sort((x, y) => (x.hero_level || 0) - (y.hero_level || 0) || (x.n > y.n ? 1 : -1));
    return listPage(LIST.runes, t('{n} runes', { n: list.length }), `<div class="chips" style="margin-bottom:16px">${chip(esc(t('All')), !R.q.r, { r: '' })}${rar.map(x => chip(esc(x), R.q.r === x, { r: R.q.r === x ? '' : x })).join('')}</div>` +
      table([[t('Rune')], [t('Effect')], [t('Hero level'), 1], [t('Class')], [t('Fits')]], list.map(r => `<tr><td style="white-space:nowrap"><a class="open" style="color:${runeTone(r)} !important" href="${href('runes', r.slug)}">${esc(r.n)}</a><span class="sub2">${esc(r.rarity_name || '')}</span>${notLive(r.live)}</td>
<td class="wrap">${esc(r.tells || '')}</td><td class="r num">${N(r.hero_level)}</td><td class="mute">${esc((r.classes || []).join(', '))}</td><td class="mute">${esc((r.slots || []).join(', '))}</td></tr>`), 'wrapc'));
  }
  function runePage(e) {
    crumbs([[LIST.runes, listHref('runes')], [e.name]]);
    const tone = runeTone(e);
    const ch = e.chance || {};
    const who = Object.keys(ch);
    const levels = [...new Set(who.flatMap(c => Object.keys(ch[c])))].sort((x, y) => x - y);
    const label = (c) => (c.endsWith('2') ? t('{cls}, second class', { cls: c.slice(0, -1) }) : c);
    return `<div class="split"><aside class="side"><div class="icard" style="border-color:${tone}"><h1 style="margin:0;font:700 24px/1.2 Cinzel,serif;color:${tone}">${esc(e.name)}</h1><span style="font-size:15px;color:${tone}">${esc(t('{rarity} rune', { rarity: e.rarity_name || '' }))}</span>
<div class="c-rule"></div><span style="font-size:17px;color:#E8A15A">${esc(e.tells || '')}</span><div class="c-rule"></div>${kv([[t('Hero level'), e.hero_level], [t('Drops from monster level'), e.from_level], [t('Classes'), (e.classes || []).join(', ')], [t('Fits'), (e.slots || []).join(', ')],
      [t('Stacks'), e.stacking && e.stacking.says ? e.stacking.says : null], [t('Sells for'), e.sale != null ? pct(e.sale) + ' ' + t('of its item') : null]])}
${e.live === false ? `<div class="c-rule"></div><span style="color:#7FD7F0">${esc(t('Not dropping yet.'))}</span>` : ''}</div></aside>
<div class="body"><section><h2 class="h2">${esc(t('Where it comes from'))}</h2>${who.length ? `<p class="mute" style="margin:0 0 12px">${esc(e.chance_note || '')}</p>${table([[t('Kill level from')]].concat(who.map(c => [label(c), 1])), levels.map(l => `<tr><td class="num">${esc(l)}</td>${who.map(c => `<td class="r">${oddsCell(ch[c][l])}</td>`).join('')}</tr>`), 'sm')}` : absent(t('It does not drop from monsters.'))}</section>
<section><h2 class="h2">${esc(t('Comments'))}</h2>${comments('runes', e.slug, [])}</section></div></div>`;
  }

  // ---- skills
  function skillsList() {
    const rows = rowsOf('skills');
    crumbs([[LIST.skills]]);
    if (rows === undefined) return listPage(LIST.skills, '', missing.has('skills') ? waitDump('skills') : ld);
    const cls = [...new Set(rows.flatMap(r => r.classes || []))];
    const list = rows.filter(r => !R.q.cls || (r.classes || []).includes(R.q.cls)).sort((x, y) => (x.kin > y.kin ? 1 : x.kin < y.kin ? -1 : (x.needLevel || 0) - (y.needLevel || 0)));
    return listPage(LIST.skills, t('{n} skills', { n: list.length }), `<div class="chips" style="margin-bottom:16px">${chip(esc(t('All')), !R.q.cls, { cls: '' })}${cls.map(c => chip(esc(CLS[c] || c), R.q.cls === c, { cls: R.q.cls === c ? '' : c })).join('')}</div>` +
      table([[''], [t('Skill')], [t('Class')], [t('Level'), 1], [t('Mana'), 1], [t('Reach'), 1], [t('What it does')]], list.map(s => `<tr><td style="width:56px">${s.icon ? `<span class="ticon" style="background-image:url(${pic(s.icon)})"></span>` : ''}</td><td>${a('skills', s.slug, s.n)}</td>
<td class="mute">${esc((s.classes || []).join(', '))}</td><td class="r num">${s.needLevel ? N(s.needLevel) : '—'}</td><td class="r num">${N(s.mana)}</td><td class="r num">${s.reach ? N(s.reach) : '—'}</td><td class="wrap mute" style="min-width:260px">${esc(s.tells || '')}</td></tr>`)));
  }
  function skillPage(e) {
    crumbs([[LIST.skills, listHref('skills')], [e.name]]);
    const trees = (e.classes || []).map(c => [c, have(`talents/${String(c).toLowerCase()}.json`)]);
    const feeds = [];
    trees.forEach(([c, tr]) => { const cols = tr && (tr.columns || tr.tree || tr.trees || tr); (Array.isArray(cols) ? cols : []).forEach(col => { if ((col.feeds || []).includes(e.number)) feeds.push(`${CLS[c] || c}: ${col.name}`); }); });
    const NUM = [['needLevel', t('Level')], ['mana', t('Mana')], ['damage', t('Damage')], ['reach', t('Reach')], ['spread', t('Shape')], ['coolTicks', t('Cooldown (ticks)')],
      ['boonTicks', t('Lasts (ticks)')], ['splash', t('Splash')], ['summons', t('Summons')], ['force', t('Force')]];
    const rest = Object.entries(e).filter(([k, v]) => !['kind', 'slug', 'commit', 'protocol', 'name', 'tells', 'icon_png', 'classes'].includes(k) && (typeof v !== 'object' || v === null));
    return `<div class="split"><aside class="side"><div class="icard"><div style="display:flex;gap:16px;align-items:center">${e.icon_png ? `<span class="ticon" style="width:46px;height:64px;border-color:#6A5426;background-image:url(${pic(e.icon_png)})"></span>` : ''}
<div><h1 style="margin:0;font:700 24px/1.2 Cinzel,serif;color:#F3E8D4">${esc(e.name)}</h1><span class="mute">${esc((e.classes || []).map(c => CLS[c] || c).join(', '))}</span></div></div><div class="c-rule"></div><span>${esc(e.tells || '')}</span></div>
${feeds.length ? `<section class="rbox"><b>${esc(t('Talent columns that feed it'))}</b>${feeds.map(f => `<div>· ${esc(f)}</div>`).join('')}<a class="crumb" style="text-decoration:underline" href="${listHref('talents')}">${esc(t('Open the talent calculator'))}</a></section>` : ''}</aside>
<div class="body"><section><h2 class="h2">${esc(t('Numbers'))}</h2><div class="tbox"><table class="tbl sm wrapc"><tbody>${NUM.filter(([k]) => e[k] != null && e[k] !== 0 && e[k] !== '').map(([k, l]) => `<tr><th scope="row" class="plain rowh">${esc(l)}</th><td>${esc(e[k])}</td></tr>`).join('')}</tbody></table></div></section>
<details class="rbox"><summary style="cursor:pointer">${esc(t('Every number the game has for it'))}</summary><div class="tbox" style="margin-top:12px"><table class="tbl sm"><tbody>${rest.map(([k, v]) => `<tr><th scope="row" class="plain rowh">${esc(k)}</th><td class="num">${esc(v)}</td></tr>`).join('')}</tbody></table></div></details>
<section><h2 class="h2">${esc(t('Comments'))}</h2>${comments('skills', e.slug, [])}</section></div></div>`;
  }

  // ---- the talent calculator: the game's tree (sim/talents_table.inc via the dump), its rules mirrored
  // from sim/talents.cpp (talentOpen, talentTakeable, talentPoints) with the dump's constants.
  function talentsPage(slug) {
    const rows = rowsOf('talents');
    crumbs([[LIST.talents]]);
    if (rows === undefined) return ld;
    if (!rows || !rows.length) return listPage(LIST.talents, '', waitDump('talents'));
    const cls = rows.some(r => r.slug === slug) ? slug : rows[0].slug;
    const T = have(`talents/${cls}.json`);
    if (!T) return listPage(LIST.talents, '', T === undefined ? ld : waitDump('talents'));
    const K = T.constants || {};
    const per = (K.pointsPerLevel || {})[T.class] || 5, maxLv = K.kMaximumLevel || 400, perTalent = K.kStatPointsPerTalent || 25;
    const smallMost = K.kTalentSmallMost || 10, opensAt = K.kTalentMajorOpensAt || 10;
    const lv = Math.max(1, Math.min(maxLv, +R.q.lv || maxLv)), rb = Math.max(0, +R.q.rb || 0);
    const table0 = T.points && T.points[String(lv)];
    const total = table0 && table0[rb] != null ? table0[rb] : Math.floor(((lv - 1) * per + rb * maxLv * per) / perTalent);
    const cols = T.trees || T.columns || (T.tree && T.tree.trees && T.tree.trees[T.class]) || [];
    const ranks = cols.map((c, ci) => (c.nodes || []).map((n, i) => { const ch = (R.q.p || '').charAt(ci * 9 + i); return ch ? parseInt(ch, 36) || 0 : 0; }));
    const isMajor = (i) => i % 3 === 2, most = (i) => (isMajor(i) ? 1 : smallMost);
    const open = (col, i) => { const tier = Math.floor(i / 3); return isMajor(i) ? (col[i - 2] >= opensAt || col[i - 1] >= opensAt) : (tier === 0 || col[tier * 3 - 1] >= 1); };
    const takeable = (col, i) => { if (!col[i]) return false; const next = col.slice(); next[i]--; return next.every((r, j) => !r || open(next, j)); };
    const spent = ranks.flat().reduce((s, x) => s + x, 0), left = total - spent;
    const code = (rk) => rk.map(c => c.map(x => x.toString(36)).join('').padEnd(9, '0')).join('').replace(/0+$/, '');
    const set = (ci, i, d) => { const rk = ranks.map(c => c.slice()); rk[ci][i] += d; return qLink({ p: code(rk) }); };
    const icon = (n) => (T.icons && T.icons[String(n.icon)] ? `background-image:url(${pic(T.icons[String(n.icon)])})` : '');
    const said = (n) => (n.amount && !isMajor(0) ? '' : '') + n.text;
    const picks = [];
    const colsHtml = cols.map((c, ci) => {
      const col = ranks[ci];
      const tiers = [0, 1, 2].map(tr => [tr * 3, tr * 3 + 1, tr * 3 + 2].filter(i => c.nodes[i]));
      return `<div class="tcol"><h2>${esc(c.name)}</h2><div class="mute small" style="text-align:center;min-height:40px;margin:4px 0 10px">${esc(c.sub || '')}</div>${tiers.map(tier => {
        const node = (i) => {
          const n = c.nodes[i], have2 = col[i], op = open(col, i), maj = isMajor(i);
          if (have2) picks.push(`<div style="padding:3px 0"><b style="color:${maj ? '#F2D58A' : '#F3E8D4'}">${esc(n.name)}${maj ? '' : ' ' + have2 + '/' + most(i)}</b> <span class="mute">${esc(maj ? n.text : (n.amount ? `+${+(n.amount * have2).toFixed(2)} ` : '') + n.text)}</span></div>`);
          const tip = `${n.name} — ${maj ? n.text : (n.amount ? `+${n.amount} ${t('a point')}: ` : '') + n.text}${op ? '' : ' · ' + (maj ? t('opens when a talent above it is full') : t('opens with the major above it'))}`;
          return `<div class="tnode${maj ? ' major' : ''}${op || have2 ? '' : ' off'}" title="${esc(tip)}"><span class="ic" aria-hidden="true" style="${icon(n)};border-color:${have2 ? (maj ? '#F2C14E' : '#C2362B') : op ? '#6A5426' : '#2A211B'}"></span>
<span class="num small" style="color:${have2 ? '#F2D58A' : op ? '#C9BFAE' : '#6F665A'}">${maj ? esc(n.name) : `${have2}/${most(i)}`}</span>
<span style="display:flex;gap:4px"><a class="tb" role="button" data-q data-keep ${have2 && takeable(col, i) ? `href="${esc(set(ci, i, -1))}"` : 'aria-disabled="true" style="color:#3A322B;pointer-events:none"'} aria-label="${esc(t('Take a point from {name}', { name: n.name }))}">−</a>
<a class="tb" role="button" data-q data-keep ${op && have2 < most(i) && left > 0 ? `href="${esc(set(ci, i, 1))}"` : 'aria-disabled="true" style="color:#3A322B;pointer-events:none"'} aria-label="${esc(t('Add a point to {name}', { name: n.name }))}">+</a></span></div>`;
        };
        return `<div style="display:flex;justify-content:center;gap:14px;margin-bottom:8px">${tier.filter(i => !isMajor(i)).map(node).join('')}</div>${tier.filter(isMajor).map(i => `<div style="margin:0 auto 16px">${node(i)}</div>`).join('')}`;
      }).join('')}</div>`;
    }).join('');
    const step = (d) => qLink({ lv: String(Math.max(1, Math.min(maxLv, lv === 1 && d > 0 ? 10 : lv + d))) });
    return `<div class="ttl"><h1>${esc(t('Talent calculator'))}</h1><span class="mute" style="font-size:15px">${esc(t('{per} stat points a level, a talent for each {n}', { per, n: perTalent }))}</span></div>
<p class="mute" style="margin:0 0 20px">${esc(t('Small talents take up to {n} points. A major opens when a small talent above it holds {m}, and taking it opens the next tier.', { n: smallMost, m: opensAt }))}</p>
<div class="tbar"><div class="chips">${rows.map(r => `<a class="chip" data-q data-keep href="${P}/armory/talents/${r.slug}${location.search.replace(/([?&])p=[^&]*/, '$1').replace(/[?&]$/, '')}" aria-pressed="${r.slug === cls ? 'true' : 'false'}">${esc(CLS[r.k] || r.n)}</a>`).join('')}</div>
<div class="ctlrow" style="gap:10px"><span>${esc(t('Level'))}</span><a class="stp" role="button" data-q data-keep href="${esc(step(-10))}" aria-label="${esc(t('10 levels less'))}" style="display:inline-flex;align-items:center;justify-content:center">−</a><b class="num" style="min-width:40px;text-align:center">${lv}</b><a class="stp" role="button" data-q data-keep href="${esc(step(10))}" aria-label="${esc(t('10 levels more'))}" style="display:inline-flex;align-items:center;justify-content:center">+</a></div>
<div class="ctlrow" style="gap:10px"><span>${esc(t('Rebirths'))}</span><a class="stp" role="button" data-q data-keep href="${esc(qLink({ rb: rb > 1 ? String(rb - 1) : '' }))}" aria-label="${esc(t('One rebirth less'))}" style="display:inline-flex;align-items:center;justify-content:center">−</a><b class="num" style="min-width:24px;text-align:center">${rb}</b><a class="stp" role="button" data-q data-keep href="${esc(qLink({ rb: String(rb + 1) }))}" aria-label="${esc(t('One rebirth more'))}" style="display:inline-flex;align-items:center;justify-content:center">+</a></div>
<div style="margin-left:auto;display:flex;gap:16px;align-items:center"><span style="font-size:17px">${esc(t('Points left'))} <b class="num" style="color:${left < 0 ? '#E07A6C' : '#F2D58A'};font-size:22px">${left}</b> <span class="mute">${esc(t('of {n}', { n: total }))}</span></span><a class="abtn dark" data-q data-keep href="${esc(qLink({ p: '' }))}">${esc(t('Reset'))}</a></div></div>
<div style="overflow-x:auto;padding-bottom:8px"><div style="display:flex;gap:12px;min-width:max-content">${colsHtml}</div></div>
<div class="rbox" style="margin-top:20px"><b>${esc(t('Your picks'))}</b>${picks.join('') || `<span class="mute">${esc(t('Nothing taken yet. Point at a talent to read it.'))}</span>`}<p class="mute small" style="margin:10px 0 0">${esc(t('The address holds your picks: copy it to share the build.'))}</p></div>`;
  }

  // ---- the Chaos Machine, Elites, boxes
  function mixCard(m, full) {
    const F = by('npcs', 'npc'), I = by('items', 'k');
    const npc = F && F[m.npc];
    const asks = (m.asks || []).map(x => `<div style="padding:2px 0"><span style="color:#F3E8D4">${esc(x.name)}</span> <span class="mute">${x.need ? (x.at_least ? t('{n} or more', { n: x.need }) : '×' + x.need) : t('optional')}</span></div>`).join('');
    const res = (m.result || []).map(r => { const it = I && I[r.item]; return `<div style="padding:2px 0">${it ? a('items', it.slug, r.name || it.n) : esc(r.name || r.item)} <span class="mute">${esc(r.class ? CLS[r.class] || r.class : '')}</span></div>`; }).join('');
    return `<article class="rbox" style="padding:20px 24px;margin-bottom:16px"><div style="display:flex;flex-wrap:wrap;justify-content:space-between;gap:8px 16px;align-items:baseline;margin-bottom:12px">
<h2 style="margin:0;font:700 22px/1.2 Cinzel,serif">${full ? esc(m.name || m.n) : a('mixes', m.slug, m.name || m.n, 'open')}</h2>${npc ? a('npcs', npc.slug, npc.n, 'crumb') : ''}</div>
<div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:16px 28px"><div><div class="h3" style="margin-bottom:8px">${esc(t('Put in'))}</div>${asks || `<span class="mute">${esc(m.title || '')}</span>`}</div>
<div><div class="h3" style="margin-bottom:8px">${esc(t('Success'))}</div>${kv([[t('Chance'), m.rate ? m.rate + '%' : t('by what you put in')], m.zen ? [t('Zen'), N(m.zen), 1] : null])}</div>
<div><div class="h3" style="margin-bottom:8px">${esc(t('You get'))}</div>${res || `<span>${esc(m.success || '')}</span>`}${m.failure ? `<div style="margin-top:8px;color:#E8A15A;font-size:15px">${esc(t('On failure: {what}', { what: m.failure }))}</div>` : ''}</div></div></article>`;
  }
  function mixesList() {
    const rows = rowsOf('mixes');
    crumbs([[LIST.mixes]]);
    if (rows === undefined) return listPage(LIST.mixes, '', missing.has('rules') ? waitDump('rules') : ld);
    const list = rows.filter(m => m.listed !== false);
    // The list rows carry scalars only; a mix's asks and results are its own file's.
    return listPage(LIST.mixes, t('{n} mixes', { n: list.length }), list.map(m => { const e = have(`mixes/${m.slug}.json`); return e ? mixCard(e) : mixCard(m); }).join(''));
  }
  function elitesList() {
    const rows = rowsOf('elites');
    crumbs([[LIST.elites]]);
    if (rows === undefined) return listPage(LIST.elites, '', missing.has('rules') ? waitDump('rules') : ld);
    const e0 = rows[0] || {};
    return listPage(LIST.elites, t('{n} maps', { n: rows.length }), `<div class="roll" style="margin-bottom:20px"><div class="rbox"><b>${esc(t('What sets them apart'))}</b>${kv([[t('Damage'), e0.damage != null ? '×' + e0.damage : null], [t('Defense'), e0.defense != null ? '×' + e0.defense : null], [t('Experience'), e0.experience != null ? '×' + e0.experience : null], [t('Zen'), e0.zen_times != null ? '×' + e0.zen_times : null], [t('Levels higher'), e0.levels_up]])}</div>
<div class="rbox"><b>${esc(t('Loot'))}</b>${kv([[t('Pieces'), e0.pieces ? e0.pieces.join('–') : null], [t('A jewel'), e0.jewel_in_100 != null ? e0.jewel_in_100 + '%' : null], [t('A second jewel'), e0.second_jewel_in_100 != null ? e0.second_jewel_in_100 + '%' : null], [t('A second excellent'), e0.second_excellent_in_100 != null ? e0.second_excellent_in_100 + '%' : null], [t('Luck'), e0.luck_in_100 != null ? e0.luck_in_100 + '%' : null]])}</div></div>` +
      table([[t('Map')], [t('At once'), 1], [t('Life'), 1], [t('Damage'), 1], [t('Defense'), 1], [t('Rises every')]], rows.map(e => `<tr><td>${a('elites', e.slug, worldName(e.world || e.k))}${notLive(e.live)}</td><td class="r num">${N(e.count)}</td><td class="r num" style="color:#F2C14E">×${N(e.life)}</td><td class="r num">×${N(e.damage)}</td><td class="r num">×${N(e.defense)}</td>
<td class="mute">${e.rise_seconds ? esc(t('{a}–{b} min', { a: e.rise_seconds[0] / 60, b: e.rise_seconds[1] / 60 })) : '—'}</td></tr>`), 'sm'));
  }
  function genericPage(kind, e) {
    crumbs([[LIST[kind] || kind, listHref(kind)], [e.name || e.title || e.slug]]);
    let body = '';
    if (kind === 'mixes') body = mixCard(e, true);
    else if (kind === 'boxes') {
      const I = by('items', 'k');
      const res = (e.result || []).slice().sort((x, y) => (y.chance || 0) - (x.chance || 0));
      body = table([[t('Item')], [t('How')], [t('Chance a box'), 1]], res.map(r => { const it = I && I[r.item]; return `<tr><td>${it ? `<span class="nm" data-card="${esc(it.slug)}">${a('items', it.slug, it.n)}<span class="pop" role="tooltip">${simpleCard(it)}</span></span>` : esc(r.item)}</td><td style="color:${r.excellent ? '#7FD66A' : '#C9BFAE'}">${esc(ROLL[r.roll] || pretty(r.roll || ''))}</td><td class="r">${oddsCell(r.chance)}</td></tr>`; }), 'sm pops');
    } else {
      const rest = Object.entries(e).filter(([k]) => !['kind', 'slug', 'commit', 'protocol', 'key'].includes(k));
      body = `<div class="tbox"><table class="tbl sm wrapc"><tbody>${rest.map(([k, v]) => `<tr><th scope="row" class="plain rowh">${esc(pretty(k))}</th><td>${esc(Array.isArray(v) ? v.map(x => (typeof x === 'object' ? JSON.stringify(x) : x)).join(', ') : typeof v === 'object' && v ? JSON.stringify(v) : v)}</td></tr>`).join('')}</tbody></table></div>`;
      if (kind === 'elites') body = `<div class="roll" style="margin-bottom:20px"><div class="rbox"><b>${esc(worldName(e.world))}</b>${kv([[t('At once'), e.count], [t('Life'), '×' + e.life], [t('Damage'), '×' + e.damage], [t('Defense'), '×' + e.defense], [t('Levels higher'), e.levels_up]])}</div></div>` + body;
    }
    return `<div class="top2"><div class="grow"><h1 class="h1 big">${esc(e.name || e.title || e.slug)}</h1></div>${notLive(e.live)}</div><div class="body">${body}<section><h2 class="h2">${esc(t('Comments'))}</h2>${comments(kind, e.slug, [])}</section></div>`;
  }
  function genericList(kind) {
    const rows = rowsOf(kind);
    crumbs([[LIST[kind] || kind]]);
    if (rows === undefined) return listPage(LIST[kind] || kind, '', ld);
    return listPage(LIST[kind] || kind, t('{n} in all', { n: rows.length }), table([[t('Name')]], rows.map(r => `<tr><td>${a(kind, r.slug, r.n)}</td></tr>`), 'sm'));
  }

  // ---- drawing
  function view() {
    if (!B.data) return absent(t('The Armory has not been built yet.'));
    if (R.view === 'list') {
      const L = { items: itemsList, monsters: monstersList, npcs: npcsList, quests: questsList, events: eventsList, runes: runesList, skills: skillsList,
        talents: () => talentsPage(''), mixes: mixesList, elites: elitesList, worlds: worldsList };
      return (L[R.kind] || (() => genericList(R.kind)))();
    }
    if (R.kind === 'talents') return talentsPage(R.slug);
    const e = have(`${R.kind}/${R.slug}.json`);
    if (e === undefined) return ld;
    if (e === null) { crumbs([[t('Not found')]]); return absent(t('Nothing by that name in this release.')); }
    const pages = { items: itemPage, monsters: monsterPage, npcs: npcPage, worlds: worldPage, quests: questPage, events: eventPage, runes: runePage, skills: skillPage };
    return (pages[R.kind] || ((x) => genericPage(R.kind, x)))(e);
  }
  // Every page's indexes, asked up front so they come together.
  const NEEDS = { items: ['items'], monsters: ['items', 'worlds', 'events', 'elites'], npcs: ['items', 'worlds', 'quests', 'mixes'], worlds: ['worlds'], quests: ['npcs', 'items', 'monsters', 'quests'],
    events: ['events'], runes: [], skills: [], mixes: ['npcs', 'items'], elites: ['worlds'], boxes: ['items'], talents: ['talents'] };
  function titleOf() {
    const e = R.view === 'entity' ? files[`${R.kind}/${R.slug}.json`] : null;
    const name = e ? (e.name || e.title || e.class) : (LIST[R.kind] || '');
    return `${name || t('Armory')} — MU Classic Armory`;
  }
  let drawing = false;
  function draw() {
    if (drawing) return;
    drawing = true;
    requestAnimationFrame(() => {
      drawing = false;
      (NEEDS[R.kind] || []).concat(R.view === 'list' ? [R.kind] : []).forEach(k => { if ((B.kinds || []).includes(k)) have(`index/${k}.json`); });
      const keepDraft = document.getElementById('cmt');
      if (keepDraft) draft = keepDraft.value;
      VIEW.innerHTML = view();
      document.title = titleOf();
    });
  }

  // ---- links, buttons, search
  const ours = (u) => u.origin === location.origin && (u.pathname === P + '/armory' || u.pathname.startsWith(P + '/armory/')) && !u.pathname.includes('/armory/data/') && !u.pathname.endsWith('.xml');
  document.addEventListener('click', async (ev) => {
    if (ev.defaultPrevented || ev.button !== 0 || ev.metaKey || ev.ctrlKey || ev.shiftKey || ev.altKey) return;
    const link = ev.target.closest('a[href]');
    if (link && !link.target) {
      const u = new URL(link.href, location.href);
      if (ours(u)) {
        ev.preventDefault();
        hideHits();
        go(u.pathname + u.search, link.hasAttribute('data-keep'));
        return;
      }
    }
    const b = ev.target.closest('button');
    if (!b || !window.MU) return;
    if (b.dataset.post) {
      const [kind, slug, about] = b.dataset.post.split('|');
      const box = document.getElementById('cmt');
      const text = (box ? box.value : '').trim();
      if (!text) return;
      b.disabled = true;
      const r = await act('/api/armory/comments', { kind, slug, about, text });
      postErr = r.status === 201 ? '' : (r.said.error || t('Could not post it.'));
      if (r.status === 201) draft = '';
      if (box && r.status === 201) box.value = '';
      loadTalk(kind, slug, true);
    } else if (b.dataset.vote || b.dataset.report || b.dataset.delete) {
      const id = +(b.dataset.vote || b.dataset.report || b.dataset.delete);
      if (b.dataset.delete && !confirm(t('Delete this comment?'))) return;
      const why = b.dataset.report ? (prompt(t('What is wrong with it? (optional)')) ?? null) : '';
      if (why === null) return;
      await act(b.dataset.vote ? '/api/armory/vote' : b.dataset.report ? '/api/armory/report' : '/api/armory/delete', { id, why });
      const k = R.view === 'entity' ? [R.kind, R.slug] : null;
      if (k) loadTalk(k[0], k[1], true);
    }
  });
  window.addEventListener('popstate', () => { R = routeOf(); draw(); });

  const input = document.getElementById('aq');
  const hits = document.getElementById('ahits');
  const KIND_NAME = { item: t('Item'), monster: t('Monster'), npc: t('NPC'), world: t('Map'), quest: t('Quest'), rune: t('Rune'), skill: t('Skill'), event: t('Event'), mix: t('Mix'), box: t('Box'), elite: t('Elite') };
  let sel = -1;
  function hideHits() { hits.hidden = true; input.setAttribute('aria-expanded', 'false'); sel = -1; }
  function showHits() {
    const s = have('search.json');
    const q = input.value.trim().toLowerCase();
    if (!q) return hideHits();
    if (!s) { hits.innerHTML = `<p>${esc(t('Loading…'))}</p>`; hits.hidden = false; return; }
    const all = (s.rows || []).filter(r => r.n.toLowerCase().includes(q));
    all.sort((x, y) => (x.n.toLowerCase().startsWith(q) ? 0 : 1) - (y.n.toLowerCase().startsWith(q) ? 0 : 1) || x.n.length - y.n.length);
    const top = all.slice(0, 12);
    hits.innerHTML = (top.length ? top.map((r, i) => `<a href="${P}/armory/${r.k === 'talent' ? 'talents' : r.k}/${r.s}" role="option" aria-selected="${i === sel ? 'true' : 'false'}">${esc(r.n)}<span>${esc(KIND_NAME[r.k] || r.k)}</span></a>`).join('')
      : `<p>${esc(t('Nothing by that name.'))}</p>`) + (all.length ? `<a href="${listHref('items', { q: input.value.trim() })}"><i>${esc(t('Items matching “{q}”', { q: input.value.trim() }))}</i><span></span></a>` : '');
    hits.hidden = false;
    input.setAttribute('aria-expanded', 'true');
  }
  input.addEventListener('focus', () => { have('search.json'); });
  input.addEventListener('input', () => { sel = -1; showHits(); });
  input.addEventListener('keydown', (ev) => {
    const opts = [...hits.querySelectorAll('a')];
    if (ev.key === 'ArrowDown' || ev.key === 'ArrowUp') { ev.preventDefault(); sel = Math.max(0, Math.min(opts.length - 1, sel + (ev.key === 'ArrowDown' ? 1 : -1))); opts.forEach((o, i) => o.setAttribute('aria-selected', i === sel ? 'true' : 'false')); }
    else if (ev.key === 'Enter') { ev.preventDefault(); const o = opts[sel >= 0 ? sel : 0]; if (o) { hideHits(); go(new URL(o.href).pathname + new URL(o.href).search); } }
    else if (ev.key === 'Escape') hideHits();
  });
  document.addEventListener('click', (ev) => { if (!ev.target.closest('.asearch')) hideHits(); });
  const redrawSearch = () => { if (document.activeElement === input && input.value) showHits(); };
  const drawWas = draw;
  // The search list follows search.json's arrival as the page does.
  setInterval(() => { if (files['search.json'] && !hits.hidden && hits.textContent === t('Loading…')) redrawSearch(); }, 300);

  if (window.MU) MU.bar(!!MU.token.get());
  drawWas();
})();
