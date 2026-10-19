"""The site in other languages: the English pages are the source, each language a table of their
words, and a page in Portuguese is the English one with its words put in as it is served.

A page's words ("units") are found by reading it: an element that holds text and nothing bigger
than <a>, <b>, <span> and the like is one unit, its whole inside with its tags (so a sentence keeps
its link and its word order is the translator's); text beside a block or an <svg> is a unit of its
own; and the alt, title, aria-label and placeholder of an element, and the content of the
description and og: tags. Nothing under translate="no", <script> or <style> is a unit. What a
script says goes through MU.t('...') (site.js), and those strings are units too: the page is served
with window.MU_T, their translations.

A unit's key is its English source with each run of white space made one space. A translation of
a unit with tags must keep the very same tags; one without tags is text, put in escaped.

Standard library only (as server.py).
"""

import html
import json
import re
from html.parser import HTMLParser
from pathlib import Path

# The languages beside English, by the path they are served under: its name in itself, the
# <html lang>, and og:locale. Chosen 2026-10-19 for MU's private-server communities: Brazil,
# Spanish-speaking Latin America and Spain, Russia and its neighbours, Vietnam, Poland.
LANGS = {
    "pt": ("Português", "pt-BR", "pt_BR"),
    "es": ("Español", "es", "es_ES"),
    "ru": ("Русский", "ru", "ru_RU"),
    "vi": ("Tiếng Việt", "vi", "vi_VN"),
    "pl": ("Polski", "pl", "pl_PL"),
}
# The pages served in every language, by file, their path, and their name on the /translate page.
PAGES = {
    "index.html": ("/", "Front page"),
    "account.html": ("/account", "Account"),
    "privacy.html": ("/privacy", "Privacy"),
    "contact.html": ("/contact", "Contact"),
}
SITE = "https://mu-classic.com"
# Cinzel has no Cyrillic and no Vietnamese; those pages set its headings in Alegreya SC (site.css).
HEADING_FONT = ('<link href="https://fonts.googleapis.com/css2?family=Alegreya+SC:wght@700&display=swap" '
                'rel="stylesheet">')
NEEDS_HEADING_FONT = ("ru", "vi")

# What ends a unit: a child that is a block, or a picture or a field, makes its parent's text
# loose pieces of their own.
BLOCK = {"address", "article", "aside", "blockquote", "body", "details", "dialog", "div", "dl", "dd", "dt",
         "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "head",
         "header", "hr", "html", "li", "main", "nav", "ol", "p", "pre", "section", "summary", "table", "tbody",
         "td", "tfoot", "th", "thead", "tr", "ul", "svg", "script", "style", "img", "input", "select",
         "textarea", "button", "label", "iframe", "video", "audio", "canvas", "noscript", "title", "meta",
         "link"}
VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr"}
SKIP = {"script", "style", "svg", "code"}
ATTRS = ("alt", "title", "aria-label", "placeholder")
META = ("description", "og:title", "og:description", "og:image:alt")
LETTER = re.compile(r"[^\W\d_]")
TAG = re.compile(r"<[^>]+>")
T_CALL = re.compile(r"\bt\(\s*'((?:[^'\\\n]|\\.)*)'")
UPDATES = ("<!--updates-->", "<!--/updates-->")


def key(source: str) -> str:
    return re.sub(r"\s+", " ", source.strip())


def has_tags(source: str) -> bool:
    return "<" in source


def same_tags(source: str, text: str) -> bool:
    return sorted(TAG.findall(source)) == sorted(TAG.findall(text))


def check(source: str, text: str):
    """Why a translation cannot stand for its source, or None."""
    if has_tags(source):
        if not same_tags(source, text):
            return "keep the same tags as the English: " + " ".join(TAG.findall(source))
    elif "<" in text or ">" in text:
        return "no tags here"
    return None


