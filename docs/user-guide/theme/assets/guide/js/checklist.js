// Task lists. Markdown writes a task list's boxes disabled and outside their item's text, so each box takes its item's
// text as its name. A tutorial's "What you need" list (ul.need) becomes a checklist the reader ticks while gathering
// things, with its progress under it. Nothing is stored: the list starts empty on every visit.
(function () {
  "use strict";

  function textOf(item) {
    var text = (item.textContent || "").replace(/\s+/g, " ").trim();
    return text.length > 120 ? text.slice(0, 117) + "..." : text;
  }

  Array.prototype.forEach.call(document.querySelectorAll(".task-list-item"), function (item) {
    var box = item.querySelector("input[type=checkbox]");
    if (box && !box.getAttribute("aria-label")) {
      box.setAttribute("aria-label", textOf(item));
    }
  });

  Array.prototype.forEach.call(document.querySelectorAll("ul.need"), function (list) {
    var boxes = Array.prototype.slice.call(list.querySelectorAll(":scope > li input[type=checkbox]"));
    if (!boxes.length) {
      return;
    }
    var progress = document.createElement("p");
    progress.className = "prog";
    var count = document.createElement("span");
    count.setAttribute("role", "status");
    var bar = document.createElement("span");
    bar.className = "bar";
    bar.setAttribute("aria-hidden", "true");
    var fill = document.createElement("b");
    bar.appendChild(fill);
    progress.appendChild(count);
    progress.appendChild(bar);

    function update() {
      var ready = 0;
      boxes.forEach(function (box) {
        var item = box.closest("li");
        if (box.checked) {
          ready += 1;
        }
        if (item) {
          item.classList.toggle("done", box.checked);
        }
      });
      count.textContent = ready + " of " + boxes.length + " ready";
      fill.style.width = (100 * ready / boxes.length) + "%";
    }

    boxes.forEach(function (box) {
      box.disabled = false;
      box.checked = false;
      box.addEventListener("change", update);
      var item = box.closest("li");
      if (item) {
        // The whole card ticks its box, except where the click belongs to a link or to the box itself.
        item.addEventListener("click", function (event) {
          if (event.target.closest("a, label, input")) {
            return;
          }
          box.checked = !box.checked;
          update();
        });
      }
    });
    list.classList.add("live");
    list.parentNode.insertBefore(progress, list.nextSibling);
    update();
  });
})();
