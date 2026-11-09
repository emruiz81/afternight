// A Copy button on every code block. The clipboard API needs a secure context, which a page opened from disk may not
// be, so the button falls back to selecting the text and asking the browser to copy it.
(function () {
  "use strict";

  function copyText(text) {
    if (navigator.clipboard && window.isSecureContext) {
      return navigator.clipboard.writeText(text);
    }
    return new Promise(function (resolve, reject) {
      var area = document.createElement("textarea");
      area.value = text;
      area.setAttribute("readonly", "");
      area.style.position = "fixed";
      area.style.top = "0";
      area.style.opacity = "0";
      document.body.appendChild(area);
      area.select();
      try {
        if (document.execCommand("copy")) {
          resolve();
        } else {
          reject(new Error("copy refused"));
        }
      } catch (error) {
        reject(error);
      } finally {
        document.body.removeChild(area);
      }
    });
  }

  Array.prototype.forEach.call(document.querySelectorAll(".prose pre > code"), function (code) {
    var pre = code.parentElement;
    if (pre.classList.contains("an-mermaid")) {
      return;
    }
    var holder = pre.parentElement && pre.parentElement.classList.contains("highlight") ? pre.parentElement : null;
    if (!holder) {
      return;
    }
    var button = document.createElement("button");
    button.type = "button";
    button.className = "copy-btn";
    button.textContent = "Copy";
    button.setAttribute("aria-label", "Copy the code");
    button.addEventListener("click", function () {
      copyText(code.innerText).then(function () {
        button.textContent = "Copied";
      }, function () {
        button.textContent = "Select and copy";
      });
      window.setTimeout(function () {
        button.textContent = "Copy";
      }, 1800);
    });
    holder.appendChild(button);
  });
})();
