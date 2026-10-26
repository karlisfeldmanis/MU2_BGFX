// Turns the Talents canvas's data (its project/Passive.dc.html, read with the Artifact tool) into
// src/sim/talents_table.inc:
//   node pipeline/talents_table.js <Passive.dc.html> <talents_table.inc>
// And the Dark Lord's, from his own canvas (https://claude.ai/artifact/1zGbUUcZYinz51tqbPW1Tr, its
// project/Main.dc.html, kept as docs/plans/talents/Lord.dc.html) into src/sim/talents_lord.inc:
//   node pipeline/talents_table.js --lord docs/plans/talents/Lord.dc.html src/sim/talents_lord.inc
// Every small talent's label and every major's name must map to a sim stat or behaviour here, or it stops.
const fs = require('fs');
const lord = process.argv[2] === '--lord';
const [canvasPath, outPath] = process.argv.slice(lord ? 3 : 2);
const html = fs.readFileSync(canvasPath, 'utf8');
function lordData() {
  const start = html.indexOf('const LORD = [');
  const end = html.indexOf('\n];', start) + 3;
  const cols = new Function(html.slice(start, end) + '; return LORD;')();
  // The canvas types a curly apostrophe; the names here are keyed with a straight one.
  for (const col of cols) for (const nd of col.nodes) nd.n = nd.n.replace(/’/g, "'");
  return { CLASSES: { 'Dark Lord': cols } };
}
const data = lord ? lordData() : (() => {
  const start = html.indexOf('const S = (');
  const end = html.indexOf('const ATLAS');
  return new Function(html.slice(start, end) + '; return { CLASSES, FORTUNE, PER_LEVEL };')();
})();

