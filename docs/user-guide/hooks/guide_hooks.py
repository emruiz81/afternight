# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"""The user guide's MkDocs hooks: the data its theme renders from, the HTML passes over each page, and the search index.

mkdocs.yml names this file under `hooks:`. MkDocs puts the file's folder on the import path while it loads it,
so it imports its siblings directly.

- on_config: reads data/stages.yml, registers the `controls` and `figure` blocks of guide_fences.py with
  pymdownx.superfences, and reads AFTERNIGHT_GUIDE_VERSION into `extra.guide_version` for the footer.
- on_nav: keeps the nav for the later events.
- on_page_markdown: tells guide_fences.py which page renders next and where its blocks are, for their messages.
- on_page_content: lifts the page's H1 out of its content (the page head renders it), wraps tables, makes scrolling
  blocks reachable by the keyboard, collects the page's search entries, and dresses the sections the theme draws as
  components (guide_html.dress_sections).
- on_env: builds `extra.guide`, the data the layouts share: the stages with their pages in nav order and by nav group,
  the guide's sections with their page counts, the header's links, and what the home page draws - its picture, its
  actions, paths and popular pages, and the steps of the first tutorial. It warns about every page whose front matter
  does not fit the schema of guide_model.py, and about a home page that names a page or a picture the guide does not
  have, so a page the theme cannot draw fails the strict build.
- on_page_context: adds `gp`, what a page's layout renders: its head and chips, crumbs, stepper, left nav, lead and
  body, table of contents, and on a process page its position among the stages and its "At a glance" card.
- on_post_build: writes search/search_index.js.

The guide's data folder sits beside the configuration file; a configuration elsewhere - a fixture site - names the
guide's folder in `extra.guide_root`, relative to itself.

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

import guide_fences
import guide_html
import guide_model
import guide_search

log = get_plugin_logger('guide')

# The header's links, by the title of the nav section each opens.
HEADER_SECTIONS = ('Getting Started', 'Interface', 'Processes', 'Workflows', 'Extensions', 'Reference')
HEADER_LABELS = {'Getting Started': 'Getting started'}
FALLBACK_KIND = 'reference'
# The home and the catalog are drawn from data, so the search holds their summary and not a body the page does not
# show.
DATA_DRAWN = ('home', 'catalog')
# How many pages a stage or a section names where it is only pointed at.
SAMPLE = 3
GENERATED_SECTIONS = (('Where it belongs', 'where-it-belongs'), ('At a glance', 'at-a-glance'))
STAGE_NOTES = {
    'linear': 'The image is still linear at this stage.',
    'turn': 'The image turns nonlinear at this stage.',
    'nonlinear': 'The image is nonlinear at this stage.',
}
SUPERFENCES = 'pymdownx.superfences'


@dataclass
class _State:
    stages: list = field(default_factory=list)
    nav: object = None
    heads: dict = field(default_factory=dict)
    search: list = field(default_factory=list)
    controls: dict = field(default_factory=dict)
    home: dict = field(default_factory=dict)


_state = _State()


def _guide_dir(config):
    folder = os.path.dirname(os.path.abspath(config.config_file_path))
    root = config.extra.get('guide_root')
    return os.path.normpath(os.path.join(folder, root)) if isinstance(root, str) else folder


def on_config(config):
    global _state
    _state = _State()
    try:
        _state.stages = guide_model.load_stages(_guide_dir(config))
    except guide_model.GuideDataError as error:
        raise PluginError(str(error)) from error
    config.extra['guide_version'] = os.environ.get('AFTERNIGHT_GUIDE_VERSION', '').strip() or None
    fences = config.mdx_configs.setdefault(SUPERFENCES, {}).setdefault('custom_fences', [])
    names = [fence.get('name') for fence in fences]
    fences.extend(fence for fence in guide_fences.custom_fences() if fence['name'] not in names)
    return config


def on_nav(nav, config, files):
    _state.nav = nav
    return nav


def _top_section(page):
    """The top-level nav section that holds a page, or None for a page at the top (the home page)."""
    ancestors = list(page.ancestors)
    return ancestors[-1] if ancestors else None


