# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""The user guide's MkDocs hooks: the data its theme renders from, the HTML passes over each page, and the search index.

mkdocs.theme.yml names this file under `hooks:`. MkDocs puts the file's folder on the import path while it loads it,
so it imports its siblings directly.

- on_config: reads data/stages.yml, and AFTERNIGHT_GUIDE_VERSION into `extra.guide_version` for the footer.
- on_nav: keeps the nav for the later events.
- on_page_content: lifts the page's H1 out of its content (the page head renders it), wraps tables, makes scrolling
  blocks reachable by the keyboard, and collects the page's search entries.
- on_env: builds `extra.guide`, the data the layouts share: the stages with their pages in nav order, the guide's
  sections with their page counts and the header's links. It warns about every page whose front matter does not fit
  the schema of guide_model.py, so a page the theme cannot place fails the strict build.
- on_page_context: adds `guide_page`, what a page's layout renders: its head, crumbs, stepper, left nav, lead and
  body, and table of contents.
- on_post_build: writes search/search_index.js.

Every problem is a warning through the `guide` plugin logger, which `mkdocs build --strict` counts; only a data file
that cannot be read stops the build.

Tests: docs/user-guide/hooks/guide_hooks_test.py
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field

from mkdocs.exceptions import PluginError
from mkdocs.plugins import get_plugin_logger
from mkdocs.structure.pages import Page

import guide_html
import guide_model
import guide_search

log = get_plugin_logger('guide')

# The header's links, by the title of the nav section each opens.
HEADER_SECTIONS = ('Getting Started', 'Interface', 'Processes', 'Workflows', 'Extensions', 'Reference')
HEADER_LABELS = {'Getting Started': 'Getting started'}
FALLBACK_KIND = 'reference'


@dataclass
class _State:
    stages: list = field(default_factory=list)
    nav: object = None
    heads: dict = field(default_factory=dict)
    search: list = field(default_factory=list)


_state = _State()


def _guide_dir(config):
    return os.path.dirname(os.path.abspath(config.config_file_path))


def on_config(config):
    global _state
    _state = _State()
    try:
        _state.stages = guide_model.load_stages(_guide_dir(config))
    except guide_model.GuideDataError as error:
        raise PluginError(str(error)) from error
    config.extra['guide_version'] = os.environ.get('AFTERNIGHT_GUIDE_VERSION', '').strip() or None
    return config


def on_nav(nav, config, files):
    _state.nav = nav
    return nav


def _top_section(page):
    """The top-level nav section that holds a page, or None for a page at the top (the home page)."""
    ancestors = list(page.ancestors)
    return ancestors[-1] if ancestors else None


def on_page_content(html, page, config, files):
    title_html, title_text, h1_id, html = guide_html.lift_h1(html)
    html = guide_html.focusable_scrollers(guide_html.wrap_tables(html))
    _state.heads[page.file.src_uri] = {'title_html': title_html, 'title': title_text or page.title, 'id': h1_id}
    section = _top_section(page)
    menu = guide_model.menu_paths(page.meta)
    _state.search.extend(guide_search.sections(
        html, page.url, title_text or page.title or '', section.title if section else 'Home',
        menu[0] if menu else None))
    return html


def _first_page(item):
    """The first page a nav section leads to."""
    if isinstance(item, Page):
        return item
    for child in getattr(item, 'children', None) or []:
        found = _first_page(child)
        if found:
            return found
    return None


def _pages_under(item):
    if isinstance(item, Page):
        return [item]
    return [page for child in getattr(item, 'children', None) or [] for page in _pages_under(child)]


def _page_ref(page):
    meta = page.meta or {}
    head = _state.heads.get(page.file.src_uri, {})
    return {
        'title': page.title,
        'heading': head.get('title') or page.title,
        'url': page.url,
        'summary': meta.get('summary') if isinstance(meta.get('summary'), str) else '',
        'menu': guide_model.menu_paths(meta),
        'group': page.parent.title if page.parent else '',
    }


def _validate(page, stage_ids):
    meta = page.meta or {}
    where = page.file.src_uri
    if not meta:
        log.warning('%s: the page has no front matter; the theme needs at least its `kind`', where)
        return
    for key, message in guide_model.validate_meta(meta, stage_ids):
        log.warning('%s: %s', where, message)


