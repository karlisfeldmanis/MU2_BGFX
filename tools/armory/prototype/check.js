class DCLogic{};

class Component extends DCLogic {
  constructor(props) {
    super(props);
    this.state = { sysA: null, sysB: null, icons: null, rune: '', skill: '', runeR: '', skillC: '', tcls: 'DK', tlv: 400, treb: 0, tpts: {}, boxTier: '', boxAll: false,
      items: [], monsters: [], npcs: [], quests: [], modelIdx: null, npc: '', quest: '', npcMapIx: 0, browse: 'items', mon: '', monMap: '', monCat: '', monAll: false, loading: true, src: null, srcLoading: false, view: 'list', sel: '', q: '', cat: 'All', sub: '', cls: '', srcf: '', sort: 'lv', dir: 1, page: 1,
      tab: '', plus: 0, exc: false, luck: false, opt: 0, mapf: '', allDrops: false,
      draft: '', about: 'item', cfilter: 'all',
      notes: { Sword17: [
        { id: 1, who: '[PLAYER]', about: 'dual', when: '23 Oct', text: '[Sample comment about dual wielding two of these after the update]', votes: 7, mine: false },
        { id: 3, who: '[PLAYER]', about: 'item', when: '18 Oct', text: '[Sample comment about where it drops]', votes: 1, mine: false } ] } };
  }
  componentDidMount() {
    var self = this;
    fetch('/_blob/ceed3c3fce978033027a1f049bed36b8').then(function (r) { return r.json(); }).then(function (items) {
      self.setState({ items: items, loading: false });
    }).catch(function () { self.setState({ loading: false }); });
    fetch('/_blob/8c240f153da825e4e33a3e7ab6e7574c').then(function (r) { return r.json(); }).then(function (monsters) {
      self.setState({ monsters: monsters });
    }).catch(function () {});
    fetch('/_blob/06e7c02e6d1dce5bac6986962ece6e8f').then(function (r) { return r.json(); }).then(function (d) { self.setState({ npcs: d.npcs || [] }); }).catch(function () {});
    fetch('/_blob/257eb7fcc386075ed18407a8bdeac056').then(function (r) { return r.json(); }).then(function (d) { self.setState({ quests: d.quests || [] }); }).catch(function () {});
    this.modelIndex().then(function (idx) { self.setState({ modelIdx: idx }); }).catch(function () {});
    fetch('/_blob/da10a99d60c5cfab22bf48de6343a228').then(function (r) { return r.json(); }).then(function (d) { self.setState({ sysA: d }); }).catch(function () {});
    fetch('/_blob/159bc721afdc0d66d9650517e3e24210').then(function (r) { return r.json(); }).then(function (d) { self.setState({ sysB: d }); }).catch(function () {});
    fetch('/_blob/417d01ca9f375bdb3bfe9887dce26763').then(function (r) { return r.json(); }).then(function (d) { self.setState({ cards: d }); }).catch(function () {});
    fetch('./icons/index.json').then(function (r) { return r.json(); }).then(function (d) { self.setState({ icons: d }); }).catch(function () {});
  }
  // Every monster's drops: the sources turned round, built once.
  dropsByMonster(src) {
    if (this._revOf === src) return this._rev;
    var rev = {}, lv = {};
    this._lv = lv;
    Object.keys(src).forEach(function (k) {
      if (k.charAt(0) === '_') return;
      (src[k].monsters || []).forEach(function (m) {
        var list = rev[m.name] || (rev[m.name] = {});
        var had = list[k];
        lv[m.name] = lv[m.name] || {};
        lv[m.name][m.map] = m.level;
        var bm = {}; bm[m.map] = m.chance || 0;
        if (!had) list[k] = { k: k, maps: [m.map], plain: m.plain !== false, excellent: !!m.excellent, how: m.how || '', chance: m.chance || 0, byMap: bm, exc: m.chance_excellent || 0 };
        else {
          if (had.maps.indexOf(m.map) < 0) had.maps.push(m.map);
          had.byMap[m.map] = Math.max(had.byMap[m.map] || 0, m.chance || 0);
          had.chance = Math.max(had.chance, m.chance || 0);
          had.plain = had.plain || m.plain !== false;
          had.excellent = had.excellent || !!m.excellent;
        }
      });
    });
    this._revOf = src; this._rev = rev;
    return rev;
  }
  // The 3D viewer: one renderer on whichever page's canvas is showing, the models packed in
  // ./models/pNN.bin (index.json says which pack, where and how long).
  setCanvasEl(el) {
    if (el === this._canvas) return;
    this.stopViewer();
    this._canvas = el;
    if (!el || typeof THREE === 'undefined') return;
    var self = this;
    var renderer = new THREE.WebGLRenderer({ canvas: el, antialias: true, alpha: true });
    renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
    renderer.outputEncoding = THREE.sRGBEncoding;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.05;
    var scene = new THREE.Scene();
    if (THREE.RoomEnvironment) {
      var pm = new THREE.PMREMGenerator(renderer);
      scene.environment = pm.fromScene(new THREE.RoomEnvironment(), 0.04).texture;
    }
    scene.add(new THREE.HemisphereLight(0xfff1dc, 0x1a120c, 0.7));
    var key = new THREE.DirectionalLight(0xffe2bf, 1.6); key.position.set(2, 3, 4); scene.add(key);
    var rim = new THREE.DirectionalLight(0x9fb7ff, 0.5); rim.position.set(-3, 1, -3); scene.add(rim);
    var camera = new THREE.PerspectiveCamera(32, 1, 0.01, 100);
    var controls = new THREE.OrbitControls(camera, el);
    controls.enableDamping = true; controls.enablePan = false; controls.autoRotate = true; controls.autoRotateSpeed = 1.6;
    controls.addEventListener('start', function () { controls.autoRotate = false; });
    var fit = function () {
      var w = el.clientWidth || 300, h = el.clientHeight || 300;
      renderer.setSize(w, h, false); camera.aspect = w / h; camera.updateProjectionMatrix();
    };
    fit();
    var ro = typeof ResizeObserver !== 'undefined' ? new ResizeObserver(fit) : null;
    if (ro) ro.observe(el);
    var tick = function () {
      if (!self._three || self._three.renderer !== renderer) return;
      controls.update(); renderer.render(scene, camera);
      self._three.raf = requestAnimationFrame(tick);
    };
    this._three = { renderer: renderer, scene: scene, camera: camera, controls: controls, ro: ro, model: null, loaded: '' };
    this._three.raf = requestAnimationFrame(tick);
    this.syncModel();
  }
  stopViewer() {
    var t = this._three;
    if (!t) return;
    cancelAnimationFrame(t.raf);
    if (t.ro) t.ro.disconnect();
    t.controls.dispose(); t.renderer.dispose();
    this._three = null;
  }
  componentDidUpdate() { this.syncModel(); }
  componentWillUnmount() { this.stopViewer(); }
  modelIndex() {
    if (!this._modelIndex) this._modelIndex = fetch('./models/index.json').then(function (r) { return r.json(); });
    return this._modelIndex;
  }
  modelPack(n, name) {
    this._packs = this._packs || {};
    // Packs travel as base64 text: the canvas serves no raw binary.
    if (!this._packs[n]) this._packs[n] = fetch('./models/' + name).then(function (r) { return r.text(); }).then(function (b64) {
      var bin = atob(b64.trim()), bytes = new Uint8Array(bin.length);
      for (var i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
      return bytes.buffer;
    });
    return this._packs[n];
  }
  syncModel() {
    var t = this._three, want = this._want || '';
    if (!t || t.loaded === want) return;
    t.loaded = want;
    if (t.model) { t.scene.remove(t.model); t.model = null; }
    if (!want) return;
    var self = this, kind = want.charAt(0), key = want.slice(2), gold = want.indexOf('|elite') > 0;
    if (gold) key = key.split('|')[0];
    if (!this.state.model3dLoading) this.setState({ model3dLoading: true });
    this.modelIndex().then(function (idx) {
      var at = (kind === 'i' ? idx.items : kind === 'n' ? (idx.npcs || {}) : idx.monsters)[key];
      if (!at) throw new Error('no model');
      return self.modelPack(at[0], idx.packs[at[0]]).then(function (buf) { return buf.slice(at[1], at[1] + at[2]); });
    }).then(function (glb) {
      return new Promise(function (ok, bad) { new THREE.GLTFLoader().parse(glb, '', ok, bad); });
    }).then(function (gltf) {
      var t2 = self._three;
      if (!t2 || t2.loaded !== want) return;
      var root = gltf.scene;
      root.traverse(function (o) {
        if (!o.isMesh) return;
        var m = o.material;
        if (m.userData && m.userData.mu_additive) { m.blending = THREE.AdditiveBlending; m.depthWrite = false; }
        else if (m.transparent) m.depthWrite = false;
        if (gold && m.color) { m.color.setRGB(1.0, 0.76, 0.22); m.metalness = 0.85; m.roughness = 0.35; }
      });
      var box = new THREE.Box3().setFromObject(root), size = box.getSize(new THREE.Vector3()), mid = box.getCenter(new THREE.Vector3());
      root.position.sub(mid);
      var r = Math.max(size.x, size.y, size.z) || 1;
      t2.camera.near = r / 100; t2.camera.far = r * 20; t2.camera.updateProjectionMatrix();
      t2.camera.position.set(0, size.y * 0.08, r * 1.9);
      t2.controls.target.set(0, 0, 0); t2.controls.minDistance = r * 0.6; t2.controls.maxDistance = r * 5; t2.controls.autoRotate = true; t2.controls.update();
      t2.scene.add(root); t2.model = root;
      self.setState({ model3dLoading: false });
    }).catch(function () { self.setState({ model3dLoading: false }); });
  }
  loadSources() {
    var self = this;
    if (this.state.src || this.state.srcLoading) return;
    this.setState({ srcLoading: true });
    fetch('/_blob/c8712a62ee868bd96effa7b1a3edcd01').then(function (r) { return r.json(); }).then(function (src) {
      self.setState({ src: src, srcLoading: false });
    }).catch(function () { self.setState({ srcLoading: false }); });
  }
  renderVals() {
    var self = this, s = this.state, per = this.props.perPage ?? 20;
    var ICON = { Helms: 'cae34e2d837033d4fbe028f277ab0147', Armour: 'c458dd2f18b586b553b1022dffad73d5', Pants: '8b2c1dc3a7aafc5c077bf32757cb316a', Gloves: '36eb263c5ad90a0dd49cb3d3a4924254', Boots: '46bcf3cc94ab82ddf08ccb0ee0199f7c', Shields: '9f21a8bd86ec8b9d85d91199b4226603', Wings: '1edf1bf35542a629ae273cb8b74a95af', Pendants: '9585bfb302f5646fb0edc8c633849890', Rings: '03b340573b0d3ecf92a7cb61ad8a2337', Pets: '4192e4a080a1846335a85cbfe1551fc6' };
    var WEAPON = '50b11c9a873df13bc1cd503e58bec9b0';
    var ORDER = ['Weapons', 'Armour', 'Shields', 'Wings', 'Jewellery', 'Pets', 'Other'];
    var CLS = { DK: 'Dark Knight', DW: 'Dark Wizard', FE: 'Fairy Elf', MG: 'Magic Gladiator' };
    var SRC = { d: ['Drop', 'Dropped by monsters'], s: ['Shop', 'Sold by an NPC'], c: ['Craft', 'Made in a mix'], q: ['Quest', 'A quest reward'], e: ['Event', 'An event or raid reward'], b: ['Box', 'Found in a box'] };
    var UPD = { '23': '23 October update', '21': '21 October update' };
    // The game's own tables (src/sim/items.cpp, wear.cpp, realm_items.cpp).
    var DMG = [0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 31, 36];
    var DEF = [0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 31, 36];
    var SHD = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11];
    var DUR = [0, 1, 2, 3, 4, 6, 8, 10, 12, 14, 17, 21];
    var WIZE = [0, 3, 7, 10, 14, 17, 21, 24, 28, 31, 35, 40], WIZO = [0, 4, 7, 11, 14, 18, 21, 25, 28, 32, 36, 40];
    var EXC_DEF = ['Zen from kills +80%', 'Defense success rate +20%', 'Reflect damage +10%', 'Damage decrease +8%', 'Max mana +8%', 'Max life +8%'];
    var EXC_ATK = ['Mana per kill +mana/4', 'Life per kill +life/4', 'Attack speed +14', 'Damage +4%', 'Damage +level/10', 'Excellent damage chance +20%'];
    var EXC_WIZ = ['Mana per kill +mana/4', 'Life per kill +life/4', 'Attack speed +14', 'Wizardry damage +4%', 'Wizardry damage +level/10', 'Excellent damage chance +20%'];
    var all = s.items;
    var iconOf = function (i) { return ICON[i.s] ? '/_blob/' + ICON[i.s] : (i.c === 'Weapons' ? '/_blob/' + WEAPON : ''); };
    var dual = function (i) { return i.aff.indexOf('dual') >= 0; };
    var shield = function (i) { return i.c === 'Shields'; };
    var gear = function (i) { return (i.c === 'Weapons' && i.s !== 'Ammo') || i.c === 'Armour' || i.c === 'Shields'; };
    var jewel = function (i) { return i.c === 'Jewellery'; };
    var excellentable = function (i) { return gear(i) || jewel(i); };
    var tunable = function (i) { return gear(i) || i.c === 'Wings' || jewel(i); };
    var maxPlus = function (i) { return jewel(i) ? 4 : 11; };
    var need = function (raw, mult, i, plus, exc) {
      if (!raw) return 0;
      return Math.floor(mult * (i.dl + 3 * plus + (exc ? 25 : 0)) * raw / 100) + 20;
    };
    var needs = function (i, plus, exc) {
      var lvl = i.lv + (i.c === 'Wings' ? (i.num <= 2 ? 4 : 5) * plus : 0);
      return { lv: lvl, str: need(i.str, 3, i, plus, exc), agi: need(i.agi, 3, i, plus, exc), ene: need(i.ene, 4, i, plus, exc) };
    };
    var reqText = function (n) {
      var r = [];
      if (n.lv) r.push('Level ' + n.lv);
      if (n.str) r.push('Strength ' + n.str);
      if (n.agi) r.push('Agility ' + n.agi);
      if (n.ene) r.push('Energy ' + n.ene);
      return r.length ? 'Requires ' + r.join(', ') : 'No requirements';
    };
    var excDmg = function (i) { return i.dmg && i.dmg[0] > 0 ? Math.floor(i.dmg[0] * 25 / Math.max(1, i.dl)) + 5 : 0; };
    var excDef = function (i) { return i.def && !shield(i) && i.c === 'Armour' ? Math.floor(i.def * 12 / Math.max(1, i.dl)) + Math.floor(Math.max(1, i.dl) / 5) + 4 : 0; };
    var excBlock = function (i) { return shield(i) && i.dr ? Math.floor(i.dr * 25 / Math.max(1, i.dl)) + 5 : 0; };
    var dmgAt = function (i, p, exc) { var b = DMG[p] + (exc ? excDmg(i) : 0); return [i.dmg[0] + b, i.dmg[1] + b]; };
    var defAt = function (i, p, exc) { return i.def + (shield(i) ? SHD[p] : DEF[p]) + (exc ? excDef(i) : 0); };
    var wizAt = function (i, p) { return Math.floor(i.mp / 2) + (i.mp % 2 === 0 ? WIZE[p] : WIZO[p]); };
    var durAt = function (i, p, exc) { return Math.min(255, i.dur + DUR[p] + (exc ? 15 : 0)); };
    var statOf = function (i) { return i.dmg ? i.dmg[0] + ' ~ ' + i.dmg[1] : i.def ? String(i.def) : ''; };
    var statName = function (i) { return i.dmg ? 'Damage' : i.def ? 'Defense' : ''; };
    var lvText = function (i) { return i.lv ? 'Lv ' + i.lv : i.dl ? 'Drop ' + i.dl : '—'; };
    var kindOf = function (i) { return (i.two ? 'Two-handed ' : '') + i.s.replace(/s$/, '').toLowerCase() + (i.cl2 ? ' · second class' : ''); };
    var clsLong = function (i) { return i.cls.length === 4 ? 'Every class' : i.cls.map(function (c) { return CLS[c]; }).join(', '); };
    var updText = function (i) {
      if (i.add !== 'base') return 'New in the ' + UPD[i.add];
      if (dual(i)) return 'Changed in the 23 October update';
      return '';
    };
    var open = function (k) { return function () { self.loadSources(); self.setState({ view: 'item', sel: k, tab: '', gcState: '', plus: 0, exc: false, luck: false, opt: 0, mapf: '', allDrops: false, draft: '', about: 'item', cfilter: 'all' }); }; };
    var toList = function (cat, sub) { return function () { self.setState({ view: 'list', browse: 'items', cat: cat, sub: sub, page: 1 }); }; };
    var openMon = function (name, map) { return function () { self.loadSources(); self.setState({ view: 'mon', mon: name, monCtx: map || '', monCat: '', monAll: false }); }; };
    var EVENTS = [['bc1', 'Blood Castle 1', '15–80'], ['bc2', 'Blood Castle 2', '81–130'], ['bc3', 'Blood Castle 3', '131–180'], ['bc4', 'Blood Castle 4', '181–230'], ['bc5', 'Blood Castle 5', '231–280'], ['bc6', 'Blood Castle 6', '281 and up'],
      ['ds1', 'Devil Square 1', '10–99'], ['ds2', 'Devil Square 2', '100–179'], ['ds3', 'Devil Square 3', '180–249'], ['ds4', 'Devil Square 4', '250 and up'],
      ['gd', 'Golden Dragon', 'a party of 10']];
    var openEvent = function (id) { return function () { self.loadSources(); self.setState({ view: 'event', event: id, evCat: '', evAll: false }); }; };
    var toEvents = function () { self.loadSources(); self.setState({ view: 'list', browse: 'events' }); };
    var toMons = function (map) { return function () { self.loadSources(); self.setState({ view: 'list', browse: 'monsters', monMap: map }); }; };
    var npcById = {}, npcByName = {}, questById = {}, questByName = {};
    s.npcs.forEach(function (n) { npcById[n.id] = n; npcByName[n.name.toLowerCase()] = n; });
    s.quests.forEach(function (q) { questById[q.id] = q; questByName[q.name.toLowerCase()] = q; });
    var openNpc = function (id) { return function () { self.setState({ view: 'npc', npc: id, npcMapIx: 0 }); }; };
    var openQuest = function (id) { return function () { self.setState({ view: 'quest', quest: id }); }; };
    var openNpcNamed = function (name) {
      var n = npcByName[String(name).toLowerCase()];
      if (!n) { var bare = String(name).replace(/\s*\(.*\)$/, '').replace(/^the /i, '').toLowerCase(); n = s.npcs.filter(function (x) { return x.name.toLowerCase().indexOf(bare) >= 0; })[0]; }
      return n ? openNpc(n.id) : function () {};
    };
    var itemByKey = function (k) { for (var i = 0; i < s.items.length; i++) if (s.items[i].k === k) return s.items[i]; return null; };
    // The game's item card: its own rows (scratchpad cards.json, from src/game/ui/describe.cpp) turned
    // into what the markup draws. Inks are tip.cpp's tones; a tail is its tone a third toward white.
    var TONE = { White: ['#ffffff', '#ffffff'], Blue: ['#80b2ff', '#accdff'], Red: ['#ff331a', '#ff7a6a'], Yellow: ['#ffcc1a', '#ffde6a'], Green: ['#1aff80', '#6affac'],
      Gray: ['#96998e', '#bbbdb6'], Violet: ['#b266ff', '#cd9cff'], RedPurple: ['#cc80cc', '#deacde'], Orange: ['#e66b0a', '#ef9f60'], Uncommon: ['#ffffff', '#ffffff'],
      Rare: ['#0070dd', '#59a2e9'], Epic: ['#a335ee', '#c37cf4'], Legendary: ['#1eff00', '#6dff59'], Artifact: ['#ffd100', '#ffe159'], Quest: ['#d685ff', '#e4b0ff'],
      RuneLegendary: ['#ff8000', '#ffac59'], RuneAncient: ['#00ccff', '#59deff'] };
    var ink = function (t, d) { return t && TONE[t] ? TONE[t][0] : (d || '#ffffff'); };
    var tailInk = function (t) { return t && TONE[t] ? TONE[t][1] : '#ffffff'; };
    var rgba = function (hex, a) { var n = parseInt(hex.slice(1), 16); return 'rgba(' + (n >> 16) + ',' + ((n >> 8) & 255) + ',' + (n & 255) + ',' + a + ')'; };
    var cardView = function (card) {
      if (!card || !card.rows) return null;
      var v = { title: '', sub: '', bound: '', tone: '#ffffff', rows: [], footL: [], footR: [], hasFoot: false, sell: '' };
      var brk = null;
      card.rows.forEach(function (r) {
        if (r.kind !== 'breakdown') brk = null;
        if (r.kind === 'title') { v.title = r.text; v.tone = ink(r.tone); }
        else if (r.kind === 'subtitle') v.sub = r.text;
        else if (r.kind === 'bound') v.bound = r.text;
        else if (r.kind === 'headline') v.rows.push({ isHead: true, text: r.text, color: ink(r.tone, '#ffcc1a'), word: r.word || '' });
        else if (r.kind === 'breakdown') {
          if (!brk) { brk = { isBreak: true, items: [] }; v.rows.push(brk); }
          brk.items.push({ figure: r.figure || '', color: r.figureTone ? ink(r.figureTone) : '#c4c1b4', text: r.text || '' });
        }
        else if (r.kind === 'rule') v.rows.push({ isRule: true });
        else if (r.kind === 'stat') v.rows.push({ isStat: true, label: r.label || r.text || '', labelColor: r.labelTone ? ink(r.labelTone) : '#c4c1b4',
          tail: r.tail || (r.values ? r.values.map(function (x) { return x.text || x; }).join(' · ') : ''), tailColor: ink(r.tailTone), gap: r.gap ? '9px' : '0' });
        else if (r.kind === 'foot') {
          v.hasFoot = true;
          (r.left || []).forEach(function (l) { v.footL.push({ text: l.text, color: l.tone === 'Red' ? '#ff331a' : l.quiet ? '#7d8077' : ink(l.tone, '#7d8077') }); });
          (r.right || []).forEach(function (l) { var w = l.worn != null ? l.worn : null; var c = 2 * Math.PI * 8; v.footR.push({ text: l.text, ring: w != null, dash: w != null ? (c * w).toFixed(2) + ' ' + c.toFixed(2) : '' }); });
        }
        else if (r.kind === 'sell') v.sell = r.text;
        else if (r.kind === 'gap') v.rows.push({ isOpt: true, text: '', gap: '9px' });
        else {
          var m = r.mark || '';
          v.rows.push({ isOpt: true, keyword: r.keyword || '', kwColor: r.keywordTone ? ink(r.keywordTone) : '#ffffff', text: r.text || '', color: ink(r.tone),
            tail: r.tail || '', tailColor: tailInk(r.tone), gap: r.gap ? '9px' : '0', mColor: ink(r.markTone, ink(r.tone)),
            mDia: m === 'Diamond', mStar: m === 'Star', mSocket: m === 'Socket', mRing: m === 'Ring', mSet: m === 'RingSet' });
        }
      });
      v.headBg = 'linear-gradient(' + rgba(v.tone, 0.12) + ', rgba(0,0,0,0))';
      v.lineBg = 'linear-gradient(90deg, rgba(0,0,0,0), ' + rgba(v.tone, 0.8) + ', rgba(0,0,0,0))';
      return v;
    };
    var CARDS = s.cards || null;
    var hasFigure = function (fig) { return !!(fig && s.modelIdx && s.modelIdx.npcs && s.modelIdx.npcs[fig]); };
    var monByName = {};
    s.monsters.forEach(function (m) { monByName[m.n] = m; });
    var rev = s.src ? this.dropsByMonster(s.src) : null;
    var ELITE_LIFE = { Lorencia: 8, Noria: 6, Devias: 3, Dungeon: 3, 'Lost Tower': 2.5, Atlans: 2.5, Tarkan: 2, Icarus: 2, Aida: 1.5 };
    // A per-kill chance said as players say it: "1 in 263", with the percent beside.
    var odds = function (c) {
      if (!c) return { one: '—', pct: '' };
      if (c >= 0.999) return { one: 'Always', pct: '' };
      var pct = c * 100;
      var p = pct >= 10 ? pct.toFixed(0) : pct >= 1 ? pct.toFixed(1) : pct >= 0.01 ? pct.toFixed(2) : pct.toPrecision(2);
      return { one: c >= 0.2 ? pct.toFixed(0) + '%' : '1 in ' + Math.round(1 / c).toLocaleString('en'), pct: c >= 0.2 ? '' : p + '%' };
    };
    var baseOf = function (name) { return !monByName[name] && name.indexOf('Elite ') === 0 ? name.slice(6) : ''; };

    var q = s.q.trim().toLowerCase();
    var base = all.filter(function (i) { return (!q || i.n.toLowerCase().indexOf(q) >= 0) && (!s.cls || i.cls.indexOf(s.cls) >= 0) && (!s.srcf || i.src.indexOf(s.srcf) >= 0); });
    var cats = [{ cls: 'ci', label: 'All items', n: base.length, on: s.cat === 'All' ? 'true' : 'false', pick: toList('All', '') }];
    ORDER.forEach(function (c) {
      var inC = base.filter(function (i) { return i.c === c; });
      cats.push({ cls: 'ci', label: c, n: inC.length, on: s.cat === c && !s.sub ? 'true' : 'false', pick: toList(c, '') });
      if (s.cat === c) {
        var subs = [];
        inC.forEach(function (i) { if (subs.indexOf(i.s) < 0) subs.push(i.s); });
        if (subs.length > 1) subs.forEach(function (sb) {
          cats.push({ cls: 'ci sub', label: sb, n: inC.filter(function (i) { return i.s === sb; }).length, on: s.sub === sb ? 'true' : 'false', pick: toList(c, sb) });
        });
      }
    });
    var list = base.filter(function (i) { return (s.cat === 'All' || i.c === s.cat) && (!s.sub || i.s === s.sub); });
    var sortStat = function (i) { return i.dmg ? i.dmg[1] : (i.def || i.mp || 0); };
    var key = { n: function (i) { return i.n; }, s: function (i) { return i.s; }, lv: function (i) { return i.lv || i.dl; }, st: sortStat, rq: function (i) { var n = needs(i, 0, false); return n.str + n.agi + n.ene; }, cl: function (i) { return i.cls.join(''); } }[s.sort];
    list.sort(function (a, b) { var x = key(a), y = key(b); return (x > y ? 1 : x < y ? -1 : a.n > b.n ? 1 : -1) * s.dir; });
    var pages = Math.max(1, Math.ceil(list.length / per));
    var page = Math.min(s.page, pages);
    var cols = [['n', 'Name', ''], ['s', 'Type', ''], ['lv', 'Level', 'r'], ['st', 'Damage / Def', 'r'], ['rq', 'Requires', ''], ['cl', 'Class', '']].map(function (c) {
      var on = s.sort === c[0];
      return { label: c[1], cls: c[2], arrow: on ? (s.dir > 0 ? ' ▲' : ' ▼') : '', aria: on ? (s.dir > 0 ? 'ascending' : 'descending') : undefined,
        pick: function () { self.setState({ sort: c[0], dir: on ? -s.dir : 1, page: 1 }); } };
    });
    var rows = list.slice((page - 1) * per, page * per).map(function (i, ix) {
      var n = needs(i, 0, false);
      var gcv = CARDS && CARDS[i.k] ? cardView(CARDS[i.k].plain || CARDS[i.k][Object.keys(CARDS[i.k])[0]]) : null;
      return { gc: gcv || {}, hasGc: !!gcv, noGc: !gcv, tid: 'tt' + ix, n: i.n, s: i.s, icon: iconOf(i), kind: kindOf(i), dorm: !!i.dorm,
        lvText: lvText(i), statCol: statOf(i) || (i.mp ? 'Wiz ' + wizAt(i, 0) + '%' : '—'), statText: statOf(i), statName: statName(i), sp: i.sp || 0,
        reqText: reqText(n), reqShort: [n.str ? 'Str ' + n.str : '', n.agi ? 'Agi ' + n.agi : '', n.ene ? 'Ene ' + n.ene : ''].filter(Boolean).join(' · ') || '—',
        clsLong: clsLong(i), clsShort: i.cls.length === 4 ? 'All' : i.cls.join(' ') || '—', updText: updText(i), open: open(i.k),
        srcs: i.src.split('').filter(function (c) { return SRC[c]; }).map(function (c) { return { label: SRC[c][0], title: SRC[c][1] }; }),
        srcText: i.src ? i.src.split('').map(function (c) { return SRC[c][1]; }).join(' · ') : (i.dorm ? 'Drops when Kanturu opens' : '') };
    });
    var from = list.length ? (page - 1) * per + 1 : 0, to = Math.min(page * per, list.length);
    var srcChips = ['d', 's', 'c', 'q', 'e', 'b'].map(function (c) {
      return { label: SRC[c][0], on: s.srcf === c ? 'true' : 'false', pick: function () { self.setState({ srcf: s.srcf === c ? '' : c, page: 1 }); } };
    });

    var sel = all.filter(function (i) { return i.k === s.sel; })[0];
    var isItem = s.view === 'item' && !!sel;
    var isMon = s.view === 'mon' && !!s.mon;
    var npcSel = npcById[s.npc], questSel = questById[s.quest];
    var isNpc = s.view === 'npc' && !!npcSel, isQuest = s.view === 'quest' && !!questSel;
    var evSel = EVENTS.filter(function (e) { return e[0] === s.event; })[0];
    var isGold = s.view === 'event' && !!evSel && evSel[0] === 'gd';
    var isEvent = s.view === 'event' && !!evSel && !isGold;
    var A = s.sysA || {}, B = s.sysB || {};
    var RUNES = (A.runes && A.runes.runes) || [], SKILLS = (B.skills && B.skills.skills) || [];
    var runeSel = RUNES.filter(function (r) { return r.id === s.rune; })[0];
    var skillSel = SKILLS.filter(function (k) { return String(k.id) === String(s.skill); })[0];
    var isRune = s.view === 'rune' && !!runeSel, isSkill = s.view === 'skill' && !!skillSel;
    var openRune = function (id) { return function () { self.setState({ view: 'rune', rune: id }); }; };
    var openSkill = function (id) { return function () { self.setState({ view: 'skill', skill: String(id) }); }; };
    var goBrowse = function (b) { return function () { self.setState({ view: 'list', browse: b }); }; };
    var detail = isItem || isMon || isNpc || isQuest || isEvent || isRune || isSkill || isGold;
    var isEventList = !detail && s.browse === 'events';
    var isRuneList = !detail && s.browse === 'runes', isSkillList = !detail && s.browse === 'skills', isTalents = !detail && s.browse === 'talents';
    var isMixList = !detail && s.browse === 'mixes', isEliteList = !detail && s.browse === 'elites';
    var BROWSE_NAMES = { runes: 'Runes', skills: 'Skills', talents: 'Talents', mixes: 'Chaos Machine', elites: 'Elites' };
    var isMonList = !detail && s.browse === 'monsters';
    var isNpcList = !detail && s.browse === 'npcs';
    var isQuestList = !detail && s.browse === 'quests';
    var toNpcs = function () { self.setState({ view: 'list', browse: 'npcs' }); };
    var toQuests = function () { self.setState({ view: 'list', browse: 'quests' }); };
    var crumbs = [{ label: 'Armory', link: true, go: toList('All', '') }];
    if (isGold) {
      crumbs.push({ label: 'Events', link: true, go: toEvents });
      crumbs.push({ label: 'Golden Dragon', cur: true });
    } else if (isRune) {
      crumbs.push({ label: 'Runes', link: true, go: goBrowse('runes') });
      crumbs.push({ label: runeSel.name, cur: true });
    } else if (isSkill) {
      crumbs.push({ label: 'Skills', link: true, go: goBrowse('skills') });
      crumbs.push({ label: skillSel.name, cur: true });
    } else if (!detail && BROWSE_NAMES[s.browse]) {
      crumbs.push({ label: BROWSE_NAMES[s.browse], cur: true });
    } else if (isEvent) {
      crumbs.push({ label: 'Events', link: true, go: toEvents });
      crumbs.push({ label: evSel[1], cur: true });
    } else if (isEventList) {
      crumbs.push({ label: 'Events', cur: true });
    } else if (isMon && s.monCtx && /^(Blood Castle|Devil Square)/.test(s.monCtx)) {
      var evOf = EVENTS.filter(function (e) { return e[1] === s.monCtx; })[0];
      crumbs.push({ label: 'Events', link: true, go: toEvents });
      if (evOf) crumbs.push({ label: evOf[1], link: true, go: openEvent(evOf[0]) });
      crumbs.push({ label: s.mon, cur: true });
    } else if (isNpc) {
      crumbs.push({ label: 'NPCs', link: true, go: toNpcs });
      crumbs.push({ label: npcSel.name, cur: true });
    } else if (isQuest) {
      crumbs.push({ label: 'Quests', link: true, go: toQuests });
      crumbs.push({ label: questSel.name, cur: true });
    } else if (isNpcList) {
      crumbs.push({ label: 'NPCs', cur: true });
    } else if (isQuestList) {
      crumbs.push({ label: 'Quests', cur: true });
    } else if (isMon) {
      crumbs.push({ label: 'Monsters', link: true, go: toMons('') });
      crumbs.push({ label: s.mon, cur: true });
    } else if (isMonList) {
      if (s.monMap) { crumbs.push({ label: 'Monsters', link: true, go: toMons('') }); crumbs.push({ label: s.monMap, cur: true }); }
      else crumbs.push({ label: 'Monsters', cur: true });
    } else if (isItem) {
      crumbs.push({ label: 'Items', link: true, go: toList('All', '') });
      crumbs.push({ label: sel.c, link: true, go: toList(sel.c, '') });
      if (sel.s !== sel.c) crumbs.push({ label: sel.s, link: true, go: toList(sel.c, sel.s) });
      crumbs.push({ label: sel.n, cur: true });
    } else {
      var trail = [['Items', 'All', '']];
      if (s.cat !== 'All') trail.push([s.cat, s.cat, '']);
      if (s.sub) trail.push([s.sub, s.cat, s.sub]);
      trail.forEach(function (t, ix) {
        if (ix === trail.length - 1) crumbs.push({ label: t[0], cur: true });
        else crumbs.push({ label: t[0], link: true, go: toList(t[1], t[2]) });
      });
    }

    var out = { it: {}, hist: [], similar: [], abouts: [], cfilters: [], comments: [], tabs: [], scaleHead: [], scaleRows: [], rolls: [],
      drops: [], mapChips: [], shops: [], crafts: [], used: [], quests: [], events: [], boxes: [] };
    if (isItem) {
      var mp = maxPlus(sel), plus = Math.min(s.plus, mp);
      var canExc = excellentable(sel), exc = canExc && s.exc;
      var canOpt = gear(sel) || sel.c === 'Wings';
      var src = (s.src && s.src[sel.k]) || {};
      // The card at the chosen plus.
      var kv = [], lines = [];
      var statText = '', sName = '';
      if (sel.dmg) { var d = dmgAt(sel, plus, exc); statText = d[0] + ' ~ ' + d[1]; sName = 'Damage'; }
      else if (sel.def) { statText = String(defAt(sel, plus, exc)); sName = 'Defense'; }
      if (sel.mp) kv.push({ k: 'Wizardry', v: '+' + wizAt(sel, plus) + '%' });
      if (sel.sp) kv.push({ k: 'Attack speed', v: sel.sp });
      if (sel.dr) kv.push({ k: 'Defense rate', v: sel.dr + (exc ? excBlock(sel) : 0) });
      if (sel.res) kv.push({ k: sel.res.charAt(0).toUpperCase() + sel.res.slice(1) + ' resistance', v: plus });
      if (sel.dur) kv.push({ k: 'Durability', v: tunable(sel) ? durAt(sel, plus, exc) : sel.dur });
      if (sel.sz) kv.push({ k: 'Size', v: sel.sz[0] + ' × ' + sel.sz[1] });
      if (s.luck && canOpt) lines.push({ text: 'Luck · +5% critical, +25% Jewel of Soul success', color: '#7FB2FF' });
      if (s.opt && canOpt) lines.push({ text: shield(sel) ? 'Additional defense rate +' + s.opt * 5 : sel.mp ? 'Additional wizardry +' + s.opt * 4 + '%' : sel.def && !sel.dmg ? 'Additional defense +' + s.opt * 4 : 'Additional damage +' + s.opt * 4, color: '#7FB2FF' });
      if (exc) lines.push({ text: 'Excellent: one or two options of six', color: '#7FD66A' });
      var nd = needs(sel, plus, exc);
      var gcAll = CARDS && CARDS[sel.k] ? CARDS[sel.k] : null;
      var gcKeys = gcAll ? Object.keys(gcAll) : [];
      var gcKey = gcKeys.indexOf(s.gcState) >= 0 ? s.gcState : gcKeys[0];
      var gcSel = gcAll ? cardView(gcAll[gcKey]) : null;
      var gcLabel = function (g) {
        if (g === 'plain') return 'Plain';
        if (/unmet/.test(g)) return 'Not wearable yet';
        return g.replace(/\(bits[^)]*\)\s*/, '').replace(/ 3 sockets/, ', 3 runes').replace(/luck/g, 'Luck').replace(/excellent/g, 'Excellent');
      };
      var runeHit = sel.k === 'Jewel22';
      var changes = (sel.add !== 'base' ? 1 : 0) + (dual(sel) ? 1 : 0) + (runeHit ? 1 : 0);
      var facts = [];
      if (sel.dl) facts.push({ k: 'Drop level', v: sel.dl, color: '#C9BFAE' });
      if (sel.lv) facts.push({ k: 'Level', v: sel.lv, color: '#C9BFAE' });
      if (sel.src) facts.push({ k: 'From', v: sel.src.split('').map(function (c) { return SRC[c][0]; }).join(' · '), color: '#C9BFAE' });
      if (sel.dorm) facts.push({ k: 'Kanturu', v: 'not dropping yet', color: '#7FD7F0' });
      if (changes) facts.push({ k: 'Updates', v: changes === 1 ? '1 change' : changes + ' changes', color: '#F2C14E' });
      out.it = { n: sel.n, icon: iconOf(sel), kind: kindOf(sel), facts: facts,
        cardName: sel.n + (plus ? ' +' + plus : ''), nameColor: exc ? '#7FD66A' : plus >= 7 ? '#F2C14E' : '#F3E8D4',
        cardKind: (exc ? 'Excellent ' : '') + kindOf(sel), statText: statText, statName: sName, kv: kv, lines: lines,
        reqText: reqText(nd), clsLong: clsLong(sel), tunable: tunable(sel), canExc: canExc, canOpt: canOpt,
        noLess: plus <= 0, noMore: plus >= mp, subLower: sel.s.toLowerCase(), isRune: sel.k === 'Jewel22',
        gc: gcSel || {}, hasGc: !!gcSel, noGc: !gcSel, showCtl: !gcSel && tunable(sel),
        gcStates: gcKeys.length > 1 ? gcKeys.map(function (g) { return { label: gcLabel(g), on: g === gcKey ? 'true' : 'false', pick: function () { self.setState({ gcState: g }); } }; }) : [],
        teaches: SKILLS.filter(function (k) { return (k.learned_from || []).some(function (f) { return f.item === sel.k; }); }).map(function (k) { return { name: k.name, open: openSkill(k.id) }; }) };

      // The scaling table.
      if (tunable(sel)) {
        var ps = []; for (var p = 0; p <= mp; p++) ps.push(p);
        out.scaleHead = ps.map(function (p) { return { label: '+' + p, color: p === plus ? '#F2D58A' : '#8F8575' }; });
        var cell = function (p, v) { return { v: v, color: p === plus ? '#F2D58A' : '#F3E8D4', bg: p === plus ? '#1E160F' : 'transparent' }; };
        var srow = function (label, f) { return { label: label, cells: ps.map(function (p) { return cell(p, f(p)); }) }; };
        if (sel.dmg) out.scaleRows.push(srow('Damage', function (p) { var d = dmgAt(sel, p, exc); return d[0] + '~' + d[1]; }));
        if (sel.mp) out.scaleRows.push(srow('Wizardry %', function (p) { return wizAt(sel, p); }));
        if (sel.def) out.scaleRows.push(srow('Defense', function (p) { return defAt(sel, p, exc); }));
        if (sel.dur) out.scaleRows.push(srow('Durability', function (p) { return durAt(sel, p, exc); }));
        if (sel.c === 'Wings') out.scaleRows.push(srow('Level', function (p) { return needs(sel, p, exc).lv; }));
        if (sel.str) out.scaleRows.push(srow('Strength', function (p) { return needs(sel, p, exc).str; }));
        if (sel.agi) out.scaleRows.push(srow('Agility', function (p) { return needs(sel, p, exc).agi; }));
        if (sel.ene) out.scaleRows.push(srow('Energy', function (p) { return needs(sel, p, exc).ene; }));
        if (sel.res) out.scaleRows.push(srow('Resistance', function (p) { return p; }));
        if (gear(sel)) out.rolls.push({ title: 'Luck', color: '#7FB2FF', lines: ['+5% critical damage chance', '+25% Jewel of Soul success'] });
        if (canOpt) out.rolls.push({ title: 'Additional option', color: '#7FB2FF', lines: [shield(sel) ? 'Defense rate +5, +10, +15, +20' : sel.mp ? 'Wizardry +4% to +16%' : sel.def && !sel.dmg ? 'Defense +4, +8, +12, +16' : 'Damage +4, +8, +12, +16'], note: 'Drops roll up to +12. A Jewel of Life adds a step.' });
        if (canExc) out.rolls.push({ title: 'Excellent options', color: '#7FD66A', lines: (sel.c === 'Armour' || shield(sel) || sel.s === 'Rings') ? EXC_DEF : sel.mp ? EXC_WIZ : EXC_ATK,
          note: (excDmg(sel) ? 'Damage +' + excDmg(sel) + '. ' : excDef(sel) ? 'Defense +' + excDef(sel) + '. ' : excBlock(sel) ? 'Defense rate +' + excBlock(sel) + '. ' : '') + 'Asks as if it dropped 25 levels deeper.' });
        if (gear(sel) || jewel(sel)) out.rolls.push({ title: 'Sockets', color: '#C58CF5', lines: [jewel(sel) && sel.s === 'Rings' ? 'Up to 1 socket' : 'Up to 3 sockets'], note: 'A rare roll. Its runes can raise the level it asks.' });
      }

      // Where it comes from.
      var mons = (src.monsters || []).slice().sort(function (a, b) { return (b.chance || 0) - (a.chance || 0) || a.level - b.level; });
      var maps = [];
      mons.forEach(function (m) { if (maps.indexOf(m.map) < 0) maps.push(m.map); });
      var mapf = maps.indexOf(s.mapf) >= 0 ? s.mapf : '';
      var mf = mons.filter(function (m) { return !mapf || m.map === mapf; });
      out.mapChips = maps.length > 1 ? [{ label: 'All maps · ' + mons.length, on: !mapf ? 'true' : 'false', pick: function () { self.setState({ mapf: '' }); } }].concat(maps.map(function (m) {
        return { label: m, on: mapf === m ? 'true' : 'false', pick: function () { self.setState({ mapf: m, allDrops: true }); } };
      })) : [];
      out.drops = (s.allDrops ? mf : mf.slice(0, 20)).map(function (m) {
        var how = m.how ? m.how : m.plain === false ? 'Excellent only' : m.excellent ? 'Plain or excellent' : 'Plain';
        var o = odds(m.chance);
        var split = m.chance_plain != null && m.chance_excellent ? odds(m.chance_plain).one + ' plain · ' + odds(m.chance_excellent).one + ' excellent' : '';
        return { name: m.name, level: m.level, map: m.map, elite: !!m.elite, how: how, howColor: m.plain === false ? '#7FD66A' : m.how ? '#F2C14E' : '#C9BFAE', open: openMon(m.name),
          one: o.one, pct: o.pct, split: split, open: openMon(m.name, m.map) };
      });
      out.dropsMore = !s.allDrops && mf.length > 20 ? 'Show all ' + mf.length : '';
      out.shops = (src.shops || []).map(function (x) { return { npc: x.npc, open: openNpcNamed(x.npc), map: x.map, plus: x.plus ? '+' + x.plus : '—', price: x.price ? x.price.toLocaleString('en') + ' Zen' + (x.pieces > 1 ? ' for ' + x.pieces : '') : '—' }; });
      out.crafts = (src.crafts || []).map(function (c) { return { recipe: c.recipe, needs: c.needs || [], note: c.note || '' }; });
      out.used = (src.used_in || []).map(function (u) { return { recipe: u.recipe, note: u.note || '' }; });
      out.quests = (src.quests || []).map(function (x) {
        var pays = [(x.count ? x.count + ' ×' : '1 ×'), x.plus ? '+' + x.plus : '', x['class'] ? 'for a ' + x['class'] : '', x.rank ? '(' + x.rank + ')' : ''].filter(Boolean).join(' ');
        var qq = questByName[String(x.quest).toLowerCase()];
        return { quest: x.quest, npc: x.npc, map: x.map, open: qq ? openQuest(qq.id) : function () {}, openGiver: qq ? openNpc(qq.giver) : openNpcNamed(x.npc), when: x.when + (x.repeat ? ', every ' + x.repeat : ''), pays: pays + (x.note ? ' · ' + x.note : '') };
      });
      out.events = (src.events || []).map(function (e) { return { event: e.event, note: e.note || '' }; });
      out.boxes = (src.boxes || []).map(function (b) { return { box: b.box + (b.chance ? ' · ' + odds(b.chance).one + (odds(b.chance).pct ? ' (' + odds(b.chance).pct + ')' : '') : ''), excellent: !!b.excellent }; });
      var rewardN = out.quests.length + out.events.length + out.boxes.length;
      out.hasQuests = out.quests.length > 0; out.hasEvents = out.events.length > 0; out.hasBoxes = out.boxes.length > 0;

      // Updates.
      var notes = s.notes[sel.k] || [];
      var count = function (a) { return notes.filter(function (c) { return c.about === a; }).length; };
      var showAbout = function (a) { return function () { self.setState({ tab: 'comments', cfilter: a }); }; };
      var label = { item: 'The item', dual: 'Dual wield change', runes: 'Rune level change', added: 'Its arrival' };
      var hist = [];
      if (sel.add !== 'base') hist.push({ date: UPD[sel.add], title: 'Added to the game', pip: '#F2C14E', dateColor: '#F2C14E', hasDiff: false,
        note: sel.dorm ? 'In the database now. It drops once Kanturu opens.' : 'Drops on the maps its level fits.', about: 'added', cmtText: count('added') + ' comments', show: showAbout('added') });
      if (dual(sel)) hist.push({ date: UPD['23'], title: 'Stronger with a second weapon', pip: '#F2C14E', dateColor: '#F2C14E', hasDiff: true,
        was: 'Two matched weapons hit at 55% each; mixed 43%', now: 'Two matched weapons hit at 62% each; mixed 49%', about: 'dual', cmtText: count('dual') + ' comments on this change', show: showAbout('dual') });
      if (runeHit) hist.push({ date: UPD['23'], title: 'Runes ask a hero level', pip: '#F2C14E', dateColor: '#F2C14E', hasDiff: true,
        was: 'A rune asked no level', now: 'Common 20, Rare 80, Epic 150, Legendary 250. Gear asks the highest level of its runes. Any rebirth meets them all.', about: 'runes', cmtText: count('runes') + ' comments on this change', show: showAbout('runes') });
      if (sel.add === 'base' && hist.length) hist.push({ date: '14 October 2026', title: 'In the game since the first release', pip: '#4A4038', dateColor: '#8F8575', hasDiff: false });
      out.hist = hist;

      var tabDefs = [];
      var cons = A.consumables && A.consumables[sel.k];
      var gp = B.gear_powers && B.gear_powers[sel.k];
      if (cons) tabDefs.push(['use', 'What it does', '']);
      if (gp) tabDefs.push(['powers', 'Powers', '']);
      if (tunable(sel)) tabDefs.push(['scale', 'Stats & scaling', '']);
      if (mons.length) tabDefs.push(['drops', 'Dropped by', mons.length]);
      if (out.shops.length) tabDefs.push(['shops', 'Sold by', out.shops.length]);
      if (out.crafts.length) tabDefs.push(['crafts', 'Created by', out.crafts.length]);
      if (out.used.length) tabDefs.push(['used', 'Used in', out.used.length]);
      if (rewardN) tabDefs.push(['rewards', 'Reward from', rewardN]);
      if (changes) tabDefs.push(['updates', 'Updates', changes]);
      tabDefs.push(['comments', 'Comments', notes.length || '']);
      var ids = tabDefs.map(function (t) { return t[0]; });
      var tab = ids.indexOf(s.tab) >= 0 ? s.tab : ids[0];
      out.tabs = tabDefs.map(function (t) { return { label: t[1], n: t[2], on: t[0] === tab ? 'true' : 'false', pick: function () { self.setState({ tab: t[0] }); } }; });
      out.tabUse = tab === 'use'; out.tabPowers = tab === 'powers';
      var pretty = function (k) { k = String(k).replace(/_/g, ' '); return k.charAt(0).toUpperCase() + k.slice(1); };
      var scalar = function (v) { return v == null ? '' : typeof v === 'object' ? '' : typeof v === 'number' ? v.toLocaleString('en') : String(v); };
      out.use = {};
      if (cons) {
        var rates = [];
        Object.keys(cons.rates || {}).forEach(function (k) { var v = cons.rates[k]; if (scalar(v)) rates.push({ k: pretty(k), v: scalar(v) + (typeof v === 'number' && /rate|success|luck|chance|item|excellent/i.test(k) ? '%' : '') }); });
        if (cons.rates && cons.rates.tiers) cons.rates.tiers.forEach(function (t) { rates.push({ k: '+' + t.plus + ' box', v: [t.item != null ? t.item + '% an item' : '', t.excellent_of_items != null ? t.excellent_of_items + '% of those excellent' : '', t.plain_plus ? 'plain ones ' + t.plain_plus : '', t.zen ? 'else ' + t.zen.toLocaleString('en') + ' Zen' : ''].filter(Boolean).join(' · ') }); });
        if (cons.stack) rates.push({ k: 'Stacks', v: cons.stack + ' a cell' });
        if (cons.price) rates.push({ k: 'Price', v: cons.price.toLocaleString('en') + ' Zen' + (cons.sells_for ? ' · sells for ' + cons.sells_for.toLocaleString('en') : '') });
        var contents = cons.contents, tierKeys = contents && !Array.isArray(contents) ? Object.keys(contents) : [];
        var tierK = tierKeys.indexOf(s.boxTier) >= 0 ? s.boxTier : tierKeys[tierKeys.length - 1];
        var list = Array.isArray(contents) ? contents : contents ? contents[tierK] || [] : [];
        list = list.slice().sort(function (a, b) { return (b.chance || 0) - (a.chance || 0); });
        out.use = { does: cons.does || [], rates: rates, hasRates: rates.length > 0, hasBox: list.length > 0,
          tiers: tierKeys.length > 1 ? tierKeys.map(function (t) { return { label: t + ' box', on: t === tierK ? 'true' : 'false', pick: function () { self.setState({ boxTier: t }); } }; }) : [],
          box: (s.boxAll ? list : list.slice(0, 25)).map(function (b) { var o = odds(b.chance); return { name: b.name || (itemByKey(b.k) || {}).n || b.k, one: o.one, pct: o.pct, exc: b.excellent_chance ? odds(b.excellent_chance).one : '—', open: itemByKey(b.k) ? open(b.k) : function () {} }; }),
          boxMore: !s.boxAll && list.length > 25 ? 'Show all ' + list.length : '', showAll: function () { self.setState({ boxAll: true }); } };
      }
      out.pw = {};
      if (gp) {
        var pkv = [], tables = [];
        Object.keys(gp).forEach(function (k) {
          var v = gp[k];
          if (['name', 'kind', 'summary', 'slot'].indexOf(k) >= 0 || v == null) return;
          if (Array.isArray(v) && v.length && typeof v[0] === 'object') {
            var head = Object.keys(v[0]);
            tables.push({ title: pretty(k), head: head.map(pretty), rows: v.map(function (r) { return head.map(function (h) { return scalar(r[h]) || (Array.isArray(r[h]) ? r[h].join(', ') : '—'); }); }) });
          } else if (Array.isArray(v)) {
            pkv.push({ k: pretty(k), v: v.join(' · ') });
          } else if (typeof v === 'object') {
            if (v.by_plus && typeof v.by_plus === 'object' && !Array.isArray(v.by_plus)) {
              var ks = Object.keys(v.by_plus);
              tables.push({ title: pretty(k) + (v.element ? ' · ' + v.element : ''), head: ks, rows: [ks.map(function (p) { return scalar(v.by_plus[p]); })] });
            }
            Object.keys(v).forEach(function (sk2) { if (sk2 !== 'by_plus' && scalar(v[sk2])) pkv.push({ k: pretty(k) + ' · ' + sk2.replace(/_/g, ' '), v: scalar(v[sk2]) }); });
          } else pkv.push({ k: pretty(k), v: scalar(v) });
        });
        out.pw = { summary: gp.summary || '', kv: pkv, hasKv: pkv.length > 0, tables: tables };
      }
      out.tabScale = tab === 'scale'; out.tabDrops = tab === 'drops'; out.tabShops = tab === 'shops'; out.tabCrafts = tab === 'crafts';
      out.tabUsed = tab === 'used'; out.tabRewards = tab === 'rewards'; out.tabUpdates = tab === 'updates'; out.tabComments = tab === 'comments';

      var lv = function (i) { return i.lv || i.dl; };
      out.similar = all.filter(function (i) { return i.s === sel.s && i.k !== sel.k; })
        .sort(function (a, b) { return Math.abs(lv(a) - lv(sel)) - Math.abs(lv(b) - lv(sel)); })
        .slice(0, 6).map(function (i) { return { n: i.n, lvText: lvText(i), open: open(i.k) }; });
      var keys = ['item'].concat(hist.filter(function (h) { return h.about; }).map(function (h) { return h.about; }));
      out.abouts = keys.map(function (k) { return { label: label[k], on: s.about === k ? 'true' : 'false', pick: function () { self.setState({ about: k }); } }; });
      out.cfilters = notes.length ? [{ k: 'all', l: 'All · ' + notes.length }].concat(keys.map(function (k) { return { k: k, l: label[k] + ' · ' + count(k) }; })).map(function (f) {
        return { label: f.l, on: s.cfilter === f.k ? 'true' : 'false', pick: function () { self.setState({ cfilter: f.k }); } };
      }) : [];
      out.comments = notes.filter(function (c) { return s.cfilter === 'all' || c.about === s.cfilter; }).map(function (c) {
        return Object.assign({}, c, { aboutLabel: c.about === 'item' ? '' : 'on ' + label[c.about].toLowerCase(), vote: function () {
          var n = Object.assign({}, self.state.notes);
          n[sel.k] = (n[sel.k] || []).map(function (x) { return x.id === c.id ? Object.assign({}, x, { mine: !x.mine, votes: x.votes + (x.mine ? -1 : 1) }) : x; });
          self.setState({ notes: n });
        } });
      });
      out.plus = plus;
      out.excNote = exc ? ' · excellent' : '';
    }