def on_page_markdown(markdown, page, config, files):
    source = page.file.abs_src_path
    body, first_line = markdown, 1
    try:
        front = guide_model.split_front_matter(guide_model.read_text(source))
        body, first_line = front.body, front.body_line
    except OSError:
        pass  # A page some plugin generated: its blocks are found in what MkDocs read, without the file's lines.
    guide_fences.begin_page(page.file.src_uri, os.path.dirname(source), os.path.abspath(config.docs_dir), body,
                            first_line)
    return markdown


def _kind(meta):
    return meta.get('kind') if meta.get('kind') in guide_model.KINDS else FALLBACK_KIND


def _summary(meta):
    if _kind(meta) == 'home':
        hero = meta.get('hero')
        return hero['lede'] if isinstance(hero, dict) and isinstance(hero.get('lede'), str) else ''
    return meta.get('summary') if isinstance(meta.get('summary'), str) else ''


def on_page_content(html, page, config, files):
    meta = page.meta or {}
    kind = _kind(meta)
    title_html, title_text, h1_id, html = guide_html.lift_h1(html)
    html = guide_html.focusable_scrollers(guide_html.wrap_tables(html))
    _state.heads[page.file.src_uri] = {'title_html': title_html, 'title': title_text or page.title, 'id': h1_id}
    section = _top_section(page)
    menu = guide_model.menu_paths(meta)
    searched = '<p>%s</p>' % guide_html.html.escape(_summary(meta)) if kind in DATA_DRAWN else html
    _state.search.extend(guide_search.sections(
        searched, page.url, title_text or page.title or '', section.title if section else 'Home',
        menu[0] if menu else None))
    html = guide_html.dress_sections(html, kind)
    _state.controls[page.file.src_uri] = guide_html.controls_of(html)
    # The anchors the layouts add, which MkDocs cannot see in the Markdown: a link to one from another page is valid.
    anchors = getattr(page, 'present_anchor_ids', None)
    if anchors is not None:
        if kind == 'catalog':
            anchors.update('stage-' + stage.id for stage in _state.stages)
        elif kind == 'process':
            anchors.add('where-it-belongs')
            if isinstance(meta.get('glance'), dict):
                anchors.add('at-a-glance')
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
        'data': _data_state(meta),
    }


def _data_state(meta):
    """What a process page states of the image it takes and leaves: {'given', 'left', 'label'}, or None."""
    given = meta.get('data_in') if meta.get('data_in') in guide_model.DATA_STATES else None
    left = meta.get('data_out') if meta.get('data_out') in guide_model.DATA_STATES else None
    if not given and not left:
        return None
    if given and left and given != left:
        label = '%s \u2192 %s' % (given, left)
    else:
        label = given or left
    return {'given': given, 'left': left, 'label': label}


def _groups(members):
    """Pages by their nav group, the groups in the order the nav first names them."""
    groups = []
    for ref in members:
        if not groups or groups[-1]['title'] != ref['group']:
            found = next((group for group in groups if group['title'] == ref['group']), None)
            if found is None:
                found = {'title': ref['group'], 'pages': []}
                groups.append(found)
            found['pages'].append(ref)
        else:
            groups[-1]['pages'].append(ref)
    return groups


def _step_headings(page):
    """The steps of a tutorial: the `<h3>` headings under its "Steps" section, as {'title', 'url'}."""
    for top in page.toc or []:
        for item in (top.children if top.level == 1 else [top]):
            if item.title.strip().lower() == guide_html.STEPS:
                return [{'title': child.title, 'url': page.url + child.url} for child in item.children]
    return []


