// The game's item card on the web: one Sheet (src/game/ui/tip.h, as the dump writes it from the
// game's own describe()) drawn as src/game/ui/tip.cpp draws it, sizes and inks from
// tools/armory/prototype/card_style.json (1080-line pixels; the card is 346 wide).
// A pure function, so the page, the tests and the visual check all draw the same card.
(function (root) {
  'use strict';
  // tip::colourOf, and each tone's tail: the tone a third of the way to white (card_style.json tones).
  var TONE = {
    White: ['#ffffff', '#ffffff'], Blue: ['#80b2ff', '#accdff'], Red: ['#ff331a', '#ff7a6a'],
    Yellow: ['#ffcc1a', '#ffde6a'], Green: ['#1aff80', '#6affac'], Gray: ['#96998e', '#bbbdb6'],
    Violet: ['#b266ff', '#cd9cff'], RedPurple: ['#cc80cc', '#deacde'], Orange: ['#e66b0a', '#ef9f60'],
    Uncommon: ['#ffffff', '#ffffff'], Rare: ['#0070dd', '#59a2e9'], Epic: ['#a335ee', '#c37cf4'],
    Legendary: ['#1eff00', '#6dff59'], Artifact: ['#ffd100', '#ffe159'], Quest: ['#d685ff', '#e4b0ff'],
    RuneLegendary: ['#ff8000', '#ffac59'], RuneAncient: ['#00ccff', '#59deff']
  };
  var LABEL = '#c4c1b4', QUIET = '#96998e', FOOT = '#7d8077', LETTERING = '#e6dbc2';
  var ink = function (t, d) { return t && TONE[t] ? TONE[t][0] : (d || '#ffffff'); };
  var tail = function (t) { return t && TONE[t] ? TONE[t][1] : '#ffffff'; };
  var esc = function (s) {
    return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
    });
  };
  var rgba = function (hex, a) {
    var n = parseInt(hex.slice(1), 16);
    return 'rgba(' + (n >> 16) + ',' + ((n >> 8) & 255) + ',' + (n & 255) + ',' + a + ')';
  };

  // tip::glyphAt's marks, in a 16 px gutter (Ring and RingSet 18).
  function mark(m, colour) {
    if (m === 'Diamond') return '<svg width="16" height="21" viewBox="-8 -10.5 16 21"><rect x="-2.48" y="-2.48" width="4.96" height="4.96" fill="' + colour + '"/></svg>';
    if (m === 'Star') return '<svg width="16" height="21" viewBox="-8 -10.5 16 21"><polygon points="0,-4 1.28,-1.28 4,0 1.28,1.28 0,4 -1.28,1.28 -4,0 -1.28,-1.28" fill="' + colour + '"/></svg>';
    if (m === 'Socket') return '<svg width="16" height="21" viewBox="-8 -10.5 16 21"><rect x="-2.5" y="-2.5" width="5" height="5" fill="none" stroke="' + colour + '" stroke-width="1"/></svg>';
    if (m === 'Triangle') return '<svg width="16" height="21" viewBox="-8 -10.5 16 21"><polygon points="0,-3.6 3.6,3 -3.6,3" fill="' + colour + '"/></svg>';
    if (m === 'Ring' || m === 'RingSet') {
      var s = '<svg width="18" height="21" viewBox="-9 -10.5 18 21"><polygon points="0,-9 7.2,0 0,9 -7.2,0" fill="rgba(0,0,0,.7)"/>' +
        '<polygon points="0,-7.92 6.34,0 0,7.92 -6.34,0" fill="#9e733d"/><polygon points="0,-5.4 4.32,0 0,5.4 -4.32,0" fill="#0d0908"/>';
      if (m === 'RingSet') s += '<polygon points="0,-5.58 4.46,0 0,5.58 -4.46,0" fill="#8c80ff"/><polygon points="0,-3.6 2.88,0 0,3.6 -2.88,0" fill="#ff63b3"/>';
      return s + '</svg>';
    }
    if (m && m !== 'None') return '<svg width="16" height="21" viewBox="-8 -10.5 16 21"><rect x="-2.48" y="-2.48" width="4.96" height="4.96" fill="' + colour + '"/></svg>';
    return '';
  }

  function value(v) {
    var c = ink(v.tone);
    if (v.chip) return '<span class="gchip" style="color:' + c + ';border-color:' + rgba(c, 0.5) + '">' + esc(v.text) + '</span>';
    return '<b style="color:' + c + '">' + esc(v.text) + '</b>';
  }

  function row(r) {
    var gap = r.gap ? ' gap' : '';
    var m = r.mark && r.mark !== 'None' ? '<span class="gmark" aria-hidden="true">' + mark(r.mark, ink(r.markTone, ink(r.freeTone))) + '</span>' : '';
    if (r.free != null && r.free !== '' || r.keyword) {
      var kw = r.keyword ? '<span style="color:' + ink(r.keywordTone) + '">' + esc(r.keyword) + '</span> · ' : '';
      var t = r.tail ? ' <b style="color:' + tail(r.freeTone) + '">' + esc(r.tail) + '</b>' : '';
      return '<div class="gopt' + gap + '">' + m + kw + '<span style="color:' + ink(r.freeTone) + '">' + esc(r.free) + '</span>' + t + '</div>';
    }
    var lab = r.labelToned ? ink(r.labelTone) : LABEL;
    return '<div class="gstat' + gap + '">' + m + '<span style="color:' + lab + '">' + esc(r.label) + '</span><span class="gvals">' +
      (r.values || []).map(value).join('') + '</span></div>';
  }

  // The card's HTML. `picture`: the 96 px thumbnail's URL for the head's picture, or none.
  function render(sheet, picture) {
    if (!sheet || !sheet.name) return '';
    var tone = ink(sheet.nameTone);
    var art = 96 * (sheet.artScale || 1);
    var wide = sheet.wide || 346;
    var out = '<div class="gcard" style="width:' + wide + 'px">';
    var pic = picture ? '<img class="gpic" src="' + esc(picture) + '" alt="" width="' + art + '" height="' + art + '" style="width:' + art + 'px;height:' + art + 'px">' : '';
    // The head and the headline stand at least as tall as the picture (body_top).
    out += '<div class="gtop" style="min-height:' + (picture ? 6 + art + 4 : 0) + 'px">' + pic;
    out += '<div class="ghead" style="background:linear-gradient(' + rgba(tone, 0.122) + ',rgba(0,0,0,0))">' +
      '<span class="gline" style="background:linear-gradient(90deg,rgba(0,0,0,0),' + rgba(tone, 0.8) + ',rgba(0,0,0,0))"></span>' +
      '<div class="gtitle" style="color:' + tone + ';padding-right:' + (picture ? Math.round(0.6 * art) : 0) + 'px">' + esc(sheet.name) + '</div>';
    if (sheet.base) out += '<div class="gsub" style="color:' + tone + '">' + esc(sheet.base) + '</div>';
    if (sheet.bound) out += '<div class="gbound" style="color:' + ink(sheet.boundTone || 'RedPurple') + '">' + esc(sheet.bound) + '</div>';
    out += '</div>';
    var h = sheet.hero;
    if (h && h.value) {
      out += '<div class="ghero"><div class="ghl"><b style="color:' + ink(h.tone, '#ffcc1a') + '">' + esc(h.value) + '</b><span>' + esc(h.word) + '</span></div>';
      if (h.rail && h.rail.length) {
        out += '<div class="gbr">' + h.rail.map(function (r) {
          var v = (r.values || [])[0] || {};
          return '<div><b style="color:' + (v.tone && v.tone !== 'White' ? ink(v.tone) : LABEL) + '">' + esc(v.text) + '</b>' + esc(r.label) + '</div>';
        }).join('') + '</div>';
      }
      out += '</div>';
    }
    out += '</div>';
    (sheet.sections || []).forEach(function (s) {
      if (!s.rows || !s.rows.length) return;
      var kick = !sheet.item && s.kicker ? '<div class="gkick">' + esc(s.kicker) + '</div>' : '';
      out += '<div class="gsec">' + kick + (s.framed ? '<div class="gframe">' : '') + s.rows.map(row).join('') + (s.framed ? '</div>' : '') + '</div>';
    });
    var left = (sheet.who || []).map(function (l) {
      var c = l.tone === 'Red' ? ink('Red') : l.quiet ? FOOT : ink(l.tone, FOOT);
      return '<div style="color:' + c + '">' + esc(l.text) + '</div>';
    });
    var right = [];
    if (sheet.wear) {
      var circ = 2 * Math.PI * 8, worn = Math.max(0, Math.min(1, sheet.worn == null ? 1 : sheet.worn));
      var arc = sheet.wearTone && sheet.wearTone !== 'White' ? ink(sheet.wearTone) : LETTERING;
      right.push('<div class="r"><svg width="19" height="19" viewBox="-9.5 -9.5 19 19" aria-hidden="true"><circle r="8" fill="none" stroke="rgba(255,255,255,.113)" stroke-width="3"/>' +
        '<circle r="8" fill="none" stroke="' + arc + '" stroke-width="3" stroke-dasharray="' + (circ * worn).toFixed(2) + ' ' + circ.toFixed(2) + '" transform="rotate(-90)"/></svg>' + esc(sheet.wear) + '</div>');
    }
    if (sheet.note) right.push('<div class="r" style="color:' + ink(sheet.noteTone || 'Red') + '">' + esc(sheet.note) + '</div>');
    (sheet.keep || []).forEach(function (l) { right.push('<div class="r" style="color:' + (l.quiet ? FOOT : ink(l.tone, FOOT)) + '">' + esc(l.text) + '</div>'); });
    if (sheet.price) right.push('<div class="r" style="color:' + ink(sheet.priceTone || 'Yellow') + '">' + esc(sheet.price) + '</div>');
    if (left.length || right.length) out += '<div class="gfoot"><div>' + left.join('') + '</div><div>' + right.join('') + '</div></div>';
    if (sheet.sell) {
      out += '<div class="gsell"><span>SELLS FOR</span><b><svg width="15" height="15" viewBox="-7.5 -7.5 15 15" aria-hidden="true">' +
        '<circle r="6.5" fill="#d9a52b" stroke="#7a5512" stroke-width="1"/><circle r="3.6" fill="none" stroke="#f5d27a" stroke-width="1"/></svg>' + esc(sheet.sell) + '</b></div>';
    }
    return out + '</div>';
  }

  // A card's states, in the order a page offers them, with a reader's label for each.
  function states(cards) {
    var keys = Object.keys(cards || {});
    var rank = function (k) { return k === 'plain' ? 0 : /unmet/.test(k) ? 9 : 1 + (parseInt((k.match(/\+(\d+)/) || [0, 0])[1], 10) || 0) / 100; };
    return keys.sort(function (a, b) { return rank(a) - rank(b); });
  }
  function stateLabel(k) {
    if (k === 'plain') return 'Plain';
    if (/unmet/.test(k)) return 'Not wearable yet';
    return k.replace(/\(bits[^)]*\)\s*/, '').replace(/ 3 sockets/, ', 3 runes').replace(/\bluck\b/g, 'Luck')
      .replace(/\bexcellent\b/g, 'Excellent').replace(/\boption\b/g, 'option').replace(/\s+/g, ' ').trim();
  }

  root.MUCard = { render: render, states: states, stateLabel: stateLabel, ink: ink, esc: esc, TONE: TONE };
})(typeof window !== 'undefined' ? window : globalThis);
