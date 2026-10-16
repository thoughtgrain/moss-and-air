/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Bryo site behaviour. No framework, no build step.
 *
 * Every feature finds its own elements and returns quietly when they aren't on the page, so one file
 * serves both index.html and docs.html. Without JavaScript the pages still work: the menu links are
 * plain anchors (the footer repeats them), the first tab's panel is visible, and the hero shows the
 * boot screen.
 */
(function () {
  "use strict";

  var reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

  function $(sel, root) { return (root || document).querySelector(sel); }
  function $$(sel, root) { return Array.prototype.slice.call((root || document).querySelectorAll(sel)); }

  /* ---------- menu: a <dialog>, so focus is trapped and Escape closes it natively ---------- */
  function initMenu() {
    var menu = $("#menu");
    var openBtn = $("[data-menu-open]");
    if (!menu || !openBtn) return;

    // Very old browsers without <dialog>: send the button to the footer's links instead.
    if (typeof menu.showModal !== "function") {
      openBtn.addEventListener("click", function () {
        var footer = $(".site-footer");
        if (footer) footer.scrollIntoView();
      });
      return;
    }

    openBtn.addEventListener("click", function () {
      menu.showModal();
      openBtn.setAttribute("aria-expanded", "true");
    });

    menu.addEventListener("close", function () {
      openBtn.setAttribute("aria-expanded", "false");
      openBtn.focus();
    });

    $$("[data-menu-close]", menu).forEach(function (btn) {
      btn.addEventListener("click", function () { menu.close(); });
    });

    // A link to a spot on this page has to close the menu, or the dialog stays over it.
    $$("a", menu).forEach(function (a) {
      a.addEventListener("click", function () { menu.close(); });
    });
  }

  /* ---------- hero: cycle through real screens ----------
   * One <img> whose source changes, and every screen loaded before the first change. (It was a stack of
   * cross-faded images, lazily loaded, under a 3D tilt; Safari on iOS didn't always paint the ones after the
   * first.) It moves on its own, so WCAG 2.2.2 wants a way to stop it: the Pause button. With reduced motion
   * it starts paused. */
  function initScreens() {
    var root = $("[data-screens]");
    if (!root) return;
    var img = $("[data-screen-img]", root);
    var label = $("[data-screen-label]", root);
    var toggle = $("[data-screen-toggle]", root);
    var list;
    try { list = JSON.parse(root.getAttribute("data-list") || "[]"); } catch (e) { list = []; }
    if (!img || !toggle || list.length < 2) {
      if (toggle) toggle.hidden = true;
      return;
    }

    var ready = list.map(function () { return false; });
    ready[0] = true;
    list.forEach(function (s, n) {
      if (n === 0) return;
      var pre = new Image();
      pre.onload = function () { ready[n] = true; };
      pre.src = s[0];
    });

    var i = 0;
    var timer = null;
    var INTERVAL = 2600;

    function show(n) {
      // skip a screen that hasn't arrived yet rather than flash a broken image
      for (var k = 0; k < list.length; k++) {
        var j = (n + k) % list.length;
        if (ready[j]) { n = j; break; }
      }
      i = n;
      img.src = list[i][0];
      img.alt = list[i][2] || "";
      if (label) label.textContent = list[i][1] || "";
    }

    function play() {
      if (timer) return;
      timer = window.setInterval(function () { show((i + 1) % list.length); }, INTERVAL);
      toggle.textContent = "Pause";
      toggle.setAttribute("aria-pressed", "false");
    }

    function pause() {
      window.clearInterval(timer);
      timer = null;
      toggle.textContent = "Play";
      toggle.setAttribute("aria-pressed", "true");
    }

    toggle.addEventListener("click", function () { timer ? pause() : play(); });

    // Don't spin while the tab is hidden; pick up again only if it was running.
    var wasPlaying = false;
    document.addEventListener("visibilitychange", function () {
      if (document.hidden) { wasPlaying = !!timer; pause(); }
      else if (wasPlaying) play();
    });

    if (reduceMotion.matches) pause(); else play();
  }

  /* ---------- the walkthrough: the FM-1's panel, played step by step ----------
   * The panel drawing (img/panel.svg, made by tools/gen_panel.py) is fetched and put inline so its controls can
   * be lit: every control is <g id="ctl-NAME"> with an outline rect.hit, a knob has a pointer rect.ptr. Each step
   * presses (a flash), holds (lit for the step) or turns (the pointer moves to an angle) controls, then shows the
   * screen Bryo shows after it, on the panel's own screen and the big one beside it. If the fetch fails, the
   * <img> stays and the steps and screen still work. It plays by itself only while it's on screen, never with
   * reduced motion, and stops for good once you step through it by hand. */
  var TOUR = {
    pages: {
      start: {},
      steps: [
        { press: ["HOME"], screen: "tape", label: "Home / Tape", alt: "The TAPE page",
          say: "Press HOME. You're on the track's source, here the TAPE page." },
        { press: ["EDIT"], screen: "grain", label: "Edit / Grain", alt: "GRAIN's first page",
          say: "Press EDIT. GRAIN, the first effect in the line." },
        { press: ["FX"], screen: "color", label: "FX / Color", alt: "COLOR's first page",
          say: "Press FX. COLOR, where the drive and the crush live." },
        { press: ["FX", "FX", "FX"], screen: "space", label: "FX / Chronos", alt: "CHRONOS: TIME FDBK DLY VERB",
          say: "Press FX three more times to step through to CHRONOS, the echoes and rooms." },
        { press: ["GLO"], screen: "mixer", label: "Glo / Mixer", alt: "The mixer: four track levels",
          say: "Tap GLO. The mixer shows all four tracks at once." },
        { press: ["ARP"], screen: "arp", label: "ARP", alt: "The arpeggiator's page",
          say: "Press ARP for the arpeggiator. Every pad works this way: press it again for its next page." }
      ]
    },
    tape: {
      start: { K1: -135 },
      steps: [
        { press: ["HOME"], screen: "tape", label: "Home / Tape", alt: "TAPE's first page: STRT LEN SPD PTCH",
          say: "Press HOME for TAPE. KNOB 4, PTCH, moves the loop in semitones, so you can play it like a transpose." },
        { press: ["HOME", "HOME"], turn: { K1: 90 }, screen: "tape_frze", label: "Home / Tape tools",
          alt: "TAPE's tools: FRZE on, the frozen window framed in orange",
          say: "Press HOME twice more for TAPE's tools and turn KNOB 1 (FRZE) on. The head keeps looping the bit it was in, framed in orange. WOW and FLUT warble it like an old tape." },
        { hold: ["REC"], press: ["PLAY"], screen: "tape_playing", label: "Home / Tape", alt: "TAPE playing",
          say: "On any track, hold REC and press PLAY to bounce it: a synth line, a drum pattern or a cloud of grains goes straight onto the tape as a loop." }
      ]
    },
    grain: {
      start: { K4: -135 },
      steps: [
        { press: ["EDIT"], screen: "grain", label: "Edit / Grain", alt: "GRAIN with WET at 0",
          say: "Press EDIT for GRAIN. WET, on KNOB 4, is at 0, so it's off." },
        { press: ["PLAY"], turn: { K4: 81 }, screen: "grain_playing", label: "Edit / Grain",
          alt: "GRAIN with WET at 80 and grains playing in orange",
          say: "Press PLAY and turn KNOB 4 (WET) up. Grains start to play: each orange block is one grain." },
        { hold: ["ZERO"], screen: "grain_frozen", label: "Edit / Grain", alt: "GRAIN frozen",
          say: "Hold the black 0 key. GRAIN freezes what it heard into a loop. Tap it instead and the freeze stays on." }
      ]
    },
    scenes: {
      start: { K2: -135 },
      steps: [
        { press: ["GLO"], screen: "mixer", label: "Glo / Mixer", alt: "The mixer",
          say: "Tap GLO. The mixer stays up." },
        { press: ["EDIT"], turn: { K2: 0 }, screen: "mixer_morph", label: "Glo / Scenes",
          alt: "SCENES: knob 2 halfway to scene 2",
          say: "Tap EDIT for SCENES, then turn KNOB 2. The whole project moves halfway toward scene 2." },
        { turn: { K2: 135 }, screen: "mixer_scenes", label: "Glo / Scenes", alt: "SCENES: scene 2 reached",
          say: "Keep turning. Now it's all the way there, and scene 2 is filled in." },
        { hold: ["W2"], press: ["SAVE"], screen: "mixer_scenes", label: "Glo / Scenes", alt: "SCENES",
          say: "Hold white key 2 and tap SAVE to store the sound as it is now. Tap a scene's key to glide back any time." }
      ]
    },
    arp: {
      start: { K1: -45, K4: -135 },
      steps: [
        { press: ["ARP"], hold: ["W1", "W5"], screen: "arp", label: "ARP / Key", alt: "ARP: two notes held",
          say: "Press ARP, with MODE on KEY, and hold two white keys. They play one at a time, in time. The filled bar is the one sounding." },
        { turn: { K1: 135 }, press: ["W1", "W3", "W5", "W8"], screen: "arp_add", label: "ARP / Add",
          alt: "ARP in ADD mode with four notes piled up",
          say: "Turn KNOB 1 to ADD and tap keys one by one. Each one joins the pattern and stays." },
        { press: ["ARP"], turn: { K4: 45 }, screen: "arp_fade", label: "ARP / Page 2",
          alt: "ARP page 2: GATE SWNG GRND FADE, FADE at 331 ms",
          say: "Press ARP again for page 2 and turn KNOB 4 (FADE) up. Now when you let go, the notes keep stepping and fade away instead of stopping." },
        { press: ["ARP"], screen: "arp_res", label: "RIPL ARP", alt: "RIPL ARP plucking RIPPLE's strings",
          say: "Press ARP once more for RIPL ARP, a second arpeggio that plays RIPPLE." }
      ]
    }
  };
  var TOUR_ORDER = ["pages", "tape", "grain", "scenes", "arp"];

  function initTour() {
    var root = $("[data-tour]");
    if (!root) return;
    var panelBox = $("[data-tour-panel]", root);
    var screen = $("[data-tour-screen]", root);
    var label = $("[data-tour-label]", root);
    var list = $("[data-tour-steps]", root);
    var live = $("[data-tour-live]", root);
    var playBtn = $("[data-tour-play]", root);
    var routineBtns = $$("[data-routine]", root);
    if (!panelBox || !screen || !list || !playBtn) return;

    var svg = null;           // the inline panel, once it's loaded
    var routine = "pages";
    var index = 0;
    var timer = null;         // the next step
    var flashes = [];         // the presses in flight
    var playing = !reduceMotion.matches;
    var touched = false;      // stepped by hand: it never plays by itself again
    var visible = false;
    var DWELL = 3400;
    var FLASH = 320;

    function src(name) { return "img/" + name + ".png"; }

    // every screen the tour shows, loaded up front so a step never waits on one
    TOUR_ORDER.forEach(function (k) {
      TOUR[k].steps.forEach(function (st) { var im = new Image(); im.src = src(st.screen); });
    });

    function ctl(name) { return svg ? svg.querySelector("#ctl-" + name) : null; }
    function clearClasses() {
      if (!svg) return;
      $$(".ctl", svg).forEach(function (g) { g.classList.remove("is-press", "is-hold", "is-turn"); });
    }
    function setAngle(name, deg, instant) {
      var g = ctl(name);
      var ptr = g && g.querySelector(".ptr");
      if (!ptr) return;
      if (instant) g.classList.add("no-anim");
      ptr.style.transform = "rotate(" + deg + "deg)";
      if (instant) { void ptr.getBoundingClientRect(); g.classList.remove("no-anim"); }
    }
    function resetKnobs() {
      ["K1", "K2", "K3", "K4"].forEach(function (k) {
        setAngle(k, TOUR[routine].start[k] != null ? TOUR[routine].start[k] : 0, true);
      });
    }
    function cancelFlashes() {
      flashes.forEach(function (t) { window.clearTimeout(t); });
      flashes = [];
    }

    function showScreen(st) {
      screen.src = src(st.screen);
      screen.alt = st.alt || "";
      if (label) label.textContent = st.label || "";
      var inner = svg && svg.querySelector("#panel-screen");
      if (inner) inner.setAttribute("href", src(st.screen));
    }

    function renderList() {
      list.innerHTML = "";
      TOUR[routine].steps.forEach(function (st, i) {
        var li = document.createElement("li");
        var b = document.createElement("button");
        b.type = "button";
        b.textContent = st.say;
        b.addEventListener("click", function () { manual(); go(i, true); });
        li.appendChild(b);
        list.appendChild(li);
      });
    }

    function go(i, announce) {
      var steps = TOUR[routine].steps;
      index = (i + steps.length) % steps.length;
      var st = steps[index];
      cancelFlashes();
      clearClasses();
      if (index === 0) resetKnobs();

      $$("li", list).forEach(function (li, n) {
        if (n === index) li.setAttribute("aria-current", "step"); else li.removeAttribute("aria-current");
      });

      (st.hold || []).forEach(function (n) { var g = ctl(n); if (g) g.classList.add("is-hold"); });
      var turns = st.turn || {};
      Object.keys(turns).forEach(function (n) {
        var g = ctl(n);
        if (g) g.classList.add("is-turn");
        setAngle(n, turns[n], false);
      });
      var presses = st.press || [];
      presses.forEach(function (n, k) {
        flashes.push(window.setTimeout(function () {
          var g = ctl(n);
          if (!g) return;
          g.classList.add("is-press");
          flashes.push(window.setTimeout(function () { g.classList.remove("is-press"); }, FLASH - 60));
        }, k * FLASH));
      });
      // the screen changes once the hands have done their part
      var after = Math.max(presses.length * FLASH, Object.keys(turns).length ? 500 : 0);
      flashes.push(window.setTimeout(function () { showScreen(st); }, after));

      if (announce && live) live.textContent = "Step " + (index + 1) + " of " + steps.length + ". " + st.say;
      schedule();
    }

    function setRoutine(name, announce) {
      routine = name;
      routineBtns.forEach(function (b) {
        b.setAttribute("aria-pressed", b.getAttribute("data-routine") === name ? "true" : "false");
      });
      renderList();
      go(0, announce);
    }

    function schedule() {
      window.clearTimeout(timer);
      timer = null;
      if (!playing || !visible || document.hidden) return;
      timer = window.setTimeout(function () {
        var steps = TOUR[routine].steps;
        if (index + 1 < steps.length) go(index + 1, false);
        else setRoutine(TOUR_ORDER[(TOUR_ORDER.indexOf(routine) + 1) % TOUR_ORDER.length], false);
      }, DWELL);
    }

    function setPlaying(on) {
      playing = on;
      playBtn.textContent = on ? "Pause" : "Play";
      playBtn.setAttribute("aria-pressed", on ? "false" : "true");
      schedule();
    }
    function manual() { if (!touched) { touched = true; } setPlaying(false); }

    playBtn.addEventListener("click", function () { setPlaying(!playing); });
    var prev = $("[data-tour-prev]", root);
    var next = $("[data-tour-next]", root);
    if (prev) prev.addEventListener("click", function () { manual(); go(index - 1, true); });
    if (next) next.addEventListener("click", function () { manual(); go(index + 1, true); });
    routineBtns.forEach(function (b) {
      b.addEventListener("click", function () { setRoutine(b.getAttribute("data-routine"), true); });
    });
    document.addEventListener("visibilitychange", schedule);

    if ("IntersectionObserver" in window) {
      new IntersectionObserver(function (entries) {
        visible = entries[0].isIntersecting;
        schedule();
      }, { threshold: 0.35 }).observe(root);
    } else {
      visible = true;
    }

    // the panel inline: same origin, so a plain fetch; on any failure the <img> stays
    if (window.fetch && window.DOMParser) {
      fetch("img/panel.svg").then(function (r) {
        if (!r.ok) throw new Error("panel.svg " + r.status);
        return r.text();
      }).then(function (text) {
        var doc = new DOMParser().parseFromString(text, "image/svg+xml");
        var el = doc.documentElement;
        if (!el || el.nodeName.toLowerCase() !== "svg" || doc.getElementsByTagName("parsererror").length) {
          throw new Error("panel.svg didn't parse");
        }
        el.removeAttribute("width");
        el.removeAttribute("height");
        el.setAttribute("aria-hidden", "true");
        el.setAttribute("focusable", "false");
        var title = el.querySelector("title");
        if (title) title.parentNode.removeChild(title);
        svg = document.importNode(el, true);
        panelBox.innerHTML = "";
        panelBox.appendChild(svg);
        go(index, false);                      // light the step that's showing
      }).catch(function (err) {
        if (window.console) console.warn("bryo-site: the walkthrough keeps the static panel:", err);
      });
    }

    setPlaying(playing);
    setRoutine("pages", false);
  }

  /* ---------- tabs (WAI-ARIA tabs pattern, automatic activation) ---------- */
  function initTabs() {
    $$("[data-tabs]").forEach(function (root) {
      var tabs = $$('[role="tab"]', root);
      if (!tabs.length) return;

      function select(tab, focus) {
        tabs.forEach(function (t) {
          var on = t === tab;
          t.setAttribute("aria-selected", on ? "true" : "false");
          t.tabIndex = on ? 0 : -1;
          var panel = document.getElementById(t.getAttribute("aria-controls"));
          if (panel) panel.hidden = !on;
        });
        if (focus) tab.focus();
        if (tab.scrollIntoView) tab.scrollIntoView({ block: "nearest", inline: "nearest" });
      }

      tabs.forEach(function (tab, idx) {
        tab.addEventListener("click", function () { select(tab, false); });
        tab.addEventListener("keydown", function (e) {
          var next = null;
          switch (e.key) {
            case "ArrowRight": next = tabs[(idx + 1) % tabs.length]; break;
            case "ArrowLeft": next = tabs[(idx - 1 + tabs.length) % tabs.length]; break;
            case "Home": next = tabs[0]; break;
            case "End": next = tabs[tabs.length - 1]; break;
            default: return;
          }
          e.preventDefault();
          select(next, true);
        });
      });
    });
  }

  /* ---------- docs: highlight the section you're reading in the contents ---------- */
  function initScrollSpy() {
    var toc = $(".toc");
    if (!toc || !("IntersectionObserver" in window)) return;
    var links = $$('a[href^="#"]', toc);
    var byId = {};
    links.forEach(function (a) { byId[a.getAttribute("href").slice(1)] = a; });
    var sections = Object.keys(byId)
      .map(function (id) { return document.getElementById(id); })
      .filter(Boolean);
    if (!sections.length) return;

    var visible = {};
    function mark() {
      // the first section (in document order) that has any part in the reading band wins
      var current = null;
      for (var k = 0; k < sections.length; k++) {
        if (visible[sections[k].id]) { current = sections[k].id; break; }
      }
      if (!current) return;
      links.forEach(function (a) {
        if (a === byId[current]) a.setAttribute("aria-current", "true");
        else a.removeAttribute("aria-current");
      });
    }

    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (en) { visible[en.target.id] = en.isIntersecting; });
      mark();
    }, { rootMargin: "-30% 0px -60% 0px" });
    sections.forEach(function (s) { io.observe(s); });

    // On phones the contents is a <details>: close it after you pick a section.
    var details = toc.closest("details") || (toc.tagName === "DETAILS" ? toc : null);
    if (details) {
      links.forEach(function (a) {
        a.addEventListener("click", function () {
          if (window.matchMedia("(max-width: 999px)").matches) details.open = false;
        });
      });
      // Desktop shows it as a sticky sidebar, always open; phones start it closed so it doesn't push
      // the page down. (Without JavaScript it stays open, which is the safe default.)
      var wide = window.matchMedia("(min-width: 1000px)");
      var sync = function () { details.open = wide.matches; };
      sync();
      if (wide.addEventListener) wide.addEventListener("change", sync);
    }
  }

  /* ---------- docs: copy buttons on code blocks ---------- */
  function initCopy() {
    var blocks = $$(".code");
    if (!blocks.length) return;

    var status = document.createElement("p");
    status.className = "visually-hidden";
    status.setAttribute("role", "status");
    document.body.appendChild(status);

    function announce(msg) {
      status.textContent = "";
      window.setTimeout(function () { status.textContent = msg; }, 50);
    }

    function fallbackCopy(text) {
      var ta = document.createElement("textarea");
      ta.value = text;
      ta.setAttribute("readonly", "");
      ta.style.position = "fixed";
      ta.style.opacity = "0";
      document.body.appendChild(ta);
      ta.select();
      var ok = false;
      try { ok = document.execCommand("copy"); } catch (e) { ok = false; }
      document.body.removeChild(ta);
      return ok ? Promise.resolve() : Promise.reject(new Error("copy failed"));
    }

    blocks.forEach(function (block) {
      var pre = $("pre", block);
      if (!pre) return;
      var btn = document.createElement("button");
      btn.type = "button";
      btn.className = "btn code__copy";
      btn.textContent = "Copy";
      btn.setAttribute("aria-label", "Copy code");
      block.appendChild(btn);

      btn.addEventListener("click", function () {
        var text = pre.innerText.replace(/\n$/, "");
        var p = navigator.clipboard && window.isSecureContext
          ? navigator.clipboard.writeText(text).catch(function () { return fallbackCopy(text); })
          : fallbackCopy(text);
        p.then(function () {
          btn.textContent = "Copied";
          announce("Copied to the clipboard");
        }, function () {
          btn.textContent = "Select";
          announce("Couldn't copy. Select the text and copy it yourself.");
          var range = document.createRange();
          range.selectNodeContents(pre);
          var sel = window.getSelection();
          sel.removeAllRanges();
          sel.addRange(range);
        }).then(function () {
          window.setTimeout(function () { btn.textContent = "Copy"; }, 1800);
        });
      });
    });
  }

  function init() {
    var steps = [initMenu, initScreens, initTour, initTabs, initScrollSpy, initCopy];
    steps.forEach(function (fn) {
      // One feature failing shouldn't take the others down with it.
      try { fn(); } catch (err) { if (window.console) console.error("bryo-site:", fn.name, err); }
    });
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init);
  else init();
})();