    // Monsters: the list, and one monster's page.
    var whereOf = function (name) {
      var m = monByName[name] || monByName[baseOf(name)];
      var w = m && !baseOf(name) ? m.where.map(function (x) { return x.map; }) : [];
      if (rev && rev[name]) Object.keys(rev[name]).forEach(function (k) { rev[name][k].maps.forEach(function (mp) { if (w.indexOf(mp) < 0) w.push(mp); }); });
      return w;
    };
    var dropN = function (name) { return rev && rev[name] ? Object.keys(rev[name]).length : 0; };
    var mons = s.monsters.slice().sort(function (a, b) { return a.lv - b.lv || (a.n > b.n ? 1 : -1); });
    var monMapCount = {};
    mons.forEach(function (m) { m.where.forEach(function (w) { monMapCount[w.map] = (monMapCount[w.map] || 0) + 1; }); });
    out.monMaps = [{ label: 'Every map', n: mons.length, on: !s.monMap ? 'true' : 'false', pick: toMons('') }].concat(Object.keys(monMapCount).map(function (mp) {
      return { label: mp, n: monMapCount[mp], on: s.monMap === mp ? 'true' : 'false', pick: toMons(mp) };
    }));
    var monList = mons.filter(function (m) { return (!s.monMap || m.where.some(function (w) { return w.map === s.monMap; })) && (!q || m.n.toLowerCase().indexOf(q) >= 0); });
    out.monRows = monList.map(function (m) {
      return { n: m.n, lv: m.lv, hp: m.hp != null ? m.hp.toLocaleString('en') : '—', dmg: m.dmg[0] != null ? m.dmg[0] + ' ~ ' + m.dmg[1] : '—', def: m.def != null ? m.def : '—',
        where: m.where.map(function (w) { return w.map; }).join(', ') || m.home || '—', drops: rev ? dropN(m.n) : '…', open: openMon(m.n) };
    });
    out.monTitle = s.monMap ? 'Monsters in ' + s.monMap : 'Monsters';
    out.monCount = monList.length + ' monsters';
    out.mo = {};
    if (isMon) {
      var bn = baseOf(s.mon), mb = monByName[s.mon] || monByName[bn] || {};
      var elite = !!bn;
      var wh = whereOf(s.mon);
      // One name can be several monsters (a castle's skeleton at each castle's level): the map
      // it was opened from picks which, by the level the drops list gives it there.
      var ctx = s.monCtx && wh.indexOf(s.monCtx) >= 0 ? s.monCtx : '';
      var lvHere = ctx && this._lv && this._lv[s.mon] ? this._lv[s.mon][ctx] : null;
      if (lvHere != null) {
        var same = s.monsters.filter(function (m) { return m.n === (bn || s.mon); });
        mb = same.filter(function (m) { return m.lv === lvHere || m.lv + 2 === lvHere; })[0] || mb;
      }
      var eliteRow = elite ? ((A.elites && A.elites.elites) || []).filter(function (e) { return e.map === (ctx || wh[0]); })[0] : null;
      var lifeX = elite ? (eliteRow ? eliteRow.life_x : (ELITE_LIFE[ctx || wh[0]] || 2)) : 1;
      var dmgX = elite ? (eliteRow ? eliteRow.damage_x : 2) : 1, defX = elite ? (eliteRow ? eliteRow.defense_x : 1.5) : 1;
      var lvl = (mb.lv || 0) + (elite ? 2 : 0);
      var kv = [];
      if (mb.hp != null) kv.push({ k: 'Life', v: Math.round(mb.hp * lifeX).toLocaleString('en') });
      if (mb.dmg && mb.dmg[0] != null) kv.push({ k: 'Damage', v: Math.round(mb.dmg[0] * dmgX) + ' ~ ' + Math.round(mb.dmg[1] * dmgX) });
      if (mb.def != null) kv.push({ k: 'Defense', v: Math.round(mb.def * defX) });
      if (mb.ar != null) kv.push({ k: 'Attack rate', v: mb.ar });
      if (mb.dr != null) kv.push({ k: 'Defense rate', v: mb.dr });
      if (mb.rng != null) kv.push({ k: 'Attack range', v: mb.rng + (mb.rng === 1 ? ' tile' : ' tiles') });
      if (mb.view != null) kv.push({ k: 'Sees', v: mb.view + ' tiles' });
      if (mb.spd != null) kv.push({ k: 'Attacks every', v: (mb.spd / 1000).toFixed(1) + ' s' });
      var dl = rev && rev[s.mon] ? Object.keys(rev[s.mon]).map(function (k) {
        var e = rev[s.mon][k]; var it = all.filter(function (i) { return i.k === k; })[0];
        if (!it || (ctx && e.byMap[ctx] == null)) return null;
        return { e: ctx ? Object.assign({}, e, { chance: e.byMap[ctx] }) : e, it: it };
      }).filter(Boolean) : [];
      dl.sort(function (a, b) { return (b.e.chance || 0) - (a.e.chance || 0) || (a.it.n > b.it.n ? 1 : -1); });
      var cats = [];
      dl.forEach(function (x) { if (cats.indexOf(x.it.c) < 0) cats.push(x.it.c); });
      var monCat = cats.indexOf(s.monCat) >= 0 ? s.monCat : '';
      var shown = dl.filter(function (x) { return !monCat || x.it.c === monCat; });
      var where = mb.where && !elite ? mb.where.map(function (w) { return { map: w.map, count: w.count + ' in its nests' }; }) : [];
      wh.forEach(function (mp) { if (!where.some(function (w) { return w.map === mp; })) where.push({ map: mp, count: elite ? 'one or two at a time' : (self._lv && self._lv[s.mon] && self._lv[s.mon][mp] != null ? 'level ' + self._lv[s.mon][mp] : 'event') }); });
      where = where.map(function (w) { var on = ctx === w.map; return Object.assign({}, w, { on: on ? 'true' : 'false', bg: on ? '#1E160F' : 'transparent', color: on ? '#F2D58A' : '#C9BFAE', pick: openMon(s.mon, w.map) }); });
      var eliteName = 'Elite ' + s.mon;
      var wanted = s.quests.filter(function (q) { return (q.kills || []).some(function (k) { return k.monster === s.mon || k.monster + 's' === s.mon; }); }).map(function (q) {
        var k = q.kills.filter(function (k) { return k.monster === s.mon || k.monster + 's' === s.mon; })[0];
        return { name: q.name, count: '×' + k.count, open: openQuest(q.id) };
      });
      if (lvHere != null) lvl = lvHere;
      out.mo = { n: s.mon, nameColor: elite ? '#F2C14E' : '#F3E8D4', lv: lvl, ctx: ctx, clearCtx: openMon(s.mon, ''),
        kind: (elite ? 'Elite · ' : '') + 'Level ' + lvl + (ctx ? ' · in ' + ctx : wh.length ? ' · ' + wh.slice(0, 3).join(', ') + (wh.length > 3 ? ' and more' : '') : ''),
        facts: [{ k: 'Level', v: lvl }].concat(mb.hp != null ? [{ k: 'Life', v: Math.round(mb.hp * lifeX).toLocaleString('en') }] : []).concat(rev ? [{ k: 'Drops', v: dl.length + ' items' }] : []),
        kv: kv, where: where,
        eliteNote: elite ? 'An Elite: ×' + lifeX + ' life in ' + (ctx || wh[0] || 'its map') + ', ×' + dmgX + ' damage, ×' + defX + ' defense, 2 levels higher. ' + (eliteRow && eliteRow.immune ? 'It shrugs off ' + eliteRow.immune.join(', ') + '.' : 'Stuns, freezes and slows don’t hold it.') : '',
        hasElite: !elite && !!(rev && rev[eliteName]), eliteLink: 'Its Elite: ' + eliteName, openElite: openMon(eliteName),
        hasBase: elite, baseLink: 'The ordinary ' + bn, openBase: openMon(bn),
        hasWanted: wanted.length > 0, wanted: wanted,
        dropCount: rev ? shown.length + ' items' : '',
        cats: cats.length > 1 ? [{ label: 'All · ' + dl.length, on: !monCat ? 'true' : 'false', pick: function () { self.setState({ monCat: '' }); } }].concat(cats.map(function (c) {
          return { label: c + ' · ' + dl.filter(function (x) { return x.it.c === c; }).length, on: monCat === c ? 'true' : 'false', pick: function () { self.setState({ monCat: c, monAll: true }); } };
        })) : [],
        drops: (s.monAll ? shown : shown.slice(0, 25)).map(function (x) {
          var how = x.e.how ? x.e.how : !x.e.plain ? 'Excellent only' : x.e.excellent ? 'Plain or excellent' : 'Plain';
          var o = odds(x.e.chance);
          return { n: x.it.n, s: x.it.s, dl: x.it.dl || '—', how: how, howColor: !x.e.plain ? '#7FD66A' : x.e.how ? '#F2C14E' : '#C9BFAE', open: open(x.it.k), one: o.one, pct: o.pct };
        }),
        more: !s.monAll && shown.length > 25 ? 'Show all ' + shown.length : '',
        showAll: function () { self.setState({ monAll: true }); } };
    }

