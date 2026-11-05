"use strict";

(() => {
  function postMessageToParent(message, targetOrigin) {
    parent.postMessage(message, targetOrigin || "*");
  }

  function importScriptsInFrame(...urls) {
    return urls.reduce(
      (chain, url) =>
        chain.then(
          () =>
            new Promise((resolve, reject) => {
              const script = document.createElement("script");
              script.src = url;
              script.onload = resolve;
              script.onerror = () => reject(new Error(`Unable to load ${url}`));
              document.body.appendChild(script);
            }),
        ),
      Promise.resolve(),
    );
  }

  function cloneMessage(message) {
    if (typeof structuredClone === "function") {
      return structuredClone(message);
    }
    return JSON.parse(JSON.stringify(message));
  }

  function attributeEscape(value) {
    return `${value}`
      .replace(/&/g, "&amp;")
      .replace(/"/g, "&quot;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;");
  }

  class IFrameWorker extends EventTarget {
    constructor(url) {
      super();

      this.url = `${url}`;
      this.iframe = document.createElement("iframe");
      this.iframe.hidden = true;
      document.body.appendChild(this.iframe);

      this.ready = new Promise((resolve, reject) => {
        this.resolveReady = resolve;
        this.rejectReady = reject;
      });

      this.handleMessage = event => {
        if (event.source !== this.window) {
          return;
        }

        if (event.data && event.data.__iframeWorkerReady === this.url) {
          this.resolveReady();
          return;
        }

        const message = new MessageEvent("message", { data: event.data });
        this.dispatchEvent(message);
        if (this.onmessage) {
          this.onmessage(message);
        }
      };

      this.handleError = event => {
        const filename = `${event.filename || ""}`;
        if (filename && filename !== this.url && !filename.endsWith(this.url)) {
          return;
        }

        const error = new ErrorEvent("error", {
          message: event.message,
          filename,
          lineno: event.lineno,
          colno: event.colno,
          error: event.error,
        });
        this.dispatchEvent(error);
        if (this.onerror) {
          this.onerror(error);
        }
        this.rejectReady(error);
      };

      window.addEventListener("message", this.handleMessage);
      window.addEventListener("error", this.handleError);

      const workerUrl = JSON.stringify(this.url);
      const frameDocument = this.window.document;
      frameDocument.open();
      frameDocument.write(`
<!doctype html>
<html>
  <body>
    <script>
      postMessage = ${postMessageToParent};
      importScripts = ${importScriptsInFrame};
      addEventListener("error", event => {
        parent.dispatchEvent(new ErrorEvent("error", {
          filename: ${workerUrl},
          message: event.message,
          error: event.error
        }));
      });
    <\/script>
    <script src="${attributeEscape(this.url)}?${Date.now()}"><\/script>
    <script>
      parent.postMessage({ __iframeWorkerReady: ${workerUrl} }, "*");
    <\/script>
  </body>
</html>`);
      frameDocument.close();
    }

    terminate() {
      window.removeEventListener("message", this.handleMessage);
      window.removeEventListener("error", this.handleError);
      this.iframe.remove();
    }

    postMessage(message) {
      this.ready.then(() => {
        this.window.dispatchEvent(
          new MessageEvent("message", { data: cloneMessage(message) }),
        );
      });
    }

    get window() {
      return this.iframe.contentWindow;
    }
  }

  window.IFrameWorker = IFrameWorker;

  if (location.protocol === "file:") {
    window.Worker = IFrameWorker;
  }
})();
