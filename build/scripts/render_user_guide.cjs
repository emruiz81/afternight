// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"use strict";

// Opens the built user guide from disk in real browsers and fails every page load that would look or work wrong in
// the copy the installers bundle: a request that leaves the site, a console or page error, a local file that does not
// load, a page wider than the window, an image or a font face that did not load, an equation MathJax did not typeset,
// a Mermaid diagram that did not render, and, when asked, a serious or critical accessibility violation. Search
// queries are timed from the first focus of the search box and must return results.
//
// The strict MkDocs build proves the site's structure and `check_user_guide.py --site` its references; this proves
// what a reader sees, in the browsers people open the installed copy with.
//
// Setup (PowerShell), which keeps the Node packages and the browsers outside the repository:
//   $tools = Join-Path $env:TEMP "afternight-guide-render"
//   npm install --prefix $tools --no-save playwright-core@1.61.1 axe-core@4.14.0
//   $env:PLAYWRIGHT_BROWSERS_PATH = Join-Path $tools "browsers"
//   & (Join-Path $tools "node_modules\.bin\playwright-core.cmd") install firefox
//   $env:NODE_PATH = Join-Path $tools "node_modules"
// Chrome and Edge are the installed browsers (Playwright's `channel`); Firefox is Playwright's build. Without
// PLAYWRIGHT_BROWSERS_PATH the script looks for the `browsers` folder beside the `node_modules` that holds
// playwright-core. Never drive `msedge --headless` directly: it does nothing while Edge is running.
//
// Usage:
//   node build/scripts/render_user_guide.cjs [--site <dir>] [--pages all|<page.html,...>]
//        [--browsers chrome,msedge,firefox] [--widths 1440,1024,390] [--themes dark,light] [--offline] [--a11y]
//        [--search "histogram,flat,Shadows"] [--shots <dir>] [--report <file>] [--jobs <n>] [--timeout <seconds>]
//
//   --site      the built site; default docs/user-guide/site
//   --pages     site-relative HTML files, comma separated; default all but 404.html. A web server shows 404.html at
//               whatever address was missing, so MkDocs writes its references root-relative and it cannot work
//               opened from disk; named here, it is served from the site's root by a web server on the loopback
//               address, whose requests do not count as leaving the site
//   --browsers  default chrome,firefox
//   --widths    window widths in CSS pixels; default 1440,1024,390
//   --themes    the emulated prefers-color-scheme; default dark,light
//   --offline   abort every http(s) request, as on a machine with no network (any such request fails the load
//               either way; offline, the page also shows what the reader sees without it)
//   --a11y      run axe-core on every load and fail on a serious or critical violation
//   --search    queries typed into the search box of the home page, once per browser at the first width and theme
//   --shots     write a full-page PNG per load and print its width, so overflow shows up as a wider image
//   --report    the JSON report; default <shots>/report.json, else user-guide-render-report.json in the temp folder
//   --jobs      page loads at once per browser; default 4
//   --timeout   seconds a page may take to load and settle; default 45
//
// Exit codes: 0 every load and query passed; 1 one failed; 2 a usage or setup error.

const fs = require("fs");
const http = require("http");
const os = require("os");
const path = require("path");
const { pathToFileURL } = require("url");

const PLAYWRIGHT_VERSION = "1.61.1";
const AXE_VERSION = "4.14.0";
const REPO_ROOT = path.resolve(__dirname, "..", "..");
// Pages a web server shows at any address; they are rendered served, never from disk.
const SERVED_AT_ANY_ADDRESS = new Set(["404.html"]);
const CONTENT_TYPES = {
  ".html": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".webp": "image/webp",
  ".gif": "image/gif",
  ".ico": "image/x-icon",
  ".woff2": "font/woff2",
  ".woff": "font/woff",
  ".txt": "text/plain; charset=utf-8",
  ".xml": "application/xml"
};

