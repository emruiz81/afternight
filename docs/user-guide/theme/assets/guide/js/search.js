// The guide's search. Nothing loads until the reader first focuses the box or presses "/": then lunr and the index the
// guide's hooks wrote (search/search_index.js, which sets window.__guideSearchIndex) load as scripts - a page opened
// from disk can neither fetch a file nor start a worker - and lunr indexes the entries. Results are grouped by the
// guide's section, a process shows its menu path, and the keys are /, Up, Down, Enter and Esc.
(function () {
  "use strict";

  var root = document.documentElement;
  var input = document.getElementById("guide-search");
  var list = document.getElementById("guide-search-results");
  var status = document.querySelector(".search-status");
  if (!input || !list || !window.Promise) {
    return;
  }

  var SEPARATOR = /[\s\-,:!=\[\]()"\/]+/;
  var MAX_RESULTS = 40;
  var PER_PAGE = 3;
  // What a path within the guide's site is made of. It holds no colon, so it names no scheme.
  var SITE_PATH = /^[A-Za-z0-9._~\/-]+$/;

  // An address the page states for one of the guide's own files, as a path within the site: relative to the page, or
  // from the site's root. The theme writes these addresses itself, but none is taken on trust. One with a scheme or a
  // host of its own names another place: no script is loaded from it and no result links to it. Returns null for it.
  function sitePath(stated) {
    if (typeof stated !== "string" || !SITE_PATH.test(stated) || stated.indexOf("//") === 0) {
      return null;
    }
    // A path of those characters is spelled as an address the same way; it is handed on as one.
    return encodeURI(stated);
  }

  var base = sitePath(root.getAttribute("data-base")) || ".";
  var state = { loading: null, index: null, entries: [], options: [], active: -1, timer: 0, query: "" };

  function load(stated) {
    return new Promise(function (resolve, reject) {
      var src = sitePath(stated);
      if (!src) {
        reject(new Error("The search could not load its files: the page states no address of them within the guide."));
        return;
      }
      var script = document.createElement("script");
      script.src = src;
      script.onload = resolve;
      script.onerror = function () {
        reject(new Error("The search could not load " + src));
      };
      document.head.appendChild(script);
    });
  }

  function href(location) {
    if (/\/$/.test(base)) {
      return base + location;
    }
    return base + "/" + location;
  }

  function announce(text) {
    if (status) {
      status.textContent = text;
    }
  }

  function setOpen(open) {
    list.hidden = !open;
    input.setAttribute("aria-expanded", open ? "true" : "false");
    if (!open) {
      input.removeAttribute("aria-activedescendant");
      state.active = -1;
    }
  }

  function note(text) {
    list.innerHTML = "";
    var paragraph = document.createElement("p");
    paragraph.className = "note";
    paragraph.textContent = text;
    list.appendChild(paragraph);
    state.options = [];
    setOpen(true);
  }

  function prepare() {
    if (state.loading) {
      return state.loading;
    }
    if (input.value.trim()) {
      note("Preparing search…");
    }
    state.loading = Promise.all([
      window.lunr ? Promise.resolve() : load(root.getAttribute("data-search-engine")),
      window.__guideSearchIndex ? Promise.resolve() : load(root.getAttribute("data-search-index"))
    ]).then(function () {
      var data = window.__guideSearchIndex;
      state.entries = data.entries;
      window.lunr.tokenizer.separator = SEPARATOR;
      state.index = window.lunr(function () {
        var builder = this;
        builder.ref("i");
        builder.field("t", { boost: 10 });
        builder.field("p", { boost: 4 });
        builder.field("x");
        data.entries.forEach(function (entry, number) {
          builder.add({ i: String(number), t: entry.t, p: entry.p === entry.t ? "" : entry.p, x: entry.x });
        });
      });
    });
    state.loading.catch(function (error) {
      note(error.message);
    });
    return state.loading;
  }

  function terms(query) {
    return query.toLowerCase().split(SEPARATOR).filter(function (term) {
      return term.length > 0;
    });
  }

  function find(query) {
    var words = terms(query);
    if (!words.length) {
      return [];
    }
    var lunr = window.lunr;
    return state.index.query(function (q) {
      words.forEach(function (word, position) {
        var last = position === words.length - 1;
        if (!last) {
          // The index holds no stop words, so requiring one would match nothing.
          if (lunr.stopWordFilter(word)) {
            q.term(word, { presence: lunr.Query.presence.REQUIRED });
          }
          return;
        }
        q.term(word, { boost: 4 });
        q.term(word, { wildcard: lunr.Query.wildcard.TRAILING, usePipeline: false });
      });
    });
  }

  function snippet(text, words) {
    var lower = text.toLowerCase();
    var at = -1;
    words.some(function (word) {
      at = lower.indexOf(word);
      return at >= 0;
    });
    var start = Math.max(0, at - 40);
    var piece = text.slice(start, start + 160);
    return (start > 0 ? "…" : "") + piece + (start + 160 < text.length ? "…" : "");
  }

  function render(query) {
    state.query = query;
    if (!query.trim()) {
      list.innerHTML = "";
      setOpen(false);
      return;
    }
    var results = find(query);
    if (!results.length) {
      note("Nothing in the guide matches “" + query.trim() + "”.");
      announce("No results");
      return;
    }
    var words = terms(query);
    var perPage = {};
    var groups = [];
    var byGroup = {};
    var count = 0;
    results.forEach(function (result) {
      if (count >= MAX_RESULTS) {
        return;
      }
      var entry = state.entries[Number(result.ref)];
      var page = entry.l.split("#")[0];
      perPage[page] = (perPage[page] || 0) + 1;
      if (perPage[page] > PER_PAGE) {
        return;
      }
      if (!byGroup[entry.s]) {
        byGroup[entry.s] = [];
        groups.push(entry.s);
      }
      byGroup[entry.s].push(entry);
      count += 1;
    });
    list.innerHTML = "";
    state.options = [];
    groups.forEach(function (name, groupNumber) {
      var group = document.createElement("div");
      group.setAttribute("role", "group");
      var heading = document.createElement("div");
      heading.className = "group";
      heading.id = "guide-search-group-" + groupNumber;
      heading.textContent = name;
      group.setAttribute("aria-labelledby", heading.id);
      group.appendChild(heading);
      byGroup[name].forEach(function (entry) {
        var option = document.createElement("a");
        option.setAttribute("role", "option");
        option.setAttribute("aria-selected", "false");
        option.id = "guide-search-option-" + state.options.length;
        option.href = href(entry.l);
        var title = document.createElement("b");
        title.textContent = entry.t;
        option.appendChild(title);
        var where = entry.m || (entry.p !== entry.t ? entry.p : "");
        if (where) {
          var place = document.createElement("span");
          place.className = "where";
          place.textContent = where;
          option.appendChild(place);
        }
        if (entry.x) {
          var text = document.createElement("span");
          text.className = "snippet";
          text.textContent = snippet(entry.x, words);
          option.appendChild(text);
        }
        group.appendChild(option);
        state.options.push(option);
      });
      list.appendChild(group);
    });
    setOpen(true);
    announce(count + (count === 1 ? " result" : " results"));
  }

  function search() {
    var query = input.value;
    prepare().then(function () {
      if (input.value === query) {
        render(query);
      }
    });
  }

  function activate(position) {
    if (!state.options.length) {
      return;
    }
    if (state.active >= 0) {
      state.options[state.active].setAttribute("aria-selected", "false");
    }
    state.active = (position + state.options.length) % state.options.length;
    var option = state.options[state.active];
    option.setAttribute("aria-selected", "true");
    input.setAttribute("aria-activedescendant", option.id);
    if (option.scrollIntoView) {
      option.scrollIntoView({ block: "nearest" });
    }
  }

  input.addEventListener("focus", function () {
    prepare();
    if (input.value.trim() && state.options.length) {
      setOpen(true);
    }
  });

  input.addEventListener("input", function () {
    window.clearTimeout(state.timer);
    state.timer = window.setTimeout(search, 60);
  });

  input.addEventListener("keydown", function (event) {
    if (event.key === "ArrowDown") {
      event.preventDefault();
      activate(state.active + 1);
    } else if (event.key === "ArrowUp") {
      event.preventDefault();
      activate(state.active - 1);
    } else if (event.key === "Enter") {
      var option = state.options[state.active >= 0 ? state.active : 0];
      if (option) {
        event.preventDefault();
        window.location.href = option.href;
      }
    } else if (event.key === "Escape") {
      if (!list.hidden) {
        setOpen(false);
      } else {
        input.blur();
      }
    }
  });

  document.addEventListener("keydown", function (event) {
    if (event.key !== "/" || event.ctrlKey || event.metaKey || event.altKey) {
      return;
    }
    var target = event.target;
    var editing = target && (target.isContentEditable || /^(input|textarea|select)$/i.test(target.tagName));
    if (!editing) {
      event.preventDefault();
      input.focus();
    }
  });

  document.addEventListener("click", function (event) {
    if (!list.hidden && !event.target.closest(".search")) {
      setOpen(false);
    }
  });
})();
