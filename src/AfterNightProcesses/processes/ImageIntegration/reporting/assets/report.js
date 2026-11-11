// The integration report's script, carried inside every report. It shows the tip of what the reader points at or
// reaches with the keyboard, switches and remembers the theme, sorts a table by a column, opens a folded section a
// link leads into or that is about to be printed, and moves a comparison's divider. It reads what the document
// already holds and computes no figure of the report; it asks nothing of any network. With scripts off the report
// still reads in full.
(function () {
  'use strict';
  var root = document.documentElement, key = 'afternight-report-theme', saved = null;
  try { saved = localStorage.getItem(key); } catch (ignore) { saved = null; }
  var asked = new URLSearchParams(location.search).get('theme') || saved;
  if (asked === 'light' || asked === 'dark') { root.setAttribute('data-theme', asked); }
  root.classList.add('js');

  function shownTheme() {
    return root.getAttribute('data-theme') || (matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark');
  }

  // A link into a folded section opens every section around its target.
  function openAround(target) {
    for (var fold = target.closest('details'); fold; fold = fold.parentElement && fold.parentElement.closest('details')) {
      fold.open = true;
    }
  }
  function openHash() {
    var target = location.hash.length > 1 && document.getElementById(decodeURIComponent(location.hash.slice(1)));
    if (target) { openAround(target); target.scrollIntoView(); }
  }
  addEventListener('hashchange', openHash);
  addEventListener('beforeprint', function () {
    document.querySelectorAll('details').forEach(function (fold) { fold.open = true; });
  });

  // The tip: the first line of data-tip is its heading, the others its lines.
  var tip = document.createElement('div');
  tip.className = 'tip';
  tip.hidden = true;
  tip.setAttribute('role', 'tooltip');
  function showTip(owner, x, y) {
    tip.textContent = '';
    owner.getAttribute('data-tip').split('\n').forEach(function (line, index) {
      var part = document.createElement(index ? 'span' : 'b');
      part.textContent = line;
      tip.appendChild(part);
    });
    tip.hidden = false;
    tip.style.left = Math.max(8, Math.min(innerWidth - tip.offsetWidth - 8, x + 14)) + 'px';
    tip.style.top = (y + tip.offsetHeight + 24 > innerHeight ? Math.max(8, y - tip.offsetHeight - 12) : y + 16) + 'px';
  }
  function tipOwner(node) { return node && node.closest ? node.closest('[data-tip]') : null; }
  document.addEventListener('DOMContentLoaded', function () { document.body.appendChild(tip); openHash(); });
  document.addEventListener('pointermove', function (event) {
    var owner = tipOwner(event.target);
    if (owner) { showTip(owner, event.clientX, event.clientY); } else { tip.hidden = true; }
  });
  document.addEventListener('pointerleave', function () { tip.hidden = true; });
  document.addEventListener('focusin', function (event) {
    var owner = tipOwner(event.target);
    if (!owner) { tip.hidden = true; return; }
    var box = owner.getBoundingClientRect();
    showTip(owner, box.left + box.width / 2, box.bottom);
  });
  document.addEventListener('focusout', function () { tip.hidden = true; });

  // A table of class sortable is put in the order of the column clicked: by each cell's data-v where it states
  // one, and by its text otherwise.
  function sortKey(cell) { return cell.hasAttribute('data-v') ? cell.getAttribute('data-v') : cell.textContent; }
  function sortBy(heading) {
    var table = heading.closest('table'), body = table.tBodies[0];
    var column = Array.prototype.indexOf.call(heading.parentNode.children, heading);
    var direction = heading.getAttribute('aria-sort') === 'ascending' ? -1 : 1;
    table.querySelectorAll('th').forEach(function (other) { other.removeAttribute('aria-sort'); });
    heading.setAttribute('aria-sort', direction > 0 ? 'ascending' : 'descending');
    var rows = Array.prototype.slice.call(body.rows);
    rows.sort(function (first, second) {
      var a = sortKey(first.cells[column]), b = sortKey(second.cells[column]);
      var x = parseFloat(a), y = parseFloat(b);
      return (isNaN(x) || isNaN(y) ? String(a).localeCompare(String(b)) : x - y) * direction;
    });
    rows.forEach(function (row) { body.appendChild(row); });
  }

  document.addEventListener('click', function (event) {
    if (!event.target.closest) { return; }
    if (event.target.closest('[data-theme-toggle]')) {
      var next = shownTheme() === 'dark' ? 'light' : 'dark';
      root.setAttribute('data-theme', next);
      try { localStorage.setItem(key, next); } catch (ignore) { saved = null; }
      return;
    }
    var heading = event.target.closest('table.sortable thead th');
    if (heading) { sortBy(heading); }
  });
  document.addEventListener('keydown', function (event) {
    var heading = event.target.closest && event.target.closest('table.sortable thead th');
    if (heading && (event.key === 'Enter' || event.key === ' ')) { event.preventDefault(); sortBy(heading); }
  });

  // A comparison's divider follows its range input.
  document.addEventListener('input', function (event) {
    var comparison = event.target.closest && event.target.closest('[data-compare]');
    if (comparison && event.target.type === 'range') { comparison.style.setProperty('--pos', event.target.value + '%'); }
  });
})();
