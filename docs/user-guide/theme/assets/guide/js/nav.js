// The drawer the menu button opens below 1024 px, the Print action, and the table of contents' mark on the section in
// view.
(function () {
  "use strict";

  var root = document.documentElement;
  var button = document.querySelector(".menu-btn");
  var drawer = document.getElementById("guide-drawer");
  var backdrop = document.querySelector(".drawer-backdrop");

  function setOpen(open) {
    root.classList.toggle("nav-open", open);
    if (button) {
      button.setAttribute("aria-expanded", open ? "true" : "false");
      button.setAttribute("aria-label", open ? "Close the navigation" : "Open the navigation");
    }
    if (open && drawer) {
      var first = drawer.querySelector("a, summary");
      if (first) {
        first.focus();
      }
    }
  }

  if (button && drawer) {
    button.addEventListener("click", function () {
      setOpen(!root.classList.contains("nav-open"));
    });
    if (backdrop) {
      backdrop.addEventListener("click", function () {
        setOpen(false);
      });
    }
    document.addEventListener("keydown", function (event) {
      if (event.key === "Escape" && root.classList.contains("nav-open")) {
        setOpen(false);
        button.focus();
      }
    });
    drawer.addEventListener("click", function (event) {
      if (event.target.closest && event.target.closest("a")) {
        setOpen(false);
      }
    });
  }

  Array.prototype.forEach.call(document.querySelectorAll(".print-btn"), function (print) {
    print.addEventListener("click", function () {
      window.print();
    });
  });

  var links = Array.prototype.slice.call(document.querySelectorAll(".toc-list a"));
  if (!links.length || !("IntersectionObserver" in window)) {
    return;
  }
  var byId = {};
  links.forEach(function (link) {
    var id = decodeURIComponent((link.getAttribute("href") || "").replace(/^#/, ""));
    if (id) {
      byId[id] = link;
    }
  });
  var headings = Object.keys(byId).map(function (id) {
    return document.getElementById(id);
  }).filter(Boolean);
  headings.sort(function (a, b) {
    return a.compareDocumentPosition(b) & Node.DOCUMENT_POSITION_FOLLOWING ? -1 : 1;
  });

  function mark() {
    var current = null;
    for (var index = 0; index < headings.length; index += 1) {
      if (headings[index].getBoundingClientRect().top <= 220) {
        current = headings[index].id;
      } else {
        break;
      }
    }
    links.forEach(function (link) {
      link.classList.remove("in-view");
    });
    if (current && byId[current]) {
      byId[current].classList.add("in-view");
    }
  }

  var observer = new IntersectionObserver(mark, { rootMargin: "-64px 0px -55% 0px" });
  headings.forEach(function (heading) {
    observer.observe(heading);
  });
  mark();
})();