    // Events: each castle and square is its own map in the sources, so its monsters and drops
    // are the entries filed under that map.
    var eventInfo = function (name) {
      var monsters = {}, items = {};
      if (rev) Object.keys(rev).forEach(function (mn) {
        Object.keys(rev[mn]).forEach(function (k) {
          var c = rev[mn][k].byMap[name];
          if (c == null) return;
          monsters[mn] = monsters[mn] || { name: mn, level: self._lv[mn][name], n: 0 };
          monsters[mn].n++;
          if (!items[k] || c > items[k].chance) items[k] = { k: k, chance: c, from: mn };
        });
      });
      return { monsters: monsters, items: items };
    };
    out.eventRows = EVENTS.map(function (e) {
      var inf = eventInfo(e[0] === 'gd' ? 'Lorencia (Golden Invasion)' : e[1]);
      var lvs = Object.keys(inf.monsters).map(function (m) { return inf.monsters[m].level; });
      return { name: e[1], band: e[2], open: openEvent(e[0]), mons: rev ? Object.keys(inf.monsters).length : '…', items: rev ? Object.keys(inf.items).length : '…',
        mlv: lvs.length ? Math.min.apply(null, lvs) + '–' + Math.max.apply(null, lvs) : '—' };
    });
    // The Golden Dragon: its rules from src/sim/invasion.h and raid.h, its drops from the sources.
    out.gd = {};
    if (isGold) {
      var TOWNS = [['Lorencia', 'lorencia', [[86, 111, 'West field'], [183, 111, 'East field'], [145, 175, 'South field']]],
        ['Devias', 'devias', [[165, 42, 'Field 1'], [241, 87, 'Field 2'], [200, 84, 'Field 3']]],
        ['Noria', 'noria', [[211, 92, 'Field 1'], [128, 138, 'Field 2'], [199, 168, 'Field 3']]]];
      var town = TOWNS.filter(function (t) { return t[0] === s.gdTown; })[0] || TOWNS[0];
      var ginf = eventInfo(town[0] + ' (Golden Invasion)');
      var glist = Object.keys(ginf.items).map(function (k2) { return { x: ginf.items[k2], it: itemByKey(k2) }; }).filter(function (o) { return o.it; })
        .sort(function (a, b) { return b.x.chance - a.x.chance; });
      var dragonModel = monByName['Golden Dragon'];
      var hoardItem = function (k2, name, note) { var it = k2 ? itemByKey(k2) : null; return { link: !!it, plainName: !it, name: it ? it.n : name, open: it ? open(k2) : function () {}, note: note }; };
      out.gd = {
        lede: 'It comes with the rain. Each storm over Lorencia, Devias or Noria has a one-in-three chance of bringing it: Golden Budge Dragons cross the sky, and the Golden Dragon comes down on a field outside the town. The storm holds until it dies or flies away.',
        facts: [{ k: 'Health', v: '1,800,000' }, { k: 'Built for', v: '10 players' }, { k: 'Time to kill', v: '8 minutes' }, { k: 'Towns', v: 'Lorencia · Devias · Noria' }],
        hasModel: !!dragonModel,
        kv: [{ k: 'Health', v: '1,800,000, the same for any party' }, { k: 'Stays', v: '30 minutes once landed' }, { k: 'Not struck', v: 'flies away after 2 minutes' }, { k: 'Clock', v: 'flies away 8 minutes into the fight' },
          { k: 'Crowd control', v: 'immune' }, { k: 'Tuned against', v: 'ten level-400, +11 heroes, no rebirth' }, { k: 'On the map', v: 'shown on every player’s minimap while it stands' }],
        openBudge: openMon('Golden Budge Dragon', town[0] + ' (Golden Invasion)'),
        minions: ['Up to 24 at once, from the Flight stage on', 'Each leaves a Box of Luck', 'A jewel 1 in 4 · a rune 1 in 10'],
        stages: [{ name: 'Ground', hp: '100% to 70% health', lines: ['Bite, fire breath in a 60° cone of 6 tiles', 'Roar: a shock wave 2 tiles round it'] },
          { name: 'Flight', hp: '70% to 40%', lines: ['Golden Budge Dragons join; a second wave at 50%', 'Fire from the sky in lines, 6 strikes at a time', 'Fire Ball and Meteorite'] },
          { name: 'Enraged', hp: '40% to 15%', lines: ['A meteor on every fighter every 8 seconds', 'Each leaves a burning pool for 12 seconds', 'Hellfire when two or more crowd it'] },
          { name: 'Last stand', hp: '15% to 0%', lines: ['The Golden Inferno: hide in the shadows it casts', 'Everything it has, faster'] }],
        dragonfire: ['Every fire blow that lands lays a stack: up to 5, each +12% fire damage taken, gone 12 seconds after the last.', 'Fire resistance cuts both the fire and what the stacks add: at 10 points you take half. Bring fire-resistance rings and pendants.'],
        hoard: [hoardItem('MagicBox08', 'Box of Kundun', '×5, +1 to +3, scattered round it'), hoardItem('Jewel22', 'Rune of Creation', '×2'), hoardItem(null, 'Jewels of Bless, Soul, Chaos or Life', '×3'),
          hoardItem(null, 'An excellent item', '1, and a second one time in 3'), hoardItem('Quest04', 'Loch’s Feather', '1 in 4')],
        towns: TOWNS.map(function (t) { return { label: t[0], on: t[0] === town[0] ? 'true' : 'false', pick: function () { self.setState({ gdTown: t[0] }); } }; }),
        town: town[0], mapPng: './maps/' + town[1] + '.png',
        fields: town[2].map(function (f) { return { label: f[2] + ' · ' + f[0] + ', ' + f[1], left: ((f[0] + 0.5) / 256 * 100).toFixed(2) + '%', top: ((f[1] + 0.5) / 256 * 100).toFixed(2) + '%' }; }),
        dropCount: rev ? glist.length + ' items' : '',
        drops: (s.gdAll ? glist : glist.slice(0, 20)).map(function (o) { var od = odds(o.x.chance); return { n: o.it.n, s: o.it.s, from: o.x.from, one: od.one, pct: od.pct, open: open(o.x.k) }; }),
        more: !s.gdAll && glist.length > 20 ? 'Show all ' + glist.length : '', showAll: function () { self.setState({ gdAll: true }); } };
    }
    out.ev = {};
    if (isEvent) {
      var inf = eventInfo(evSel[1]);
      var bc = evSel[0].charAt(0) === 'b';
      var mlist = Object.keys(inf.monsters).map(function (m) { return inf.monsters[m]; }).sort(function (a, b) { return a.level - b.level; });
      var ilist = Object.keys(inf.items).map(function (k) { return { x: inf.items[k], it: itemByKey(k) }; }).filter(function (o) { return o.it; })
        .sort(function (a, b) { return b.x.chance - a.x.chance; });
      var ecats = [];
      ilist.forEach(function (o) { if (ecats.indexOf(o.it.c) < 0) ecats.push(o.it.c); });
      var ecat = ecats.indexOf(s.evCat) >= 0 ? s.evCat : '';
      var shownE = ilist.filter(function (o) { return !ecat || o.it.c === ecat; });
      var win = [];
      if (s.src) Object.keys(s.src).forEach(function (k) {
        if (k.charAt(0) === '_') return;
        (s.src[k].events || []).forEach(function (e) { if (e.event === evSel[1]) { var it = itemByKey(k); win.push({ name: it ? it.n : k, note: (e.count > 1 ? '×' + e.count + ' · ' : '') + (e.note || ''), open: it ? open(k) : function () {} }); } });
      });
      var lvs2 = mlist.map(function (m) { return m.level; });
      out.ev = { name: evSel[1],
        lede: (bc ? 'Enter with an Invisibility Cloak from the Messenger of Archangel' : 'Enter with a Devil’s Invitation from Charon') + ' · for heroes level ' + evSel[2] + (bc ? '' : ' (a Magic Gladiator’s level counts one and a half times)'),
        facts: [{ k: 'Your level', v: evSel[2] }].concat(lvs2.length ? [{ k: 'Monsters', v: 'level ' + Math.min.apply(null, lvs2) + '–' + Math.max.apply(null, lvs2) }] : []).concat(rev ? [{ k: 'Drops', v: ilist.length + ' items' }] : []),
        siblings: EVENTS.filter(function (e) { return e[0].charAt(0) === evSel[0].charAt(0); }).map(function (e) { return { label: e[1].replace(/^(Blood Castle|Devil Square) /, (bc ? 'Castle ' : 'Square ')), on: e[0] === evSel[0] ? 'true' : 'false', pick: openEvent(e[0]) }; }),
        monsters: mlist.map(function (m) { return { name: m.name, level: m.level, n: m.n, open: openMon(m.name, evSel[1]) }; }),
        hasWin: win.length > 0, win: win,
        dropCount: rev ? shownE.length + ' items' : '',
        cats: ecats.length > 1 ? [{ label: 'All · ' + ilist.length, on: !ecat ? 'true' : 'false', pick: function () { self.setState({ evCat: '' }); } }].concat(ecats.map(function (c) {
          return { label: c + ' · ' + ilist.filter(function (o) { return o.it.c === c; }).length, on: ecat === c ? 'true' : 'false', pick: function () { self.setState({ evCat: c, evAll: true }); } };
        })) : [],
        drops: (s.evAll ? shownE : shownE.slice(0, 30)).map(function (o) {
          var od = odds(o.x.chance);
          var e = rev[o.x.from][o.x.k];
          return { n: o.it.n, s: o.it.s, from: o.x.from, one: od.one, pct: od.pct, exc: e && !e.plain ? 'Excellent only' : '', open: open(o.x.k), openMon: openMon(o.x.from, evSel[1]) };
        }),
        more: !s.evAll && shownE.length > 30 ? 'Show all ' + shownE.length : '',
        showAll: function () { self.setState({ evAll: true }); } };
    }

