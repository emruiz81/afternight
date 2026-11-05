"use strict";

// PowerShell setup (keeps Node dependencies outside the repository):
//   $validatorRoot = Join-Path $env:TEMP "afternight-ug65-playwright"
//   npm install --prefix $validatorRoot --no-save playwright-core@1.61.1
//   $env:NODE_PATH = Join-Path $validatorRoot "node_modules"
//   node build\scripts\validate_ug65_user_guide_diagrams.cjs

const fs = require("fs");
const path = require("path");
const { pathToFileURL } = require("url");
const { chromium } = require("playwright-core");
const playwrightPackage = require("playwright-core/package.json");

const expectedPlaywrightVersion = "1.61.1";
if (playwrightPackage.version !== expectedPlaywrightVersion) {
  throw new Error(
    `Expected playwright-core ${expectedPlaywrightVersion}, found ${playwrightPackage.version}`
  );
}

const siteRoot = path.resolve(
  process.argv[2] || path.join(__dirname, "..", "..", "docs", "user-guide", "site")
);
const edgeExecutable =
  process.env.AFTERNIGHT_UG65_BROWSER ||
  "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe";
const guideSourceRoot = path.resolve(
  __dirname,
  "..",
  "..",
  "docs",
  "user-guide",
  "docs",
  "en"
);

if (!fs.existsSync(siteRoot)) {
  throw new Error(`Built user-guide site not found: ${siteRoot}`);
}
if (!fs.existsSync(edgeExecutable)) {
  throw new Error(`Browser executable not found: ${edgeExecutable}`);
}

const pageCases = [
  { id: "home", relativePath: "index.html", kind: "html" },
  { id: "quick-start", relativePath: "getting-started/quick-start.html", kind: "mermaid" },
  { id: "supported-formats", relativePath: "getting-started/supported-formats.html", kind: "mermaid" },
  { id: "interface-overview", relativePath: "interface/overview.html", kind: "mermaid" },
  { id: "process-catalog", relativePath: "processes/index.html", kind: "mermaid" },
  {
    id: "image-integration-lite",
    relativePath: "processes/stacking-integration/image-integration-lite.html",
    kind: "mermaid"
  },
  {
    id: "image-list-analyzer",
    relativePath: "processes/stacking-integration/image-list-analyzer.html",
    kind: "mermaid"
  },
  {
    id: "image-integration",
    relativePath: "processes/stacking-integration/image-integration.html",
    kind: "mermaid"
  },
  { id: "broadband", relativePath: "workflows/broadband-imaging.html", kind: "mermaid" }
];

const viewportCases = [
  { width: 360, height: 800 },
  { width: 390, height: 844 },
  { width: 768, height: 1024 },
  { width: 1280, height: 900 }
];
const colorSchemes = ["light", "dark"];
const deferredWorkflowPaths = [
  "workflows/narrowband-imaging.html",
  "workflows/planetary-imaging.html",
  "workflows/mosaic-workflow.html"
];
const expectedColors = {
  light: {
    scheme: "default",
    label: "rgb(23, 32, 51)",
    node: "rgb(245, 247, 253)",
    nodeBorder: "rgb(0, 124, 124)",
    edge: "rgb(79, 94, 120)",
    surface: "rgb(245, 247, 253)",
    homeRaised: "rgb(255, 255, 255)",
    homeMuted: "rgb(79, 94, 120)"
  },
  dark: {
    scheme: "slate",
    label: "rgb(245, 248, 255)",
    node: "rgb(28, 37, 58)",
    nodeBorder: "rgb(111, 224, 216)",
    edge: "rgb(174, 187, 210)",
    surface: "rgb(21, 28, 46)",
    homeRaised: "rgb(28, 37, 58)",
    homeMuted: "rgb(186, 198, 220)"
  }
};

function parseRgb(color) {
  const components = color?.match(/[\d.]+/g)?.slice(0, 3).map(Number);
  return components?.length === 3 ? components : null;
}

function contrastRatio(foreground, background) {
  const foregroundRgb = parseRgb(foreground);
  const backgroundRgb = parseRgb(background);
  if (!foregroundRgb || !backgroundRgb) return 0;
  const luminance = (rgb) => {
    const linear = rgb.map((component) => {
      const channel = component / 255;
      return channel <= 0.04045 ? channel / 12.92 : ((channel + 0.055) / 1.055) ** 2.4;
    });
    return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2];
  };
  const lighter = Math.max(luminance(foregroundRgb), luminance(backgroundRgb));
  const darker = Math.min(luminance(foregroundRgb), luminance(backgroundRgb));
  return (lighter + 0.05) / (darker + 0.05);
}