def on_env(env, config, files):
    nav = _state.nav
    stage_ids = [stage.id for stage in _state.stages]
    pages = list(nav.pages) if nav else []
    for page in pages:
        _validate(page, stage_ids)
    stages = []
    for index, stage in enumerate(_state.stages, start=1):
        members = [_page_ref(page) for page in pages
                   if (page.meta or {}).get('kind') == 'process' and (page.meta or {}).get('stage') == stage.id]
        stages.append({'id': stage.id, 'number': stage.number, 'name': stage.name,
                       'description': stage.description, 'index': index, 'pages': members})
    sections = []
    for item in (nav.items if nav else []):
        first = _first_page(item)
        if first is None or isinstance(item, Page):
            continue
        sections.append({'title': item.title, 'label': HEADER_LABELS.get(item.title, item.title), 'url': first.url,
                         'count': len(_pages_under(item))})
    config.extra['guide'] = {
        'version': config.extra.get('guide_version'),
        'stages': stages,
        'stage_css': _stage_css(),
        'sections': sections,
        'header': [section for section in sections if section['title'] in HEADER_SECTIONS],
    }
    return env


def _stage_css():
    """The stage hues as custom properties, from data/stages.yml, so the hues have one source."""
    dark = ';'.join('--s%d:%s' % (index, stage.hue_dark) for index, stage in enumerate(_state.stages, start=1))
    light = ';'.join('--s%d:%s' % (index, stage.hue_light) for index, stage in enumerate(_state.stages, start=1))
    return (':root{--stage-count:%d;%s}:root[data-theme="light"]{%s}' % (len(_state.stages), dark, light))


def _stage_of(stage_id):
    for index, stage in enumerate(_state.stages, start=1):
        if stage.id == stage_id:
            return {'id': stage.id, 'number': stage.number, 'name': stage.name, 'description': stage.description,
                    'index': index}
    return None


def _crumbs(page):
    crumbs = []
    for ancestor in reversed(list(page.ancestors)):
        first = _first_page(ancestor)
        crumbs.append({'title': ancestor.title, 'url': first.url if first else None})
    return crumbs


def _toc(page, kind):
    """The table of contents under the page's H1: its `<h2>` sections, and on tutorials and workflows their steps."""
    entries = []
    for top in page.toc or []:
        items = top.children if top.level == 1 else [top]
        for item in items:
            children = [{'title': child.title, 'url': child.url} for child in item.children] \
                if kind in ('tutorial', 'workflow') else []
            entries.append({'title': item.title, 'url': item.url, 'children': children})
    return entries


def on_page_context(context, page, config, nav):
    meta = page.meta or {}
    kind = meta.get('kind') if meta.get('kind') in guide_model.KINDS else FALLBACK_KIND
    head = _state.heads.get(page.file.src_uri, {})
    section = _top_section(page)
    stage = _stage_of(meta.get('stage')) if kind == 'process' else None
    spans = [_stage_of(stage_id) for stage_id in meta.get('stages') or [] if _stage_of(stage_id)] \
        if isinstance(meta.get('stages'), list) else []
    if kind == 'home':
        stepper = 'big'
    elif kind == 'catalog':
        stepper = 'filter'
    elif kind == 'process':
        stepper = 'here'
    elif spans:
        stepper = 'span'
    else:
        stepper = 'plain'
    lead, body = guide_html.split_lead(page.content or '')
    siblings = [_page_ref(other) for other in (page.parent.children if page.parent else [])
                if isinstance(other, Page)]
    same_stage = []
    if stage:
        same_stage = [ref for ref in context['config'].extra['guide']['stages'][stage['index'] - 1]['pages']
                      if ref['url'] != page.url]
    context['guide'] = context['config'].extra['guide']
    context['gp'] = {
        'kind': kind,
        'layout': 'tutorial' if kind == 'workflow' else kind,
        'title_html': head.get('title_html') or page.title,
        'title': head.get('title') or page.title,
        'h1_id': head.get('id', ''),
        'summary': meta.get('summary') if isinstance(meta.get('summary'), str) else '',
        'menu': guide_model.menu_paths(meta),
        'needs': meta.get('needs') if isinstance(meta.get('needs'), list) else None,
        'stage': stage,
        'stages': spans,
        'stepper': stepper,
        'current_stages': [stage['id']] if stage else [span['id'] for span in spans],
        'section': section.title if section else None,
        'group': page.parent.title if page.parent and page.parent is not section else None,
        'crumbs': _crumbs(page),
        'siblings': siblings,
        'same_stage': same_stage,
        'lead': lead,
        'body': body,
        'toc': _toc(page, kind),
    }
    return context


def on_template_context(context, template_name, config):
    """The 404 page, a static template with no page of its own: the shared data, and no page data."""
    context['guide'] = config.extra['guide']
    context['gp'] = None
    return context


def on_post_build(config):
    folder = os.path.join(config.site_dir, 'search')
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, 'search_index.js'), 'w', encoding='utf-8', newline='\n') as handle:
        handle.write(guide_search.index_script(_state.search))
