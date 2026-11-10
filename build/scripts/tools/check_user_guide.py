#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Hold the user guide to the rules its offline copy and its pages depend on.

The installers bundle the built guide, and Help opens it from disk: a page that names a missing file, a folder, or a
script, stylesheet, font or image on the network works on a web server and breaks in the copy people read offline.
The strict MkDocs build checks the links between pages as Markdown; this checks what the built site references, and
what each page states about itself in its front matter against the guide's data and the application's menus.

Rules, each reported with its id:

  R2  Front matter. Every page opens with YAML front matter whose keys, types and values fit its `kind`, and whose
      stage ids are those of data/stages.yml (the schema is docs/user-guide/hooks/guide_model.py's).
  R3  Every page under the docs folder is in the nav, every nav entry names a page, and each page has exactly one H1.
  R4  Menu parity with the application, read from ProcessRegistryBuiltins.cpp (the registered actions) and
      AfterNightAppUiBuilder.h (labels and menus): every `menu` path a page states is a menu or an action of the
      application, and every registered action, and every other action of the Process menu, sits on a page's `menu`,
      as the path itself or under a submenu the page names.
  R5  Every image a page shows exists, spelled with the file's own case: its Markdown and HTML images, the images of
      its `figure` blocks and the picture its front matter names (the home page's `hero.image`). An image under
      assets/images that no page, README.md or the site's logo and favicon uses is a warning. A picture a `figure`
      block states `owed: true` for is not yet taken: it must not exist, it is counted in the summary and listed by
      --owed, and --no-owed, as a release runs it, refuses it.
  R6  Credits. A process page that states `derived: true` has a `## Credits` section that says something: the work
      its result is computed with, its authors and its licence are credited on the page itself.

The rules of a rewritten page hold a page to the guide's writing system: the templates under
docs/user-guide/templates/, the budgets of data/limits.yml and the vocabulary of data/vocabulary.yml, which
docs/dev/USER_GUIDE_STYLE.md explains. A page that states `rewritten: true` fails on them. A page that does not is
only counted against R7, R8, R9 and R11, as warnings, and is not held to R10, R12, R13 and R14: a page not yet
rewritten never fails a rule it cannot meet, and a rewritten one is protected from the day it lands.

  R7  Headings. A page has the sections of its kind's template, in the template's order, and no others; the parts
      the theme draws from them have their shape - a process page's steps an ordered list with bold leads and its
      controls in a `controls` block, a tutorial's steps `###` headings, a troubleshooting entry a **Cause.** and a
      **Fix.**. The glossary is `## <letter>` sections of `### <term>` entries. The home page and the catalog have
      nothing below their H1.
  R8  Limits. The page is within the budgets of data/limits.yml: the words of its summary, its opening and its
      body, its steps, bullets, callouts, troubleshooting entries, related links and credits, and the columns and
      rows of its tables.
  R9  Vocabulary. The page says none of the forbidden words of data/vocabulary.yml - what cites its proof, names
      code or dates the application - in its prose, its front matter, its `controls` facets and its captions, apart
      from what the allow-list states with a reason. An allowance its page does not use is a warning. An HTML
      comment is a note for the page's authors, and is refused with them.
  R10 Figures. Every picture is a `figure` block with a caption and an alternative text; a process or interface page
      opens with one, before its first section; a pin's number is a step's; and on an interface page every item of
      "What you see" has its pin. A process page of a nav group data/limits.yml lists under `before_after_groups`
      shows a before-and-after pair in its worked example (a warning).
  R11 Image files. A picture a page shows is a PNG, WebP or JPEG file within the size and weight of data/limits.yml,
      with a row in the media provenance page.
  R12 A rewritten process page states every key its "At a glance" is drawn from, the four texts of `glance` among
      them, and says whether it is `derived`.
  R13 Glossary. The first use on a page of a term the glossary defines is a link to the glossary (a warning). A term
      inside the words of a link to another page, a bold label or a quoted message is part of a name the page
      repeats, not a use of the term: the next use is the first. The glossary itself and the media provenance, a record of
      files, link no term.
  R14 Controls. The `controls` blocks of a rewritten process page say what its windows hold. The windows are the ones
      its `menu` reaches, and what they hold is their inventory under data/ui/, which a test of the application
      writes by opening every process window and fails on when a window changes. Every control of the inventory is
      on a block, or named in the page's `controls_ignore` with a reason, or shared by every window (the preview's
      histogram toolbar, the window's own buttons); every control a block lists is in the window, under the label
      the window gives it, with its `object` where the window has the label more than once; and its kind, its
      default, the numbers of its `widget` and its `options` are the window's, apart from what the inventory records
      as varying with the image and from a list the window opens empty. A control the window gives no caption - the
      inventory knows it by its object name - is named by the writer and found by its `object`. An entry of the
      kind `readout` - a count, a plot, a status line: what the window shows and takes no input - is no control and
      is not looked for in the inventory. A block
      whose `window` is the title of a tab or a group holds that tab's controls; one named for a window the
      inventory does not have - a dialog a button opens - is not held. Controls in another order than the window's
      are a warning.

  R15 Help pages. Help (F1) in a process window opens that process's page, which the application reads from the
      help table of ProcessRegistryBuiltins.cpp: a row per action, and one page for the non-interactive operations
      that have no row. Every path of the table is the path of a built page (`<folder>/<page>.html`) whose source the
      guide has, spelled with its own case; every row is of a registered action, once; every registered action has a
      row or is a non-interactive operation; and the page a row names is one whose `menu` reaches the action, so
      Help opens the page that describes the window it was asked from. A page that moves is moved in the table.

  R1  The built site (--site). Every relative reference in its HTML - `href`, `src`, `srcset`, `poster`, `data`,
      inline `style` and `<style>` - and every `url()` and `@import` in its stylesheets names a file inside the site,
      spelled with the file's own case; a folder does not count, because a browser opened from disk lists a folder
      instead of opening its index.html. A root-relative reference (`/x`) and a `file:` URL are refused. Nothing the
      site loads - a script, stylesheet, font, image, frame or preload - comes from http(s); a link a reader follows
      (`<a>`, and `<link>` relations such as `canonical`) may lead anywhere. The one exception is the site's
      404.html: a web server shows it at whatever address was missing, so MkDocs writes its references
      root-relative, and they must name files from the site's root.
  R16 Token parity (--tokens, run where the website's sources are at hand). The theme's tokens.css is a verbatim copy
      of the website's, so the guide and the site wear one skin: the two files are equal once their line endings are.

Usage, with the documentation toolchain's Python, which has PyYAML (docs/user-guide/.venv, made by the build helper):
    python build/scripts/tools/check_user_guide.py [--guide docs/user-guide] [--page <page> ...] [--pending]
                                                   [--site <site>] [--tokens <the website's assets/css/tokens.css>]

R2-R14 hold every page, or with --page only the pages named (relative to the docs folder or to the working
directory); the guide-wide parts - nav entries, menu coverage, the help table (R15) and unused images - run only over
every page. R1 runs
when --site names a built site, R16 when --tokens names the website's file. Each finding prints as
`path:line: Rnn message`, a warning as `path:line: Rnn warning: message`, then a count per rule and what was read.
The warnings of the pages not yet rewritten are counted and not listed, so that the errors can be read; --page lists
those of the pages it names, each with the words R8 counted against its budget, and --pending lists them all.
Exits 1 on any error, 0 otherwise, and 2 when the guide's own data cannot be read.

Tests: build/scripts/tools/check_user_guide_test.py
"""

from __future__ import annotations

import argparse
import html.parser
import os
import posixpath
import re
import sys
import urllib.parse
from dataclasses import dataclass

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
REGISTRY = 'src/AfterNightProcesses/app/ProcessRegistryBuiltins.cpp'
UI_BUILDER = 'src/AfterNightControls/app/AfterNightAppUiBuilder.h'
IMAGE_EXTENSIONS = ('.png', '.webp', '.jpg', '.jpeg', '.gif', '.svg')
CREDITS = 'Credits'

RULES = {
    'R1': 'built-site references',
    'R2': 'front matter',
    'R3': 'nav and H1',
    'R4': 'menu parity',
    'R5': 'images',
    'R6': 'credits of derived pages',
    'R7': 'headings',
    'R8': 'limits',
    'R9': 'vocabulary',
    'R10': 'figures',
    'R11': 'image files',
    'R12': 'process keys',
    'R13': 'glossary links',
    'R14': 'controls of the windows',
    'R15': 'help pages',
    'R16': 'token parity',
}
THEME_TOKENS = 'theme/assets/guide/css/tokens.css'

# --- The templates (R7) ----------------------------------------------------------------------------------------------
# The sections of a page of each kind, in order: (the titles a section may have, whether the page must have it). The
# theme finds the parts it draws by these titles (docs/user-guide/hooks/guide_html.py), and the templates under
# docs/user-guide/templates/ are held to them by this script's test.

ANY_SUBSECTIONS = 'any'
CREDITS_SECTION = ((CREDITS,), False)
SECTIONS = {
    'process': (
        (('Before you start',), True),
        (('Steps',), True),
        (('Controls',), True),
        (('How it works',), True),
        (('Starting points',), False),
        (('When it goes wrong',), True),
        (('Related',), True),
        CREDITS_SECTION,
    ),
    'interface': (
        (('What you see',), True),
        (('What you can do',), True),
        (('Controls',), False),
        (('When it goes wrong',), True),
        (('Related',), True),
        CREDITS_SECTION,
    ),
    'tutorial': (
        (('What you need',), True),
        (('Your camera', 'Your data'), False),
        (('Steps',), True),
        (('The result',), True),
        (('Next',), True),
        CREDITS_SECTION,
    ),
}
SECTIONS['workflow'] = SECTIONS['tutorial']
TEMPLATES = 'docs/user-guide/templates/'
TEMPLATE_FILES = {
    'process': 'PROCESS_PAGE_TEMPLATE.md',
    'interface': 'INTERFACE_PAGE_TEMPLATE.md',
    'tutorial': 'TUTORIAL_PAGE_TEMPLATE.md',
    'workflow': 'TUTORIAL_PAGE_TEMPLATE.md',
    'reference': 'REFERENCE_PAGE_TEMPLATE.md',
}
# The `###` headings a section may hold: a fixed list, in order, or any (one per window, tab, task or step). A
# section not named here has none.
SUBSECTIONS = {
    'process': {'Controls': ANY_SUBSECTIONS, 'How it works': ('Model', 'Boundaries', 'Worked example')},
    'interface': {'What you can do': ANY_SUBSECTIONS, 'Controls': ANY_SUBSECTIONS},
    'tutorial': {'Steps': ANY_SUBSECTIONS},
    'workflow': {'Steps': ANY_SUBSECTIONS},
}
DATA_DRAWN_KINDS = ('home', 'catalog')
GLOSSARY_SECTION = re.compile(r'^(?:[A-Z]|0-9)$')
# What R13 does not take for a use of a term: a bold label and a quoted message of the application.
GLOSSARY_NAMES = re.compile(r'\*\*.+?\*\*|"[^"]*"')
TASK_ITEM = re.compile(r'^\[[ xX]\][ \t]')
WARNING_CALLOUTS = ('warning', 'danger')
WRONG_CALLOUT = 'wrong'
# What a rewritten process page states beside the keys every process page needs (R12).
PROCESS_KEYS = ('data_in', 'data_out', 'destructive', 'preview', 'sequences', 'masks', 'external', 'glance',
                'derived')
# The sentences a block puts on the page, which R9 reads; a control's name, default and options are the window's own
# words.
BLOCK_PROSE = {
    'controls': ('range', 'effect', 'enabled', 'tradeoff', 'scope', 'persistence', 'note'),
    'figure': ('alt', 'caption', 'label'),
}


def check_tokens(guide, website_tokens, display):
    """R16: the theme's tokens.css against the website's, line endings aside."""
    ours = os.path.join(guide, THEME_TOKENS)
    try:
        theirs_text = _read(website_tokens)
    except OSError as error:
        return [Finding(display(website_tokens), 1, 'R16', 'the website\'s tokens cannot be read: %s' % error)]
    try:
        ours_text = _read(ours)
    except OSError as error:
        return [Finding(display(ours), 1, 'R16', 'the theme\'s tokens cannot be read: %s' % error)]
    if ours_text == theirs_text:
        return []
    ours_lines, theirs_lines = ours_text.split('\n'), theirs_text.split('\n')
    line = next((number for number, (a, b) in enumerate(zip(ours_lines, theirs_lines), start=1) if a != b),
                min(len(ours_lines), len(theirs_lines)) + 1)
    return [Finding(display(ours), line, 'R16', 'differs from %s from this line; copy the website\'s file again' %
                    display(website_tokens))]

# <link> relations whose target the browser loads with the page. Every other relation names a document a reader may
# follow, so it may lead off the site.
LOADED_LINK_RELATIONS = frozenset({
    'stylesheet', 'icon', 'apple-touch-icon', 'apple-touch-icon-precomposed', 'mask-icon', 'manifest', 'preload',
    'prefetch', 'modulepreload', 'preconnect', 'dns-prefetch', 'prerender',
})
# Attributes whose URL any element loads with the page.
LOADED_ATTRIBUTES = ('src', 'poster', 'data')
SRCSET_ATTRIBUTES = ('srcset', 'imagesrcset')
FOLLOWED_TAGS = frozenset({'a', 'area'})
# Pages a web server shows at any address, whose references MkDocs writes root-relative.
SERVED_AT_ANY_ADDRESS = frozenset({'404.html'})

SCHEME = re.compile(r'^([A-Za-z][A-Za-z0-9+.-]*):')
NETWORK_SCHEMES = frozenset({'http', 'https', 'ftp', 'ws', 'wss'})
INERT_SCHEMES = frozenset({'data', 'blob', 'mailto', 'tel', 'javascript', 'about', 'sms'})
CSS_COMMENT = re.compile(r'/\*.*?\*/', re.S)
CSS_URL = re.compile(r'url\(\s*(?:"([^"]*)"|\'([^\']*)\'|([^)\s"\']*))\s*\)', re.I)
CSS_IMPORT = re.compile(r'@import\s+(?:"([^"]*)"|\'([^\']*)\')', re.I)
REFRESH_URL = re.compile(r'^\s*\d+\s*[;,]\s*url\s*=\s*[\'"]?([^\'"]+)', re.I)


@dataclass(frozen=True)
class Finding:
    path: str
    line: int
    rule: str
    message: str
    severity: str = 'error'
    # A warning about a page not yet rewritten: counted, and listed only when asked for.
    pending: bool = False

    def __str__(self):
        return '%s:%d: %s %s%s' % (self.path, self.line, self.rule, 'warning: ' if self.severity == 'warning' else '',
                                   self.message)


class _ExactCase:
    """Whether a site-relative path names a file, with every component spelled as the file system spells it.

    Windows and macOS open `Image.png` for `image.png`; Linux, and a web server on it, do not."""

    def __init__(self, root):
        self.root = root
        self.listings = {}

    def _listing(self, directory):
        if directory not in self.listings:
            try:
                self.listings[directory] = set(os.listdir(os.path.join(self.root, directory)))
            except OSError:
                self.listings[directory] = set()
        return self.listings[directory]

    def kind(self, relative):
        """'file', 'folder', 'case' (exists under another spelling) or None."""
        directory = ''
        parts = [part for part in relative.split('/') if part]
        for index, part in enumerate(parts):
            listing = self._listing(directory)
            if part not in listing:
                if any(name.lower() == part.lower() for name in listing):
                    return 'case'
                return None
            directory = posixpath.join(directory, part) if directory else part
            if index < len(parts) - 1 and not os.path.isdir(os.path.join(self.root, directory)):
                return None
        return 'folder' if os.path.isdir(os.path.join(self.root, directory)) else 'file'


def _line_of(text, offset):
    return text.count('\n', 0, offset) + 1


def _css_references(text):
    """(offset, url) for every url() and @import in a stylesheet, comments skipped."""
    text = CSS_COMMENT.sub(lambda match: re.sub(r'[^\n]', ' ', match.group(0)), text)
    for match in CSS_URL.finditer(text):
        yield match.start(), next(group for group in match.groups() if group is not None)
    for match in CSS_IMPORT.finditer(text):
        yield match.start(), next(group for group in match.groups() if group is not None)


def _srcset_urls(value):
    for candidate in value.split(','):
        candidate = candidate.strip()
        if candidate:
            yield candidate.split()[0]


class _Collector(html.parser.HTMLParser):
    """(line, what, url, loaded) for every reference of one HTML document."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.references = []
        self._style_line = None
        self._style_text = []

    def _add(self, what, url, loaded, line=None):
        self.references.append((line or self.getpos()[0], what, url, loaded))

    def handle_starttag(self, tag, attrs):
        values = {name: (value or '') for name, value in attrs}
        for name in LOADED_ATTRIBUTES:
            if name in values:
                self._add('<%s %s>' % (tag, name), values[name], True)
        for name in SRCSET_ATTRIBUTES:
            if name in values:
                for url in _srcset_urls(values[name]):
                    self._add('<%s %s>' % (tag, name), url, True)
        for name in ('href', 'xlink:href'):
            if name not in values:
                continue
            if tag in FOLLOWED_TAGS:
                loaded = False
            elif tag == 'link':
                relations = set(values.get('rel', '').lower().split())
                loaded = bool(relations & LOADED_LINK_RELATIONS)
            else:
                loaded = True
            self._add('<%s %s>' % (tag, name), values[name], loaded)
        if 'style' in values:
            for _, url in _css_references(values['style']):
                self._add('<%s style>' % tag, url, True)
        if tag == 'meta' and values.get('http-equiv', '').lower() == 'refresh':
            match = REFRESH_URL.match(values.get('content', ''))
            if match:
                self._add('<meta refresh>', match.group(1).strip(), False)
        if tag == 'style':
            self._style_line = self.getpos()[0]
            self._style_text = []

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag == 'style':
            self._style_line = None

    def handle_data(self, data):
        if self._style_line is not None:
            self._style_text.append(data)

    def handle_endtag(self, tag):
        if tag == 'style' and self._style_line is not None:
            text = ''.join(self._style_text)
            for offset, url in _css_references(text):
                self._add('<style>', url, True, self._style_line + _line_of(text, offset) - 1)
            self._style_line = None


class _SiteCheck:
    def __init__(self, site, display):
        self.site = site
        self.display = display
        self.exact = _ExactCase(site)
        self.findings = []

    def _report(self, relative, line, message):
        self.findings.append(Finding(posixpath.join(self.display, relative), line, 'R1', message))

    def reference(self, relative, line, what, url, loaded):
        url = url.strip()
        if not url or url.startswith('#'):
            return
        scheme = SCHEME.match(url)
        if url.startswith('//') or (scheme and scheme.group(1).lower() in NETWORK_SCHEMES):
            if loaded:
                self._report(relative, line, '%s loads %s from the network' % (what, url))
            return
        if scheme:
            name = scheme.group(1).lower()
            if name == 'file':
                self._report(relative, line, '%s names %s, a file: URL that leads off the site' % (what, url))
            elif name not in INERT_SCHEMES and len(name) == 1:
                self._report(relative, line, '%s names %s, an absolute path that leads off the site' % (what, url))
            return
        path = urllib.parse.unquote(url.split('#', 1)[0].split('?', 1)[0])
        if not path:
            return
        if path.startswith('/') and relative in SERVED_AT_ANY_ADDRESS:
            resolved = posixpath.normpath(path.lstrip('/')) if path.strip('/') else 'index.html'
        elif path.startswith('/') or path.startswith('\\'):
            self._report(relative, line, '%s names %s, which is root-relative and breaks when the site is opened from '
                         'disk' % (what, url))
            return
        else:
            resolved = posixpath.normpath(posixpath.join(posixpath.dirname(relative), path))
        if resolved == '..' or resolved.startswith('../'):
            self._report(relative, line, '%s names %s, which leads outside the site' % (what, url))
            return
        kind = self.exact.kind(resolved)
        if kind == 'folder':
            self._report(relative, line, '%s names %s, a folder: a browser opened from disk lists it instead of '
                         'opening its index.html' % (what, url))
        elif kind == 'case':
            self._report(relative, line, '%s names %s, which exists only under another spelling of its case' %
                         (what, url))
        elif kind is None:
            self._report(relative, line, '%s names %s, which is not in the site' % (what, url))

    def html(self, relative, text):
        collector = _Collector()
        collector.feed(text)
        collector.close()
        for line, what, url, loaded in collector.references:
            self.reference(relative, line, what, url, loaded)
        return len(collector.references)

    def css(self, relative, text):
        count = 0
        for offset, url in _css_references(text):
            count += 1
            self.reference(relative, _line_of(text, offset), 'url()' if not text[offset] == '@' else '@import',
                           url, True)
        return count


def _read(path):
    with open(path, encoding='utf-8', errors='replace', newline='') as handle:
        return handle.read().replace('\r\n', '\n')


def check_site(site, display=None):
    """R1 over a built site: (findings, HTML documents read, stylesheets read, references read)."""
    site = os.path.abspath(site)
    check = _SiteCheck(site, display if display is not None else site.replace('\\', '/'))
    documents = stylesheets = references = 0
    for directory, folders, files in os.walk(site):
        folders.sort()
        for name in sorted(files):
            full = os.path.join(directory, name)
            relative = os.path.relpath(full, site).replace('\\', '/')
            if name.endswith('.html'):
                documents += 1
                references += check.html(relative, _read(full))
            elif name.endswith('.css'):
                stylesheets += 1
                references += check.css(relative, _read(full))
    return check.findings, documents, stylesheets, references


_ACTION_TEXT = re.compile(
    r'(?s)(action[A-Za-z0-9_]+)->setText\(.*?QCoreApplication::translate\("AfterNightAppClass",\s*"([^"]+)".*?\)\s*\);')
_MENU_TITLE = re.compile(
    r'(?s)(menu[A-Za-z0-9_]+)->setTitle\(.*?QCoreApplication::translate\("AfterNightAppClass",\s*"([^"]+)".*?\)\s*\);')
_ADD_ACTION = re.compile(r'(?m)^\s*(menu[A-Za-z0-9_]+)->addAction\((action[A-Za-z0-9_]+)\);')
_ADD_MENU = re.compile(r'(?m)^\s*(menu[A-Za-z0-9_]+)->addAction\((menu[A-Za-z0-9_]+)->menuAction\(\)\);')
_REGISTRATION = re.compile(
    r'(?ms)^\s+AN_REGISTER_(PROCESS_(?:SINGLE|TOPLEVEL|NON_INTERACTIVE)|CHANNEL|IMAGE_OPERATION)\((.*?)\);')
_ACTION_NAME = re.compile(r'\b(action[A-Za-z0-9_]+)\b')
_HELP_TABLE = re.compile(r'(?s)\bkHelpPages\[\]\s*=\s*\{(.*?)\n\};')
_HELP_ROW = re.compile(r'\{\s*"(action[A-Za-z0-9_]+)"\s*,\s*"([^"]*)"\s*\}')
_HELP_DEFAULT = re.compile(r'\bkNonInteractiveHelpPage\s*=\s*"([^"]*)"')
_BUILT_PAGE = re.compile(r'(?:[A-Za-z0-9_-]+/)*[A-Za-z0-9_-]+\.html')
PROCESS_MENU = 'menuProcess'
NON_INTERACTIVE_CATEGORY = 'kCategoryNonInteractive'


class ProductMenus:
    """The application's menus as its UI builder declares them, and the actions its process registry registers.

    A menu path is the titles from the menu bar down, then the action's label, joined by " > ", the `&` of a mnemonic
    dropped: `Process > Intensity Transformations > Histogram Transformation`."""

    def __init__(self, repo):
        ui_text = _read(os.path.join(repo, UI_BUILDER))
        registry_text = _read(os.path.join(repo, REGISTRY))
        self.labels = {match.group(1): match.group(2).replace('&', '') for match in _ACTION_TEXT.finditer(ui_text)}
        self.titles = {match.group(1): match.group(2).replace('&', '') for match in _MENU_TITLE.finditer(ui_text)}
        self.placements = {}
        for match in _ADD_ACTION.finditer(ui_text):
            self.placements.setdefault(match.group(2), []).append((match.group(1), _line_of(ui_text, match.start(1))))
        # A menu added to a titled menu is its submenu; one added to the menu bar, which has no title, is a top menu.
        self.parents = {}
        for match in _ADD_MENU.finditer(ui_text):
            if match.group(1) in self.titles:
                self.parents.setdefault(match.group(2), []).append(match.group(1))
        self.registered = []
        # The actions registered in the non-interactive category: the channel and image operations, and a process
        # registered under it by name.
        self.non_interactive = set()
        for match in _REGISTRATION.finditer(registry_text):
            action = _ACTION_NAME.search(match.group(2))
            if action:
                self.registered.append((action.group(1), _line_of(registry_text, match.start(2))))
                if (not match.group(1).startswith('PROCESS_') or
                        match.group(2).split(',')[0].strip() == NON_INTERACTIVE_CATEGORY):
                    self.non_interactive.add(action.group(1))
        # The help table: (action, page, line) for each row, and (page, line) of the non-interactive operations' page.
        self.help_rows = None
        table = _HELP_TABLE.search(registry_text)
        if table:
            self.help_rows = [(row.group(1), row.group(2), _line_of(registry_text, table.start(1) + row.start()))
                              for row in _HELP_ROW.finditer(table.group(1))]
        default = _HELP_DEFAULT.search(registry_text)
        self.non_interactive_help = (default.group(1), _line_of(registry_text, default.start(1))) if default else None

    def _menu_trails(self, menu, seen=()):
        """Every list of menu ids from the menu bar down to a menu."""
        parents = [parent for parent in self.parents.get(menu, []) if parent not in seen]
        if not parents:
            return [[menu]]
        return [trail + [menu] for parent in parents for trail in self._menu_trails(parent, seen + (menu,))]

    def placements_of(self, action):
        """(menu path, the ids of the menus above it, UI builder line) for every place an action sits."""
        found = []
        for menu, line in self.placements.get(action, []):
            for trail in self._menu_trails(menu):
                titles = [self.titles.get(item, item) for item in trail]
                found.append((' > '.join(titles + [self.labels.get(action, action)]), trail, line))
        return found

    def existing_paths(self):
        """Every menu path an application menu or action has: each action's path and every menu above it."""
        paths = set()
        for action in self.placements:
            for path, _, _ in self.placements_of(action):
                parts = path.split(' > ')
                for end in range(1, len(parts) + 1):
                    paths.add(' > '.join(parts[:end]))
        return paths

    def process_menu_actions(self):
        """(action, UI builder line) for every action the Process menu holds, at any depth."""
        found = {}
        for action in self.placements:
            for _, trail, line in self.placements_of(action):
                if trail[0] == PROCESS_MENU:
                    found.setdefault(action, line)
        return sorted(found.items())


def _covered(path, page_paths):
    return any(path == menu or path.startswith(menu + ' > ') for menu in page_paths)


def windows_of(product, inventory, menus, every_pages_menus):
    """The inventories of the windows a page whose `menu` paths are `menus` describes, in the order the application
    registers them: the windows those paths reach that no page's paths name more exactly."""
    found = []
    for action, _ in product.registered:
        if action not in inventory:
            continue
        places = [place for place, _, _ in product.placements_of(action)]
        own = _closeness(places, menus)
        if own and own == max(_closeness(places, other) for other in every_pages_menus):
            found.append(inventory[action])
    return found


def guide_menus(model, docs, pages):
    """The `menu` paths every page of `pages` states, by page."""
    menus = {}
    for relative in pages:
        front = model.split_front_matter(model.read_text(os.path.join(docs, relative)))
        if front.meta:
            menus[relative] = model.menu_paths(front.meta)
    return menus


def _closeness(places, menus):
    """How exactly a page's `menu` paths name an action that sits at `places`: the length of the longest path that
    covers one of them, and 0 when none does."""
    return max((len(menu.split(' > ')) for menu in menus if _covered_by(places, menu)), default=0)


def _covered_by(places, menu):
    return any(_covered(place, (menu,)) for place in places)


def _article(kind):
    return ('an %s' if kind[:1] in 'aeiou' else 'a %s') % kind


def _count(count, singular, plural=None):
    return '%d %s' % (count, singular if count == 1 else plural or singular + 's')


def _template_order(kind):
    """The sections of a kind's template, as a message says them."""
    return 'the sections of %s page are, in order: %s' % (_article(kind), ', '.join(
        ' or '.join(titles) + ('' if required else ' (optional)') for titles, required in SECTIONS[kind]))


def _meta_prose(meta):
    """(key, text) for every sentence a page's front matter puts on the page."""
    def texts(value):
        if isinstance(value, str):
            yield value
        elif isinstance(value, list):
            for item in value:
                yield from texts(item)
        elif isinstance(value, dict):
            for key, item in value.items():
                if key not in ('page', 'image', 'actions'):
                    yield from texts(item)

    for key in ('summary', 'external', 'glance', 'needs', 'notice', 'reach', 'hero', 'paths'):
        for text in texts(meta.get(key)):
            yield key, text


class _Writing:
    """One page held to the rules of a rewritten page: R7-R10 and R12-R14."""

    def __init__(self, check, relative, path, front):
        model = check.model
        self.check = check
        self.model = model
        self.limits = check.limits
        self.relative = relative
        self.path = path
        self.front = front
        self.meta = front.meta
        self.kind = front.meta['kind']
        self.rewritten = front.meta.get('rewritten') is True
        self.lines = model.prose_lines(front.body, front.body_line)
        self.headings = model.headings(self.lines)
        self.sections = [heading for heading in self.headings if heading.level == 2]
        self.fences = model.fence_blocks(front.body, front.body_line)
        title = next((heading for heading in self.headings if heading.level == 1), None)
        self.top = title.line if title else front.body_line

    def report(self, rule, line, message):
        """A finding that fails a rewritten page and only counts against a page not yet rewritten."""
        if self.rewritten:
            self.check.report(rule, self.path, line, message)
        else:
            self.check.report(rule, self.path, line, message, 'warning', pending=True)

    def run(self):
        self.check_headings()
        self.check_limits()
        self.check_vocabulary()
        if self.rewritten:
            self.check_figures()
            self.check_process_keys()
            self.check_glossary_links()
            self.check_controls()

    # --- Reading the page ---

    def section(self, *titles):
        return next((heading for heading in self.sections if heading.title in titles), None)

    def under(self, heading):
        """The lines of a heading's section, its subsections included."""
        return self.model.section_lines(self.lines, self.headings, heading)

    def own(self, heading):
        """The lines under a heading, before the next heading of any level."""
        end = next((other.line for other in self.headings if other.line > heading.line), None)
        return [(number, text) for number, text in self.lines
                if number > heading.line and (end is None or number < end)]

    def subsections(self, heading, title=None):
        lines = {number for number, _ in self.under(heading)}
        return [other for other in self.headings if other.level == heading.level + 1 and other.line in lines
                and (title is None or other.title == title)]

    def blocks_under(self, heading, language):
        """(line, source) of the fenced blocks of a language in a heading's section."""
        lines = {number for number, _ in self.under(heading)}
        return [(line, source) for name, line, source in self.fences if name == language and line in lines]

    def steps(self):
        """The number of steps a pin may name, or None on a page whose kind has no steps."""
        if self.kind == 'process':
            heading = self.section('Steps')
            return len(self.model.ordered_items(self.own(heading))) if heading else 0
        if self.kind == 'interface':
            heading = self.section('What you see')
            return len(self.model.ordered_items(self.own(heading))) if heading else 0
        if self.kind in ('tutorial', 'workflow'):
            heading = self.section('Steps')
            return len(self.subsections(heading)) if heading else 0
        return None

    # --- R7: headings ---

    def check_headings(self):
        if self.kind in DATA_DRAWN_KINDS:
            self._nothing_below_the_title()
        elif self.relative == self.limits.glossary_page:
            self._glossary_sections()
        elif self.kind in SECTIONS:
            self._template_sections()
            self._component_shapes()

    def _nothing_below_the_title(self):
        visible = re.sub(r'(?s)<!--.*?-->', lambda match: re.sub(r'[^\n]', ' ', match.group(0)), self.front.body)
        for number, text in enumerate(visible.split('\n'), start=self.front.body_line):
            if number > self.top and text.strip():
                self.report('R7', number, 'the %s page is drawn from its front matter; it has nothing below its H1' %
                            ('home' if self.kind == 'home' else 'catalog'))
                return

    def _template_sections(self):
        kind = self.kind
        template = SECTIONS[kind]
        order = _template_order(kind)
        seen = {}
        last = None    # (place in the template, title) of the section furthest down the template so far
        parent = None  # the section a `###` belongs to, when the template has it
        met = []       # the subsections of `parent` so far
        for heading in self.headings:
            if heading.level == 2:
                parent, met = None, []
                place = next((index for index, (titles, _) in enumerate(template) if heading.title in titles), None)
                if place is None:
                    self.report('R7', heading.line, '`## %s` is no section of %s page (%s)' %
                                (heading.title, _article(kind), TEMPLATES + TEMPLATE_FILES[kind]))
                elif place in seen:
                    self.report('R7', heading.line, '`## %s` repeats `## %s` of line %d; a page has the section once'
                                % (heading.title, seen[place].title, seen[place].line))
                else:
                    seen[place] = heading
                    parent = heading
                    if last is not None and place < last[0]:
                        self.report('R7', heading.line, '`## %s` comes after `## %s`; %s' %
                                    (heading.title, last[1], order))
                    else:
                        last = (place, heading.title)
            elif heading.level == 3:
                if parent is None:
                    if not any(other.line < heading.line for other in self.sections):
                        self.report('R7', heading.line, '`### %s` comes before the first section; the opening of a '
                                    'page has no heading' % heading.title)
                    continue  # under a section the template does not have, which is reported
                allowed = SUBSECTIONS[kind].get(parent.title)
                if allowed == ANY_SUBSECTIONS:
                    continue
                if not allowed:
                    self.report('R7', heading.line, '`### %s` is under `## %s`, which has no subsections' %
                                (heading.title, parent.title))
                elif heading.title not in allowed:
                    self.report('R7', heading.line, '`### %s` is no subsection of `## %s`; they are %s, in that order'
                                % (heading.title, parent.title, ', '.join(allowed)))
                elif heading.title in met:
                    self.report('R7', heading.line, '`### %s` comes twice under `## %s`' %
                                (heading.title, parent.title))
                else:
                    if met and allowed.index(heading.title) < allowed.index(met[-1]):
                        self.report('R7', heading.line, '`### %s` comes after `### %s`; the subsections of `## %s` '
                                    'are %s, in that order' % (heading.title, met[-1], parent.title,
                                                               ', '.join(allowed)))
                    met.append(heading.title)
            elif heading.level > 3:
                self.report('R7', heading.line, '"%s" is a heading of level %d; a page has its template\'s sections '
                            'and subsections, and one that needs more splits off a reference page' %
                            (heading.title, heading.level))
        for place, (titles, required) in enumerate(template):
            if required and place not in seen:
                self.report('R7', self.top, 'the page has no `## %s`, which %s page has (%s)' %
                            (titles[0], _article(kind), TEMPLATES + TEMPLATE_FILES[kind]))

    def _component_shapes(self):
        """The parts the theme draws from a section have the shape it draws."""
        model = self.model
        kind = self.kind
        steps = self.section('Steps')
        if steps and kind == 'process':
            items = model.ordered_items(self.own(steps))
            if not items:
                self.report('R7', steps.line, '`## Steps` has no ordered list; the steps of a process page are its '
                            'items, each a bold lead and a sentence or two')
            for position, (line, text) in enumerate(items, start=1):
                if not text.startswith('**'):
                    self.report('R7', line, 'step %d opens without a bold lead, such as `**Open the window.**`' %
                                position)
        if steps and kind in ('tutorial', 'workflow') and not self.subsections(steps):
            self.report('R7', steps.line, '`## Steps` has no step; each step of %s is a `###` heading' %
                        _article(kind))
        controls = self.section('Controls')
        if controls and kind == 'process' and not self.blocks_under(controls, 'controls'):
            self.report('R7', controls.line, '`## Controls` has no `controls` block; the controls of a window are '
                        'written in one')
        shown = self.section('What you see')
        if shown and not model.ordered_items(self.own(shown)):
            self.report('R7', shown.line, '`## What you see` has no numbered list; its items are the pins of the '
                        'page\'s figure')
        need = self.section('What you need')
        if need and not any(TASK_ITEM.match(text) for _, text in model.bullets(self.own(need))):
            self.report('R7', need.line, '`## What you need` has no task list (`- [ ] ...`), which the page draws as '
                        'a checklist')
        trouble = self.section('When it goes wrong')
        if trouble:
            for line, term, texts in model.definitions(self.under(trouble)):
                for part in ('Cause', 'Fix'):
                    if not any(text.startswith('**%s.**' % part) for text in texts):
                        self.report('R7', line, 'the entry "%s" has no `**%s.**`; a troubleshooting entry is the '
                                    'symptom, then `:   **Cause.** ...` and `:   **Fix.** ...`' % (term, part))

    def _glossary_sections(self):
        letter = None
        last = None
        for heading in self.headings:
            if heading.level == 2:
                letter = None
                if not GLOSSARY_SECTION.match(heading.title):
                    self.report('R7', heading.line, '`## %s` is no section of the glossary; its sections are single '
                                'letters, `## A` to `## Z`, and `## 0-9`' % heading.title)
                    continue
                if last is not None and heading.title <= last:
                    self.report('R7', heading.line, '`## %s` comes after `## %s`; the glossary\'s sections are in '
                                'alphabetical order, each once' % (heading.title, last))
                else:
                    last = heading.title
                letter = heading.title
            elif heading.level == 3:
                if letter is None:
                    if not any(other.line < heading.line for other in self.sections):
                        self.report('R7', heading.line, '`### %s` is under no letter; a term sits under the section '
                                    'of its first letter' % heading.title)
                    continue
                first = next((character for character in heading.title if character.isalnum()), '')
                if not (first.isdigit() if letter == '0-9' else first.upper() == letter):
                    self.report('R7', heading.line, '`### %s` is under `## %s`; a term sits under the section of its '
                                'first letter' % (heading.title, letter))
            elif heading.level > 3:
                self.report('R7', heading.line, '"%s" is a heading of level %d; the glossary has letters and terms' %
                            (heading.title, heading.level))

    # --- R8: limits ---

    def _limit(self, name, count, line, said):
        limit = self.limits.of(self.kind, name, self.relative)
        if limit is not None and count > limit:
            large = self.relative in self.limits.large_pages and name in self.limits.large.get(self.kind, {})
            self.report('R8', line, '%s, over the limit of %d for %s page' %
                        (said, limit, 'a large %s' % self.kind if large else _article(self.kind)))

    def check_limits(self):
        model, limits, kind = self.model, self.limits, self.kind
        summary = self.meta.get('summary')
        if isinstance(summary, str) and model.count_words(summary) > limits.summary_words:
            self.report('R8', self.front.key_lines.get('summary', 1), '`summary` has %d words, over the limit of %d' %
                        (model.count_words(summary), limits.summary_words))
        for line, columns, _ in model.tables(self.lines):
            if columns > limits.table_columns:
                self.report('R8', line, 'the table has %d columns, over the limit of %d; write it as a definition '
                            'list, or as a table of what a reader compares' % (columns, limits.table_columns))
        if self.relative == limits.glossary_page:
            for letter in self.sections:
                for entry in self.subsections(letter) if GLOSSARY_SECTION.match(letter.title) else []:
                    words = model.count_words('\n'.join(text for _, text in self.under(entry)))
                    if words > limits.glossary_entry_words:
                        self.report('R8', entry.line, 'the entry "%s" has %d words, over the limit of %d for a '
                                    'glossary entry' % (entry.title, words, limits.glossary_entry_words))
        if kind not in limits.kinds:
            return
        credits = self.section(CREDITS)
        left_out = {number for number, _ in self.under(credits)} | {credits.line} if credits else set()
        words = model.count_words('\n'.join(text for number, text in self.lines if number not in left_out))
        self._limit('words', words, self.top, 'the page has %d words' % words)
        self.check.word_counts[self.relative] = (words, self.limits.of(kind, 'words', self.relative))
        first = self.sections[0].line if self.sections else None
        opening = model.count_words('\n'.join(text for number, text in self.lines
                                              if number > self.top and (first is None or number < first)))
        self._limit('opening_words', opening, self.top, 'the opening has %d words' % opening)
        for name, title, read, scope, singular, plural in (
                ('before_you_start', 'Before you start', model.bullets, self.own, 'bullet', None),
                ('how_it_works', 'How it works', model.ordered_items, self.own, 'numbered statement', None),
                ('troubleshooting', 'When it goes wrong', model.definitions, self.under, 'entry', 'entries'),
                ('related', 'Related', model.bullets, self.under, 'link', None),
                ('credits', CREDITS, model.bullets, self.under, 'entry', 'entries')):
            heading = self.section(title)
            if heading:
                found = read(scope(heading))
                self._limit(name, len(found), heading.line, '`## %s` has %s' %
                            (title, _count(len(found), singular, plural)))
        steps = self.section('Steps')
        if steps and kind == 'process':
            found = model.ordered_items(self.own(steps))
            self._limit('steps', len(found), steps.line, '`## Steps` has %s' % _count(len(found), 'step'))
        elif steps:
            found = self.subsections(steps)
            self._limit('steps', len(found), steps.line, '`## Steps` has %s' % _count(len(found), 'step'))
            for step in found:
                wrong = [line for line, name in model.callouts(self.under(step)) if name == WRONG_CALLOUT]
                self._limit('wrong_per_step', len(wrong), step.line, 'the step "%s" has %s' %
                            (step.title, _count(len(wrong), '`wrong` box', '`wrong` boxes')))
        how = self.section('How it works')
        for boundaries in self.subsections(how, 'Boundaries') if how else []:
            found = model.bullets(self.under(boundaries))
            self._limit('boundaries', len(found), boundaries.line, '`### Boundaries` has %s' %
                        _count(len(found), 'bullet'))
        starting = self.section('Starting points')
        for line, columns, rows in model.tables(self.under(starting)) if starting else []:
            self._limit('starting_points_rows', rows, line, 'the table of `## Starting points` has %s' %
                        _count(rows, 'row'))
            self._limit('starting_points_columns', columns, line, 'the table of `## Starting points` has %s' %
                        _count(columns, 'column'))
        warnings = [line for line, name in model.callouts(self.lines) if name in WARNING_CALLOUTS]
        self._limit('warnings', len(warnings), warnings[-1] if warnings else self.top,
                    'the page has %d warning and danger callouts' % len(warnings))

    # --- R9: vocabulary ---

    def check_vocabulary(self):
        model = self.model
        said = [(number, model.without_addresses(text)) for number, text in self.lines]
        said += [(self.front.key_lines.get(key, 1), text) for key, text in _meta_prose(self.meta)]
        for language, line, source in self.fences:
            said += [(line + offset, text) for offset, _, text in model.block_texts(source,
                                                                                    BLOCK_PROSE.get(language, ()))]
        # A comment says nothing to the reader: it is a note for the page's authors, which a page does not carry. The
        # lines of a fenced block are empty here, so a comment shown as code is not one.
        for (number, text), written in zip(self.lines, self.front.body.split('\n')):
            if '<!--' in written and text:
                self.report('R9', number, 'the page has an HTML comment; a page carries no note for its authors')
        allowances = [allowance for allowance in self.check.vocabulary.allow if allowance.page == self.relative]
        for number, text in said:
            plain = model.without_code_spans(text)
            allowed = []
            for allowance in allowances:
                start = text.find(allowance.text)
                while start >= 0:
                    allowed.append((start, start + len(allowance.text)))
                    self.check.used_allowances.add(allowance)
                    start = text.find(allowance.text, start + 1)
            reported = []  # one finding for one word, whichever pattern finds it first
            for pattern in self.check.vocabulary.patterns:
                for match in pattern.regex.finditer(text if pattern.code else plain):
                    if any(start <= match.start() and match.end() <= end for start, end in allowed) \
                            or any(match.start() < end and start < match.end() for start, end in reported):
                        continue
                    reported.append((match.start(), match.end()))
                    self.report('R9', number, 'says "%s": %s' % (match.group(0), pattern.why))

    # --- R10: figures ---

    def check_figures(self):
        model = self.model
        kind = self.kind
        for line, _ in model.inline_images(self.front.body, self.front.body_line):
            self.report('R10', line, 'the picture is not in a `figure` block, which gives it its caption and its '
                        'alternative text')
        figures = [(line, model.read_figure(source)) for name, line, source in self.fences if name == 'figure']
        figures = [(line, figure) for line, figure in figures if figure is not None]
        steps = self.steps()
        for line, figure in figures:
            if not figure['caption']:
                self.report('R10', line, 'the figure has no `caption`; say in one sentence what the picture shows')
            if any(not image['alt'] for image in figure['images']):
                self.report('R10', line, 'the figure has a picture without `alt`; describe it for a reader who '
                            'cannot see it')
            for pin in figure['pins']:
                # A page without its steps is R7's to report, once.
                if steps and not 1 <= pin <= steps:
                    self.report('R10', line, 'pin %d matches no step; the page has %s' % (pin, _count(steps, 'step')))
        if kind in ('process', 'interface'):
            first = self.sections[0].line if self.sections else None
            opening = [figure for line, figure in figures if first is None or line < first]
            if len(opening) != 1 or opening[0]['pair']:
                self.report('R10', self.top, '%s page opens with one figure of its window, before its first section; '
                            'this one has %s there' % (_article(kind).capitalize(), 'a before-and-after pair'
                                                       if len(opening) == 1 else _count(len(opening), 'figure')))
            elif kind == 'interface':
                unpinned = [str(item) for item in range(1, steps + 1) if item not in opening[0]['pins']]
                if unpinned:
                    self.report('R10', self.top, '%s of `## What you see` %s no pin on the opening figure' %
                                ('item ' + unpinned[0] if len(unpinned) == 1 else 'items ' + ', '.join(unpinned),
                                 'has' if len(unpinned) == 1 else 'have'))
        groups = self.check.nav_groups.get(self.relative, ())
        if kind == 'process' and groups and groups[-1] in self.limits.before_after_groups:
            how = self.section('How it works')
            worked = self.subsections(how, 'Worked example') if how else []
            pairs = [line for example in worked for line, source in self.blocks_under(example, 'figure')
                     if (model.read_figure(source) or {}).get('pair')]
            if not pairs:
                self.check.report('R10', self.path, worked[0].line if worked else self.top, 'a %s process is judged '
                                  'by its result, and the page shows no before-and-after pair under `### Worked '
                                  'example`' % groups[-1], 'warning')

    # --- R12: the keys of a rewritten process page ---

    def check_process_keys(self):
        if self.kind != 'process':
            return
        for key in PROCESS_KEYS:
            if key not in self.meta:
                self.report('R12', self.front.key_lines.get('kind', 1), 'a rewritten process page states `%s`' % key)
        glance = self.meta.get('glance')
        if isinstance(glance, dict):
            missing = [key for key in self.model.GLANCE_KEYS if key not in glance]
            if missing:
                self.report('R12', self.front.key_lines.get('glance', 1), '`glance` lacks %s; it states %s' %
                            (', '.join('`%s`' % key for key in missing), ', '.join(self.model.GLANCE_KEYS)))

    # --- R13: glossary terms linked on first use ---

    def check_glossary_links(self):
        model = self.model
        glossary = self.limits.glossary_page
        # The glossary defines the terms, and the media provenance is a record of files whose descriptions no reader
        # learns the terms from: neither links them.
        if self.kind in DATA_DRAWN_KINDS or self.relative in (glossary, self.limits.provenance_page):
            return
        terms = self.check.glossary_terms()
        if not terms:
            return
        heading_lines = {heading.line for heading in self.headings}
        folder = posixpath.dirname(self.relative)
        pending = list(terms)
        for number, text in self.lines:
            if not pending or number in heading_lines or not text.strip():
                continue
            said = model.without_addresses(model.without_code_spans(text))
            to_glossary, elsewhere = [], []
            for start, end, address in model.links(text):
                target = posixpath.normpath(posixpath.join(folder, urllib.parse.unquote(
                    address.split('#', 1)[0].split('?', 1)[0])))
                (to_glossary if target == glossary else elsewhere).append((start, end))
            # A term inside the words of a link to another page, a bold label or a quoted message is part of a name
            # the page repeats - a process, a control, a message of the application - not a use of the term, and the
            # first use is the next one. Linked to its entry there all the same, it is the linked first use.
            named = elsewhere + [match.span() for match in GLOSSARY_NAMES.finditer(said)]
            for term, regex in list(pending):
                for match in regex.finditer(said):
                    linked = any(start <= match.start() and match.end() <= end for start, end in to_glossary)
                    if not linked and any(start <= match.start() and match.end() <= end for start, end in named):
                        continue
                    pending.remove((term, regex))
                    if not linked:
                        self.check.report('R13', self.path, number, '"%s" is a term of the glossary; link its first '
                                          'use on the page to its entry' % term, 'warning')
                    break


    # --- R14: the controls against the windows' inventory ---

    def windows(self):
        """The inventories of the windows the page describes, in the order the application registers them: the
        windows its `menu` reaches that no other page's `menu` names more exactly. A page that names a submenu has
        the windows under it that have no page of their own."""
        product, inventory = self.check.product, self.check.inventory
        if product is None or not inventory:
            return []
        return windows_of(product, inventory, self.model.menu_paths(self.meta), self.check.all_menus().values())

    def check_controls(self):
        if self.kind != 'process':
            return
        windows = self.windows()
        if not windows:
            return  # a page of actions that open no window
        heading = self.section('Controls')
        anchor = heading.line if heading else self.top
        blocks = []
        for name, line, source in self.fences:
            if name == 'controls':
                block = self.model.read_controls(source)
                if block is None:
                    self.report('R14', line, 'the `controls` block is not a list of controls, so the page cannot be '
                                'held to its window; the build says what is wrong with it')
                    return
                blocks.append((line, block))
        if not blocks:
            return  # R7 reports a process page whose controls are in no block
        self.check.held_windows.update(window.id for window in windows)
        matched = {window.id: {} for window in windows}   # the place of a control in its window -> (line, name)
        # The controls a finding about a block already speaks of, which are not then reported as unlisted too.
        spoken_of = {window.id: set() for window in windows}
        described = set()
        for line, block in blocks:
            if block['window'] is None and len(windows) > 1:
                self.report('R14', line, 'the page describes %d windows (%s); the block says which one it is of with '
                            '`window`' % (len(windows), ', '.join('"%s"' % window.name for window in windows)))
                continue
            target = _block_target(block['window'], windows)
            if target is None:
                continue  # a window the inventory does not have: a dialog one of the window's buttons opens
            window, group = target
            described.add(window.id)
            for entry in block['controls']:
                # A readout is what the window shows, not a control: the inventory has none to hold it to.
                if entry['name'] and entry['kind'] != 'readout':
                    self._check_control(window, group, entry, line + entry['line'], matched[window.id],
                                        spoken_of[window.id])
        ignores = [dict(entry, used=False) for entry in self.meta.get('controls_ignore') or []
                   if isinstance(entry, dict)]
        for window in windows:
            if window.id not in described:
                self.report('R14', anchor, 'no `controls` block is of the window "%s"; a block of it states no '
                            '`window`, or the window\'s title, or the title of a tab or a group in it' % window.name)
                continue
            for control in window.controls:
                if control.index in matched[window.id] or control.index in spoken_of[window.id] or control.shared:
                    continue
                excuses = [entry for entry in ignores if entry.get('label') == control.label
                           and entry.get('object', control.object) == control.object]
                for entry in excuses:
                    entry['used'] = True
                if not excuses:
                    self.report('R14', anchor, 'the window "%s" has the %s "%s"%s, which no `controls` block lists; '
                                'document it, or name it in `controls_ignore` with the reason' %
                                (window.name, _KIND_WORDS[control.kind], control.label, _whereabouts(window, control)))
            self._check_order(window, matched[window.id])
        for entry in ignores:
            if not entry['used']:
                self.check.report('R14', self.path, self.front.key_lines.get('controls_ignore', 1), '`controls_ignore` '
                                  'names "%s", which excuses no control: the page\'s windows do not have it, or its '
                                  'blocks list it; remove the entry' % entry.get('label'), 'warning')

    def _check_control(self, window, group, entry, line, matched, spoken_of):
        name = entry['name']
        if entry['object']:
            found = [control for control in window.controls if control.object == entry['object']]
            if not found:
                self.report('R14', line, '"%s" states `object: %s`, and the window "%s" has no control of that '
                            'object name' % (name, entry['object'], window.name))
                return
            control = next((control for control in found if control.index not in matched), found[0])
            # A control is called by its label; one the window gives no caption is the writer's to name.
            if control.label != name and control.label != control.object:
                self.report('R14', line, '"%s" states `object: %s`, which the window "%s" labels "%s"; a control is '
                            'called by its label' % (name, entry['object'], window.name, control.label))
                matched[control.index] = (line, name)
                return
        else:
            found = [control for control in window.controls
                     if control.label == name and (group is None or group in control.groups)]
            if not found:
                self.report('R14', line, '"%s" is no control of %s%s' %
                            (name, 'the window "%s"' % window.name if group is None else
                             '"%s" in the window "%s"' % (group, window.name), _near_miss(window, name, group)))
                spoken_of.update(control.index for control in window.controls
                                 if control.label.lower() == name.lower())
                return
            if len(found) > 1:
                spoken_of.update(control.index for control in found)
                objects = [control.object for control in found if control.object]
                self.report('R14', line, 'the window "%s" has %d controls labelled "%s"%s; %s' %
                            (window.name, len(found), name, ' under "%s"' % group if group else '',
                             'say which one this is with `object`: %s' % ', '.join(objects)
                             if len(objects) == len(found) else
                             'put each in a block of its own tab or group, or name the rest in `controls_ignore`'))
                return
            control = found[0]
        if control.index in matched:
            self.report('R14', line, '"%s" is the control "%s" of line %d again' %
                        (name, matched[control.index][1], matched[control.index][0]))
            return
        matched[control.index] = (line, name)
        if entry['kind'] and entry['kind'] != control.kind:
            self.report('R14', line, '"%s" is %s in the window%s, and the block says `kind: %s`' %
                        (name, _article(_KIND_WORDS[control.kind]),
                         ', which moves with a slider beside it' if control.slider else '', entry['kind']))
            return
        # A list the window opens empty is filled from what is installed or open: its choices are the writer's to say.
        empty_list = control.options is not None and not control.options
        if control.default is not None and entry['default'] is not None and entry['default'] != control.default \
                and not empty_list:
            self.report('R14', line, '"%s" opens with "%s" in the window, and the block says `default: "%s"`' %
                        (name, control.default, entry['default']))
        for key in self.model.UI_NUMBERS:
            if key not in control.numbers:
                continue
            stated = entry['widget'].get(key)
            if stated is None:
                self.report('R14', line, '"%s": the window has `%s` %s, and the block\'s `widget` does not state it' %
                            (name, key, _number_text(control.numbers[key])))
            elif not _same_number(stated, control.numbers[key]):
                self.report('R14', line, '"%s": `widget.%s` is %s, and the window has %s' %
                            (name, key, stated.strip(), _number_text(control.numbers[key])))
        if control.options and entry['options'] is not None and entry['options'] != list(control.options):
            self.report('R14', line, '"%s": its `options` are not the window\'s: %s' %
                        (name, _options_difference(entry['options'], list(control.options))))

    def _check_order(self, window, matched):
        """A warning for the first control the page lists before one the window shows earlier."""
        last = None
        for index, (line, name) in sorted(matched.items(), key=lambda item: item[1][0]):
            if last is not None and index < last[0]:
                self.check.report('R14', self.path, line, '"%s" comes before "%s" in the window "%s", and after it on '
                                  'the page; the blocks follow the window\'s order' % (name, last[1], window.name),
                                  'warning')
                return
            last = (index, name)


_KIND_WORDS = {
    'spinbox': 'spin box', 'slider': 'slider', 'combo': 'combo box', 'check': 'check box', 'radio': 'set of radio '
    'buttons', 'toolbutton': 'toolbar button', 'button': 'button', 'field': 'text field', 'action': 'menu action',
}


def _block_target(name, windows):
    """What a block named `name` is of: (window, None) for a window by its title or for the page's one window when
    the block has no name, (window, group) for the title of a tab or a group in a window; None for another window."""
    if name is None:
        return windows[0], None
    for window in windows:
        if name == window.name:
            return window, None
    for window in windows:
        if any(name in control.groups for control in window.controls):
            return window, name
    return None


def _whereabouts(window, control):
    """Where a control sits and how to name it, for a message: its group, and its object when its label repeats."""
    parts = []
    if control.group:
        parts.append('under "%s"' % control.group)
    if control.object and sum(1 for other in window.controls if other.label == control.label) > 1:
        parts.append('object `%s`' % control.object)
    return ' (%s)' % ', '.join(parts) if parts else ''


def _near_miss(window, name, group):
    """What a name the window lacks may have meant: the same label under another group, or in another case."""
    elsewhere = sorted({control.group for control in window.controls if control.label == name and control.group})
    if group is not None and elsewhere:
        return '; the window has it under %s' % ', '.join('"%s"' % place for place in elsewhere)
    spelled = sorted({control.label for control in window.controls if control.label.lower() == name.lower()})
    if spelled:
        return '; the window labels it "%s"' % spelled[0]
    return ''


def _number_text(number):
    return ('%d' % number) if float(number).is_integer() and abs(number) < 1e15 else ('%.12g' % number)


def _same_number(stated, number):
    try:
        value = float(stated.strip())
    except ValueError:
        return False
    return abs(value - number) <= 1e-9 * max(1.0, abs(number))


def _options_difference(stated, recorded):
    missing = [option for option in recorded if option not in stated]
    extra = [option for option in stated if option not in recorded]
    if missing:
        return 'the window also has %s' % ', '.join('"%s"' % option for option in missing)
    if extra:
        return 'the window has no %s' % ', '.join('"%s"' % option for option in extra)
    return 'the window lists them as %s' % ', '.join(recorded)


class _GuideCheck:
    def __init__(self, model, repo, guide, display):
        self.model = model
        self.repo = repo
        self.guide = guide
        self.display = display
        self.findings = []
        self.config = model.load_config(guide)
        self.docs = model.docs_dir(guide, self.config)
        self.stages = model.load_stages(guide)
        self.stage_ids = [stage.id for stage in self.stages]
        self.limits = model.load_limits(guide)
        self.vocabulary = model.load_vocabulary(guide)
        self.inventory = model.load_ui_inventory(guide)
        self.product = None            # the application's menus, once read
        self.held_windows = set()      # the ids of the windows whose inventory a rewritten page was held to
        self._menus = None
        self.nav = model.nav_entries(self.config)
        self.nav_groups = {entry.path: entry.groups for entry in self.nav}
        self.exact = _ExactCase(self.docs)
        self.pages = {}
        self.shown = {}                # image -> [(the page that shows it, whether that page is rewritten)]
        self.owed = []                 # (page, line, url) of every picture a figure block says is not yet taken
        self.refuse_owed = False       # a release asks for every picture: an owed one is then an R5 error
        self.used_allowances = set()
        self.word_counts = {}          # page -> (the words R8 counted, its limit or None)
        self._glossary_terms = None
        self.help_summary = None       # what R15 read of the help table, once it has

    def report(self, rule, path, line, message, severity='error', pending=False):
        self.findings.append(Finding(self.display(path), line, rule, message, severity, pending))

    def page_path(self, relative):
        return os.path.join(self.docs, relative)

    def all_menus(self):
        """The `menu` paths of every page of the guide, by page, whichever pages are being checked."""
        if self._menus is None:
            self._menus = guide_menus(self.model, self.docs, self.all_pages())
        return self._menus

    def all_pages(self):
        found = []
        for directory, folders, files in os.walk(self.docs):
            folders.sort()
            for name in sorted(files):
                if name.endswith('.md'):
                    found.append(os.path.relpath(os.path.join(directory, name), self.docs).replace('\\', '/'))
        return found

    def resolve_page(self, given):
        """A page named on the command line, as a path relative to the docs folder, or None."""
        for candidate in (os.path.join(self.docs, given), os.path.abspath(given)):
            try:
                relative = os.path.relpath(os.path.abspath(candidate), self.docs).replace('\\', '/')
            except ValueError:
                continue  # another drive than the docs folder's
            if not relative.startswith('../') and os.path.isfile(candidate):
                return relative
        return None

    def check_page(self, relative, product, product_paths):
        model = self.model
        path = self.page_path(relative)
        text = model.read_text(path)
        front = model.split_front_matter(text)
        self.pages[relative] = front
        nav_paths = {entry.path for entry in self.nav}
        # R2: front matter.
        if not front.present:
            self.report('R2', path, 1, 'the page has no front matter; it needs at least `kind` and, except on the '
                        'home page, `summary`')
        elif front.error:
            self.report('R2', path, 1, front.error)
        else:
            for key, message in model.validate_meta(front.meta, self.stage_ids):
                self.report('R2', path, front.key_lines.get(key, 1) if key else 1, message)
        # R3: the nav and the H1.
        if relative not in nav_paths:
            self.report('R3', path, 1, 'the page is not in the nav of mkdocs.yml')
        headings = model.h1_lines(front.body, front.body_line)
        if not headings:
            self.report('R3', path, front.body_line, 'the page has no H1')
        elif len(headings) > 1:
            self.report('R3', path, headings[1], 'the page has %d H1 headings, at lines %s; it needs exactly one' %
                        (len(headings), ', '.join(str(line) for line in headings)))
        # R4: the menu paths it states exist.
        if front.meta and product is not None:
            for menu in model.menu_paths(front.meta):
                if menu not in product_paths:
                    self.report('R4', path, front.key_lines.get('menu', 1), '`menu` names %s, which is no menu or '
                                'action of the application' % menu)
        # R5: its images exist.
        referenced = set()
        shown = model.image_references(front.body, front.body_line)
        if front.meta:
            shown += [(front.key_lines.get('hero', 1), url) for url in model.meta_images(front.meta)]
        # A picture a figure block says is owed must not exist yet; it is counted, and refused only when a release asks
        # for every picture.
        for line, url in model.image_references(front.body, front.body_line, owed=True):
            target = urllib.parse.unquote(url.strip().split('#', 1)[0].split('?', 1)[0])
            resolved = posixpath.normpath(posixpath.join(posixpath.dirname(relative), target))
            if self.exact.kind(resolved) in ('file', 'case'):
                self.report('R5', path, line, 'image %s exists: remove `owed`, and the page shows it' % url)
                continue
            self.owed.append((relative, line, url))
            if self.refuse_owed:
                self.report('R5', path, line, 'image %s is owed: take it before a release' % url)
        for line, url in shown:
            resolved = self.check_image(path, relative, line, url)
            if resolved:
                referenced.add(resolved)
                self.shown.setdefault(resolved, []).append((relative, bool(front.meta)
                                                            and front.meta.get('rewritten') is True))
        # R6: a derived page credits the work it is derived from.
        if front.meta and front.meta.get('derived') is True:
            credits = model.section(front.body, CREDITS, front.body_line)
            if credits is None:
                self.report('R6', path, front.key_lines.get('derived', 1), 'the page states `derived: true` and has '
                            'no `## Credits` section; it credits the upstream work, its authors and its licence there')
            elif not credits[1]:
                self.report('R6', path, credits[0], 'the `## Credits` section is empty, and the page states '
                            '`derived: true`; it credits the upstream work, its authors and its licence there')
        # R7-R10, R12-R14: the page against its template, its budgets, the guide's vocabulary and its windows.
        if front.meta and front.meta.get('kind') in model.KINDS:
            _Writing(self, relative, path, front).run()
        return referenced

    def glossary_terms(self):
        """(term, the expression that finds it in prose) for every `### <term>` of the glossary's letter sections. A
        term matches whole, with or without a plural ending on any of its words; one written all in capitals matches
        only so."""
        if self._glossary_terms is None:
            model = self.model
            self._glossary_terms = []
            path = self.page_path(self.limits.glossary_page)
            if os.path.isfile(path):
                front = model.split_front_matter(model.read_text(path))
                under_letter = False
                for heading in model.headings(model.prose_lines(front.body, front.body_line)):
                    if heading.level == 2:
                        under_letter = bool(GLOSSARY_SECTION.match(heading.title))
                    elif heading.level == 3 and under_letter and heading.title:
                        flags = 0 if heading.title.upper() == heading.title else re.IGNORECASE
                        # Any word of the term may take the plural: "light frames", "fractions of full scale".
                        words = r'(?:e?s)?\s+'.join(re.escape(word) for word in heading.title.split())
                        self._glossary_terms.append((heading.title, re.compile(
                            r'(?<![\w-])%s(?:e?s)?(?![\w-])' % words, flags)))
        return self._glossary_terms

    def check_image_files(self):
        """R11: every picture a checked page shows, as a file."""
        model = self.model
        limits = self.limits
        provenance = os.path.join(self.docs, limits.provenance_page)
        recorded = set()
        if os.path.isfile(provenance):
            recorded = {posixpath.normpath(name) for name in re.findall(r'`([^`\s]+)`', model.read_text(provenance))}
        for image in sorted(self.shown):
            full = os.path.join(self.docs, image)
            pages = self.shown[image]
            rewritten = any(marked for _, marked in pages)
            problems = []
            try:
                size = model.image_size(full)
                weight = os.path.getsize(full)
            except OSError:
                continue  # R5 reports a picture that cannot be opened under the name the page gives it
            if size is None:
                problems.append('is not a PNG, WebP or JPEG file')
            elif max(size) > limits.image_longest_side:
                problems.append('is %d px on its longest side, over the limit of %d px' %
                                (max(size), limits.image_longest_side))
            if weight > limits.image_max_bytes:
                problems.append('weighs %d KB, over the limit of %d KB' %
                                ((weight + 1023) // 1024, limits.image_max_bytes // 1024))
            if image not in recorded:
                problems.append('has no row in %s' % limits.provenance_page)
            shown_by = sorted({page for page, marked in pages if marked or not rewritten})
            for problem in problems:
                self.report('R11', full, 1, 'the picture %s (shown by %s)' % (problem, ', '.join(shown_by)),
                            'error' if rewritten else 'warning', pending=not rewritten)

    def check_allowances(self, pages, whole_guide):
        """R9: an allowance that allows nothing is stale."""
        vocabulary = os.path.join(self.guide, 'data', 'vocabulary.yml')
        for allowance in self.vocabulary.allow:
            if allowance.page in pages:
                if allowance not in self.used_allowances:
                    self.report('R9', vocabulary, allowance.line, 'the allow-list allows "%s" on %s, which does not '
                                'say it; remove the entry' % (allowance.text, allowance.page), 'warning')
            elif whole_guide:
                self.report('R9', vocabulary, allowance.line, 'the allow-list names %s, which is not a page' %
                            allowance.page, 'warning')

    def check_image(self, path, relative, line, url):
        url = url.strip()
        if not url or SCHEME.match(url) or url.startswith('//'):
            return None
        target = urllib.parse.unquote(url.split('#', 1)[0].split('?', 1)[0])
        if target.startswith('/'):
            self.report('R5', path, line, 'image %s is root-relative; name it relative to the page' % url)
            return None
        resolved = posixpath.normpath(posixpath.join(posixpath.dirname(relative), target))
        if resolved == '..' or resolved.startswith('../'):
            self.report('R5', path, line, 'image %s lies outside the docs folder' % url)
            return None
        kind = self.exact.kind(resolved)
        if kind == 'case':
            self.report('R5', path, line, 'image %s exists only under another spelling of its case' % url)
        elif kind != 'file':
            self.report('R5', path, line, 'image %s does not exist' % url)
            return None
        return resolved

    def check_nav(self):
        config_path = os.path.join(self.guide, 'mkdocs.yml')
        config_lines = _read(config_path).split('\n')
        for entry in self.nav:
            if not os.path.isfile(self.page_path(entry.path)):
                line = next((number for number, text in enumerate(config_lines, start=1) if entry.path in text), 1)
                self.report('R3', config_path, line, 'the nav names %s, which is not a page' % entry.path)

    def check_coverage(self, product):
        page_paths = [menu for front in self.pages.values() if front.meta
                      for menu in self.model.menu_paths(front.meta)]
        registry = os.path.join(self.repo, REGISTRY)
        ui_builder = os.path.join(self.repo, UI_BUILDER)
        registered = set()
        covered = 0
        for action, line in product.registered:
            registered.add(action)
            places = product.placements_of(action)
            if action not in product.labels:
                self.report('R4', registry, line, 'registered action %s has no label in %s' % (action, UI_BUILDER))
            if not places:
                self.report('R4', registry, line, 'registered action %s is in no menu of %s' % (action, UI_BUILDER))
            elif not any(_covered(place, page_paths) for place, _, _ in places):
                self.report('R4', registry, line, 'registered action %s (%s) is on no page\'s `menu`' %
                            (action, '; '.join(place for place, _, _ in places)))
            else:
                covered += 1
        unregistered = [(action, line) for action, line in product.process_menu_actions() if action not in registered]
        for action, line in unregistered:
            places = [place for place, trail, _ in product.placements_of(action) if trail[0] == PROCESS_MENU]
            if not any(_covered(place, page_paths) for place in places):
                self.report('R4', ui_builder, line, 'Process menu action %s (%s) is on no page\'s `menu`' %
                            (action, '; '.join(places)))
            else:
                covered += 1
        return len(product.registered), len(unregistered), covered

    def check_help(self, product):
        """R15: the help table against the registered actions and the guide's pages."""
        registry = os.path.join(self.repo, REGISTRY)
        if product.help_rows is None:
            self.report('R15', registry, 1, '%s has no help table (`kHelpPages[] = {...};`), so Help opens no '
                        'process\'s page' % REGISTRY)
            return
        pages = set(self.all_pages())
        menus = self.all_menus()

        def named(page, action, line):
            """The source of the page a path names, or None once the path is reported."""
            if not _BUILT_PAGE.fullmatch(page):
                self.report('R15', registry, line, 'the help table names "%s" for %s, which is not the path of a '
                            'built page (`<folder>/<page>.html`, from the guide\'s root)' % (page, action))
                return None
            source = page[:-len('.html')] + '.md'
            if source not in pages:
                self.report('R15', registry, line, 'the help table names %s for %s, and the guide has no page %s' %
                            (page, action, source))
                return None
            return source

        def reaches(source, page, action, line):
            places = [place for place, _, _ in product.placements_of(action)]
            if places and not _closeness(places, menus.get(source, [])):
                self.report('R15', registry, line, 'the help table names %s for %s (%s), and that page\'s `menu` '
                            'does not reach it' % (page, action, '; '.join(places)))

        registered = {action for action, _ in product.registered}
        rows = {}
        for action, page, line in product.help_rows:
            if action in rows:
                self.report('R15', registry, line, 'the help table has a row for %s again; its first is on line %d' %
                            (action, rows[action]))
                continue
            rows[action] = line
            if action not in registered:
                self.report('R15', registry, line, 'the help table has a row for %s, which is no registered action' %
                            action)
                continue
            source = named(page, action, line)
            if source:
                reaches(source, page, action, line)

        default = product.non_interactive_help
        default_source = named(default[0], 'the non-interactive operations', default[1]) if default else None
        shared = 0
        for action, line in product.registered:
            if action in rows:
                continue
            if action not in product.non_interactive:
                self.report('R15', registry, line, 'registered action %s has no row in the help table, so Help opens '
                            'no page from its window' % action)
            elif default is None:
                self.report('R15', registry, line, 'registered action %s is a non-interactive operation, and the '
                            'registry states no page for those (`kNonInteractiveHelpPage`)' % action)
            else:
                shared += 1
                if default_source:
                    reaches(default_source, default[0], action, line)
        own = sum(1 for action in registered if action in rows)
        self.help_summary = ('help: %d of %d registered actions open a page of their own, %d the page of the '
                             'non-interactive operations' % (own, len(registered), shared))

    def check_unused_images(self, referenced):
        images = os.path.join(self.docs, 'assets', 'images')
        theme = self.config.get('theme') if isinstance(self.config.get('theme'), dict) else {}
        for key in ('logo', 'favicon'):
            if isinstance(theme.get(key), str):
                referenced.add(posixpath.normpath(theme[key]))
        readme = os.path.join(self.repo, 'README.md')
        if os.path.isfile(readme):
            for _, url in self.model.image_references(self.model.read_text(readme)):
                target = os.path.normpath(os.path.join(self.repo, urllib.parse.unquote(url.split('#', 1)[0])))
                relative = os.path.relpath(target, self.docs).replace('\\', '/')
                if not relative.startswith('../'):
                    referenced.add(relative)
        for directory, folders, files in os.walk(images):
            folders.sort()
            for name in sorted(files):
                if not name.lower().endswith(IMAGE_EXTENSIONS):
                    continue
                full = os.path.join(directory, name)
                relative = os.path.relpath(full, self.docs).replace('\\', '/')
                if relative not in referenced:
                    self.report('R5', full, 1, 'no page, README.md, logo or favicon shows this image', 'warning')

    def run(self, selected=None):
        """R2-R15 over every page, or R2-R14 over the pages selected; returns what the summary reports."""
        try:
            product = ProductMenus(self.repo)
            product_paths = product.existing_paths()
        except OSError as error:
            product, product_paths = None, set()
            self.report('R4', os.path.join(self.repo, REGISTRY), 1, 'the application sources cannot be read: %s' %
                        error)
        self.product = product
        pages = selected if selected is not None else self.all_pages()
        referenced = set()
        for relative in pages:
            referenced |= self.check_page(relative, product, product_paths)
        self.check_image_files()
        self.check_allowances(set(pages), selected is None)
        coverage = None
        if selected is None:
            self.check_nav()
            if product is not None:
                coverage = self.check_coverage(product)
                self.check_help(product)
            self.check_unused_images(referenced)
        return pages, coverage


def _guide_summary(check, pages, coverage):
    kinds = {}
    stages = {}
    derived = 0
    rewritten = 0
    for relative in pages:
        meta = check.pages[relative].meta or {}
        kinds[meta.get('kind')] = kinds.get(meta.get('kind'), 0) + 1
        rewritten += meta.get('rewritten') is True
        if meta.get('kind') == 'process':
            stages[meta.get('stage')] = stages.get(meta.get('stage'), 0) + 1
            derived += meta.get('derived') is True
    lines = ['%d pages: %s' % (len(pages), ', '.join('%d %s' % (kinds[kind], kind) for kind in
                                                      list(check.model.KINDS) + [None] if kinds.get(kind)))]
    if stages:
        lines.append('process pages by stage: %s; %d derived' % (
            ', '.join('%s %d' % (stage, stages[stage]) for stage in check.stage_ids + [None] if stages.get(stage)),
            derived))
    lines.append('%d of %d pages rewritten and held to R7-R14 as errors' % (rewritten, len(pages)))
    if check.inventory:
        lines.append('controls: %d of the %d windows of the inventory described by rewritten pages' %
                     (len(check.held_windows), len(check.inventory)))
    if coverage:
        registered, unregistered, covered = coverage
        lines.append('menus: %d of %d registered actions and other Process menu actions on a page\'s `menu`' %
                     (covered, registered + unregistered))
    if check.help_summary:
        lines.append(check.help_summary)
    return lines


def _load_model():
    sys.path.insert(0, os.path.join(REPO_ROOT, 'docs', 'user-guide', 'hooks'))
    import guide_model  # noqa: E402 - found beside the guide's hooks, needs PyYAML
    return guide_model


def _display(path):
    """The path as the user gave it, relative to the working directory when it lies under it."""
    absolute = os.path.abspath(path)
    try:
        relative = os.path.relpath(absolute)
    except ValueError:
        return absolute.replace('\\', '/')
    return (absolute if relative.startswith('..') else relative).replace('\\', '/')


def main(argv=None, stream=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--repo', default=REPO_ROOT, help='the repository whose application sources R4 reads')
    parser.add_argument('--guide', help='the guide folder that holds mkdocs.yml; default <repo>/docs/user-guide')
    parser.add_argument('--page', action='append', help='hold only this page to R2-R13; may be repeated')
    parser.add_argument('--pending', action='store_true', help='list the warnings of the pages not yet rewritten, '
                        'which are otherwise only counted')
    parser.add_argument('--site', help='a built site to hold to R1, such as docs/user-guide/site')
    parser.add_argument('--tokens', help='the website\'s assets/css/tokens.css, to hold the theme\'s copy to (R16)')
    parser.add_argument('--owed', action='store_true', help='list every picture a figure block says is not yet taken')
    parser.add_argument('--no-owed', action='store_true', help='refuse every picture not yet taken (R5), as a release '
                        'does')
    arguments = parser.parse_args(argv)
    stream = stream or sys.stdout
    guide = arguments.guide or os.path.join(arguments.repo, 'docs', 'user-guide')

    findings = []
    summary = []
    try:
        model = _load_model()
    except ImportError as error:
        print('check_user_guide.py needs PyYAML (%s): run it with the documentation toolchain\'s Python, '
              'docs/user-guide/.venv, which the guide\'s build helper makes' % error, file=stream)
        return 2
    try:
        check = _GuideCheck(model, arguments.repo, guide, _display)
    except model.GuideDataError as error:
        print('the guide\'s data cannot be read: %s' % error, file=stream)
        return 2
    check.refuse_owed = arguments.no_owed
    selected = None
    if arguments.page:
        selected = []
        for given in arguments.page:
            relative = check.resolve_page(given)
            if relative is None:
                print('%s is not a page of %s' % (given, _display(check.docs)), file=stream)
                return 2
            selected.append(relative)
    pages, coverage = check.run(selected)
    findings.extend(check.findings)
    summary.extend(_guide_summary(check, pages, coverage))
    if check.owed:
        owing = len({page for page, _, _ in check.owed})
        summary.append('%d picture%s owed, on %d page%s%s' % (
            len(check.owed), '' if len(check.owed) == 1 else 's', owing, '' if owing == 1 else 's',
            '' if arguments.owed else '; --owed lists them'))
    if arguments.owed:
        for page, line, url in check.owed:
            print('owed: %s:%d: %s' % (page, line, url), file=stream)
    # A page checked by name is being written: its count against its budget, which R8 states only once it is over.
    for relative in selected or []:
        if relative in check.word_counts:
            words, limit = check.word_counts[relative]
            summary.append('%s: %d words%s' % (relative, words, ', of a limit of %d' % limit if limit else ''))
    if arguments.site:
        if not os.path.isfile(os.path.join(arguments.site, 'index.html')):
            print('%s holds no index.html; build the guide first' % arguments.site, file=stream)
            return 2
        site_findings, documents, stylesheets, references = check_site(arguments.site, _display(arguments.site))
        findings.extend(site_findings)
        summary.append('%d references in %d HTML documents and %d stylesheets of %s' %
                       (references, documents, stylesheets, _display(arguments.site)))
    else:
        summary.append('R1 not run: no --site')
    if arguments.tokens:
        findings.extend(check_tokens(guide, arguments.tokens, _display))
    else:
        summary.append('R16 not run: no --tokens')

    list_pending = arguments.pending or bool(arguments.page)
    unlisted = [finding for finding in findings if finding.pending and not list_pending]
    for finding in findings:
        if list_pending or not finding.pending:
            print(finding, file=stream)
    if unlisted:
        summary.append('%d warning(s) about %d file(s) of pages not yet rewritten are counted and not listed; --page '
                       '<page> lists a page\'s, --pending all of them' %
                       (len(unlisted), len({finding.path for finding in unlisted})))
    errors = 0
    ran = {'R1': bool(arguments.site), 'R16': bool(arguments.tokens)}
    for rule, name in RULES.items():
        ruled = [finding for finding in findings if finding.rule == rule]
        rule_errors = sum(1 for finding in ruled if finding.severity == 'error')
        errors += rule_errors
        if ran.get(rule, True):
            print('%s %s: %d error(s), %d warning(s)' % (rule, name, rule_errors, len(ruled) - rule_errors),
                  file=stream)
    for line in summary:
        print(line, file=stream)
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