async function measurePage(page, pageCase) {
  if (pageCase.kind === "mermaid") {
    await page.waitForFunction(
      () => document.querySelector(".an-mermaid svg")?.getAttribute("aria-roledescription") === "flowchart-v2",
      null,
      { timeout: 15000 }
    );
  }

  return page.evaluate(({ id, kind }) => {
    const hostSelector = kind === "mermaid" ? ".an-mermaid" : ".an-flow-diagram";
    const host = document.querySelector(hostSelector);
    if (!host) {
      return { id, kind, missingHost: true };
    }

    const hostRect = host.getBoundingClientRect();
    const hostStyle = getComputedStyle(host);
    const common = {
      id,
      kind,
      scheme: document.body.dataset.mdColorScheme,
      hostWidth: hostRect.width,
      hostHeight: hostRect.height,
      overflow: host.scrollWidth - host.clientWidth,
      surfaceColor: hostStyle.backgroundColor
    };

    if (kind === "html") {
      const fontSizes = Array.from(
        host.querySelectorAll(".an-flow-step,.an-flow-row__label,.an-flow-handoff"),
        (element) => Number.parseFloat(getComputedStyle(element).fontSize)
      );
      const semanticElements = Array.from(
        host.querySelectorAll(".an-flow-step,.an-flow-row__label,.an-flow-handoff")
      );
      const contentClipped = semanticElements.some((element) => {
        const rect = element.getBoundingClientRect();
        return (
          rect.left < hostRect.left - 1 ||
          rect.right > hostRect.right + 1 ||
          rect.top < hostRect.top - 1 ||
          rect.bottom > hostRect.bottom + 1
        );
      });
      const stepStyle = getComputedStyle(host.querySelector(".an-flow-step"));
      const labelStyle = getComputedStyle(host.querySelector(".an-flow-row__label"));
      const handoffStyle = getComputedStyle(host.querySelector(".an-flow-handoff"));
      return {
        ...common,
        minFontSize: Math.min(...fontSizes),
        accessible: Boolean(host.getAttribute("aria-label")),
        listCount: host.querySelectorAll("ol").length,
        stepCount: host.querySelectorAll("li").length,
        contentClipped,
        stepTextColor: stepStyle.color,
        stepBackgroundColor: stepStyle.backgroundColor,
        labelTextColor: labelStyle.color,
        handoffTextColor: handoffStyle.color,
        handoffBackgroundColor: handoffStyle.backgroundColor
      };
    }

    const svg = host.querySelector("svg");
    const svgRect = svg.getBoundingClientRect();
    const viewBox = svg.viewBox.baseVal;
    const contentBounds = svg.getBBox();
    const scale = viewBox.width ? svgRect.width / viewBox.width : 1;
    const effectiveFontSizes = Array.from(
      svg.querySelectorAll("text,.nodeLabel,.edgeLabel"),
      (element) => Number.parseFloat(getComputedStyle(element).fontSize) * scale
    ).filter((value) => Number.isFinite(value) && value > 0);
    const styleValue = (selector, property) => {
      const element = svg.querySelector(selector);
      return element ? getComputedStyle(element)[property] : null;
    };

    return {
      ...common,
      svgWidth: svgRect.width,
      svgHeight: svgRect.height,
      viewBoxWidth: viewBox.width,
      viewBoxHeight: viewBox.height,
      contentClipped:
        contentBounds.x < viewBox.x - 1 ||
        contentBounds.y < viewBox.y - 1 ||
        contentBounds.x + contentBounds.width > viewBox.x + viewBox.width + 1 ||
        contentBounds.y + contentBounds.height > viewBox.y + viewBox.height + 1,
      minFontSize: Math.min(...effectiveFontSizes),
      title: svg.querySelector("title")?.textContent || null,
      description: svg.querySelector("desc")?.textContent || null,
      role: svg.getAttribute("role"),
      roleDescription: svg.getAttribute("aria-roledescription"),
      labelledBy: svg.getAttribute("aria-labelledby"),
      rawCodePresent: Boolean(host.querySelector("code")),
      labelColor: styleValue(".nodeLabel", "color"),
      nodeColor: styleValue(".node rect,.node polygon,.node path", "fill"),
      nodeBorderColor: styleValue(".node rect,.node polygon,.node path", "stroke"),
      edgeColor: styleValue(".flowchart-link", "stroke")
    };
  }, pageCase);
}

function collectHtmlFiles(directory) {
  const result = [];
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const entryPath = path.join(directory, entry.name);
    if (entry.isDirectory()) result.push(...collectHtmlFiles(entryPath));
    else if (entry.isFile() && entry.name.endsWith(".html")) result.push(entryPath);
  }
  return result;
}

