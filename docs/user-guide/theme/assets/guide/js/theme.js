// The theme toggle. The inline script in the page's head has already set data-theme from the reader's stored choice,
// or else from the system's colour scheme; the toggle flips it, stores the choice when it can, and tells the page
// (an-themechange) so that whatever draws in theme colours - the diagrams - can redraw. Without a stored choice the
// page follows the system's scheme as it changes.
(function () {
  "use strict";

  var KEY = "afternight-guide-theme";
  var root = document.documentElement;
  var buttons = Array.prototype.slice.call(document.querySelectorAll(".theme-btn"));

  function current() {
    return root.getAttribute("data-theme") === "light" ? "light" : "dark";
  }

  function label() {
    var next = current() === "dark" ? "light" : "dark";
    buttons.forEach(function (button) {
      button.setAttribute("aria-label", "Switch to the " + next + " theme");
    });
  }

  function apply(theme, store) {
    root.setAttribute("data-theme", theme);
    if (store) {
      try {
        window.localStorage.setItem(KEY, theme);
      } catch (error) {
        // Storage refused (a private window, or a browser that keeps none for local files): the choice lasts this page.
      }
    }
    label();
    var event;
    try {
      event = new CustomEvent("an-themechange", { detail: { theme: theme } });
    } catch (error) {
      event = document.createEvent("CustomEvent");
      event.initCustomEvent("an-themechange", false, false, { theme: theme });
    }
    document.dispatchEvent(event);
  }

  buttons.forEach(function (button) {
    button.addEventListener("click", function () {
      apply(current() === "dark" ? "light" : "dark", true);
    });
  });
  label();

  var stored = null;
  try {
    stored = window.localStorage.getItem(KEY);
  } catch (error) {
    stored = null;
  }
  if (stored !== "light" && stored !== "dark" && window.matchMedia) {
    var query = window.matchMedia("(prefers-color-scheme: light)");
    var follow = function () {
      apply(query.matches ? "light" : "dark", false);
    };
    if (query.addEventListener) {
      query.addEventListener("change", follow);
    } else if (query.addListener) {
      query.addListener(follow);
    }
  }
})();
