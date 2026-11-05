# Security policy

## Reporting a vulnerability

Report a vulnerability privately, through GitHub's private vulnerability reporting: open the repository's
**Security** tab and choose **Report a vulnerability**. The report reaches the maintainer and nobody else.

Do not open a public issue, a pull request or a discussion for a vulnerability, and do not publish it before a fix
is released or the maintainer has agreed to its disclosure.

Include what is needed to reproduce and judge it:

- the AfterNight version (the About dialog shows it) and the operating system;
- what an attacker controls - a crafted image or project file, an extension package, a catalog download - and what
  it achieves;
- the steps or a minimal file that reproduces it, and the crash dump or the log if it crashes;
- whether it is already public anywhere.

A report is acknowledged, investigated, and answered with what will be done and when. A fix is released as a new
version, and the report's author is credited in its release notes unless they ask not to be.

## Supported versions

Security fixes are made to the latest release. Once AfterNight 1.0.0 is published, that is the 1.0.x series; a fix
is released as the next version of the series, and earlier versions are not patched.

## Scope

In scope: AfterNight's own code - the application, the command-line tool, the engine, the extension hosts and the
runtime backend, the Python SDK, the installers and the scripts in this repository - and the way AfterNight uses the
libraries it ships.

A defect in a third-party library is reported to that library's project as well; say in your report whether you
have. A defect in a third-party extension is reported to its author.