async function main() {
  const sourceFailures = [];
  const diagramSourceContracts = [
    {
      relativePath: "processes/stacking-integration/image-integration-lite.md",
      required: [
        "Mode{Drizzle?}",
        "Mode -- No --> Align[Write aligned<br/>lights]",
        "Mode -- Yes --> Keep[Retain<br/>transforms]",
        "Drizzle[Drizzle resampling<br/>and stacking]"
      ],
      forbidden: ["Cal --> Reg[Register and align]", "Norm --> Drizzle[Optional drizzle]"]
    },
    {
      relativePath: "processes/stacking-integration/image-integration.md",
      required: [
        "Mode{Drizzle?}",
        "Mode -- No --> Align[Write aligned<br/>lights]",
        "Mode -- Yes --> Keep[Retain<br/>transforms]",
        "Drizzle[Drizzle resampling<br/>and stacking]"
      ],
      forbidden: ["E --> F[Alignment]", "Optional drizzle, then weighted rejection and stacking"]
    }
  ];
  for (const contract of diagramSourceContracts) {
    const source = fs.readFileSync(path.join(guideSourceRoot, contract.relativePath), "utf8");
    for (const fragment of contract.required) {
      if (!source.includes(fragment)) {
        sourceFailures.push(`${contract.relativePath}: missing source-backed branch: ${fragment}`);
      }
    }
    for (const fragment of contract.forbidden) {
      if (source.includes(fragment)) {
        sourceFailures.push(`${contract.relativePath}: retains inaccurate linear stage: ${fragment}`);
      }
    }
  }
  const expectedMermaidPaths = pageCases
    .filter((pageCase) => pageCase.kind === "mermaid")
    .map((pageCase) => pageCase.relativePath.replaceAll("\\", "/"))
    .sort();
  const actualMermaidPaths = collectHtmlFiles(siteRoot)
    .filter((htmlPath) => fs.readFileSync(htmlPath, "utf8").includes('<pre class="an-mermaid"'))
    .map((htmlPath) => path.relative(siteRoot, htmlPath).replaceAll("\\", "/"))
    .sort();

  if (JSON.stringify(actualMermaidPaths) !== JSON.stringify(expectedMermaidPaths)) {
    sourceFailures.push(
      `Built Mermaid inventory differs: expected ${expectedMermaidPaths.join(", ")}; found ${actualMermaidPaths.join(", ")}`
    );
  }
  for (const relativePath of deferredWorkflowPaths) {
    const html = fs.readFileSync(path.join(siteRoot, relativePath), "utf8");
    if (/class="(?:an-)?mermaid"/.test(html)) {
      sourceFailures.push(`${relativePath}: deferred workflow still contains a diagram`);
    }
  }
  for (const runtimePath of [
    "assets/javascripts/mermaid/mermaid.min.js",
    "assets/javascripts/mermaid/mermaid-init.js",
    "assets/javascripts/mermaid/LICENSE-MIT"
  ]) {
    if (!fs.existsSync(path.join(siteRoot, runtimePath))) {
      sourceFailures.push(`Missing local runtime asset: ${runtimePath}`);
    }
  }

  const browser = await chromium.launch({ executablePath: edgeExecutable, headless: true });
  const rows = [];
  const blockedExternalRequests = new Set();
  const pageErrors = [];

  try {
    for (const viewport of viewportCases) {
      for (const colorScheme of colorSchemes) {
        const context = await browser.newContext({ viewport, colorScheme });
        await context.route(/^https?:/, (route) => {
          blockedExternalRequests.add(route.request().url());
          route.abort();
        });
        const page = await context.newPage();
        page.on("pageerror", (error) => pageErrors.push(error.message));

        for (const pageCase of pageCases) {
          const pageUrl = pathToFileURL(path.join(siteRoot, pageCase.relativePath)).href;
          await page.goto(pageUrl, { waitUntil: "load" });
          const result = await measurePage(page, pageCase);
          rows.push({ ...viewport, colorScheme, ...result });
        }

        await context.close();
      }
    }
  } finally {
    await browser.close();
  }

  const failures = [...sourceFailures];
  for (const row of rows) {
    const prefix = `${row.id} ${row.width}x${row.height} ${row.colorScheme}`;
    if (row.missingHost) failures.push(`${prefix}: diagram host missing`);
    if (row.overflow > 1) failures.push(`${prefix}: horizontal overflow ${row.overflow}px`);
    if (row.minFontSize < 12) failures.push(`${prefix}: effective text ${row.minFontSize.toFixed(2)}px`);
    if (row.contentClipped) failures.push(`${prefix}: diagram content extends outside its rendered bounds`);

    if (row.kind === "html") {
      if (!row.accessible || row.listCount !== 2 || row.stepCount !== 11) {
        failures.push(`${prefix}: semantic HTML flow contract failed`);
      }
      const expected = expectedColors[row.colorScheme];
      if (
        row.scheme !== expected.scheme ||
        row.surfaceColor !== expected.surface ||
        row.stepTextColor !== expected.label ||
        row.stepBackgroundColor !== expected.homeRaised ||
        row.labelTextColor !== expected.label ||
        row.handoffTextColor !== expected.homeMuted ||
        row.handoffBackgroundColor !== expected.homeRaised
      ) {
        failures.push(`${prefix}: semantic Home theme color contract failed`);
      }
      const homeTextContrast = Math.min(
        contrastRatio(row.stepTextColor, row.stepBackgroundColor),
        contrastRatio(row.labelTextColor, row.surfaceColor),
        contrastRatio(row.handoffTextColor, row.handoffBackgroundColor)
      );
      row.minTextContrast = homeTextContrast;
      if (homeTextContrast < 4.5) {
        failures.push(`${prefix}: Home text contrast ${homeTextContrast.toFixed(2)}:1`);
      }
      continue;
    }

    if (!row.title || !row.description || !row.labelledBy || !row.role?.includes("graphics-document")) {
      failures.push(`${prefix}: accessible title/description/role contract failed`);
    }
    if (row.roleDescription !== "flowchart-v2" || row.rawCodePresent) {
      failures.push(`${prefix}: Mermaid did not produce a flowchart SVG`);
    }

    const expected = expectedColors[row.colorScheme];
    if (
      row.scheme !== expected.scheme ||
      row.labelColor !== expected.label ||
      row.nodeColor !== expected.node ||
      row.nodeBorderColor !== expected.nodeBorder ||
      row.surfaceColor !== expected.surface ||
      row.edgeColor !== expected.edge
    ) {
      failures.push(`${prefix}: theme color contract failed`);
    }
    row.minTextContrast = contrastRatio(row.labelColor, row.nodeColor);
    row.edgeContrast = contrastRatio(row.edgeColor, row.surfaceColor);
    row.nodeBorderContrast = contrastRatio(row.nodeBorderColor, row.nodeColor);
    if (row.minTextContrast < 4.5) {
      failures.push(`${prefix}: label contrast ${row.minTextContrast.toFixed(2)}:1`);
    }
    if (row.edgeContrast < 3) {
      failures.push(`${prefix}: edge contrast ${row.edgeContrast.toFixed(2)}:1`);
    }
    if (row.nodeBorderContrast < 3) {
      failures.push(`${prefix}: node-border contrast ${row.nodeBorderContrast.toFixed(2)}:1`);
    }
  }

  if (pageErrors.length) failures.push(`Browser page errors: ${[...new Set(pageErrors)].join(" | ")}`);
  const mermaidNetworkRequests = [...blockedExternalRequests].filter((url) => /mermaid|unpkg/i.test(url));
  if (mermaidNetworkRequests.length) {
    failures.push(`External Mermaid requests: ${mermaidNetworkRequests.join(", ")}`);
  }

  const summary = {};
  for (const pageCase of pageCases) {
    const pageRows = rows.filter((row) => row.id === pageCase.id);
    summary[pageCase.id] = {
      runs: pageRows.length,
      minEffectiveFontPx: Number(Math.min(...pageRows.map((row) => row.minFontSize)).toFixed(2)),
      maxHorizontalOverflowPx: Math.max(...pageRows.map((row) => row.overflow)),
      clippedContentCases: pageRows.filter((row) => row.contentClipped).length,
      minTextContrast: Number(Math.min(...pageRows.map((row) => row.minTextContrast)).toFixed(2)),
      minEdgeContrast: pageCase.kind === "mermaid"
        ? Number(Math.min(...pageRows.map((row) => row.edgeContrast)).toFixed(2))
        : undefined,
      minNodeBorderContrast: pageCase.kind === "mermaid"
        ? Number(Math.min(...pageRows.map((row) => row.nodeBorderContrast)).toFixed(2))
        : undefined,
      mobile360: pageRows
        .filter((row) => row.width === 360 && row.colorScheme === "light")
        .map((row) => ({
          host: [Number(row.hostWidth.toFixed(1)), Number(row.hostHeight.toFixed(1))],
          svg: row.svgWidth
            ? [Number(row.svgWidth.toFixed(1)), Number(row.svgHeight.toFixed(1))]
            : undefined
        }))[0]
    };
  }

  const output = {
    status: failures.length ? "FAIL" : "PASS",
    playwrightCore: playwrightPackage.version,
    browserExecutable: edgeExecutable,
    siteRoot,
    runs: rows.length,
    blockedExternalRequestCount: blockedExternalRequests.size,
    externalMermaidRequestCount: mermaidNetworkRequests.length,
    summary,
    failures
  };
  console.log(JSON.stringify(output, null, 2));

  if (failures.length) process.exitCode = 1;
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