// The search boxes the runner knows, by the markup of the theme that built the site.
const SEARCH_UIS = [
  {
    theme: "guide",
    open: null,
    input: "#guide-search",
    result: '#guide-search-results [role="option"]',
    resultLink: '#guide-search-results [role="option"]',
    resultTitle: '#guide-search-results [role="option"] b',
    group: '#guide-search-results [role="group"]',
    groupTitle: ".group"
  },
  {
    theme: "material",
    open: 'label[for="__search"]',
    input: "input.md-search__input",
    result: ".md-search-result__item",
    resultLink: ".md-search-result__item a",
    resultTitle: ".md-search-result__item h1, .md-search-result__item h2"
  }
];

class UsageError extends Error {}

function parseArgs(argv) {
  const options = {
    site: path.join(REPO_ROOT, "docs", "user-guide", "site"),
    pages: "all",
    browsers: ["chrome", "firefox"],
    widths: [1440, 1024, 390],
    themes: ["dark", "light"],
    offline: false,
    a11y: false,
    search: [],
    shots: null,
    report: null,
    jobs: 4,
    timeout: 45
  };
  const list = (value) => value.split(",").map((item) => item.trim()).filter(Boolean);
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    const value = () => {
      if (i + 1 >= argv.length) {
        throw new UsageError(`${arg} needs a value`);
      }
      i += 1;
      return argv[i];
    };
    switch (arg) {
      case "--site": options.site = path.resolve(value()); break;
      case "--pages": options.pages = value(); break;
      case "--browsers": options.browsers = list(value()); break;
      case "--widths": options.widths = list(value()).map(Number); break;
      case "--themes": options.themes = list(value()); break;
      case "--offline": options.offline = true; break;
      case "--a11y": options.a11y = true; break;
      case "--search": options.search = list(value()); break;
      case "--shots": options.shots = path.resolve(value()); break;
      case "--report": options.report = path.resolve(value()); break;
      case "--jobs": options.jobs = Number(value()); break;
      case "--timeout": options.timeout = Number(value()); break;
      case "--help":
      case "-h":
        throw new UsageError("");
      default:
        throw new UsageError(`unknown argument ${arg}`);
    }
  }
  for (const browser of options.browsers) {
    if (!["chrome", "msedge", "firefox"].includes(browser)) {
      throw new UsageError(`unknown browser ${browser}; use chrome, msedge or firefox`);
    }
  }
  for (const theme of options.themes) {
    if (!["dark", "light"].includes(theme)) {
      throw new UsageError(`unknown theme ${theme}; use dark or light`);
    }
  }
  if (options.widths.length === 0 || options.widths.some((width) => !Number.isInteger(width) || width < 200)) {
    throw new UsageError("--widths takes whole numbers of CSS pixels, 200 or more");
  }
  if (!Number.isInteger(options.jobs) || options.jobs < 1) {
    throw new UsageError("--jobs takes a whole number, 1 or more");
  }
  if (!(options.timeout > 0)) {
    throw new UsageError("--timeout takes a number of seconds");
  }
  if (!options.report) {
    options.report = options.shots
      ? path.join(options.shots, "report.json")
      : path.join(os.tmpdir(), "user-guide-render-report.json");
  }
  return options;
}

function loadPlaywright() {
  let packageJson;
  try {
    packageJson = require.resolve("playwright-core/package.json");
  } catch (error) {
    throw new UsageError(
      "playwright-core is not installed; follow the setup in this script's header and set NODE_PATH");
  }
  if (!process.env.PLAYWRIGHT_BROWSERS_PATH) {
    const browsers = path.resolve(path.dirname(packageJson), "..", "..", "browsers");
    if (fs.existsSync(browsers)) {
      process.env.PLAYWRIGHT_BROWSERS_PATH = browsers;
    }
  }
  const version = require(packageJson).version;
  if (version !== PLAYWRIGHT_VERSION) {
    throw new UsageError(`expected playwright-core ${PLAYWRIGHT_VERSION}, found ${version}`);
  }
  return require("playwright-core");
}

function axeSource() {
  let packageJson;
  try {
    packageJson = require.resolve("axe-core/package.json");
  } catch (error) {
    throw new UsageError("axe-core is not installed; --a11y needs it (see this script's header)");
  }
  const version = require(packageJson).version;
  if (version !== AXE_VERSION) {
    throw new UsageError(`expected axe-core ${AXE_VERSION}, found ${version}`);
  }
  return fs.readFileSync(path.join(path.dirname(packageJson), "axe.min.js"), "utf8");
}