    // NPCs and quests: the lists, and one page of each.
    var itemByK = {};
    all.forEach(function (i) { itemByK[i.k] = i; });
    var mapKeyOf = {};
    s.npcs.forEach(function (n) { (n.where || []).forEach(function (w) { mapKeyOf[w.map] = w.map_key; }); });
    var levelText = function (q) { var l = q.level || {}; return l.min ? String(l.min) + (l.max ? '–' + l.max : '+') : '—'; };
    out.npcRows = s.npcs.slice().sort(function (a, b) { return a.name > b.name ? 1 : -1; }).map(function (n) {
      var maps = [];
      (n.where || []).forEach(function (w) { if (maps.indexOf(w.map) < 0) maps.push(w.map); });
      return { name: n.name, role: n.role || '', where: maps.join(', '), sells: (n.sells || []).length || '—', quests: (n.quests || []).length || '—', open: openNpc(n.id) };
    });
    out.npcCount = s.npcs.length + ' NPCs';
    out.questRows = s.quests.map(function (q) {
      var g = npcById[q.giver];
      var rw0 = q.rewards || {};
      return { name: q.name, giver: g ? g.name : q.giver, map: q.map || '', level: levelText(q), repeat: q.repeat || '', open: openQuest(q.id), openGiver: openNpc(q.giver),
        exp: rw0.exp ? rw0.exp.toLocaleString('en') : rw0.first_exp ? rw0.first_exp.toLocaleString('en') : '—',
        firstExp: rw0.first_exp && rw0.exp ? 'first ' + rw0.first_exp.toLocaleString('en') : '',
        zen: rw0.zen ? rw0.zen.toLocaleString('en') : '—' };
    });
    out.questCount = s.quests.length + ' quests';
    out.np = {};
    if (isNpc) {
      var wh2 = npcSel.where || [];
      var ix = Math.min(s.npcMapIx || 0, Math.max(0, wh2.length - 1));
      var here = wh2[ix] || {};
      out.np = { name: npcSel.name, short: npcSel.name.split(' ')[0] === 'the' ? npcSel.name : npcSel.name.replace(/ (the|The) .*$/, ''), role: npcSel.role || '', about: npcSel.about || '',
        services: npcSel.services || [], hasModel: hasFigure(npcSel.figure),
        facts: [{ k: 'In', v: wh2.map(function (w) { return w.map; }).filter(function (m, i, a) { return a.indexOf(m) === i; }).join(', ') || '—' }]
          .concat((npcSel.sells || []).length ? [{ k: 'Sells', v: npcSel.sells.length + ' items' }] : [])
          .concat((npcSel.quests || []).length ? [{ k: 'Quests', v: npcSel.quests.length }] : []),
        maps: wh2.length > 1 ? wh2.map(function (w, i) { return { label: w.map + ' · ' + w.x + ', ' + w.y, on: i === ix ? 'true' : 'false', pick: function () { self.setState({ npcMapIx: i }); } }; }) : [],
        at: here.map ? here.map + ' · ' + here.x + ', ' + here.y : '',
        mapName: here.map || '', mapPng: './maps/' + (here.map_key || 'lorencia') + '.png',
        left: ((here.x + 0.5) / 256 * 100).toFixed(2) + '%', top: ((here.y + 0.5) / 256 * 100).toFixed(2) + '%',
        hasSells: (npcSel.sells || []).length > 0,
        sells: (npcSel.sells || []).map(function (x) {
          var it = itemByK[x.k] || { n: x.k, s: '' };
          return { n: it.n, s: it.s, icon: itemByK[x.k] ? iconOf(it) : '', plus: x.plus ? '+' + x.plus : '—', price: x.price != null ? x.price.toLocaleString('en') + ' Zen' + (x.pieces > 1 ? ' for ' + x.pieces : '') : '—', open: itemByK[x.k] ? open(x.k) : function () {} };
        }),
        hasQuests: (npcSel.quests || []).length > 0,
        quests: (npcSel.quests || []).map(function (id) {
          var q = questById[id] || { name: id }, r = q.rewards || {};
          var paid = [r.exp ? r.exp.toLocaleString('en') + ' exp' : '', r.zen ? r.zen.toLocaleString('en') + ' Zen' : ''].filter(Boolean).join(' · ');
          return { name: q.name, meta: [q.level ? 'Level ' + levelText(q) : '', paid, q.repeat || ''].filter(Boolean).join(' · '), open: openQuest(id) };
        }) };
    }
    out.qu = {};
    if (isQuest) {
      var q = questSel, giver = npcById[q.giver] || { name: q.giver };
      var reqs = [{ k: 'Level', v: levelText(q) }];
      if (q.level && q.level.monsters) reqs.push({ k: 'Its monsters', v: 'level ' + q.level.monsters[0] + '–' + q.level.monsters[1] });
      reqs.push({ k: 'Who', v: q.classes === 'any' ? 'Any class' : (q.classes || 'Any class') });
      if (q.rank) reqs.push({ k: 'Rank', v: q.rank });
      if (q.natives && q.natives.length) reqs.push({ k: 'First-clear rewards for', v: q.natives.join(', ') });
      reqs.push({ k: 'Repeats', v: q.repeat || 'once' });
      var line = function (r) {
        var it = r.k ? itemByK[r.k] : null;
        var meta = [r.count > 1 ? '×' + r.count : '', r.plus ? '+' + r.plus : '', r['class'] ? 'for a ' + r['class'] : '', r.rank ? '(' + r.rank + ')' : '', r.note || ''].filter(Boolean).join(' ');
        return { k: !!it, plainText: !it, name: it ? it.n : (r.name || ''), meta: meta, open: it ? open(r.k) : function () {} };
      };
      var rw = q.rewards || {};
      var groups = [];
      var firstLines = (rw.first || []).map(line);
      if (firstLines.length) groups.push({ title: 'First clear', sub: 'Items paid once, to a hero of this town' + (q.natives_note ? ' · ' + q.natives_note : ''), lines: firstLines });
      var everyLines = (rw.every || []).map(line);
      if (everyLines.length) groups.push({ title: 'Every clear', sub: q.repeat && q.repeat !== 'once' ? 'Items, again ' + q.repeat : '', lines: everyLines });
      var again = q.repeat && q.repeat !== 'once' ? 'each clear, ' + q.repeat : 'once';
      var pay = [];
      if (rw.exp || rw.first_exp) pay.push({ k: 'Experience', v: (rw.exp || rw.first_exp).toLocaleString('en'), color: '#B9A2F2',
        sub: rw.first_exp && rw.exp ? rw.first_exp.toLocaleString('en') + ' the first time · then ' + again : again });
      if (rw.zen) pay.push({ k: 'Zen', v: rw.zen.toLocaleString('en'), color: '#F2C14E', sub: again });
      if (rw.firecrackers) pay.push({ k: 'Firecrackers', v: String(rw.firecrackers), color: '#F3E8D4', sub: 'each clear' });
      var killsLeft = (q.kills || []).slice();
      var objectives = (q.objectives || []).map(function (t) {
        var k = killsLeft.filter(function (k) { return t.indexOf(k.monster) >= 0 || t.indexOf(String(k.count)) >= 0 && t.toLowerCase().indexOf('kill') === 0; })[0];
        if (k && t.toLowerCase().indexOf('kill') === 0) {
          killsLeft.splice(killsLeft.indexOf(k), 1);
          return { isKill: true, isText: false, count: k.count, monster: k.monster, map: k.map, level: k.level, open: openMon(k.monster) };
        }
        return { isKill: false, isText: true, text: t };
      });
      out.qu = { name: q.name, giver: giver.name, map: q.map || '', openGiver: openNpc(q.giver), hasModel: hasFigure(giver.figure),
        facts: [{ k: 'Level', v: levelText(q) }, { k: 'Repeats', v: q.repeat || 'once' }].concat(q.voice ? [{ k: 'Voiced', v: 'yes' }] : []),
        reqs: reqs, text: q.text || '',
        hasPrereq: (q.prereq || []).length > 0, prereqLabel: q.prereq_any ? 'After any one of' : 'After',
        prereq: (q.prereq || []).map(function (id) { return { name: (questById[id] || { name: id }).name, open: openQuest(id) }; }),
        hasNext: (q.next || []).length > 0,
        next: (q.next || []).map(function (id) { return { name: (questById[id] || { name: id }).name, open: openQuest(id) }; }),
        objectives: objectives, rewards: groups, pay: pay, hasPay: pay.length > 0 };
    }

