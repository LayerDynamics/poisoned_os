import "./style.css";

const app = document.querySelector<HTMLDivElement>("#app");
if (!app) throw new Error("Site root is missing");

app.innerHTML = `
  <div class="site-shell">
    <header class="topbar">
      <a class="brand" href="/" aria-label="Poisoned OS home"><span class="brand-mark">☣</span><span>Poisoned_Os</span></a>
      <nav class="topnav" aria-label="Primary navigation"><a href="/docs/">Docs</a><a href="/docs/#/getting-started">Get started</a><a class="nav-quiet" href="https://github.com/LayerDynamics/poisoned_os">GitHub <span aria-hidden="true">↗</span></a></nav>
    </header>

    <main>
      <section class="hero section-pad">
        <div class="hero-copy">
          <p class="eyebrow"><span class="signal-dot"></span> Firmware for the Flipper Zero · target 7</p>
          <h1>The whole device.<br><em>Still in your pocket.</em></h1>
          <p class="hero-lede">Poisoned_Os turns the Flipper Zero into a browser-controlled field instrument. Keep the hardware attached to its Wi-Fi Dev Board, keep your hands free, and keep every capture, run, and decision together.</p>
          <div class="hero-actions"><a class="button button-primary" href="/docs/#/getting-started">Start with the docs <span aria-hidden="true">→</span></a><a class="button button-secondary" href="https://web-installer-production.up.railway.app">Install over USB <span aria-hidden="true">↗</span></a></div>
          <p class="hero-note"><span class="note-rule"></span> Local-first by design. No account required for device control.</p>
        </div>
        <div class="hero-instrument" aria-label="Illustration of the pocket interface">
          <div class="contamination-mark" aria-hidden="true"><svg viewBox="0 0 80 80"><path d="M40 7 48 28l22 1-17 14 6 22-19-12-19 12 6-22L10 29l22-1z"/><circle cx="40" cy="37" r="8"/><path d="M40 45v28m-6-9 6 9 6-9"/></svg><span>AUTHORIZED<br>FIELD TOOL</span></div>
          <div class="instrument-label instrument-label-top">POISONED_OS / LIVE LINK</div>
          <div class="device-card"><div class="device-top"><span>FLIPPER ZERO</span><span class="device-online">● ONLINE</span></div><div class="device-display"><span class="display-cursor">_</span><strong>FIELD CASE</strong><small>4 CAPTURES · 2 NOTES</small></div><div class="device-controls"><span class="d-pad"></span><span class="control-buttons"><i></i><i></i><i></i><i></i></span></div></div>
          <div class="instrument-line line-one"></div><div class="instrument-line line-two"></div><div class="instrument-label instrument-label-bottom">BROWSER / WI-FI / DEVICE</div>
          <div class="instrument-readout"><span>SESSION</span><strong>SECURE</strong><span>↳ AUDIT CHAIN INTACT</span></div>
        </div>
      </section>

      <section class="proof-strip"><div><strong>01</strong><span>Live control</span><small>Screen, input, apps, and hardware tools from a browser.</small></div><div><strong>02</strong><span>Evidence intact</span><small>Captures keep files, notes, checksums, and export history together.</small></div><div><strong>03</strong><span>Offline core</span><small>Local device control works without a hosted account or WAN.</small></div></section>

      <section class="capabilities section-pad"><div class="section-intro"><p class="eyebrow">What the system is for</p><h2>A field instrument with a memory.</h2><p>Poisoned_Os brings the device, its work, and the record of that work into one deliberate workspace.</p></div><div class="capability-grid"><article><span class="card-index">A / 01</span><h3>Operate from the browser</h3><p>Pair once through the local runtime, then use a phone or computer for the live screen, controls, apps, files, and output.</p><a href="/docs/#/dashboard">Read the dashboard guide <span>→</span></a></article><article><span class="card-index">B / 02</span><h3>Keep the case together</h3><p>Organize captures with original bytes, notes, checksums, audit events, and portable exports that stay linked to the work.</p><a href="/docs/#/evidence">Understand evidence workflows <span>→</span></a></article><article><span class="card-index">C / 03</span><h3>Build with boundaries</h3><p>Run JavaScript projects and constrained Rust builds with declared capabilities, reproducible inputs, and visible diagnostics.</p><a href="/docs/#/development">Explore development <span>→</span></a></article></div></section>

      <section class="manifest section-pad"><div class="manifest-stamp">OS / 01<br>LOCAL FIRST</div><div><p class="eyebrow">A clear boundary</p><h2>Your device is not a cloud feature.</h2><p>Dashboard control, files, evidence export, customization, and installed workloads remain local capabilities. The standalone cable installer runs in the browser and never uploads the package it is checking.</p><a class="text-link" href="/docs/#/architecture">See the architecture decisions <span>↗</span></a></div><div class="manifest-list"><div><span>Transport</span><strong>HTTP(S) + WS(S)</strong></div><div><span>Board path</span><strong>Wi-Fi Dev Board</strong></div><div><span>Runtime</span><strong>Local Node.js</strong></div><div><span>Policy</span><strong>Explicit capabilities</strong></div></div></section>

      <section class="closing section-pad"><p class="eyebrow">Ready when you are</p><h2>Put the OS<br>where the work is.</h2><div class="closing-actions"><a class="button button-primary" href="/docs/">Read the documentation <span aria-hidden="true">→</span></a><a class="button button-secondary" href="https://github.com/LayerDynamics/poisoned_os">Inspect the source <span aria-hidden="true">↗</span></a></div></section>
    </main>
    <footer class="site-footer"><a class="brand" href="/"><span class="brand-mark">☣</span><span>Poisoned_Os</span></a><span>GPLv3 · authorized research, labs, and education</span><span>Built for Flipper Zero target 7</span></footer>
  </div>
`;