const E = { wind: 'Element::Wind', fire: 'Element::Fire', lightning: 'Element::Lightning', ice: 'Element::Ice', poison: 'Element::Poison' };
const fx = (stat, o = {}) => Object.assign({ stat, hold: 'Any', element: 'Element::None', skills: [], special: 'None', chance: 0, seconds: 0 }, o);
const SMALL = {
  'two-handed damage': fx('Damage', { hold: 'TwoHanded' }),
  'attack speed with two hands': fx('AttackSpeed', { hold: 'TwoHanded' }),
  'Slash, Falling Slash, Death Stab and Impale damage': fx('SkillDamage', { skills: [23, 19, 43, 47] }),
  'ignore defence chance': fx('IgnoreDefense'),
  'excellent damage chance': fx('ExcellentChance'),
  'attack success rate': fx('AttackRate'),
  'dual-wield damage': fx('Damage', { hold: 'DualWield' }),
  'attack speed while dual wielding': fx('AttackSpeed', { hold: 'DualWield' }),
  'Lunge and Uppercut damage': fx('SkillDamage', { skills: [20, 21] }),
  'critical damage chance': fx('CriticalChance'),
  'defence rate': fx('DefenseRate'),
  'defence': fx('Defense'),
  'defence with a shield': fx('Defense', { hold: 'Shield' }),
  'Defense effect, past its cap': fx('GuardEffect'),
  'Defense and Magic Shield effect, past their cap': fx('GuardAndBarrierEffect'),
  'damage reflected': fx('Reflect'),
  'max SD': fx('MaxShield'),
  'wind damage': fx('ElementDamage', { element: E.wind }),
  'fire damage': fx('ElementDamage', { element: E.fire }),
  'lightning damage': fx('ElementDamage', { element: E.lightning }),
  'Cyclone and Twisting Slash mana cost': fx('SkillManaCost', { skills: [22, 41] }),
  'Cyclone and Twisting Slash damage': fx('SkillDamage', { skills: [22, 41] }),
  'ice and poison resistance': fx('IcePoisonResistance'),
  'damage': fx('Damage'),
  'Rageful Blow damage': fx('SkillDamage', { skills: [42] }),
  'of max life back on a kill': fx('LifeOnKill'),
  'attack speed': fx('AttackSpeed'),
  'of damage dealt back as life': fx('LifeSteal'),
  'Hellfire damage': fx('SkillDamage', { skills: [10] }),
  'Inferno damage': fx('SkillDamage', { skills: [14] }),
  'Hellfire and Inferno mana cost': fx('SkillManaCost', { skills: [10, 14] }),
  'Flame and Fire Ball damage': fx('SkillDamage', { skills: [5, 4] }),
  'casting speed': fx('CastSpeed'),
  'Lightning damage': fx('SkillDamage', { skills: [3] }),
  'faster Lightning channel': fx('LightningChannel'),
  'Cometfall damage': fx('SkillDamage', { skills: [13] }),
  'Lightning mana cost': fx('SkillManaCost', { skills: [3] }),
  'Twister damage': fx('SkillDamage', { skills: [8] }),
  'Ice damage': fx('SkillDamage', { skills: [7] }),
  'Poison damage': fx('SkillDamage', { skills: [1] }),
  'longer chill': fx('ChillLonger'),
  'longer poison': fx('PoisonLonger'),
  'slower swings for chilled monsters': fx('ChilledSlower'),
  'Poison mana cost': fx('SkillManaCost', { skills: [1] }),
  'Evil Spirit damage': fx('SkillDamage', { skills: [9] }),
  'max mana': fx('MaxMana'),
  'wizardry': fx('Wizardry'),
  'Evil Spirit mana cost': fx('SkillManaCost', { skills: [9] }),
  'mana regeneration': fx('ManaRegen'),
  'max life': fx('MaxLife'),
  'bow and crossbow damage': fx('Damage', { hold: 'Bow' }),
  'Multi-Shot damage': fx('SkillDamage', { skills: [24] }),
  'Penetration damage': fx('SkillDamage', { skills: [52] }),
  'healing done': fx('HealingDone'),
  'Heal and buff mana cost': fx('AidManaCost'),
  'buff effect': fx('BuffEffect'),
  'tiles of reach for heals and buffs': fx('AidReach'),
  'summon damage': fx('SummonDamage'),
  'summon life': fx('SummonLife'),
  'summon defence': fx('SummonDefense'),
  'summon attack success rate': fx('SummonAttackRate'),
  'summon mana cost': fx('SummonManaCost'),
  'block chance': fx('BlockChance'),
  'Flame damage': fx('SkillDamage', { skills: [5] }),
  'Flame mana cost': fx('SkillManaCost', { skills: [5] }),
  'damage and wizardry': fx('DamageAndWizardry'),
  'Twisting Slash damage': fx('SkillDamage', { skills: [41] }),
  'one-handed weapon damage': fx('Damage', { hold: 'OneHanded' }),
  'Cyclone damage': fx('SkillDamage', { skills: [22] }),
  'Hellfire and Flame damage': fx('SkillDamage', { skills: [10, 5] }),
  'fire spell mana cost': fx('SkillManaCost', { skills: [4, 5, 10, 14, 2] }),
  'Fire Ball and Meteorite damage': fx('SkillDamage', { skills: [4, 2] }),
  'Zen from kills': fx('MoreZen'),
  'more jewels from kills': fx('MoreJewels'),
  'potion healing': fx('PotionHealing'),
  'more items from kills': fx('MoreItems'),
  // The game's words since 1aba88d2 (defense, max shield), as the Dark Lord's canvas has them.
  'defense with a shield': fx('Defense', { hold: 'Shield' }),
  'defense rate': fx('DefenseRate'),
  'ignore defense chance': fx('IgnoreDefense'),
  'max shield': fx('MaxShield'),
  // The Dark Lord's (docs/plans/talents/Lord.dc.html).
  'Force and Force Wave damage': fx('SkillDamage', { skills: [60, 66] }),
  'Fire Burst damage': fx('SkillDamage', { skills: [61] }),
  'Fire Scream damage': fx('SkillDamage', { skills: [78] }),
  'Fire Burst and Fire Scream mana cost': fx('SkillManaCost', { skills: [61, 78] }),
  'attack speed with Fire Burst and Fire Scream': fx('AttackSpeed', { skills: [61, 78] }),
  'excellent damage chance with Fire Burst and Fire Scream': fx('ExcellentChance', { skills: [61, 78] }),
  'Electric Spike damage': fx('SkillDamage', { skills: [65] }),
  'Electric Spike mana cost': fx('SkillManaCost', { skills: [65] }),
  'Earthshake damage': fx('SkillDamage', { skills: [62] }),
  'Electric Spike critical damage chance': fx('CriticalChance', { skills: [65] }),
  'chance Electric Spike chills: half walk and slower swings for 3 s': fx('ChillChance', { skills: [65], seconds: 3 }),
  'damage while riding': fx('RidingDamage'),
  'raven damage': fx('RavenDamage'),
  'raven attack speed': fx('RavenAttackSpeed'),
  'Command': fx('Command'),
  'slower raven wear': fx('RavenWear'),
  'raven critical damage chance': fx('RavenCritical'),
  'raven damage to every monster in its ball': fx('RavenBallDamage'),
  'Critical Damage and Summon mana cost': fx('SkillManaCost', { skills: [64, 63] }),
  'tiles of reach for buffs': fx('AidReach'),
};
// A major's whole effect: `amount` is what it gives at its one point.
const sp = (special, amount, o = {}) => Object.assign(fx('Special', o), { special, amount });
const st = (stat, amount, o = {}) => Object.assign(fx(stat, o), { amount });
const MAJOR = {
  'Frost Edge': sp('ChillOnHit', 0, { hold: 'TwoHanded', chance: 10, seconds: 3 }),
  'Executioner': sp('Executioner', 20, { hold: 'TwoHanded' }),
  'Impaler': st('SkillDamage', 15, { skills: [23, 19, 43, 47] }),
  'Off-Hand Mastery': sp('OffHand', 15),
  'Riposte': sp('Riposte', 30),
  'Storm of Steel': st('AttackSpeed', 10, { hold: 'DualWield' }),
  'Hold the Line': st('DamageDecrease', 5),
  'Taunt': st('Threat', 30),
  'Last Stand': sp('LastStand', 20),
  'Eye of the Storm': sp('EyeOfTheStorm', 2),
  'Wind Attunement': sp('Attunement', 5, { element: E.wind }),
  'Fire Attunement': sp('Attunement', 5, { element: E.fire }),
  'Hurricane': st('ElementDamage', 20, { element: E.wind }),
  'Gale Archer': st('ElementDamage', 20, { element: E.wind }),
  'Conflagration': st('ElementDamage', 20, { element: E.fire }),
  'Battle Fury': sp('DamageProc', 20, { chance: 10, seconds: 5 }),
  'Bloodrush': sp('KillSpeed', 15, { seconds: 4 }),
  'Bloodlust': sp('Crowd', 10),
  'Wider Rings': sp('Reach', 1, { skills: [10, 14] }),
  'Chain Lightning': sp('ChainMore', 1, { skills: [3] }),
  'Thunderclap': sp('VsChilled', 15, { skills: [3] }),
  'Frost Burst': sp('Reach', 1, { skills: [7] }),
  'Frostbite': sp('VsChilled', 15),
  'Legion': sp('Reach', 1, { skills: [9] }),
  'Mana Surge': sp('ManaBack', 0, { chance: 10 }),
  'Archmage': st('Wizardry', 10),
  'Mana Ward': st('BarrierEffect', 10),
  'Fortitude': st('MaxLife', 10),
  'Eagle Eye': st('ExcellentChance', 10, { hold: 'Bow' }),
  'Icy Shot': sp('ChillOnHit', 0, { hold: 'Bow', chance: 10, seconds: 3 }),
  'Deadeye': sp('FarShot', 10),
  'Shared Light': sp('SharedLight', 20),
  'Patron': sp('Patron', 15),
  'Mercy': sp('Mercy', 20, { chance: 10, seconds: 5 }),
  'Pack Leader': st('SummonDamage', 15),
  'Guardian Spirit': st('SummonDamageTaken', -20),
  'Beastmaster': st('SummonDamageAndLife', 20),
  'Evasion': st('DefenseRate', 20),
  'Parry': st('BlockChance', 5),
  'Iron Will': sp('IronWill', 30, { seconds: 4 }),
  "Gladiator's Edge": sp('WizardryIntoSwings', 10),
  'Battle Mage': sp('DamageProc', 20, { chance: 10, seconds: 5, element: 'Element::None', hold: 'Any', skills: [], wizardry: true }),
  'Magic Gladiator': st('DamageAndWizardry', 10),
  'Treasure Hunter': st('MoreZen', 10),
  "Jeweller's Eye": st('MoreJewels', 5),
  'Lucky Find': st('MoreExcellentItems', 20),
  // The Dark Lord's. Force from a scepter is still Force (60) to the blow, so 66 only names it.
  'Execution': sp('Executioner', 20, { skills: [60, 66] }),
  "Emperor's Scepter": st('SkillDamage', 15, { skills: [60, 66] }),
  'Wide Burst': sp('Reach', 1, { skills: [61] }),
  'Chain Blast': sp('ChainBlast', 25, { skills: [61] }),
  'Hellstorm': st('SkillDamage', 15, { skills: [61, 78] }),
  'Overload': sp('VsChilled', 15, { skills: [65] }),
  'Trample': sp('Crowd', 10),
  'Thunderlord': st('SkillDamage', 15, { skills: [65] }),
  'Hunting Eye': sp('RavenReach', 1),
  'Hunting Bond': st('RavenDamage', 15),
  'Lord of Ravens': st('RavenAttackSpeed', 10),
  "Lord's Critical": sp('KeenMore', 15),
};
// The three attunements over more than one element are told apart by name.
const MULTI = {
  'Storm Attunement': ['lightning', 'wind'],
  'Frost and Venom Attunement': ['ice', 'poison'],
};

