// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz
"use strict";

// Opens sample integration reports from disk in real browsers, with the network blocked, and fails every load that
// would look or work wrong for the person a report is written for: a request that leaves the file, a console or
// page error, a picture that did not load, a page wider than the window, content that spills out of its card, a
// link that leads nowhere or does not land, a control that does nothing (the theme toggle, a chart's measure
// switch, the switch between a master's picture and its maps, a sortable table, a folded section a link leads
// into, a comparison's divider, its switch between masters and its switch to the detail), a tip that does not show,
// a serious or critical accessibility finding, and a print render that is not light or leaves a section folded.
//
// The report's suite (Test_ImageIntegration, ReportHtmlWriterTest) reads the document back as XML and proves its
// structure; this proves what a reader sees, in the browsers people open a report with, from file://.
//
// The samples are written by the suite:
//   set AFTERNIGHT_WRITE_REPORT_SAMPLES=<folder>
//   Test_ImageIntegration.exe --gtest_filter=ReportSamples.*
// Each scenario gets a folder there, laid out as an output folder's history is: history/<run>/ for the run reported
// and for each earlier run it is compared with, each holding workflow_report.html beside its report_previews/ (the
// linked form). The run reported - the latest - also holds workflow_report.single.html (the embedded form), which
// this tool copies alone into an empty folder before it opens it, since that is how a report sent to someone
// arrives. A folder that holds the two reports itself, with no history/, is taken as it is.
//
// Setup: the Node packages and the browsers of build/scripts/render_user_guide.cjs, in the same folder outside the
// repository (see that script's header): playwright-core and axe-core under %TEMP%\afternight-guide-render, with
// NODE_PATH set to its node_modules. Chrome is the installed browser; Firefox is Playwright's build.
//
// Usage:
//   node build/scripts/render_integration_report.cjs --samples <dir> [--scenarios all|<name,...>]
//        [--forms linked,embedded] [--browsers chrome,msedge,firefox] [--widths 1440,1024,390]
//        [--themes dark,light] [--no-a11y] [--no-print] [--shots <dir>] [--report <file>] [--jobs <n>]
//        [--timeout <seconds>]
//
//   --samples    the folder the suite wrote the samples into
//   --scenarios  the scenarios' folder names, comma separated; default all
//   --forms      default linked,embedded
//   --browsers   default chrome,firefox
//   --widths     window widths in CSS pixels; default 1440,1024,390
//   --themes     the emulated prefers-color-scheme; default dark,light
//   --no-a11y    skip axe-core
//   --no-print   skip the print render
//   --shots      write a full-page PNG per load, and from Chromium a PDF of the print render per document
//   --report     the JSON report; default <shots>/report.json, else integration-report-render.json in the temp folder
//   --jobs       loads at once per browser; default 4
//   --timeout    seconds a document may take to load; default 45
//
// Exit codes: 0 every load passed; 1 one failed; 2 a usage or setup error.

const fs = require("fs");
const os = require("os");
const path = require("path");
const { pathToFileURL } = require("url");

const PLAYWRIGHT_VERSION = "1.61.1";
const AXE_VERSION = "4.14.0";
const LINKED_NAME = "workflow_report.html";
const EMBEDDED_NAME = "workflow_report.single.html";
// The window width from which the report shows its contents beside the page.
const CONTENTS_FROM = 1181;

class UsageError extends Error {}

