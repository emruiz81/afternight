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
      assets/images that no page, README.md or the site's logo and favicon uses is a warning.

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
    python build/scripts/tools/check_user_guide.py [--guide docs/user-guide] [--page <page> ...] [--site <site>]
                                                   [--tokens <the website's assets/css/tokens.css>]

R2-R5 hold every page, or with --page only the pages named (relative to the docs folder or to the working
directory); the guide-wide parts - nav entries, menu coverage and unused images - run only over every page. R1 runs
when --site names a built site, R16 when --tokens names the website's file. Each finding prints as
`path:line: Rnn message`, then a count per rule and what was read. Exits 1 on any error, 0 otherwise, and 2 when the
guide's own data cannot be read.

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

RULES = {
    'R1': 'built-site references',
    'R2': 'front matter',
    'R3': 'nav and H1',
    'R4': 'menu parity',
    'R5': 'images',
    'R16': 'token parity',
}
THEME_TOKENS = 'theme/assets/guide/css/tokens.css'


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

    def __str__(self):
        return '%s:%d: %s %s' % (self.path, self.line, self.rule, self.message)


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
PROCESS_MENU = 'menuProcess'


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
        for match in _REGISTRATION.finditer(registry_text):
            action = _ACTION_NAME.search(match.group(2))
            if action:
                self.registered.append((action.group(1), _line_of(registry_text, match.start(2))))

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
        self.nav = model.nav_entries(self.config)
        self.exact = _ExactCase(self.docs)
        self.pages = {}

    def report(self, rule, path, line, message, severity='error'):
        self.findings.append(Finding(self.display(path), line, rule, message, severity))

    def page_path(self, relative):
        return os.path.join(self.docs, relative)

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
        for line, url in shown:
            resolved = self.check_image(path, relative, line, url)
            if resolved:
                referenced.add(resolved)
        return referenced

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
        """R2-R5 over every page, or over the pages selected; returns what the summary reports."""
        try:
            product = ProductMenus(self.repo)
            product_paths = product.existing_paths()
        except OSError as error:
            product, product_paths = None, set()
            self.report('R4', os.path.join(self.repo, REGISTRY), 1, 'the application sources cannot be read: %s' %
                        error)
        pages = selected if selected is not None else self.all_pages()
        referenced = set()
        for relative in pages:
            referenced |= self.check_page(relative, product, product_paths)
        coverage = None
        if selected is None:
            self.check_nav()
            if product is not None:
                coverage = self.check_coverage(product)
            self.check_unused_images(referenced)
        return pages, coverage


def _guide_summary(check, pages, coverage):
    kinds = {}
    stages = {}
    derived = 0
    for relative in pages:
        meta = check.pages[relative].meta or {}
        kinds[meta.get('kind')] = kinds.get(meta.get('kind'), 0) + 1
        if meta.get('kind') == 'process':
            stages[meta.get('stage')] = stages.get(meta.get('stage'), 0) + 1
            derived += meta.get('derived') is True
    lines = ['%d pages: %s' % (len(pages), ', '.join('%d %s' % (kinds[kind], kind) for kind in
                                                      list(check.model.KINDS) + [None] if kinds.get(kind)))]
    if stages:
        lines.append('process pages by stage: %s; %d derived' % (
            ', '.join('%s %d' % (stage, stages[stage]) for stage in check.stage_ids + [None] if stages.get(stage)),
            derived))
    if coverage:
        registered, unregistered, covered = coverage
        lines.append('menus: %d of %d registered actions and other Process menu actions on a page\'s `menu`' %
                     (covered, registered + unregistered))
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
    parser.add_argument('--page', action='append', help='hold only this page to R2-R5; may be repeated')
    parser.add_argument('--site', help='a built site to hold to R1, such as docs/user-guide/site')
    parser.add_argument('--tokens', help='the website\'s assets/css/tokens.css, to hold the theme\'s copy to (R16)')
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

    for finding in findings:
        print(finding, file=stream)
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
