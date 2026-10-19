// What the front page and the account page share: the sign-in kept in the browser, the calls to
// server.py, and the small helpers both draw with. Everything a player wrote goes in as text.
window.MU = (() => {
  const $ = (id) => document.getElementById(id);

  // The page's language: words a script says go through t(), English with {name} for what fills in, and a page
  // served in another language (web/i18n.py) carries their translations in window.MU_T.
  const LANG = (document.documentElement.lang || 'en').slice(0, 2);
  const SAID = window.MU_T || {};
  const t = (words, fill) => {
    let out = SAID[words] || words;
    if (fill) for (const k in fill) out = out.split('{' + k + '}').join(fill[k]);
    return out;
  };
  const home = LANG === 'en' ? '/' : '/' + LANG + '/';
  const LANGS = ['pt', 'es', 'ru', 'vi', 'pl'];
  const LANG_KEY = 'mu-lang';
  const store = (k, v) => { try { localStorage.setItem(k, v); } catch (e) {} };
  const stored = (k) => { try { return localStorage.getItem(k) || ''; } catch (e) { return ''; } };
  // A page in a language is the visitor's choice of it, and so is a language picked in the footer.
  if (LANG !== 'en') store(LANG_KEY, LANG);
  document.addEventListener('click', (e) => {
    const a = e.target.closest && e.target.closest('.langs a[data-lang]');
    if (a) store(LANG_KEY, a.dataset.lang);
  });
  // The English front page, for a first visit from a browser set to one of the languages, sends it
  // there; once a language is chosen (English too), never again.
  if (LANG === 'en' && location.pathname === '/' && !stored(LANG_KEY)) {
    const want = (navigator.languages || [navigator.language || '']).map((l) => String(l).slice(0, 2).toLowerCase())[0];
    if (LANGS.includes(want)) location.replace('/' + want + '/' + location.hash);
  }

  const el = (tag, cls, text) => {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.textContent = text;
    return e;
  };

  const KEY = 'mu-token';
  const token = {
    get() { try { return localStorage.getItem(KEY) || ''; } catch (e) { return ''; } },
    set(t) { try { t ? localStorage.setItem(KEY, t) : localStorage.removeItem(KEY); } catch (e) {} },
  };

  async function api(path, body) {
    const headers = { 'Content-Type': 'application/json' };
    const t = token.get();
    if (t) headers.Authorization = 'Bearer ' + t;
    const res = await fetch(path, { method: body ? 'POST' : 'GET', headers, body: body ? JSON.stringify(body) : undefined });
    const said = await res.json().catch(() => ({}));
    if (res.status === 401 && t) token.set('');
    return { status: res.status, said };
  }

  // The account a kept sign-in is, or null (a token gone stale is forgotten on the way).
  async function account() {
    if (!token.get()) return null;
    try {
      const { status, said } = await api('/api/account');
      return status === 200 ? said : null;
    } catch (e) {
      return null;
    }
  }

  function when(seconds) {
    const d = new Date(seconds * 1000), now = new Date();
    const locale = LANG === 'en' ? 'en-GB' : document.documentElement.lang;
    const time = d.toLocaleTimeString(locale, { hour: '2-digit', minute: '2-digit' });
    const days = (new Date(now.getFullYear(), now.getMonth(), now.getDate()) -
                  new Date(d.getFullYear(), d.getMonth(), d.getDate())) / 86400000;
    if (days === 0) return t('Today {time}', { time });
    if (days === 1) return t('Yesterday {time}', { time });
    return d.toLocaleDateString(locale, { day: 'numeric', month: 'short' }) + ' ' + time;
  }

  const fieldError = (input, out, msg) => { input.setAttribute('aria-invalid', msg ? 'true' : 'false'); out.textContent = msg || ''; };
  const hintError = (input, hint, msg, base) => {
    input.setAttribute('aria-invalid', msg ? 'true' : 'false');
    hint.textContent = msg || base;
    hint.style.color = msg ? 'var(--danger)' : '';
  };
  const alertOn = (box, msg) => { box.textContent = msg || ''; box.hidden = !msg; };
  const busy = (button, label) => {
    button.disabled = !!label;
    if (label) { button.dataset.label = button.textContent; button.textContent = label; }
    else if (button.dataset.label) button.textContent = button.dataset.label;
  };
  const OFFLINE = t('Can’t reach the server. Check your connection and try again.');
  const NAME = /^[A-Za-z0-9_]{3,16}$/;
  const EMAIL = /^[^@\s]+@[^@\s]+\.[^@\s]+$/;

  // The top bar's link: "Account" for a kept sign-in, "Sign in" otherwise; both go to /account.
  function bar(signedIn) {
    const link = $('bar-account');
    if (link) link.textContent = signedIn ? t('Account') : t('Sign in');
  }

  return { $, el, t, home, token, api, account, when, fieldError, hintError, alertOn, busy, bar, OFFLINE, NAME, EMAIL };
})();