function sitePages(site, pages) {
  if (!fs.existsSync(path.join(site, "index.html"))) {
    throw new UsageError(`no built site at ${site}; build the guide first`);
  }
  if (pages !== "all") {
    const listed = pages.split(",").map((page) => page.trim().replace(/\\/g, "/")).filter(Boolean);
    for (const page of listed) {
      if (!fs.existsSync(path.join(site, page))) {
        throw new UsageError(`no page ${page} in ${site}`);
      }
    }
    return listed;
  }
  const found = [];
  const walk = (dir) => {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) {
        walk(full);
      } else if (entry.name.endsWith(".html")) {
        found.push(path.relative(site, full).replace(/\\/g, "/"));
      }
    }
  };
  walk(site);
  return found.filter((page) => !SERVED_AT_ANY_ADDRESS.has(page)).sort();
}

// Serves the site from its root on the loopback address, for the pages a web server shows at any address.
function startServer(site) {
  const root = path.resolve(site);
  const server = http.createServer((request, response) => {
    const address = decodeURIComponent(new URL(request.url, "http://localhost").pathname);
    const file = path.resolve(root, `.${address}`);
    const inside = file === root || file.startsWith(root + path.sep);
    const target = inside && fs.existsSync(file) && fs.statSync(file).isFile() ? file : null;
    const served = target || path.join(root, "404.html");
    response.writeHead(target ? 200 : 404, {
      "Content-Type": CONTENT_TYPES[path.extname(served).toLowerCase()] || "application/octet-stream"
    });
    fs.createReadStream(served).pipe(response);
  });
  return new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => {
      resolve({ origin: `http://127.0.0.1:${server.address().port}`, close: () => server.close() });
    });
  });
}

async function launch(playwright, name) {
  if (name === "firefox") {
    return playwright.firefox.launch({ headless: true });
  }
  return playwright.chromium.launch({ channel: name, headless: true });
}

function viewportFor(width) {
  return { width, height: width < 600 ? 844 : 900 };
}

function pngWidth(file) {
  const header = Buffer.alloc(24);
  const fd = fs.openSync(file, "r");
  try {
    fs.readSync(fd, header, 0, 24, 0);
  } finally {
    fs.closeSync(fd);
  }
  return header.readUInt32BE(16);
}

// The loopback server's origin while it runs; its requests stay inside the site.
let localOrigin = null;
const isRemote = (url) => /^https?:/i.test(url) && !(localOrigin && url.startsWith(`${localOrigin}/`));

// Runs in the page once it has loaded: every lazy image is asked for, and the page settles when MathJax has typeset,
// Mermaid has drawn, every image has answered and every font the layout asked for has loaded or failed.
async function settle(page, timeoutMs) {
  return page.evaluate(async (limit) => {
    const deadline = Date.now() + limit;
    const until = (test) => new Promise((resolve) => {
      const poll = () => {
        if (test() || Date.now() > deadline) {
          resolve(test());
        } else {
          setTimeout(poll, 100);
        }
      };
      poll();
    });
    for (const image of document.images) {
      if (image.loading === "lazy") {
        image.loading = "eager";
      }
    }
    await until(() => Array.from(document.images).every((image) => image.complete));
    let mathjax = "absent";
    if (window.MathJax && window.MathJax.startup && window.MathJax.startup.promise) {
      mathjax = await Promise.race([
        window.MathJax.startup.promise.then(() => "typeset", (error) => `failed: ${error && error.message}`),
        new Promise((resolve) => setTimeout(() => resolve("timed out"), Math.max(0, deadline - Date.now())))
      ]);
    }
    await until(() => Array.from(document.querySelectorAll(".an-mermaid")).every((node) => node.querySelector("svg")));
    for (let frame = 0; frame < 3; frame += 1) {
      await new Promise((resolve) => requestAnimationFrame(() => resolve()));
    }
    const rest = Math.max(0, deadline - Date.now());
    await Promise.race([document.fonts.ready, new Promise((resolve) => setTimeout(resolve, rest))]);
    return mathjax;
  }, timeoutMs);
}