    // Icons: one sprite sheet each for skills and talents, a cell 46 x 64.
    var spr = function (kind, key, scale) {
      var sh = s.icons && s.icons[kind], at = sh && sh.at[String(key)];
      if (!at) return '';
      scale = scale || 1;
      return 'background-image: url(./icons/' + kind + '.png); background-repeat: no-repeat; background-size: ' + (sh.w * scale) + 'px ' + (sh.h * scale) + 'px; background-position: -' + (at[0] * scale) + 'px -' + (at[1] * scale) + 'px;';
    };
    var prettyOf = function (k) { k = String(k).replace(/_/g, ' '); return k.charAt(0).toUpperCase() + k.slice(1); };
    var scalarOf = function (v) { return v == null ? '' : typeof v === 'object' ? '' : typeof v === 'number' ? v.toLocaleString('en') : String(v); };
    var sheetSize = function (kind, scale) { var sh = s.icons && s.icons[kind]; return sh ? (sh.w * scale) + 'px ' + (sh.h * scale) + 'px' : 'auto'; };
    var RCOL = { Common: '#F3E8D4', Rare: '#6FA8FF', Epic: '#C58CF5', Legendary: '#FF9A3C', Ancient: '#4FD6FF' };
    var CLSN = { DK: 'Dark Knight', DW: 'Dark Wizard', FE: 'Fairy Elf', Elf: 'Fairy Elf', MG: 'Magic Gladiator', BK: 'Blade Knight', SM: 'Soul Master', ME: 'Muse Elf' };
    var clsList = function (c) { return c === 'any' || !c ? 'Every class' : Array.isArray(c) ? (c.length >= 4 ? 'Every class' : c.map(function (x) { return CLSN[x] || x; }).join(', ')) : String(c); };

