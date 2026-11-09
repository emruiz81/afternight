#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""Hold the user guide to the rules its offline copy and its pages depend on.

The installers bundle the built guide, and Help opens it from disk: a page that names a missing file, a folder, or a
script, stylesheet, font or image on the network works on a web server and breaks in the copy people read offline.
The strict MkDocs build checks the links between pages as Markdown; this checks what the built site references.

Rules, each reported with its id:

  R1  The built site (--site). Every relative reference in its HTML - `href`, `src`, `srcset`, `poster`, `data`,
      inline `style` and `<style>` - and every `url()` and `@import` in its stylesheets names a file inside the site,
      spelled with the file's own case; a folder does not count, because a browser opened from disk lists a folder
      instead of opening its index.html. A root-relative reference (`/x`) and a `file:` URL are refused. Nothing the
      site loads - a script, stylesheet, font, image, frame or preload - comes from http(s); a link a reader follows
      (`<a>`, and `<link>` relations such as `canonical`) may lead anywhere. The one exception is the site's
      404.html: a web server shows it at whatever address was missing, so MkDocs writes its references
      root-relative, and they must name files from the site's root.

Usage:
    python build/scripts/tools/check_user_guide.py [--site <built site>]

Each finding prints as `path:line: Rnn message`, then a count per rule. Exits 1 on any error, 0 otherwise.

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

RULES = {
    'R1': 'built-site references',
}

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
    parser.add_argument('--site', help='a built site to hold to R1, such as docs/user-guide/site')
    arguments = parser.parse_args(argv)
    stream = stream or sys.stdout

    findings = []
    summary = []
    if arguments.site:
        if not os.path.isfile(os.path.join(arguments.site, 'index.html')):
            print('%s holds no index.html; build the guide first' % arguments.site, file=stream)
            return 1
        site_findings, documents, stylesheets, references = check_site(arguments.site, _display(arguments.site))
        findings.extend(site_findings)
        summary.append('%d references in %d HTML documents and %d stylesheets of %s' %
                       (references, documents, stylesheets, _display(arguments.site)))
    else:
        summary.append('R1 not run: no --site')

    for finding in findings:
        print(finding, file=stream)
    errors = 0
    for rule, name in RULES.items():
        ruled = [finding for finding in findings if finding.rule == rule]
        rule_errors = sum(1 for finding in ruled if finding.severity == 'error')
        errors += rule_errors
        if arguments.site or rule != 'R1':
            print('%s %s: %d error(s), %d warning(s)' % (rule, name, rule_errors, len(ruled) - rule_errors),
                  file=stream)
    for line in summary:
        print(line, file=stream)
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