const errors = [];
const used = new Set();
const cpp = (s) => JSON.stringify(s);
const num = (x) => (Number.isInteger(x) ? x.toFixed(1) : String(x)) + 'f';
function effect(e, per) {
  const skills = [...e.skills, 0, 0, 0, 0, 0].slice(0, 5).join(', ');
  const elements = e.elements || (e.element === 'Element::None' ? '0' : `bit(${e.element})`);
  return `{TalentStat::${e.stat}, ${num(per)}, Hold::${e.hold}, ${elements}, {${skills}}, Special::${e.special}, ${e.chance}, ${e.seconds}, ${e.wizardry ? 'true' : 'false'}}`;
}
function node(nd, klass) {
  if (nd.k === 's') {
    const e = SMALL[nd.label];
    if (!e) { errors.push(`${klass}: small "${nd.n}" label "${nd.label}" has no stat`); return ''; }
    used.add(e.stat);
    return `{${cpp(nd.n)}, ${cpp(nd.label)}, ${nd.icon}, ${effect(e, nd.per)}}`;
  }
  let e = MAJOR[nd.n];
  if (!e && nd.n === 'Storm Attunement') {
    const els = /ice/.test(nd.text) ? ['lightning', 'wind', 'ice'] : MULTI['Storm Attunement'];
    e = Object.assign(sp('Attunement', 5), { elements: els.map((x) => `bit(${E[x]})`).join(' | ') });
  } else if (!e && MULTI[nd.n]) {
    e = Object.assign(sp('Attunement', 5), { elements: MULTI[nd.n].map((x) => `bit(${E[x]})`).join(' | ') });
  }
  if (!e) { errors.push(`${klass}: major "${nd.n}" has no effect`); return ''; }
  used.add(e.stat);
  return `{${cpp(nd.n)}, ${cpp(nd.text)}, ${nd.icon}, ${effect(e, e.amount)}}`;
}
const KIN = { 'Dark Wizard': 'DarkWizard', 'Elf': 'FairyElf', 'Dark Knight': 'DarkKnight', 'Magic Gladiator': 'MagicGladiator', 'Dark Lord': 'DarkLord' };
let out = lord
  ? '// GENERATED from the Dark Lord\'s Talents canvas (https://claude.ai/artifact/1zGbUUcZYinz51tqbPW1Tr,\n' +
    '// project/Main.dc.html, kept as docs/plans/talents/Lord.dc.html) by pipeline/talents_table.js --lord.\n' +
    '// Do not edit by hand: change the canvas and generate again, so the board and the game cannot drift.\n'
  : '// GENERATED from the Talents canvas (https://claude.ai/artifact/RTLpPhd7Prn4G6eX86FCaL,\n' +
    '// project/Passive.dc.html) by pipeline/talents_table.js. Do not edit by hand:\n' +
    '// change the canvas and generate again, so the board and the game cannot drift.\n';
for (const [klass, cols] of Object.entries(data.CLASSES)) {
  out += `\n// ---- ${klass} ----\n`;
  out += `inline constexpr TalentColumn k${KIN[klass]}Talents[] = {\n`;
  for (const col of cols) {
    const feeds = [...col.skills, 0, 0, 0, 0, 0, 0].slice(0, 6).join(', ');
    out += `    {${cpp(col.name)}, ${cpp(col.sub)}, {${feeds}}, {\n`;
    for (const nd of col.nodes) out += `        ${node(nd, klass)},\n`;
    out += '    }},\n';
  }
  out += '};\n';
}
if (errors.length) { console.error(errors.join('\n')); process.exit(1); }
fs.writeFileSync(outPath, out);
console.log('stats used:', [...used].sort().join(' '));