    out.moreBrowse = [['runes', 'Runes', RUNES.length], ['skills', 'Skills', SKILLS.length], ['talents', 'Talents', ''], ['mixes', 'Chaos Machine', ((A.mixes && A.mixes.mixes) || []).length], ['elites', 'Elites', ((A.elites && A.elites.elites) || []).length]].map(function (b) {
      return { label: b[1], n: b[2] || '', on: !detail && s.browse === b[0] ? 'true' : 'false', pick: goBrowse(b[0]) };
    });

    // Runes.
    var rr = A.runes && A.runes._rules;
    out.runeRules = rr ? Object.keys(rr).map(function (k) { return rr[k]; }).filter(function (v) { return typeof v === 'string'; }) : [];
    var rarities = ['Common', 'Rare', 'Epic', 'Legendary', 'Ancient'];
    out.runeChips = [{ label: 'All · ' + RUNES.length, on: !s.runeR ? 'true' : 'false', color: '#C9BFAE', pick: function () { self.setState({ runeR: '' }); } }].concat(rarities.map(function (r) {
      return { label: r + ' · ' + RUNES.filter(function (x) { return x.rarity === r; }).length, on: s.runeR === r ? 'true' : 'false', color: RCOL[r], pick: function () { self.setState({ runeR: r }); } };
    }));
    var shownRunes = RUNES.filter(function (r) { return (!s.runeR || r.rarity === s.runeR) && (!q || r.name.toLowerCase().indexOf(q) >= 0 || (r.effect || '').toLowerCase().indexOf(q) >= 0); })
      .sort(function (a, b) { return rarities.indexOf(a.rarity) - rarities.indexOf(b.rarity) || (a.name > b.name ? 1 : -1); });
    out.runeRows = shownRunes.map(function (r) { return { name: r.power_name || r.name, color: RCOL[r.rarity] || '#F3E8D4', rarity: r.rarity, dormant: !!r.dormant, effect: r.effect || '', level: r.level || '—', classes: clsList(r.classes), slots: (r.slots || []).join(', '), open: openRune(r.id) }; });
    out.runeCount = shownRunes.length + ' rune powers';
    out.ru = {};
    if (isRune) {
      var rk = [{ k: 'Level needed', v: runeSel.level }, { k: 'Classes', v: clsList(runeSel.classes) + (runeSel.second_class_only ? ' (second class)' : '') }, { k: 'Fits', v: (runeSel.slots || []).join(', ') }];
      if (runeSel.chance) rk.push({ k: 'Chance', v: runeSel.chance + '%' });
      if (runeSel.element) rk.push({ k: 'Element', v: runeSel.element });
      if (runeSel.fuses_into) rk.push({ k: 'Three fuse into', v: 'one ' + runeSel.fuses_into });
      if (runeSel.sells_for) rk.push({ k: 'Sells for', v: runeSel.sells_for.toLocaleString('en') + ' Zen' });
      var drop = (runeSel.sources || []).filter(function (x) { return x.type === 'drop'; })[0];
      var dcols = drop && drop.odds && drop.odds.length ? Object.keys(drop.odds[0]).filter(function (k) { return k !== 'monster_level'; }) : [];
      out.ru = { name: runeSel.power_name || runeSel.name, color: RCOL[runeSel.rarity] || '#F3E8D4', rarity: runeSel.rarity, effect: runeSel.effect || '', kv: rk, dormant: !!runeSel.dormant,
        openRune: open('Jewel22'), hasDrop: dcols.length > 0, dropCols: dcols.map(function (c) { return CLSN[c] || c; }),
        dropRows: drop ? drop.odds.map(function (o) { return { band: o.monster_level, cells: dcols.map(function (c) { return o[c] ? odds(o[c]).one : '—'; }) }; }) : [],
        other: (runeSel.sources || []).filter(function (x) { return x.type !== 'drop'; }).map(function (x) {
          var qq = x.quest ? questById[x.quest] : null;
          return { type: prettyOf(x.type), link: !!qq, plainName: !qq, name: qq ? qq.name : (x.name || x.event || x.box || ''), open: qq ? openQuest(qq.id) : function () {},
            note: [x.when, x['class'] ? 'for a ' + x['class'] : '', x.chance ? odds(x.chance).one : '', x.note || ''].filter(Boolean).join(' · ') };
        }) };
    }