def _home(page, pages, config):
    """What the home page draws from its front matter, its pages resolved against the nav. Whatever it names that the
    guide does not have is a warning."""
    meta = page.meta or {}
    where = page.file.src_uri
    by_source = {other.file.src_uri: other for other in pages}

    def target(source, what):
        found = by_source.get(source) if isinstance(source, str) else None
        if found is None:
            log.warning('%s: %s names %s, which is no page of the nav', where, what, source)
        return found

    hero = meta.get('hero') if isinstance(meta.get('hero'), dict) else {}
    actions = []
    for action in hero.get('actions') if isinstance(hero.get('actions'), list) else []:
        found = target(action.get('page'), '`hero.actions`') if isinstance(action, dict) else None
        if found is not None:
            actions.append({'label': action.get('label'), 'url': found.url})
    picture = None
    if isinstance(hero.get('image'), str):
        path = os.path.normpath(os.path.join(os.path.dirname(page.file.abs_src_path), hero['image']))
        size = None
        if os.path.isfile(path) and not os.path.relpath(path, os.path.abspath(config.docs_dir)).startswith('..'):
            size = guide_model.image_size(path)
        if size is None:
            log.warning('%s: `hero.image` names %s, which is no PNG, WebP or JPEG file of the docs folder', where,
                        hero['image'])
        else:
            picture = {'src': hero['image'], 'alt': hero.get('alt') or '', 'caption': hero.get('caption') or '',
                       'width': size[0], 'height': size[1]}
    paths = []
    for entry in meta.get('paths') if isinstance(meta.get('paths'), list) else []:
        found = target(entry.get('page'), '`paths`') if isinstance(entry, dict) else None
        if found is not None:
            paths.append({'tag': entry.get('tag'), 'title': entry.get('title'), 'text': entry.get('text'),
                          'url': found.url})
    popular = []
    for source in meta.get('popular') if isinstance(meta.get('popular'), list) else []:
        found = target(source, '`popular`')
        if found is not None:
            popular.append({'title': found.title, 'url': found.url})
    first = next((other for other in pages if (other.meta or {}).get('kind') == 'tutorial'
                  and (other.meta or {}).get('stages')), None)
    track = None
    if first is not None and _step_headings(first):
        track = {'title': first.title, 'url': first.url, 'summary': _summary(first.meta or {}),
                 'steps': _step_headings(first)}
    return {
        'lede': hero.get('lede') if isinstance(hero.get('lede'), str) else '',
        'accent': hero.get('accent') if isinstance(hero.get('accent'), str) else '',
        'actions': actions,
        'picture': picture,
        'notice': meta.get('notice') if isinstance(meta.get('notice'), str) else '',
        'paths': paths,
        'popular': popular,
        'track': track,
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
                       'description': stage.description, 'index': index, 'data': stage.data, 'pages': members,
                       'groups': _groups(members)})
    sections = []
    for item in (nav.items if nav else []):
        first = _first_page(item)
        if first is None or isinstance(item, Page):
            continue
        sections.append({'title': item.title, 'label': HEADER_LABELS.get(item.title, item.title), 'url': first.url,
                         'count': len(_pages_under(item)),
                         'sample': [child.title for child in item.children[:SAMPLE]],
                         'more': len(item.children) > SAMPLE})
    for page in pages:
        if (page.meta or {}).get('kind') == 'home':
            _state.home[page.file.src_uri] = _home(page, pages, config)
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


def _position(stage, stages):
    """A process page's place among the stages: the stage before, its own and the stage after, and the bar that
    shows where the image turns nonlinear. "Any time" has no neighbours."""
    numbered = [other for other in stages if other['number']]

    def neighbour(other):
        return {'index': other['index'], 'number': other['number'], 'name': other['name'], 'id': other['id'],
                'sample': [ref['title'] for ref in other['pages'][:SAMPLE]]}

    before = after = None
    if stage['number']:
        place = next(index for index, other in enumerate(numbered) if other['id'] == stage['id'])
        before = neighbour(numbered[place - 1]) if place > 0 else None
        after = neighbour(numbered[place + 1]) if place + 1 < len(numbered) else None
    return {
        'before': before,
        'after': after,
        'bar': [{'index': other['index'], 'on': other['id'] == stage['id'], 'data': other['data']}
                for other in numbered],
        'note': STAGE_NOTES.get(next((other['data'] for other in numbered if other['id'] == stage['id']), None), ''),
    }


def _yes_no(value):
    return 'Yes' if value else 'No'