class _Units(HTMLParser):
    """Each unit of a page: (start, end, kind, source), kind "text" or "attr", start and end where
    its source stands in the page, trimmed."""

    def __init__(self, page: str):
        super().__init__(convert_charrefs=False)
        self.page = page
        self.lines = [0]
        for line in page.splitlines(keepends=True):
            self.lines.append(self.lines[-1] + len(line))
        self.found = []
        # Each open element: [tag, end of its start tag, its direct text pieces, broken, candidates, skipped]
        self.stack = []

    def at(self) -> int:
        line, col = self.getpos()
        return self.lines[line - 1] + col

    def skipped(self) -> bool:
        return bool(self.stack) and self.stack[-1][5]

    def handle_starttag(self, tag, attrs):
        start = self.at()
        text = self.get_starttag_text()
        attrs = dict(attrs)
        skip = self.skipped() or tag in SKIP or attrs.get("translate") == "no"
        if not skip:
            self.attributes(start, text, tag, attrs)
        if self.stack and tag in BLOCK:
            self.stack[-1][3] = True
        if tag in VOID or text.endswith("/>"):
            return
        self.stack.append([tag, start + len(text), [], False, [], skip])

    def attributes(self, start, text, tag, attrs):
        names = ATTRS + (("content",) if tag == "meta" and (attrs.get("name") in META or attrs.get("property") in META)
                         else ())
        for found in re.finditer(r'\s([a-z-]+)="([^"]*)"', text):
            if found.group(1) in names and LETTER.search(found.group(2)):
                a, b = start + found.start(2), start + found.end(2)
                self.found.append((a, b, "attr", self.page[a:b]))

    def handle_data(self, data):
        self.text(len(data))

    def handle_entityref(self, name):
        self.text(len(name) + 2, letters=False)

    def handle_charref(self, name):
        self.text(len(name) + 3, letters=False)

    def text(self, length, letters=True):
        if not self.stack or self.skipped():
            return
        start = self.at()
        pieces = self.stack[-1][2]
        raw = self.page[start:start + length]
        if pieces and pieces[-1][1] == start:
            pieces[-1][1] = start + length
            pieces[-1][2] = pieces[-1][2] or (letters and bool(LETTER.search(raw)))
        else:
            pieces.append([start, start + length, letters and bool(LETTER.search(raw))])

    def handle_endtag(self, tag):
        if tag in VOID:
            return
        # A tag closed out of order closes what it skipped (as browsers do).
        while self.stack and self.stack[-1][0] != tag:
            self.shut(self.at())
        if self.stack:
            self.shut(self.at())

    def shut(self, end):
        tag, inner, pieces, broken, candidates, skip = self.stack.pop()
        parent = self.stack[-1] if self.stack else None
        out = []
        if not skip:
            worded = any(p[2] for p in pieces)
            if worded and not broken:
                out.append(self.trimmed(inner, end))
            else:
                # Text beside a block child: each piece of it a unit, and the child's own.
                out += [self.trimmed(a, b) for a, b, words in pieces if words]
                out += candidates
        if parent is not None:
            parent[4].extend(out)
        else:
            self.found.extend(out)

    def trimmed(self, a, b):
        piece = self.page[a:b]
        lead = len(piece) - len(piece.lstrip())
        trail = len(piece) - len(piece.rstrip())
        return (a + lead, b - trail, "text", self.page[a + lead:b - trail])


def units(page: str) -> list:
    """The page's units in order: (start, end, kind, source), and its script strings as (-1, -1, "js", source)."""
    parser = _Units(page)
    parser.feed(page)
    parser.close()
    while parser.stack:
        parser.shut(len(page))
    found = sorted(set(parser.found))
    for script in re.finditer(r"<script>(.*?)</script>", page, re.S):
        found += [(-1, -1, "js", js_string(m.group(1))) for m in T_CALL.finditer(script.group(1))]
    return found


def js_string(literal: str) -> str:
    return re.sub(r"\\(.)", r"\1", literal)


def script_units(script: str) -> list:
    return [js_string(m.group(1)) for m in T_CALL.finditer(script)]


def in_updates(page: str, start: int) -> bool:
    a, b = page.find(UPDATES[0]), page.find(UPDATES[1])
    return 0 <= a < start < b


def as_text(text: str) -> str:
    """A translation of a unit without tags, made safe to stand in the page."""
    return html.escape(html.unescape(text), quote=True)


# ---- the dates and counts of the Latest updates, in each language ---------------------------------

MONTHS = {
    "en": "January February March April May June July August September October November December",
    "pt": "janeiro fevereiro março abril maio junho julho agosto setembro outubro novembro dezembro",
    "es": "enero febrero marzo abril mayo junio julio agosto septiembre octubre noviembre diciembre",
    "ru": "января февраля марта апреля мая июня июля августа сентября октября ноября декабря",
    "pl": "stycznia lutego marca kwietnia maja czerwca lipca sierpnia września października listopada grudnia",
}


def day_label(lang: str, year: int, month: int, date: int) -> str:
    if lang == "vi":
        return f"{date} tháng {month}, {year}"
    name = MONTHS.get(lang, MONTHS["en"]).split()[month - 1]
    if lang in ("pt", "es"):
        return f"{date} de {name} de {year}"
    return f"{date} {name} {year}"


def _slavic(n: int, one: str, few: str, many: str) -> str:
    if n % 10 == 1 and n % 100 != 11:
        return one
    if 2 <= n % 10 <= 4 and not 12 <= n % 100 <= 14:
        return few
    return many