    // Skills.
    var skillCls = ['Dark Knight', 'Dark Wizard', 'Fairy Elf', 'Magic Gladiator'];
    out.skillChips = [{ label: 'Every class', on: !s.skillC ? 'true' : 'false', pick: function () { self.setState({ skillC: '' }); } }].concat(skillCls.map(function (c) {
      return { label: c, on: s.skillC === c ? 'true' : 'false', pick: function () { self.setState({ skillC: c }); } };
    }));
    var shownSkills = SKILLS.filter(function (k) { return (!s.skillC || (k.classes || [k['class']]).indexOf(s.skillC) >= 0) && (!q || k.name.toLowerCase().indexOf(q) >= 0); })
      .sort(function (a, b) { return (a.level || 0) - (b.level || 0); });
    var useOf = function (k) { return /primary/i.test(k.slot || '') ? 'Right-click' : /QWER/.test(k.slot || '') ? 'Q W E R' : (k.slot || '').split(' (')[0]; };
    out.skillRows = shownSkills.map(function (k) { return { name: k.name, cls: (k.classes || [k['class']]).join(', '), level: k.level || '—', mana: k.mana != null ? k.mana : '—', cd: k.cooldown_s ? k.cooldown_s + ' s' : '—', range: k.range_tiles != null ? k.range_tiles : '—', use: useOf(k), iconSm: spr('skills', k.id, 32 / 46), bgSize: sheetSize('skills', 32 / 46), open: openSkill(k.id) }; });
    out.skillCount = shownSkills.length + ' skills';
    out.sk = {};
    if (isSkill) {
      var k = skillSel, skv = [];
      skv.push({ k: 'Class', v: (k.classes || [k['class']]).join(', ') + (k.second_class_only ? ' (second class)' : '') });
      if (k.level) skv.push({ k: 'Level', v: k.level });
      if (k.mana != null) skv.push({ k: 'Mana', v: k.mana + (k.mana_note ? ' · ' + k.mana_note : '') });
      skv.push({ k: 'Cooldown', v: k.cooldown_s ? k.cooldown_s + ' s at 0 Agility' + (k.cooldown_rule ? ' · ' + k.cooldown_rule : '') : 'None' });
      if (k.range_tiles != null) skv.push({ k: 'Range', v: k.range_tiles + ' tiles' });
      if (k.area) skv.push({ k: 'Area', v: k.area });
      if (k.damage && k.damage.formula) skv.push({ k: 'Damage', v: k.damage.formula });
      if (k.element) skv.push({ k: 'Element', v: k.element });
      if (k.effect) skv.push({ k: 'Effect', v: typeof k.effect === 'string' ? k.effect : JSON.stringify(k.effect) });
      if (k.weapon) skv.push({ k: 'Weapon', v: k.weapon });
      skv.push({ k: 'Used with', v: k.slot || '' });
      var feeds = [];
      var TC = (B.talents && B.talents.classes) || {};
      Object.keys(TC).forEach(function (c) { (TC[c].columns || []).forEach(function (col) { if ((col.feeds_skills || []).indexOf(k.name) >= 0) feeds.push((CLSN[c] || c) + ': ' + col.name); }); });
      out.sk = { name: k.name, cls: (k.classes || [k['class']]).join(', '), icon: spr('skills', k.id, 1), card: k.card || [], kv: skv,
        hasFrom: (k.learned_from || []).length > 0,
        from: (k.learned_from || []).map(function (f) { var it = f.item ? itemByKey(f.item) : null; return { link: !!it, plainName: !it, name: it ? it.n : (f.name || f.note || ''), open: it ? open(f.item) : function () {}, note: [f.level ? 'level ' + f.level : '', f.energy ? 'Energy ' + f.energy : '', f.strength ? 'Strength ' + f.strength : '', f.note || ''].filter(Boolean).join(' · ') }; }),
        hasTalents: feeds.length > 0, talents: feeds };
    }

