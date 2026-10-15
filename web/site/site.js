// What the front page and the account page share: the sign-in kept in the browser, the calls to
// server.py, and the small helpers both draw with. Everything a player wrote goes in as text.
window.MU = (() => {
  const $ = (id) => document.getElementById(id);
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
    const time = d.toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit' });
    const days = (new Date(now.getFullYear(), now.getMonth(), now.getDate()) -
                  new Date(d.getFullYear(), d.getMonth(), d.getDate())) / 86400000;
    if (days === 0) return 'Today ' + time;
    if (days === 1) return 'Yesterday ' + time;
    return d.toLocaleDateString('en-GB', { day: 'numeric', month: 'short' }) + ' ' + time;
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
  const OFFLINE = 'Can’t reach the server. Check your connection and try again.';
  const NAME = /^[A-Za-z0-9_]{3,16}$/;
  const EMAIL = /^[^@\s]+@[^@\s]+\.[^@\s]+$/;

  // The top bar's link: "Account" for a kept sign-in, "Sign in" otherwise; both go to /account.
  function bar(signedIn) {
    const link = $('bar-account');
    if (link) link.textContent = signedIn ? 'Account' : 'Sign in';
  }

  return { $, el, token, api, account, when, fieldError, hintError, alertOn, busy, bar, OFFLINE, NAME, EMAIL };
})();
