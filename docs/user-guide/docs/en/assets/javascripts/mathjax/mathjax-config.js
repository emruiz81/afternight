// Configures the vendored MathJax before it loads, so that equations typeset from the copy the installers bundle,
// opened from disk with no network. mkdocs.yml lists this file before tex-mml-chtml.js.
window.MathJax = {
  loader: {
    // MathJax fetches its font data and web fonts from jsDelivr unless told otherwise. They are vendored beside it,
    // in fonts/mathjax-newcm-font/chtml/.
    paths: { fonts: "[mathjax]/fonts" }
  },
  options: {
    // Semantic enrichment runs the speech engine in a worker made from a blob: URL, which a page opened from disk may
    // not load; screen readers get the hidden MathML that assistive MathML puts beside each equation instead.
    enableEnrichment: false,
    enableAssistiveMml: true,
    // The menu's settings decide these options when MathJax starts, so they say the same.
    menuOptions: {
      settings: { enrich: false, speech: false, braille: false, assistiveMml: true }
    }
  }
};