def count_label(lang: str, new: int, fixed: int) -> str:
    def words(n, kind):
        if lang == "pt":
            return f"{n} " + (("novidade", "novidades"), ("correção", "correções"))[kind][n != 1]
        if lang == "es":
            return f"{n} " + (("novedad", "novedades"), ("corrección", "correcciones"))[kind][n != 1]
        if lang == "ru":
            return f"{n} " + (_slavic(n, "новинка", "новинки", "новинок") if kind == 0
                              else _slavic(n, "исправление", "исправления", "исправлений"))
        if lang == "pl":
            # "1 nowość" but "2 nowości" and "5 nowości".
            return f"{n} " + (("nowość" if n == 1 else "nowości") if kind == 0
                              else ("poprawka" if n == 1 else _slavic(n, "poprawka", "poprawki", "poprawek")))
        if lang == "vi":
            return f"{n} " + ("mới", "bản sửa")[kind]
        return f"{n} new" if kind == 0 else f"{n} fix{'es' if n != 1 else ''}"
    return " · ".join(words(n, k) for k, n in ((0, new), (1, fixed)) if n)


# ---- serving ------------------------------------------------------------------------------------

def alternates(page_path: str) -> str:
    """The <link rel="alternate" hreflang> of a page, every language's."""
    out = [f'<link rel="alternate" hreflang="en" href="{SITE}{page_path}">',
           f'<link rel="alternate" hreflang="x-default" href="{SITE}{page_path}">']
    for code, (_, tag, _) in LANGS.items():
        out.append(f'<link rel="alternate" hreflang="{tag}" href="{SITE}/{code}{page_path}">')
    return "\n".join(out)


def switcher(page_path: str, lang: str) -> str:
    """The footer's languages, each a link to this page in it; the one shown marked."""
    out = []
    for code, name in [("en", "English")] + [(c, v[0]) for c, v in LANGS.items()]:
        href = page_path if code == "en" else f"/{code}{page_path}"
        here = ' aria-current="true"' if code == lang else ""
        out.append(f'<a href="{href}" hreflang="{code}" data-lang="{code}"{here}>{name}</a>')
    return '<nav class="langs" translate="no" aria-label="Language">' + "".join(out) + "</nav>"


def localise(page: str, file: str, lang: str, texts: dict, script_strings: list) -> str:
    """The page in `lang` ("en" fills in only the languages), its units from `texts` {source key:
    translation}; a unit with none stays English. `script_strings`: what site.js says through t()."""
    page_path = PAGES[file][0]
    if lang != "en":
        found = units(page)
        out, last, said = [], 0, {}
        for start, end, kind, source in found:
            text = texts.get(key(source))
            if kind == "js":
                if text and not check(source, text):
                    said[source] = html.unescape(text) if not has_tags(source) else text
                continue
            out.append(page[last:start])
            if text and not check(key(source), text):
                out.append(text if has_tags(source) else as_text(text))
            else:
                out.append(page[start:end])
            last = end
        out.append(page[last:])
        page = "".join(out)
        for source in script_strings:
            text = texts.get(key(source))
            if text and not check(source, text):
                said[source] = text
        name, tag, locale = LANGS[lang]
        page = page.replace('<html lang="en">', f'<html lang="{tag}">', 1)
        page = re.sub(r'(<link rel="canonical" href="|<meta property="og:url" content=")' + re.escape(SITE) + r"/",
                      r"\g<1>" + SITE + f"/{lang}/", page)
        page = page.replace('<meta property="og:type"', f'<meta property="og:locale" content="{locale}">\n<meta property="og:type"', 1)
        # Links to the pages served in every language stay in this one.
        paths = "|".join(re.escape(p.lstrip("/")) for p, _ in PAGES.values() if p != "/")
        page = re.sub(r'href="/(' + paths + r'|#[\w-]*|)"', lambda m: f'href="/{lang}/{m.group(1)}"', page)
        if lang in NEEDS_HEADING_FONT:
            page = page.replace('<link rel="stylesheet" href="/site.css">', HEADING_FONT + '\n<link rel="stylesheet" href="/site.css">', 1)
        blob = json.dumps(said, ensure_ascii=False).replace("</", "<\\/")
        page = page.replace('<script src="/site.js"></script>',
                            f'<script>window.MU_T = {blob};</script>\n<script src="/site.js"></script>', 1)
    page = page.replace("<!--alternates-->", alternates(page_path), 1)
    page = page.replace("<!--langs-->", switcher(page_path, lang), 1)
    return page


def lang_of(path: str):
    """("pt", "/privacy") for "/pt/privacy"; (None, path) for a path in no language."""
    found = re.match(r"^/([a-z]{2})(/.*|)$", path)
    if found and found.group(1) in LANGS:
        return found.group(1), found.group(2) or "/"
    return None, path


def seeds(folder: Path) -> dict:
    """web/i18n/<lang>.json, each {source: translation}: the first words of each language."""
    out = {}
    for code in LANGS:
        f = folder / f"{code}.json"
        if f.is_file():
            out[code] = {key(k): v for k, v in json.loads(f.read_text()).items() if isinstance(v, str) and v.strip()}
    return out
