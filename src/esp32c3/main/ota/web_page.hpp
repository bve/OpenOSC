#pragma once
#include "esp_http_server.h"

namespace openosc::companion::ota {
inline constexpr char AppForm[] = R"html(
<form class="row" id="app-update">
<div><h3>Application</h3><p class="hint">Installs through Recovery, then restarts.</p></div>
<div class="fields"><label>Application image (.bin) <input type="file" accept=".bin" required></label></div>
<button>Update firmware</button>
</form>)html";
inline constexpr char RecoveryForm[] = R"html(
<form class="row" id="recovery-update">
<div><h3>Recovery</h3><p class="hint">Keeps the running application.</p></div>
<div class="fields"><label>Recovery image (.bin) <input type="file" accept=".bin" required></label></div>
<button>Update Recovery</button>
</form>)html";
inline constexpr char UpdateScript[] = R"html(
<div class="upload"><progress id="upload-progress" hidden max="100" value="0"></progress>
<p id="upload-status" role="status" aria-live="polite"></p></div></div>
<script>
const appForm = document.querySelector('#app-update');
const recoveryForm = document.querySelector('#recovery-update');
const progress = document.querySelector('#upload-progress');
const statusLine = document.querySelector('#upload-status');
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
async function request(path, options, timeout) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeout);
  try { return await fetch(path, {...options, signal:controller.signal}); }
  finally { clearTimeout(timer); }
}
async function waitFor(expected) {
  for (let attempt = 0; attempt < 30; ++attempt) {
    try {
      const response = await request('/update/status', {cache:'no-store'}, 2500);
      if (response.ok && (await response.text()) === expected) return true;
    } catch (_) { /* The wireless interface is restarting. */ }
    await pause(1000);
  }
  return false;
}
function upload(path, file) {
  return new Promise((resolve, reject) => {
    const request = new XMLHttpRequest();
    request.open('POST', path);
    request.timeout = 180000;
    request.setRequestHeader('Content-Type','application/octet-stream');
    request.upload.onprogress = e => { if (e.lengthComputable) progress.value = 100 * e.loaded / e.total; };
    request.onload = () => request.status === 200 ? resolve(request.responseText) : reject(new Error(request.responseText || 'Upload refused.'));
    request.onerror = request.ontimeout = () => reject(new Error('Connection interrupted. Reopen this page to retry.'));
    request.send(file);
  });
}
async function install(event, recoveryImage) {
  event.preventDefault();
  const file = event.currentTarget.querySelector('input').files[0];
  if (!file) return;
  const buttons = document.querySelectorAll('button');
  buttons.forEach(button => button.disabled = true);
  progress.value = 0;
  progress.hidden = false;
  try {
    if (!recoveryImage && recoveryForm) {
      statusLine.textContent = 'Starting update mode…';
      const response = await request('/update', {method:'POST', body:file.slice(0, 288)}, 8000);
      if (!response.ok) throw new Error(await response.text());
      if (!await waitFor('recovery')) throw new Error('Update mode did not answer on this network.');
    }
    statusLine.textContent = 'Uploading…';
    statusLine.textContent = await upload(recoveryImage ? '/update/recovery' : '/firmware', file);
    if (!recoveryImage && await waitFor('application')) location.reload();
  } catch (error) {
    statusLine.textContent = error.message;
  } finally {
    buttons.forEach(button => button.disabled = false);
  }
}
appForm.addEventListener('submit', event => install(event, false));
if (recoveryForm) recoveryForm.addEventListener('submit', event => install(event, true));
</script>)html";

inline esp_err_t updateStatus(httpd_req_t *req, const char *stage) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, stage);
}

inline esp_err_t updateControls(httpd_req_t *req, bool canUpdateRecovery) {
    // The panel closes after the progress and status line at the start of UpdateScript.
    if (httpd_resp_sendstr_chunk(req, "<div class=\"panel\">") != ESP_OK ||
        httpd_resp_sendstr_chunk(req, AppForm) != ESP_OK ||
        (canUpdateRecovery && httpd_resp_sendstr_chunk(req, RecoveryForm) != ESP_OK)) return ESP_FAIL;
    return httpd_resp_sendstr_chunk(req, UpdateScript);
}
}
