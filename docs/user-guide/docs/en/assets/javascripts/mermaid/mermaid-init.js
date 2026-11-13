(function () {
  "use strict";

  // The page carries the theme's design tokens as custom properties, and the diagrams are drawn in them: Mermaid
  // takes its colors from the tokens of the theme in force, and draws again when the reader switches theme (the
  // toggle dispatches "an-themechange"). A page without the tokens keeps Mermaid's base theme.
  const token = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

  const themed = () => token("--card") !== "" && token("--text") !== "";

  const configuration = () => {
    const settings = {
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
    };
    if (themed()) {
      const light = document.documentElement.getAttribute("data-theme") === "light";
      settings.fontFamily = token("--font-body") || settings.fontFamily;
      settings.themeVariables = {
        darkMode: !light,
        background: token("--card"),
        primaryColor: token("--card-2"),
        primaryTextColor: token("--text"),
        primaryBorderColor: token("--text-3"),
        secondaryColor: token("--bg-2"),
        secondaryTextColor: token("--text"),
        secondaryBorderColor: token("--text-3"),
        tertiaryColor: token("--bg-2"),
        tertiaryTextColor: token("--text"),
        tertiaryBorderColor: token("--text-3"),
        lineColor: token("--text-3"),
        textColor: token("--text"),
        mainBkg: token("--card-2"),
        nodeBorder: token("--text-3"),
        clusterBkg: token("--bg-2"),
        clusterBorder: token("--text-3"),
        titleColor: token("--text"),
        edgeLabelBackground: token("--card"),
        noteBkgColor: token("--bg-2"),
        noteTextColor: token("--text"),
        noteBorderColor: token("--text-3")
      };
    }
    return settings;
  };

  let rendering = Promise.resolve();

  const renderDiagrams = async () => {
    const nodes = Array.from(
      document.querySelectorAll("pre.an-mermaid:not([data-processed]):not([data-an-mermaid-pending])")
    );

    if (nodes.length === 0) {
      return;
    }

    for (const node of nodes) {
      // SuperFences wraps the source in <code>; Mermaid expects plain text in
      // each explicitly supplied node. The source is kept, so the diagram can
      // be drawn again in another theme's colors.
      if (!node.hasAttribute("data-source")) {
        node.setAttribute("data-source", node.textContent);
      }
      node.textContent = node.getAttribute("data-source");
      node.setAttribute("data-an-mermaid-pending", "true");
    }

    try {
      mermaid.initialize(configuration());
      await mermaid.run({ nodes });
    } finally {
      for (const node of nodes) {
        node.removeAttribute("data-an-mermaid-pending");
      }
    }
  };

  // One drawing at a time: a theme change during a drawing waits for it, then draws again.
  const scheduleRender = () => {
    rendering = rendering.then(renderDiagrams).catch((error) => {
      console.error(error);
    });
  };

  const redraw = () => {
    rendering = rendering.then(() => {
      for (const node of document.querySelectorAll("pre.an-mermaid[data-source][data-processed]")) {
        node.removeAttribute("data-processed");
      }
    });
    scheduleRender();
  };

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", scheduleRender, { once: true });
  } else {
    scheduleRender();
  }

  document.addEventListener("an-themechange", redraw);
})();