def _glance(meta):
    """The "At a glance" card of a process page that states `glance`: [{'label', 'value' or 'lines', 'full',
    'mono'}]. A page that does not state it draws no card."""
    stated = meta.get('glance')
    if not isinstance(stated, dict):
        return []
    rows = [{'label': 'Menu', 'lines': guide_model.menu_paths(meta), 'mono': True, 'full': True}]
    data = _data_state(meta)
    if data:
        if data['given'] and data['left']:
            words = ('%s data, kept %s' if data['given'] == data['left'] else '%s data in, %s out') % (
                data['given'], data['left'])
        else:
            words = '%s data %s' % (data['given'] or data['left'], 'in' if data['given'] else 'out')
        rows.append({'label': 'Image', 'value': words[0].upper() + words[1:]})
    if isinstance(meta.get('destructive'), bool):
        rows.append({'label': 'Target', 'value': 'Replaced, with a history step' if meta['destructive']
                     else 'Left as it is'})
    if meta.get('preview') in guide_model.PREVIEW_MODES:
        rows.append({'label': 'Preview', 'value': {'real-time': 'Real-time', 'static': 'Static', 'none': 'None'}
                     [meta['preview']]})
    if isinstance(meta.get('sequences'), bool):
        rows.append({'label': 'Sequences', 'value': _yes_no(meta['sequences'])})
    if meta.get('masks') in guide_model.MASK_MODES:
        rows.append({'label': 'Masks', 'value': {'post-blend': 'Blended after the process',
                                                 'modulates': 'Modulate the process', 'none': 'Not used'}
                     [meta['masks']]})
    if isinstance(meta.get('external'), str):
        rows.append({'label': 'External tools', 'value': 'None' if meta['external'].strip().lower() == 'none'
                     else meta['external']})
    for key, label in (('enabled', 'Enabled when'), ('inputs', 'Inputs'), ('outputs', 'Outputs'),
                       ('state', 'Recommended image state')):
        if isinstance(stated.get(key), str):
            rows.append({'label': label, 'value': stated[key], 'full': True})
    return rows


def _facts(meta):
    """The chips a process page's head shows beside its menu path and stage, from what its front matter states."""
    facts = []
    if meta.get('destructive') is True:
        facts.append({'warn': True, 'text': 'Replaces the target'})
    if meta.get('preview') in guide_model.PREVIEW_MODES:
        facts.append({'label': 'Preview', 'text': meta['preview']})
    if isinstance(meta.get('sequences'), bool):
        facts.append({'label': 'Sequences', 'text': _yes_no(meta['sequences']).lower()})
    if meta.get('masks') in guide_model.MASK_MODES:
        facts.append({'label': 'Masks', 'text': meta['masks']})
    if isinstance(meta.get('external'), str) and meta['external'].strip().lower() != 'none':
        facts.append({'label': 'Needs', 'text': meta['external']})
    return facts


def on_page_context(context, page, config, nav):
    meta = page.meta or {}
    kind = _kind(meta)
    head = _state.heads.get(page.file.src_uri, {})
    guide = context['config'].extra['guide']
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
        same_stage = [ref for ref in guide['stages'][stage['index'] - 1]['pages'] if ref['url'] != page.url]
    toc = _toc(page, kind)
    glance = _glance(meta) if kind == 'process' else []
    position = _position(guide['stages'][stage['index'] - 1], guide['stages']) if stage else None
    if kind == 'process':
        generated = [{'title': title, 'url': '#' + identifier, 'children': []}
                     for title, identifier in GENERATED_SECTIONS
                     if (identifier == 'where-it-belongs' and position) or (identifier == 'at-a-glance' and glance)]
        toc = generated + toc
    home = _state.home.get(page.file.src_uri) if kind == 'home' else None
    title_html = head.get('title_html') or page.title
    if home:
        title_html = guide_html.accent(title_html, home['accent'])
    context['guide'] = guide
    context['gp'] = {
        'kind': kind,
        'layout': 'tutorial' if kind == 'workflow' else kind,
        'title_html': title_html,
        'title': head.get('title') or page.title,
        'h1_id': head.get('id', ''),
        'summary': _summary(meta),
        'menu': guide_model.menu_paths(meta),
        'needs': meta.get('needs') if isinstance(meta.get('needs'), list) else None,
        'facts': _facts(meta) if kind == 'process' else [],
        'data': _data_state(meta) if kind == 'process' else None,
        'position': position,
        'glance': glance,
        'controls': [{'id': identifier, 'name': name}
                     for identifier, name in _state.controls.get(page.file.src_uri, [])],
        'home': home,
        'reach': meta.get('reach') if kind == 'catalog' and isinstance(meta.get('reach'), list) else [],
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
        'toc': toc,
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
