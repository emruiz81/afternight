(function () {
  "use strict";

  const renderDiagrams = async () => {
    const nodes = Array.from(
      document.querySelectorAll("pre.an-mermaid:not([data-processed]):not([data-an-mermaid-pending])")
    );

    if (nodes.length === 0) {
      return;
    }

    for (const node of nodes) {
      // SuperFences wraps the source in <code>; Mermaid expects plain text in
      // each explicitly supplied node.
      node.textContent = node.textContent;
      node.setAttribute("data-an-mermaid-pending", "true");
    }

    try {
      mermaid.initialize({
        startOnLoad: false,
        securityLevel: "strict",
        theme: "base",
        fontFamily: "system-ui, -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif",
        flowchart: {
          htmlLabels: true,
          nodeSpacing: 18,
          rankSpacing: 28,
          useMaxWidth: false
        }
      });
      await mermaid.run({ nodes });
    } finally {
      for (const node of nodes) {
        node.removeAttribute("data-an-mermaid-pending");
      }
    }
  };

  const scheduleRender = () => {
    void renderDiagrams();
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", scheduleRender, { once: true });
  } else {
    scheduleRender();
  }

  if (typeof document$ !== "undefined") {
    document$.subscribe(scheduleRender);
  }
})();