function parseArgs(argv) {
  const options = {
    samples: null,
    scenarios: "all",
    forms: ["linked", "embedded"],
    browsers: ["chrome", "firefox"],
    widths: [1440, 1024, 390],
    themes: ["dark", "light"],
    a11y: true,
    print: true,
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
      case "--samples": options.samples = path.resolve(value()); break;
      case "--scenarios": options.scenarios = value(); break;
      case "--forms": options.forms = list(value()); break;
      case "--browsers": options.browsers = list(value()); break;
      case "--widths": options.widths = list(value()).map(Number); break;
      case "--themes": options.themes = list(value()); break;
      case "--no-a11y": options.a11y = false; break;
      case "--no-print": options.print = false; break;
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
  if (!options.samples) {
    throw new UsageError("--samples names the folder the suite wrote the sample reports into");
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
  for (const form of options.forms) {
    if (!["linked", "embedded"].includes(form)) {
      throw new UsageError(`unknown form ${form}; use linked or embedded`);
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
      : path.join(os.tmpdir(), "integration-report-render.json");
  }
  return options;
}

function loadPlaywright() {
  let packageJson;
  try {
    packageJson = require.resolve("playwright-core/package.json");
  } catch (error) {
    throw new UsageError(
      "playwright-core is not installed; follow the setup in render_user_guide.cjs's header and set NODE_PATH");
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
    throw new UsageError("axe-core is not installed (see render_user_guide.cjs's header); or pass --no-a11y");
  }
  const version = require(packageJson).version;
  if (version !== AXE_VERSION) {
    throw new UsageError(`expected axe-core ${AXE_VERSION}, found ${version}`);
  }
  return fs.readFileSync(path.join(path.dirname(packageJson), "axe.min.js"), "utf8");
}

// The folder of the run a sample reports: the latest run of its history/ that holds a report, or the sample's own
// folder when the reports lie in it. Null when the sample holds no report.
function reportedRun(sample) {
  const history = path.join(sample, "history");
  if (fs.existsSync(history) && fs.statSync(history).isDirectory()) {
    const runs = fs.readdirSync(history, { withFileTypes: true })
      .filter((entry) => entry.isDirectory() && fs.existsSync(path.join(history, entry.name, LINKED_NAME)))
      .map((entry) => entry.name)
      .sort();
    if (runs.length > 0) {
      return path.join(history, runs[runs.length - 1]);
    }
  }
  return fs.existsSync(path.join(sample, LINKED_NAME)) ? sample : null;
}

// The documents to open: each scenario's linked report where it lies, and its embedded one copied alone into an
// empty folder of its own.
function sampleDocuments(options, scratch) {
  if (!fs.existsSync(options.samples) || !fs.statSync(options.samples).isDirectory()) {
    throw new UsageError(`no folder ${options.samples}`);
  }
  const present = fs.readdirSync(options.samples, { withFileTypes: true })
    .filter((entry) => entry.isDirectory() && reportedRun(path.join(options.samples, entry.name)) !== null)
    .map((entry) => entry.name)
    .sort();
  let scenarios = present;
  if (options.scenarios !== "all") {
    scenarios = options.scenarios.split(",").map((name) => name.trim()).filter(Boolean);
    for (const scenario of scenarios) {
      if (!present.includes(scenario)) {
        throw new UsageError(`no sample ${scenario} in ${options.samples}`);
      }
    }
  }
  if (scenarios.length === 0) {
    throw new UsageError(`no sample report in ${options.samples}; write them with the suite first`);
  }
  const documents = [];
  for (const scenario of scenarios) {
    const folder = reportedRun(path.join(options.samples, scenario));
    if (options.forms.includes("linked")) {
      documents.push({ scenario, form: "linked", file: path.join(folder, LINKED_NAME), folder });
    }
    if (options.forms.includes("embedded")) {
      const source = path.join(folder, EMBEDDED_NAME);
      if (!fs.existsSync(source)) {
        throw new UsageError(`${scenario} has no ${EMBEDDED_NAME}`);
      }
      const alone = path.join(scratch, scenario);
      fs.mkdirSync(alone, { recursive: true });
      const file = path.join(alone, "report.html");
      fs.copyFileSync(source, file);
      documents.push({ scenario, form: "embedded", file, folder: alone });
    }
  }
  return documents;
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

async function pool(items, size, worker) {
  const results = new Array(items.length);
  let next = 0;
  const run = async () => {
    while (next < items.length) {
      const index = next;
      next += 1;
      results[index] = await worker(items[index]);
    }
  };
  await Promise.all(Array.from({ length: Math.min(size, items.length) }, run));
  return results;
}

// Runs in the page once it has loaded: what the load is judged on before anything is clicked.
function inspect() {
  const root = document.documentElement;
  const name = (element) => element.tagName.toLowerCase()
    + (element.id ? `#${element.id}` : "")
    + (typeof element.className === "string" && element.className.trim()
      ? `.${element.className.trim().split(/\s+/).slice(0, 2).join(".")}` : "");

  const overflow = root.scrollWidth - root.clientWidth;
  const wide = [];
  if (overflow > 0) {
    for (const element of document.body.querySelectorAll("*")) {
      const box = element.getBoundingClientRect();
      if (box.width > 0 && box.right > root.clientWidth + 1) {
        const parent = element.parentElement && element.parentElement.getBoundingClientRect();
        if (!parent || parent.right <= root.clientWidth + 1) {
          wide.push(`${name(element)} (right edge ${Math.round(box.right)} px)`);
          if (wide.length >= 3) {
            break;
          }
        }
      }
    }
  }

  // Content that reaches past the card that holds it. What lies inside a container that scrolls sideways is meant
  // to, and what lies inside an svg is placed by its viewBox.
  const spilled = [];
  const scrolls = (element, container) => {
    for (let at = element; at && at !== container; at = at.parentElement) {
      const style = getComputedStyle(at);
      if (/(auto|scroll|hidden)/.test(style.overflowX) || at.tagName.toLowerCase() === "svg") {
        return true;
      }
    }
    return false;
  };
  for (const container of document.querySelectorAll(
    ".card, .inner, .tile, .issue, .hero, .mcard, .calm, .setcard, .alert, .fbody"
  )) {
    const frame = container.getBoundingClientRect();
    if (frame.width === 0) {
      continue;
    }
    for (const element of container.querySelectorAll("*")) {
      const box = element.getBoundingClientRect();
      if (box.width === 0 || box.height === 0) {
        continue;
      }
      if ((box.right > frame.right + 1.5 || box.left < frame.left - 1.5) && !scrolls(element.parentElement, container)) {
        spilled.push(`${name(element)} out of ${name(container)} by `
          + `${Math.round(Math.max(box.right - frame.right, frame.left - box.left))} px`);
        break;
      }
    }
    if (spilled.length >= 3) {
      break;
    }
  }

  const ids = new Set(Array.from(document.querySelectorAll("[id]")).map((element) => element.id));
  const dangling = Array.from(document.querySelectorAll('a[href^="#"]'))
    .map((link) => link.getAttribute("href"))
    .filter((href) => !ids.has(decodeURIComponent(href.slice(1))));
  const outward = Array.from(document.querySelectorAll("a[href], link[href], [src]"))
    .map((element) => element.getAttribute("href") || element.getAttribute("src"))
    .filter((address) => /^(https?:)?\/\//i.test(address));

  return {
    overflow,
    wide,
    spilled,
    dangling,
    outward,
    images: document.images.length,
    brokenImages: Array.from(document.images)
      .filter((image) => !image.complete || image.naturalWidth === 0)
      .map((image) => (image.getAttribute("src") || "").slice(0, 80)),
    charts: document.querySelectorAll("svg.chart").length,
    details: document.querySelectorAll("details").length
  };
}

// Clicks every entry of the contents and says which did not bring its target into the window.
async function contentsThatDoNotLand(tab) {
  const links = await tab.locator("nav.toc a").all();
  const missed = [];
  for (const link of links) {
    const href = await link.getAttribute("href");
    await link.click();
    const landed = await tab.waitForFunction((address) => {
      const target = document.getElementById(decodeURIComponent(address.slice(1)));
      if (!target || location.hash !== address) {
        return false;
      }
      const box = target.getBoundingClientRect();
      // At the top of the window, or as near it as the end of the page lets it come.
      const atEnd = window.innerHeight + window.scrollY >= document.documentElement.scrollHeight - 2;
      return (box.top >= -2 && box.top < window.innerHeight * 0.5) || (atEnd && box.top < window.innerHeight);
    }, href, { timeout: 4000 }).then(() => true, () => false);
    if (!landed) {
      missed.push(href);
    }
  }
  await tab.evaluate(() => window.scrollTo(0, 0));
  return missed;
}

// Exercises what the report's script and stylesheet let a reader do, where the document has it. Returns failures.
async function exercise(tab, width) {
  const failures = [];

  // The theme toggle: the page changes colour, and changes back.
  const toggle = tab.locator("[data-theme-toggle]").first();
  if (await toggle.count() === 0 || !(await toggle.isVisible())) {
    failures.push("the theme toggle is not shown");
  } else {
    const colour = () => tab.evaluate(() => getComputedStyle(document.body).backgroundColor);
    const before = await colour();
    await toggle.click();
    const after = await colour();
    const chosen = await tab.evaluate(() => document.documentElement.getAttribute("data-theme"));
    if (after === before || !["light", "dark"].includes(chosen)) {
      failures.push(`the theme toggle did nothing (data-theme ${chosen}, background ${before} then ${after})`);
    }
    await toggle.click();
    if (await colour() !== before) {
      failures.push("the theme toggle did not come back to the theme it left");
    }
  }

  // A tip: pointing at something that carries one shows it.
  const tipped = tab.locator("[data-tip]").filter({ visible: true }).first();
  if (await tipped.count() > 0) {
    await tipped.scrollIntoViewIfNeeded();
    // The page scrolls smoothly, so the element may still be moving when the pointer first reaches it: the pointer
    // is brought back to where the element has come to rest until the tip shows.
    let shown = false;
    for (let attempt = 0; attempt < 8 && !shown; attempt += 1) {
      const box = await tipped.boundingBox();
      if (box) {
        await tab.mouse.move(box.x + box.width / 2 + (attempt % 2), box.y + box.height / 2);
      }
      shown = await tab.waitForFunction(() => {
        const tip = document.querySelector(".tip");
        return tip && !tip.hidden && tip.textContent.trim().length > 0;
      }, null, { timeout: 500 }).then(() => true, () => false);
    }
    if (!shown) {
      failures.push("pointing at an element with a tip showed none");
    }
    await tab.mouse.move(0, 0);
  }

  // What a master's file folds away is opened before it is used, as a reader would open it.
  const unfold = async (element) => {
    await element.evaluate((node) => {
      for (let fold = node.closest("details"); fold; fold = fold.parentElement && fold.parentElement.closest("details")) {
        fold.open = true;
      }
    });
  };

  // A master's picture and its maps: picking the second view shows it, with its reading, and hides the first.
  const viewers = tab.locator(".viewer").filter({ has: tab.locator(".views label:nth-child(2)") });
  if (await viewers.count() > 0) {
    const viewer = viewers.first();
    await unfold(viewer);
    await viewer.locator(".views label:nth-child(2)").click();
    const shown = await viewer.evaluate((element) => ({
      first: getComputedStyle(element.querySelector(".view-0")).display,
      second: getComputedStyle(element.querySelector(".view-1")).display,
      firstReading: getComputedStyle(element.querySelector(".read-0")).display,
      secondReading: getComputedStyle(element.querySelector(".read-1")).display
    }));
    if (shown.first !== "none" || shown.second === "none" || shown.firstReading !== "none"
      || shown.secondReading === "none") {
      failures.push(`the switch between a master's views did nothing (views ${shown.first}, ${shown.second}; `
        + `readings ${shown.firstReading}, ${shown.secondReading})`);
    }
    await viewer.locator(".views label:nth-child(1)").click();
    const back = await viewer.evaluate((element) => getComputedStyle(element.querySelector(".view-0")).display);
    if (back === "none") {
      failures.push("the switch between a master's views did not come back to the master");
    }
  }

  // A chart of several measures: picking the second shows it and hides the first.
  const switches = tab.locator(".fchart").filter({ has: tab.locator(".switch label:nth-child(2)") });
  if (await switches.count() > 0) {
    const chart = switches.first();
    await unfold(chart);
    await chart.locator(".switch label:nth-child(2)").click();
    const layers = await chart.evaluate((element) => ({
      first: getComputedStyle(element.querySelector(".c-layer-0")).display,
      second: getComputedStyle(element.querySelector(".c-layer-1")).display
    }));
    if (layers.first !== "none" || layers.second === "none") {
      failures.push(`the measure switch did nothing (layers ${layers.first}, ${layers.second})`);
    }
    await chart.locator(".switch label:nth-child(1)").click();
  }

  // A sortable table: a click on a heading puts its rows in that column's order.
  const sortable = tab.locator("table.sortable").first();
  if (await sortable.count() > 0) {
    const fold = sortable.locator("xpath=ancestor::details[not(@open)]");
    for (const closed of await fold.all()) {
      await closed.evaluate((element) => { element.open = true; });
    }
    const heading = sortable.locator("thead th").first();
    await heading.click();
    const sorted = await sortable.evaluate((table) => {
      const key = (cell) => (cell.hasAttribute("data-v") ? cell.getAttribute("data-v") : cell.textContent);
      const values = Array.from(table.tBodies[0].rows).map((row) => key(row.cells[0]));
      const numbers = values.map(parseFloat);
      const ordered = numbers.every((value, index) => index === 0 || Number.isNaN(value)
        || Number.isNaN(numbers[index - 1]) || numbers[index - 1] <= value);
      return { ordered, marked: table.querySelector("thead th").getAttribute("aria-sort") };
    });
    if (!sorted.ordered || sorted.marked !== "ascending") {
      failures.push(`sorting a table did nothing (aria-sort ${sorted.marked})`);
    }
  }

  // A comparison: its divider follows its range input.
  const comparison = tab.locator("[data-compare]").filter({ has: tab.locator('input[type="range"]') }).first();
  if (await comparison.count() > 0) {
    const moved = await comparison.evaluate((element) => {
      const range = element.querySelector('input[type="range"]');
      range.value = "25";
      range.dispatchEvent(new Event("input", { bubbles: true }));
      return element.style.getPropertyValue("--pos");
    });
    if (moved.trim() !== "25%") {
      failures.push(`a comparison's divider did not move (--pos "${moved}")`);
    }
  }

  // A comparison of several masters: picking the second shows it and hides the first, and picking the first again
  // brings it back.
  const compared = tab.locator(".cmp").filter({ has: tab.locator(".masters label:nth-child(2)") });
  if (await compared.count() > 0) {
    const comparison = compared.first();
    const shown = () => comparison.evaluate((element) => ({
      first: getComputedStyle(element.querySelector(".cmpitem-0")).display,
      second: getComputedStyle(element.querySelector(".cmpitem-1")).display
    }));
    await comparison.locator(".masters label:nth-child(2)").click();
    const picked = await shown();
    if (picked.first !== "none" || picked.second === "none") {
      failures.push(`a comparison's switch between masters did nothing (masters ${picked.first}, ${picked.second})`);
    }
    await comparison.locator(".masters label:nth-child(1)").click();
    const back = await shown();
    if (back.first === "none" || back.second !== "none") {
      failures.push("a comparison's switch between masters did not come back to the first master");
    }
  }

  // A comparison with a detail: the switch shows the detail of both runs in the place of the whole frames, at a
  // size, and shows the whole frames again.
  const detailed = tab.locator(".cmp").filter({ has: tab.locator(".zooms label:nth-child(2)") });
  if (await detailed.count() > 0) {
    const comparison = detailed.first();
    // The master the detail is tried on is the first that has one, picked as a reader would pick it.
    const place = await comparison.evaluate((element) => Array.from(element.querySelectorAll(".cmpitem"))
      .findIndex((item) => item.querySelector(".has-detail")));
    const masters = comparison.locator(".masters label");
    if (await masters.count() > place && place >= 0) {
      await masters.nth(place).click();
    }
    const views = () => comparison.evaluate((element, index) => {
      const pair = element.querySelectorAll(".cmpitem")[index].querySelector(".has-detail");
      const visible = (selector) => Array.from(pair.querySelectorAll(selector))
        .filter((picture) => getComputedStyle(picture).display !== "none").length;
      return { whole: visible(".whole"), detail: visible(".detail"), height: pair.getBoundingClientRect().height };
    }, place);
    await comparison.locator(".zooms label:nth-child(2)").click();
    const zoomed = await views();
    if (zoomed.whole !== 0 || zoomed.detail !== 2 || zoomed.height < 20) {
      failures.push(`a comparison's switch to the detail did nothing (whole ${zoomed.whole}, detail ${zoomed.detail}, `
        + `${Math.round(zoomed.height)} px high)`);
    }
    await comparison.locator(".zooms label:nth-child(1)").click();
    const whole = await views();
    if (whole.whole !== 2 || whole.detail !== 0 || whole.height < 20) {
      failures.push(`a comparison's switch did not come back to the whole frame (whole ${whole.whole}, `
        + `detail ${whole.detail})`);
    }
    if (await masters.count() > 0) {
      await masters.first().click();
    }
  }

  // A link into a folded section opens it.
  const folded = await tab.evaluate(() => {
    for (const link of document.querySelectorAll('a[href^="#"]')) {
      const target = document.getElementById(decodeURIComponent(link.getAttribute("href").slice(1)));
      const fold = target && target.closest("details:not([open])");
      if (fold && link.offsetParent !== null && !link.closest("details:not([open])")) {
        return link.getAttribute("href");
      }
    }
    return null;
  });
  if (folded) {
    await tab.locator(`a[href="${folded}"]`).filter({ visible: true }).first().click();
    const opened = await tab.waitForFunction((address) => {
      const target = document.getElementById(decodeURIComponent(address.slice(1)));
      return target && !target.closest("details:not([open])");
    }, folded, { timeout: 3000 }).then(() => true, () => false);
    if (!opened) {
      failures.push(`the link ${folded} did not open the section it leads into`);
    }
  }

  // The contents, where the window is wide enough to show them.
  if (width >= CONTENTS_FROM) {
    if (!(await tab.locator("nav.toc").isVisible())) {
      failures.push("the contents are not shown at this width");
    } else {
      for (const href of await contentsThatDoNotLand(tab)) {
        failures.push(`the contents link ${href} does not land`);
      }
    }
  } else if (await tab.locator("nav.toc").isVisible()) {
    failures.push("the contents are shown at a width they should give way at");
  }
  return failures;
}

// The print render: light whatever the reader's theme, nothing folded, nothing that only a screen can use.
async function printed(tab, pdf) {
  const failures = [];
  await tab.emulateMedia({ media: "print" });
  await tab.evaluate(() => window.dispatchEvent(new Event("beforeprint")));
  const state = await tab.evaluate(() => {
    const shown = (selector) => Array.from(document.querySelectorAll(selector))
      .filter((element) => getComputedStyle(element).display !== "none").length;
    const channels = getComputedStyle(document.body).backgroundColor.match(/[\d.]+/g).map(Number);
    return {
      closed: document.querySelectorAll("details:not([open])").length,
      contents: shown("nav.toc"),
      screenOnly: shown(".noprint"),
      // The page is light when its background is.
      light: (channels[0] + channels[1] + channels[2]) / 3 > 200,
      layers: Array.from(document.querySelectorAll("svg.chart")).filter((chart) => {
        const layers = Array.from(chart.querySelectorAll(".c-layer"));
        return layers.length > 0 && layers.filter((layer) => getComputedStyle(layer).display !== "none").length !== 1;
      }).length,
      // Every master of a comparison is printed, not the one a switch last showed.
      comparisons: Array.from(document.querySelectorAll(".cmpitem"))
        .filter((item) => getComputedStyle(item).display === "none").length
    };
  });
  if (state.closed > 0) {
    failures.push(`print: ${state.closed} folded sections stayed closed`);
  }
  if (state.contents > 0 || state.screenOnly > 0) {
    failures.push(`print: the contents or ${state.screenOnly} screen-only elements are shown`);
  }
  if (!state.light) {
    failures.push("print: the page is not light");
  }
  if (state.layers > 0) {
    failures.push(`print: ${state.layers} charts of several measures do not show exactly one`);
  }
  if (state.comparisons > 0) {
    failures.push(`print: ${state.comparisons} masters of a comparison are not shown`);
  }
  if (pdf) {
    await tab.pdf({ path: pdf, format: "A4", printBackground: true });
  }
  await tab.emulateMedia({ media: "screen" });
  return failures;
}

async function renderDocument(browser, combo, document, options, axe) {
  const started = Date.now();
  const result = {
    scenario: document.scenario,
    form: document.form,
    browser: combo.browser,
    theme: combo.theme,
    width: combo.width,
    failures: []
  };
  const context = await browser.newContext({
    viewport: viewportFor(combo.width),
    colorScheme: combo.theme,
    deviceScaleFactor: 1
  });
  try {
    const own = pathToFileURL(document.file).href;
    const beside = `${pathToFileURL(document.folder).href}/`;
    const outside = new Set();
    // Nothing leaves the machine, and nothing is waited for either.
    await context.route((url) => !/^(file|data|about|blob):/i.test(url.href), (route) => {
      outside.add(route.request().url());
      return route.abort("internetdisconnected");
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
    tab.on("request", (request) => {
      const url = request.url();
      if (/^file:/i.test(url) && url.split(/[?#]/)[0] !== own) {
        // The embedded form reads no file but itself; the linked one, only files of its own folder.
        if (document.form === "embedded" || !url.startsWith(beside)) {
          outside.add(url);
        }
      }
    });
    tab.on("requestfailed", (request) => {
      if (/^file:/i.test(request.url())) {
        const failure = request.failure();
        missing.push(`${request.url()} (${failure ? failure.errorText : "failed"})`);
      }
    });

    try {
      await tab.goto(own, { waitUntil: "load", timeout: options.timeout * 1000 });
    } catch (error) {
      result.failures.push(`did not load: ${String(error.message).split("\n")[0]}`);
    }
    await tab.evaluate(async () => {
      for (const image of document.images) {
        image.loading = "eager";
      }
      const deadline = Date.now() + 10000;
      while (!Array.from(document.images).every((image) => image.complete) && Date.now() < deadline) {
        await new Promise((resolve) => setTimeout(resolve, 50));
      }
      await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    });

    const state = await tab.evaluate(inspect);
    result.images = state.images;
    result.charts = state.charts;
    result.overflow = state.overflow;
    if (state.overflow > 0) {
      result.failures.push(`${state.overflow} px wider than the window: ${state.wide.join(", ") || "no single element"}`);
    }
    for (const text of state.spilled) {
      result.failures.push(`content spills out of its card: ${text}`);
    }
    for (const image of state.brokenImages) {
      result.failures.push(`picture did not load: ${image}`);
    }
    for (const href of new Set(state.dangling)) {
      result.failures.push(`link leads nowhere: ${href}`);
    }
    for (const address of new Set(state.outward)) {
      result.failures.push(`the document names an address outside itself: ${address}`);
    }
    if (document.form === "embedded" && state.images > 0) {
      const linked = await tab.evaluate(() => Array.from(document.images)
        .filter((image) => !(image.getAttribute("src") || "").startsWith("data:")).length);
      if (linked > 0) {
        result.failures.push(`${linked} pictures of the embedded form are not inside it`);
      }
    }

    if (options.shots) {
      const name = `${document.scenario}-${document.form}-${combo.browser}-${combo.theme}-${combo.width}.png`;
      const file = path.join(options.shots, name);
      await tab.screenshot({ path: file, fullPage: true });
      result.shot = file;
    }

    if (options.a11y) {
      await tab.addScriptTag({ content: axe });
      const violations = await tab.evaluate(async () => {
        // No preload: axe would ask for the stylesheets over the network, and the document has none to ask for.
        const run = await window.axe.run(document, { resultTypes: ["violations"], preload: false });
        return run.violations.map((violation) => ({
          id: violation.id,
          impact: violation.impact,
          nodes: violation.nodes.length,
          target: violation.nodes.slice(0, 3).map((node) => node.target.join(" "))
        }));
      });
      result.a11y = violations;
      for (const violation of violations.filter((v) => v.impact === "serious" || v.impact === "critical")) {
        result.failures.push(`a11y ${violation.id} (${violation.impact}, ${violation.nodes} nodes: `
          + `${violation.target.join(" | ")})`);
      }
    }

    result.failures.push(...await exercise(tab, combo.width));

    if (options.print) {
      const pdf = options.shots && combo.browser !== "firefox" && combo.width === options.widths[0]
        ? path.join(options.shots, `${document.scenario}-${document.form}-${combo.theme}-print.pdf`)
        : null;
      result.failures.push(...await printed(tab, pdf));
      if (pdf) {
        result.pdf = pdf;
      }
    }

    for (const url of outside) {
      result.failures.push(`request leaves the file: ${url}`);
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
  } finally {
    await context.close();
  }
  result.ms = Date.now() - started;
  return result;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const playwright = loadPlaywright();
  const axe = options.a11y ? axeSource() : null;
  const scratch = fs.mkdtempSync(path.join(os.tmpdir(), "afternight-report-render-"));
  let code = 0;
  try {
    const documents = sampleDocuments(options, scratch);
    if (options.shots) {
      fs.mkdirSync(options.shots, { recursive: true });
    }
    fs.mkdirSync(path.dirname(options.report), { recursive: true });

    const report = {
      samples: options.samples,
      started: new Date().toISOString(),
      options: {
        browsers: options.browsers,
        widths: options.widths,
        themes: options.themes,
        forms: options.forms,
        a11y: options.a11y,
        print: options.print,
        documents: documents.length
      },
      browsers: {},
      loads: []
    };
    console.log(`${documents.length} documents x ${options.themes.length * options.widths.length} width/theme `
      + `combinations in ${options.browsers.join(", ")}, the network blocked`
      + `${options.a11y ? ", with axe-core" : ""}${options.print ? ", with a print render" : ""}`);

    for (const browserName of options.browsers) {
      const browser = await launch(playwright, browserName);
      report.browsers[browserName] = browser.version();
      try {
        const work = [];
        for (const theme of options.themes) {
          for (const width of options.widths) {
            for (const document of documents) {
              work.push({ combo: { browser: browserName, theme, width }, document });
            }
          }
        }
        let done = 0;
        let failed = 0;
        const results = await pool(work, options.jobs, async (item) => {
          let result;
          try {
            result = await renderDocument(browser, item.combo, item.document, options, axe);
          } catch (error) {
            result = {
              scenario: item.document.scenario,
              form: item.document.form,
              ...item.combo,
              failures: [`runner error: ${String(error.message).split("\n")[0]}`]
            };
          }
          done += 1;
          if (result.failures.length > 0) {
            failed += 1;
            console.log(`FAIL ${browserName} ${item.combo.theme} ${item.combo.width} `
              + `${item.document.scenario} (${item.document.form})`);
            for (const failure of result.failures.slice(0, 8)) {
              console.log(`  - ${failure}`);
            }
            if (result.failures.length > 8) {
              console.log(`  - ... ${result.failures.length - 8} more`);
            }
          }
          if (done % 50 === 0 || done === work.length) {
            console.log(`${browserName}: ${done}/${work.length} loads, ${failed} failed`);
          }
          return result;
        });
        report.loads.push(...results);
      } finally {
        await browser.close();
      }
    }

    report.finished = new Date().toISOString();
    const failedLoads = report.loads.filter((load) => load.failures.length > 0);
    const findings = {};
    for (const load of report.loads) {
      for (const violation of load.a11y || []) {
        findings[violation.impact] = (findings[violation.impact] || 0) + 1;
      }
    }
    report.summary = {
      loads: report.loads.length,
      failedLoads: failedLoads.length,
      scenarios: new Set(report.loads.map((load) => load.scenario)).size,
      a11yFindingsByImpact: findings
    };
    fs.writeFileSync(options.report, JSON.stringify(report, null, 2));

    console.log(`Browsers: ${Object.entries(report.browsers).map(([name, version]) => `${name} ${version}`).join(", ")}`);
    console.log(`Loads: ${report.loads.length}, failed ${failedLoads.length}; scenarios ${report.summary.scenarios}; `
      + `axe findings by impact ${JSON.stringify(findings)}`);
    console.log(`Report: ${options.report}`);
    code = failedLoads.length > 0 ? 1 : 0;
  } finally {
    fs.rmSync(scratch, { recursive: true, force: true });
  }
  return code;
}

main().then(
  (code) => process.exit(code),
  (error) => {
    if (error instanceof UsageError) {
      if (error.message) {
        console.error(`render_integration_report: ${error.message}`);
      }
      console.error("usage: node build/scripts/render_integration_report.cjs --samples <dir> [--scenarios all|<list>] "
        + "[--forms linked,embedded] [--browsers chrome,msedge,firefox] [--widths 1440,1024,390] "
        + "[--themes dark,light] [--no-a11y] [--no-print] [--shots <dir>] [--report <file>] [--jobs <n>] "
        + "[--timeout <seconds>]");
      process.exit(2);
    }
    console.error(error && error.stack ? error.stack : String(error));
    process.exit(2);
  }
);