    // The talent calculator.
    out.tal = {};
    if (isTalents && B.talents) {
      var TR = B.talents.rules, TCs = B.talents.classes, tc = TCs[s.tcls] ? s.tcls : 'DK';
      var ppl = (TR.points_per_level || {})[tc] || 5;
      var total = Math.floor(((s.tlv - 1) * ppl + s.treb * 400 * ppl) / (TR.stat_points_per_talent || 25));
      var pts = s.tpts[tc] || {};
      var cols = TCs[tc].columns || [];
      var tiersOf = function (col) {
        var by = {};
        col.nodes.forEach(function (n) { (by[n.tier] = by[n.tier] || []).push(n); });
        return Object.keys(by).sort().map(function (t) { return by[t]; });
      };
      var openUnder = function (p, col, ti) {
        var tiers = tiersOf(col);
        var smallOpen = function (i) { if (i === 0) return true; var mj = tiers[i - 1].filter(function (n) { return n.kind === 'major'; })[0]; return !!(mj && p[mj.id]); };
        var majorOpen = function (i) { return smallOpen(i) && tiers[i].some(function (n) { return n.kind !== 'major' && (p[n.id] || 0) >= n.max; }); };
        return { small: smallOpen(ti), major: majorOpen(ti) };
      };
      var valid = function (p) {
        return cols.every(function (col) {
          return tiersOf(col).every(function (tier, ti) {
            var o = openUnder(p, col, ti);
            return tier.every(function (n) { return !(p[n.id] > 0) || (n.kind === 'major' ? o.major : o.small); });
          });
        });
      };
      var spent = Object.keys(pts).reduce(function (a, k2) { return a + (pts[k2] || 0); }, 0);
      var left = total - spent;
      var setPts = function (np) { var all2 = Object.assign({}, self.state.tpts); all2[tc] = np; self.setState({ tpts: all2 }); };
      var picks = [];
      var nodeView = function (n, col, ti, big) {
        var o = openUnder(pts, col, ti), isOpen = n.kind === 'major' ? o.major : o.small, have = pts[n.id] || 0;
        var after = Object.assign({}, pts); after[n.id] = have - 1; if (!after[n.id]) delete after[n.id];
        if (have) picks.push({ name: n.name + (n.kind === 'major' ? '' : ' ' + have + '/' + n.max), color: n.kind === 'major' ? '#F2D58A' : '#F3E8D4', text: n.kind === 'major' ? (n.per_point || n.at_max || '') : (n.per_point || '') + ' a point' });
        return { name: n.name, pts: have, max: n.max, tip: n.name + ' — ' + (n.kind === 'major' ? (n.per_point || n.at_max || '') : (n.per_point || '') + ' a point, ' + (n.at_max || '') + ' at ' + n.max) + (isOpen ? '' : ' · Needs: ' + (n.needs || '')),
          op: isOpen || have ? '1' : '0.45', filter: isOpen || have ? 'none' : 'grayscale(1) brightness(.6)', edge: have ? (n.kind === 'major' ? '#F2C14E' : '#C2362B') : isOpen ? '#6A5426' : '#2A211B',
          color: have ? '#F2D58A' : isOpen ? '#C9BFAE' : '#6F665A',
          icon: spr('talents', n.id, 1), iconL: spr('talents', n.id, 52 / 46), bgSizeL: sheetSize('talents', 52 / 46),
          noAdd: !isOpen || have >= n.max || left <= 0, noSub: !have || !valid(after),
          add: function () { var np = Object.assign({}, pts); np[n.id] = have + 1; setPts(np); },
          sub: function () { setPts(after); } };
      };
      out.tal = { rule: (TR.in_words || {})[tc] || '', level: s.tlv, reb: s.treb, total: total, left: left,
        classes: Object.keys(TCs).map(function (c) { return { label: CLSN[c] || c, on: c === tc ? 'true' : 'false', pick: function () { self.setState({ tcls: c }); } }; }),
        lvLess: function () { self.setState({ tlv: Math.max(1, self.state.tlv - 10 === 0 ? 1 : self.state.tlv - 10) }); },
        lvMore: function () { self.setState({ tlv: Math.min(400, self.state.tlv === 1 ? 10 : self.state.tlv + 10) }); },
        rbLess: function () { self.setState({ treb: Math.max(0, self.state.treb - 1) }); },
        rbMore: function () { self.setState({ treb: self.state.treb + 1 }); },
        reset: function () { setPts({}); },
        columns: cols.map(function (col) {
          return { name: col.name, sub: col.sub || '', tiers: tiersOf(col).map(function (tier, ti) {
            return { smalls: tier.filter(function (n) { return n.kind !== 'major'; }).map(function (n) { return nodeView(n, col, ti); }),
              majors: tier.filter(function (n) { return n.kind === 'major'; }).map(function (n) { return nodeView(n, col, ti, true); }) };
          }) };
        }) };
      out.tal.picks = picks; out.tal.noPicks = picks.length === 0;
      if (left < 0) out.tal.left = left;
    }

    // The Chaos Machine and the Jeweler.
    var MIXES = (A.mixes && A.mixes.mixes) || [];
    out.mixCount = MIXES.length + ' recipes';
    out.mixCards = MIXES.map(function (m) {
      var npc = npcById[m.npc];
      var rate = [];
      if (m.rate && typeof m.rate === 'object') Object.keys(m.rate).forEach(function (k2) { var v = m.rate[k2]; if (scalarOf(v)) rate.push({ k: prettyOf(k2), v: scalarOf(v) + (typeof v === 'number' ? '%' : '') }); });
      else if (m.rate != null) rate.push({ k: 'Rate', v: String(m.rate) });
      var zen = [];
      if (m.zen && typeof m.zen === 'object') Object.keys(m.zen).forEach(function (k2) { zen.push({ k: 'Zen ' + k2, v: Number(m.zen[k2]).toLocaleString('en') }); });
      else if (m.zen) zen.push({ k: 'Zen', v: Number(m.zen).toLocaleString('en') });
      return { name: m.name, npc: npc ? npc.name : (m.npc || ''), openNpc: npc ? openNpc(npc.id) : function () {},
        needs: (m.needs || []).map(function (n) { var it = n.k ? itemByKey(n.k) : null; return { link: !!it, plainName: !it, name: it ? it.n : n.name, open: it ? open(n.k) : function () {}, meta: [n.count != null ? '×' + n.count : '', n.plus ? '+' + n.plus : '', n.note || ''].filter(Boolean).join(' · ') }; }),
        rate: rate, zen: zen,
        result: (m.result || []).map(function (r) { var it = r.k ? itemByKey(r.k) : null; return { link: !!it, name: it ? it.n : '', open: it ? open(r.k) : function () {}, note: r.note || '' }; }),
        fail: m.on_fail ? 'On a failure: ' + m.on_fail : '', notes: m.notes || [] };
    });

    // Elites.
    var ELS = (A.elites && A.elites.elites) || [];
    out.eliteCount = ELS.length + ' maps';
    out.eliteRows = ELS.map(function (e) { return { map: e.map, count: e.count, life: e.life_x, dmg: e.damage_x, def: e.defense_x, breeds: (e.breeds || []).map(function (b) { return { name: b.name, level: b.level, open: openMon('Elite ' + b.name, e.map) }; }) }; });
    var e0 = ELS[0] || {};
    out.eliteLoot = e0.loot || [];
    out.eliteRules = [e0.alert ? 'Shown by a ' + e0.alert : '', e0.immune ? 'Shrugs off ' + e0.immune.join(', ') : '', e0.level_plus ? e0.level_plus + ' levels above its breed' : '', e0.attack_speed ? 'Attack speed ' + e0.attack_speed : '', e0.move_speed ? 'Moves ' + e0.move_speed : '', e0.experience_x ? '×' + e0.experience_x + ' experience' : '', e0.zen_x ? '×' + e0.zen_x + ' Zen' : '', e0.respawn ? 'Returns ' + e0.respawn : ''].filter(Boolean);

    // Which model the viewer should hold: read back in componentDidUpdate.
    if (isGold) { var gdm = monByName['Golden Dragon']; this._want = gdm ? 'm:' + gdm.k : ''; }
    else if (isNpc) this._want = hasFigure(npcSel.figure) ? 'n:' + npcSel.figure : '';
    else if (isQuest) { var gv = npcById[questSel.giver]; this._want = gv && hasFigure(gv.figure) ? 'n:' + gv.figure : ''; }
    else if (isItem) this._want = 'i:' + sel.k;
    else if (isMon) { var mm = monByName[s.mon] || monByName[baseOf(s.mon)]; this._want = mm ? 'm:' + mm.k + (baseOf(s.mon) ? '|elite' : '') : ''; }
    else this._want = '';
    if (!this._setCanvas) this._setCanvas = function (el) { self.setCanvasEl(el); };
    return Object.assign(out, {
      setCanvas: this._setCanvas, model3dLoading: !!s.model3dLoading && (isItem || isMon),
      total: all.length || 401, q: s.q, loading: s.loading, srcLoading: (isItem || isMon || isEvent || isEventList) && s.srcLoading, isList: !detail, isItem: isItem, isMon: isMon, isNpc: isNpc, isQuest: isQuest, isEvent: isEvent, crumbs: crumbs,
      isItemList: !detail && !isMonList && !isNpcList && !isQuestList && !isEventList && !isRuneList && !isSkillList && !isTalents && !isMixList && !isEliteList,
      isMonList: isMonList, isNpcList: isNpcList, isQuestList: isQuestList, isEventList: isEventList,
      isGold: isGold, isRune: isRune, isSkill: isSkill, isRuneList: isRuneList, isSkillList: isSkillList, isTalents: isTalents, isMixList: isMixList, isEliteList: isEliteList,
      goRunes: goBrowse('runes'), goTalents: goBrowse('talents'),
      browseEvents: isEventList ? 'true' : 'false', goEvents: toEvents,
      monTotal: s.monsters.length || '', npcTotal: s.npcs.length || '', questTotal: s.quests.length || '',
      browseItems: !detail && s.browse === 'items' ? 'true' : 'false', browseMons: isMonList ? 'true' : 'false',
      browseNpcs: isNpcList ? 'true' : 'false', browseQuests: isQuestList ? 'true' : 'false',
      goItems: toList('All', ''), goMons: toMons(''), goNpcs: toNpcs, goQuests: toQuests,
      onQ: function (e) { self.setState({ q: e.target.value, page: 1, view: 'list' }); },
      cats: cats, classes: ['DK', 'DW', 'FE', 'MG'].map(function (c) { return { label: c, title: CLS[c], on: s.cls === c ? 'true' : 'false', pick: function () { self.setState({ cls: s.cls === c ? '' : c, page: 1 }); } }; }),
      srcChips: srcChips, cols: cols, rows: rows, noRows: !s.loading && list.length === 0,
      clear: function () { self.setState({ q: '', cls: '', srcf: '', cat: 'All', sub: '', page: 1 }); },
      title: s.sub || (s.cat === 'All' ? 'All items' : s.cat),
      rangeText: list.length ? from + '–' + to + ' of ' + list.length + ' items' : '',
      page: page, pages: pages, noPrev: page <= 1, noNext: page >= pages,
      prev: function () { self.setState({ page: page - 1 }); },
      next: function () { self.setState({ page: page + 1 }); },
      less: function () { self.setState({ plus: Math.max(0, self.state.plus - 1) }); },
      more: function () { self.setState({ plus: self.state.plus + 1 }); },
      excOn: s.exc ? 'true' : 'false', excLabel: s.exc ? 'On' : 'Off', togExc: function () { self.setState({ exc: !self.state.exc }); },
      luckOn: s.luck ? 'true' : 'false', luckLabel: s.luck ? 'On' : 'Off', togLuck: function () { self.setState({ luck: !self.state.luck }); },
      optText: s.opt ? '+' + s.opt * 4 : 'None', noOptLess: s.opt <= 0, noOptMore: s.opt >= 4,
      optLess: function () { self.setState({ opt: Math.max(0, self.state.opt - 1) }); },
      optMore: function () { self.setState({ opt: Math.min(4, self.state.opt + 1) }); },
      showAllDrops: function () { self.setState({ allDrops: true }); },
      noComments: isItem && out.comments.length === 0, draft: s.draft,
      onDraft: function (e) { self.setState({ draft: e.target.value }); },
      noPost: !s.draft.trim(),
      post: function () {
        var t = self.state.draft.trim();
        if (!t || !sel) return;
        var n = Object.assign({}, self.state.notes);
        n[sel.k] = [{ id: Date.now(), who: '[YOUR ACCOUNT NAME]', about: self.state.about, when: 'just now', text: t, votes: 0, mine: false }].concat(n[sel.k] || []);
        self.setState({ notes: n, draft: '', cfilter: 'all' });
      }
    });
  }
}
