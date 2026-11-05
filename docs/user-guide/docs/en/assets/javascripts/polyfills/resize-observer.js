(function () {
  "use strict";

  if (typeof window === "undefined" || window.ResizeObserver) {
    return;
  }

  function rectFor(element) {
    var bounds = element.getBoundingClientRect();
    return {
      x: bounds.x || bounds.left,
      y: bounds.y || bounds.top,
      width: bounds.width,
      height: bounds.height,
      top: bounds.top,
      right: bounds.right,
      bottom: bounds.bottom,
      left: bounds.left
    };
  }

  function ResizeObserver(callback) {
    this.callback = callback;
    this.elements = [];
    this.scheduled = false;
  }

  ResizeObserver.prototype.observe = function (element) {
    if (this.elements.indexOf(element) === -1) {
      this.elements.push(element);
    }
    this.schedule();
  };

  ResizeObserver.prototype.unobserve = function (element) {
    var index = this.elements.indexOf(element);
    if (index !== -1) {
      this.elements.splice(index, 1);
    }
  };

  ResizeObserver.prototype.disconnect = function () {
    this.elements = [];
  };

  ResizeObserver.prototype.schedule = function () {
    var observer = this;
    if (observer.scheduled) {
      return;
    }
    observer.scheduled = true;
    window.requestAnimationFrame(function () {
      observer.scheduled = false;
      var entries = observer.elements.map(function (element) {
        return {
          target: element,
          contentRect: rectFor(element)
        };
      });
      observer.callback(entries, observer);
    });
  };

  window.ResizeObserver = ResizeObserver;
})();