// Runs in the page after it settled and returns what the load is judged on.
function inspect() {
  const root = document.documentElement;
  const overflow = root.scrollWidth - root.clientWidth;
  const culprits = [];
  if (overflow > 0) {
    for (const element of document.body.querySelectorAll("*")) {
      const box = element.getBoundingClientRect();
      if (box.width > 0 && box.right > root.clientWidth + 1) {
        const parent = element.parentElement && element.parentElement.getBoundingClientRect();
        if (!parent || parent.right <= root.clientWidth + 1) {
          const name = element.tagName.toLowerCase()
            + (element.id ? `#${element.id}` : "")
            + (typeof element.className === "string" && element.className.trim()
              ? `.${element.className.trim().split(/\s+/).slice(0, 2).join(".")}` : "");
          culprits.push(`${name} (right edge ${Math.round(box.right)} px)`);
          if (culprits.length >= 3) {
            break;
          }
        }
      }
    }
  }
  const math = Array.from(document.querySelectorAll(".arithmatex"));
  const diagrams = Array.from(document.querySelectorAll(".an-mermaid"));
  const fonts = Array.from(document.fonts).map((face) => ({
    family: face.family.replace(/^["']|["']$/g, ""),
    status: face.status
  }));
  return {
    overflow,
    culprits,
    images: document.images.length,
    brokenImages: Array.from(document.images)
      .filter((image) => image.naturalWidth === 0)
      .map((image) => image.getAttribute("src") || image.currentSrc),
    math: math.length,
    untypeset: math.filter((node) => !node.querySelector("mjx-container")).length,
    mathErrors: Array.from(document.querySelectorAll("mjx-merror"))
      .map((node) => node.getAttribute("data-mjx-error") || node.textContent)
      .slice(0, 5),
    diagrams: diagrams.length,
    unrendered: diagrams.filter((node) => !node.querySelector("svg")).length,
    fonts
  };
}

async function renderPage(browser, combo, page, options, axe) {
  const started = Date.now();
  const result = {
    page,
    browser: combo.browser,
    theme: combo.theme,
    width: combo.width,
    failures: [],
    remote: []
  };
  const context = await browser.newContext({
    viewport: viewportFor(combo.width),
    colorScheme: combo.theme,
    deviceScaleFactor: 1
  });
  try {
    await context.route((url) => isRemote(url.href), (route) => {
      result.remote.push(route.request().url());
      return options.offline ? route.abort("internetdisconnected") : route.continue();
    });
    const tab = await context.newPage();
    const consoleErrors = [];
    const pageErrors = [];
    const missing = [];
    tab.on("console", (message) => {
      if (message.type() === "error") {
        consoleErrors.push(message.text());
      }
    });
    tab.on("pageerror", (error) => pageErrors.push(String((error && error.message) || error)));
    tab.on("requestfailed", (request) => {
      if (!isRemote(request.url())) {
        const failure = request.failure();
        missing.push(`${request.url()} (${failure ? failure.errorText : "failed"})`);
      }
    });

    const url = SERVED_AT_ANY_ADDRESS.has(page)
      ? `${localOrigin}/${page}`
      : pathToFileURL(path.join(options.site, page)).href;
    try {
      await tab.goto(url, { waitUntil: "load", timeout: options.timeout * 1000 });
    } catch (error) {
      result.failures.push(`did not load: ${String(error.message).split("\n")[0]}`);
    }
    result.mathjax = await settle(tab, options.timeout * 1000).catch((error) => `settle failed: ${error.message}`);
    const state = await tab.evaluate(inspect);
    result.math = { blocks: state.math, untypeset: state.untypeset, errors: state.mathErrors };
    result.diagrams = { blocks: state.diagrams, unrendered: state.unrendered };
    result.images = { count: state.images, broken: state.brokenImages };
    result.overflow = state.overflow;
    result.fonts = {
      loaded: state.fonts.filter((face) => face.status === "loaded").length,
      error: state.fonts.filter((face) => face.status === "error").map((face) => face.family),
      unloaded: state.fonts.filter((face) => face.status === "unloaded").length,
      mathLoaded: state.fonts.filter((face) => face.status === "loaded" && /^MJX-/i.test(face.family)).length
    };

    if (options.a11y) {
      await tab.addScriptTag({ content: axe });
      const violations = await tab.evaluate(async () => {
        // No preload: axe fetches the stylesheets by XMLHttpRequest for it, which file:// refuses.
        const run = await window.axe.run(document, { resultTypes: ["violations"], preload: false });
        return run.violations.map((violation) => ({
          id: violation.id,
          impact: violation.impact,
          nodes: violation.nodes.length,
          target: violation.nodes.slice(0, 2).map((node) => node.target.join(" "))
        }));
      });
      result.a11y = violations;
      for (const violation of violations.filter((v) => v.impact === "serious" || v.impact === "critical")) {
        result.failures.push(`a11y ${violation.id} (${violation.impact}, ${violation.nodes} nodes: `
          + `${violation.target.join(" | ")})`);
      }
    }

    if (options.shots) {
      const slug = page.replace(/\.html$/, "").replace(/\//g, "__");
      const name = `${combo.browser}-${combo.theme}-${combo.width}-${slug}.png`;
      const file = path.join(options.shots, name);
      await tab.screenshot({ path: file, fullPage: true });
      result.shot = { file, width: pngWidth(file) };
    }

    for (const request of new Set(result.remote)) {
      result.failures.push(`request leaves the site: ${request}`);
    }
    for (const text of consoleErrors) {
      result.failures.push(`console error: ${text}`);
    }
    for (const text of pageErrors) {
      result.failures.push(`page error: ${text}`);
    }
    for (const text of missing) {
      result.failures.push(`local file did not load: ${text}`);
    }
    if (state.overflow > 0) {
      const culprits = state.culprits.join(", ") || "no single element";
      result.failures.push(`${state.overflow} px wider than the window: ${culprits}`);
    }
    for (const image of state.brokenImages) {
      result.failures.push(`image did not load: ${image}`);
    }
    for (const family of new Set(result.fonts.error)) {
      result.failures.push(`font face did not load: ${family}`);
    }
    if (state.math > 0 && result.mathjax !== "typeset") {
      result.failures.push(`MathJax ${result.mathjax}`);
    }
    if (state.untypeset > 0) {
      result.failures.push(`${state.untypeset} of ${state.math} equations not typeset`);
    }
    for (const text of state.mathErrors) {
      result.failures.push(`equation error: ${text}`);
    }
    if (state.math > 0 && result.fonts.mathLoaded === 0) {
      result.failures.push("no MathJax font face loaded");
    }
    if (state.unrendered > 0) {
      result.failures.push(`${state.unrendered} of ${state.diagrams} Mermaid diagrams not rendered`);
    }
  } finally {
    await context.close();
  }
  result.ms = Date.now() - started;
  return result;
}

async function runSearch(browser, browserName, options, query) {
  const result = { browser: browserName, query, failures: [] };
  const context = await browser.newContext({
    viewport: viewportFor(options.widths[0]),
    colorScheme: options.themes[0]
  });
  try {
    await context.route((url) => isRemote(url.href), (route) => {
      result.failures.push(`request leaves the site: ${route.request().url()}`);
      return options.offline ? route.abort("internetdisconnected") : route.continue();
    });
    const tab = await context.newPage();
    await tab.goto(pathToFileURL(path.join(options.site, "index.html")).href, {
      waitUntil: "load",
      timeout: options.timeout * 1000
    });
    const ui = await (async () => {
      for (const candidate of SEARCH_UIS) {
        if (await tab.$(candidate.input)) {
          return candidate;
        }
      }
      return null;
    })();
    if (!ui) {
      result.failures.push("no known search box on the home page");
      return result;
    }
    result.theme = ui.theme;
    const started = Date.now();
    // A narrow window keeps the search box off screen behind a button, which is pressed when the box cannot be clicked
    // where it is; the box then takes the focus, which opens the search on a wide window too.
    const input = tab.locator(ui.input).first();
    const reachable = await input.evaluate((element) => {
      const box = element.getBoundingClientRect();
      const x = box.left + box.width / 2;
      const y = box.top + box.height / 2;
      if (box.width === 0 || x < 0 || y < 0 || x > window.innerWidth || y > window.innerHeight) {
        return false;
      }
      const hit = document.elementFromPoint(x, y);
      return hit === element || element.contains(hit);
    });
    if (!reachable && ui.open) {
      const opener = tab.locator(ui.open).filter({ visible: true }).first();
      if (await opener.count()) {
        await opener.click();
      }
    }
    await input.focus();
    await input.pressSequentially(query);
    try {
      await tab.waitForSelector(ui.result, { timeout: options.timeout * 1000 });
      result.ms = Date.now() - started;
      // The list may still answer a prefix of the query; it is read once it stops changing.
      const read = () => tab.evaluate((selectors) => {
        const link = document.querySelector(selectors.resultLink);
        const title = document.querySelector(selectors.resultTitle);
        return {
          title: title ? title.textContent.trim() : "",
          href: link ? link.getAttribute("href") : "",
          count: document.querySelectorAll(selectors.result).length
        };
      }, ui);
      let seen = await read();
      for (let settled = 0; settled < 2;) {
        await tab.waitForTimeout(250);
        const now = await read();
        settled = JSON.stringify(now) === JSON.stringify(seen) ? settled + 1 : 0;
        seen = now;
      }
      result.first = { title: seen.title, href: seen.href };
      result.count = seen.count;
      if (ui.group) {
        // The first result of each group of results, for a theme that groups them (by the guide's section).
        result.groups = await tab.evaluate((selectors) => Array.from(document.querySelectorAll(selectors.group))
          .map((group) => {
            const heading = group.querySelector(selectors.groupTitle);
            const title = group.querySelector("b");
            return { group: heading ? heading.textContent.trim() : "", first: title ? title.textContent.trim() : "" };
          }), ui);
      }
    } catch (error) {
      result.failures.push(`no results within ${options.timeout} s`);
    }
  } finally {
    await context.close();
  }
  return result;
}

async function pool(items, jobs, work) {
  const results = new Array(items.length);
  let next = 0;
  const worker = async () => {
    while (next < items.length) {
      const index = next;
      next += 1;
      results[index] = await work(items[index], index);
    }
  };
  await Promise.all(Array.from({ length: Math.min(jobs, items.length) }, worker));
  return results;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const playwright = loadPlaywright();
  const axe = options.a11y ? axeSource() : null;
  const pages = sitePages(options.site, options.pages);
  if (options.shots) {
    fs.mkdirSync(options.shots, { recursive: true });
  }
  fs.mkdirSync(path.dirname(options.report), { recursive: true });

  const report = {
    site: options.site,
    started: new Date().toISOString(),
    options: {
      browsers: options.browsers,
      widths: options.widths,
      themes: options.themes,
      offline: options.offline,
      a11y: options.a11y,
      pages: pages.length
    },
    browsers: {},
    loads: [],
    search: []
  };
  const combosPerBrowser = options.themes.length * options.widths.length;
  console.log(`${pages.length} pages x ${combosPerBrowser} width/theme combinations in ${options.browsers.join(", ")}`
    + `${options.offline ? ", offline" : ""}${options.a11y ? ", with axe-core" : ""}`);
  const server = pages.some((page) => SERVED_AT_ANY_ADDRESS.has(page)) ? await startServer(options.site) : null;
  if (server) {
    localOrigin = server.origin;
    console.log(`${Array.from(SERVED_AT_ANY_ADDRESS).join(", ")} served from ${server.origin}`);
  }

  try {
    for (const browserName of options.browsers) {
      const browser = await launch(playwright, browserName);
      report.browsers[browserName] = browser.version();
      try {
        const work = [];
        for (const theme of options.themes) {
          for (const width of options.widths) {
            for (const page of pages) {
              work.push({ combo: { browser: browserName, theme, width }, page });
            }
          }
        }
        let done = 0;
        let failed = 0;
        const results = await pool(work, options.jobs, async (item) => {
          let result;
          try {
            result = await renderPage(browser, item.combo, item.page, options, axe);
          } catch (error) {
            result = { page: item.page, ...item.combo, failures: [`runner error: ${error.message}`], remote: [] };
          }
          done += 1;
          if (result.failures.length > 0) {
            failed += 1;
            console.log(`FAIL ${browserName} ${item.combo.theme} ${item.combo.width} ${item.page}`);
            for (const failure of result.failures.slice(0, 8)) {
              console.log(`  - ${failure}`);
            }
            if (result.failures.length > 8) {
              console.log(`  - ... ${result.failures.length - 8} more`);
            }
          }
          if (result.shot) {
            console.log(`PNG ${result.shot.width} px ${path.basename(result.shot.file)}`);
          }
          if (done % 50 === 0 || done === work.length) {
            console.log(`${browserName}: ${done}/${work.length} loads, ${failed} failed`);
          }
          return result;
        });
        report.loads.push(...results);
        for (const query of options.search) {
          const result = await runSearch(browser, browserName, options, query).catch((error) => ({
            browser: browserName, query, failures: [`runner error: ${error.message}`]
          }));
          report.search.push(result);
          const first = result.first ? `first "${result.first.title}" (${result.first.href})` : "no result";
          console.log(`${result.failures.length ? "FAIL" : "SEARCH"} ${browserName} "${query}": `
            + `${result.count || 0} results in ${result.ms === undefined ? "-" : result.ms} ms, ${first}`);
          for (const group of result.groups || []) {
            console.log(`  first under ${group.group}: "${group.first}"`);
          }
          for (const failure of result.failures) {
            console.log(`  - ${failure}`);
          }
        }
      } finally {
        await browser.close();
      }
    }
  } finally {
    if (server) {
      server.close();
    }
  }

  report.finished = new Date().toISOString();
  const failedLoads = report.loads.filter((load) => load.failures.length > 0);
  const failedSearches = report.search.filter((search) => search.failures.length > 0);
  const remote = new Set(report.loads.flatMap((load) => load.remote));
  const pagesWith = (test) => new Set(report.loads.filter(test).map((load) => load.page));
  const mathPages = pagesWith((load) => load.math && load.math.blocks > 0);
  const diagramPages = pagesWith((load) => load.diagrams && load.diagrams.blocks > 0);
  report.summary = {
    loads: report.loads.length,
    failedLoads: failedLoads.length,
    searches: report.search.length,
    failedSearches: failedSearches.length,
    remoteRequests: Array.from(remote).sort(),
    mathPages: mathPages.size,
    diagramPages: diagramPages.size
  };
  fs.writeFileSync(options.report, JSON.stringify(report, null, 2));

  console.log(`Browsers: ${Object.entries(report.browsers).map(([name, version]) => `${name} ${version}`).join(", ")}`);
  console.log(`Loads: ${report.loads.length}, failed ${failedLoads.length}; pages with math ${mathPages.size}, `
    + `with Mermaid ${diagramPages.size}; distinct remote requests ${remote.size}`);
  if (report.search.length) {
    console.log(`Searches: ${report.search.length}, failed ${failedSearches.length}`);
  }
  console.log(`Report: ${options.report}`);
  return failedLoads.length + failedSearches.length > 0 ? 1 : 0;
}

main().then(
  (code) => process.exit(code),
  (error) => {
    if (error instanceof UsageError) {
      if (error.message) {
        console.error(`render_user_guide: ${error.message}`);
      }
      console.error("usage: node build/scripts/render_user_guide.cjs [--site <dir>] [--pages all|<list>] "
        + "[--browsers chrome,msedge,firefox] [--widths 1440,1024,390] [--themes dark,light] [--offline] [--a11y] "
        + "[--search <queries>] [--shots <dir>] [--report <file>] [--jobs <n>] [--timeout <seconds>]");
      process.exit(2);
    }
    console.error(error && error.stack ? error.stack : String(error));
    process.exit(2);
  }
);
