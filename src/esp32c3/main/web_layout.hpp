#pragma once
#include "esp_http_server.h"
#include <cstdio>

namespace openosc::companion::web {
inline constexpr char Head[] = R"html(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="theme-color" content="#0f151b">
<title>OpenOSC · Wireless Module</title>
<style>
:root{color-scheme:dark;--bg:#0f151b;--panel:#161e26;--line:#273440;--field:#0f151b;--edge:#3b4c5b;--ink:#e6edf3;
--muted:#9fb0bf;--accent:#4fd1c5;--accent-ink:#04211e;--danger:#ff8f86}
*{box-sizing:border-box}
[hidden]{display:none!important}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
.frame{max-width:54rem;margin:auto;padding:0 .75rem 2rem}
h1,h2,h3,p{margin:0}
.top{display:flex;align-items:center;gap:.75rem;min-height:3.25rem;border-bottom:1px solid var(--line)}
.brand{display:flex;align-items:center;flex-wrap:wrap;gap:0 .45rem;margin-right:auto;min-width:0}
.brand a{display:inline-flex;align-items:center;min-height:40px;color:var(--ink);font-weight:750;font-size:1.05rem;text-decoration:none}
.brand h1{font-size:1.05rem;font-weight:500;color:var(--muted)}
.brand h1::before{content:"/";margin-right:.45rem;color:var(--edge)}
.github{display:inline-flex;align-items:center;min-height:40px;padding:0 .7rem;border:1px solid var(--line);
border-radius:6px;color:var(--muted);font-size:.875rem;text-decoration:none;white-space:nowrap}
@media(max-width:23rem){.top{gap:.5rem}.brand{gap:0 .3rem}.brand h1::before{margin-right:.3rem}.brand a,.brand h1{font-size:.95rem}.github{padding:0 .5rem}nav a{font-size:.85rem}}
.github:hover{color:var(--ink);border-color:var(--edge)}
nav{position:sticky;top:0;z-index:1;display:grid;grid-template-columns:repeat(4,1fr);gap:2px;margin:.6rem -.75rem 0;
padding:.4rem .75rem;background:var(--bg);border-bottom:1px solid var(--line)}
nav a{display:flex;align-items:center;justify-content:center;min-height:42px;border-radius:6px;color:var(--muted);
text-decoration:none;font-weight:600;font-size:.9rem;white-space:nowrap}
nav a:hover{color:var(--ink);background:var(--panel)}
nav a[aria-current]{color:var(--accent);background:var(--panel);box-shadow:inset 0 -2px var(--accent)}
main section{padding-top:.9rem}
h2{font-size:.8rem;font-weight:650;letter-spacing:.06em;text-transform:uppercase;color:var(--muted);margin:0 0 .45rem .1rem}
h3{font-size:.95rem;font-weight:650}
.hint{color:var(--muted);font-size:.85rem}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:8px;min-width:0}
.row{display:grid;gap:.6rem 1rem;padding:.8rem .9rem;border-top:1px solid var(--line);min-width:0}
.row:first-child{border-top:0}
.fields{display:grid;gap:.6rem;grid-template-columns:repeat(auto-fit,minmax(min(100%,10.5rem),1fr));min-width:0}
@media(min-width:42rem){.frame{padding:0 1.25rem 2.5rem}nav{margin:.75rem 0 0;padding:.25rem;border:1px solid var(--line);border-radius:8px;background:var(--bg)}
.row{grid-template-columns:12rem minmax(0,1fr) 11rem;align-items:end}.row>:first-child{align-self:center}.row>button{grid-column:3}}
label{display:grid;align-content:end;gap:.25rem;color:var(--muted);font-size:.85rem;font-weight:550;min-width:0}
input,select,button,.button{font:inherit;min-height:42px;border-radius:6px}
input,select{width:100%;min-width:0;font-size:1rem;border:1px solid var(--edge);background:var(--field);color:var(--ink);padding:.45rem .6rem}
input[type=file]{font-size:.9rem;padding:.35rem .4rem}
input::file-selector-button{margin-right:.5rem;color:var(--ink);background:#24313d;border:0;border-radius:4px;padding:.3rem .55rem;font:inherit}
button,.button{display:inline-flex;align-items:center;justify-content:center;gap:.4rem;border:1px solid transparent;padding:.4rem .9rem;
color:var(--accent-ink);background:var(--accent);font-weight:650;cursor:pointer;text-decoration:none}
button:hover,.button:hover{background:#7ee3d9}
button:disabled{opacity:.5;cursor:wait}
.quiet{color:var(--ink);background:transparent;border-color:var(--edge)}
.quiet:hover{color:var(--ink);background:#202b35}
:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
.upload{padding:0 .9rem .8rem}
progress{width:100%;height:.5rem;accent-color:var(--accent)}
#upload-status:not(:empty){margin-top:.4rem;color:var(--muted);font-size:.9rem;white-space:pre-wrap;overflow-wrap:anywhere}
.mode{display:grid;gap:.6rem;padding:.8rem .9rem;margin-top:.9rem;border-left:3px solid var(--accent)}
@media(min-width:42rem){.mode{grid-template-columns:minmax(0,1fr) auto;align-items:center}}
.note{display:grid;gap:.7rem;justify-items:start;padding:.9rem;margin-top:.9rem}
</style>
</head>
<body>
<div class="frame">
<header class="top">
<div class="brand"><a href="/">OpenOSC</a><h1>Wireless Module</h1></div>
<a class="github" href="https://github.com/bve/OpenOSC" target="_blank" rel="noopener noreferrer">GitHub ↗</a>
</header>
)html";
inline constexpr char NavigationScript[] = R"html(
<script>
(() => {
  const links = [...document.querySelectorAll('nav a[href^="#"]')];
  const sections = links.map(link => document.getElementById(link.hash.slice(1)));
  if (!links.length || sections.some(section => !section)) return;
  function selectSection() {
    const active = Math.max(0, links.findIndex(link => link.hash === location.hash));
    links.forEach((link, index) => {
      sections[index].hidden = index !== active;
      if (index === active) link.setAttribute('aria-current', 'page');
      else link.removeAttribute('aria-current');
    });
    window.dispatchEvent(new CustomEvent('openosc:section', {detail:sections[active].id}));
  }
  links.forEach(link => link.addEventListener('click', event => {
    if (event.button !== 0 || event.ctrlKey || event.metaKey || event.shiftKey || event.altKey) return;
    event.preventDefault();
    if (location.hash !== link.hash) history.pushState(null, '', link.hash);
    selectSection();
    window.scrollTo(0, 0);
  }));
  window.addEventListener('hashchange', selectSection);
  selectSection();
})();
</script>
)html";

inline esp_err_t begin(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr_chunk(req, Head);
}
inline esp_err_t end(httpd_req_t *req) {
    if (httpd_resp_sendstr_chunk(req, "</div></body></html>") != ESP_OK) return ESP_FAIL;
    return httpd_resp_sendstr_chunk(req, nullptr);
}
inline esp_err_t accepted(httpd_req_t *req, const char *message, const char *section) {
    char content[496];
    const int size = std::snprintf(content, sizeof content,
        "<main><div class=\"panel note\" role=\"status\"><h3>Settings request accepted</h3><p class=\"hint\">%s</p>"
        "<a class=\"button\" href=\"/#%s\">Back to settings</a></div></main>",
        message, section);
    if (size < 0 || size >= int(sizeof content)) return ESP_FAIL;
    httpd_resp_set_status(req, "202 Accepted");
    if (begin(req) != ESP_OK || httpd_resp_sendstr_chunk(req, content) != ESP_OK) return ESP_FAIL;
    return end(req);
}
}
