#pragma once

namespace openosc::companion::web {
inline constexpr char FilesPage[] = R"html(
<style>
.danger{color:var(--danger);background:transparent;border-color:#5e3633}
.danger:hover{background:#2e1b1a}
.danger-solid{color:#1d0705;background:var(--danger)}
.danger-solid:hover{background:#ffb1aa}
.file-actions{display:flex;align-items:center;gap:.4rem;flex-wrap:wrap}
.file-actions button{min-height:40px;padding:.3rem .6rem}
.tools{display:grid;grid-template-columns:repeat(2,1fr);gap:.4rem;padding:.6rem .9rem}
.tools button{min-height:42px}
@media(min-width:42rem){.tools{display:flex}.tools .quiet:last-of-type{margin-left:auto}}
#file-path{gap:0;padding:.55rem .9rem;border-bottom:1px solid var(--line);font-size:.9rem}
#file-path button{min-height:36px;padding:.2rem .35rem;border:0;background:none;color:var(--muted);font-weight:550;
text-align:left;overflow-wrap:anywhere;max-width:100%}
#file-path button+button::before{content:"/";margin-right:.35rem;color:var(--edge)}
#file-path button:hover{color:var(--ink)}
#file-path button[aria-current]{color:var(--ink);font-weight:650}
#file-status{padding:.5rem .9rem;border-top:1px solid var(--line)}
#file-status:empty{display:none}
#file-list{list-style:none;margin:0;padding:0}
#file-list li{display:flex;flex-wrap:wrap;align-items:center;gap:.35rem .75rem;border-top:1px solid var(--line);padding:.5rem .9rem}
#file-list li>div:first-child{flex:1 1 9rem;min-width:0;overflow-wrap:anywhere}
#file-list li>.file-actions{margin-left:auto}
.entry-file{font-weight:550}
#file-list small{display:block;color:var(--muted);font-size:.8rem}
#file-list .danger{margin-left:.25rem}
.entry-name{display:flex;min-height:0;max-width:100%;padding:8px 0 7px;margin:-8px 0 -7px;border:0;background:none;justify-content:flex-start;font-weight:600;color:var(--accent);text-align:left;white-space:normal;overflow-wrap:anywhere}
.entry-name:hover{background:none;text-decoration:underline}
#files[aria-busy] #file-list{opacity:.55}
@media(max-width:23rem){#file-list .file-actions{gap:.3rem}#file-list .file-actions button{padding:.3rem .5rem}}
@media(pointer:coarse){.file-actions button,#file-path button{min-height:44px}.entry-name{padding:12px 0 10px;margin:-12px 0 -10px}}
dialog{background:var(--panel);color:var(--ink);border:1px solid var(--edge);border-radius:8px;padding:0;width:min(25rem,calc(100% - 1.5rem))}
dialog form{display:grid;gap:.7rem;padding:1rem}
dialog::backdrop{background:#070b0fcc}
#file-dialog-note{overflow-wrap:anywhere;color:var(--ink);font-weight:600}
</style>
<section id="files" aria-labelledby="files-heading">
<h2 id="files-heading">Files · SPI flash</h2>
<div class="panel browser">
  <div id="file-path" class="file-actions" role="group" aria-label="Current folder"></div>
  <div class="tools">
    <button type="button" data-file-action="upload">Upload file</button>
    <button type="button" data-file-action="folder" class="quiet">New folder</button>
    <button type="button" data-file-action="new" class="quiet">New file</button>
    <button type="button" data-file-action="refresh" class="quiet">Refresh</button>
    <input id="file-upload" type="file" hidden>
  </div>
  <p id="file-status" class="hint" role="status" aria-live="polite"></p>
  <ul id="file-list" aria-label="Directory contents"></ul>
</div>
<dialog id="file-dialog" aria-labelledby="file-dialog-title">
  <form method="dialog">
    <h3 id="file-dialog-title"></h3>
    <p id="file-dialog-note"></p>
    <p class="hint" id="file-dialog-warn">This cannot be undone.</p>
    <label id="file-dialog-label">Name <input id="file-name" maxlength="255" autocomplete="off" autocapitalize="off" spellcheck="false"></label>
    <div class="file-actions">
      <button value="confirm" id="file-confirm">Save</button>
      <button value="cancel" class="quiet" formnovalidate>Cancel</button>
    </div>
  </form>
</dialog>
</section>
)html";
inline constexpr char FilesScript[] = R"html(
<script>
(() => {
  const root = document.querySelector('#files');
  const list = root.querySelector('#file-list');
  const pathBar = root.querySelector('#file-path');
  const status = root.querySelector('#file-status');
  const chooser = root.querySelector('#file-upload');
  const dialog = root.querySelector('#file-dialog');
  const nameInput = root.querySelector('#file-name');
  const encoder = new TextEncoder();
  let folder = '/openosc';
  let busy = false;
  let initialized = false;
  let dialogAction;
  let selection;

  const child = name => folder + '/' + name;
  function nameOk(name) {
    return name && name !== '.' && name !== '..' && !/^ |[ .]$/.test(name) &&
      !/[\x00-\x1f/\\:*?"<>|]/.test(name) && !/\.tmp$/i.test(name);
  }
  function pathField(path) {
    const bytes = encoder.encode(path);
    if (!bytes.length || bytes.length >= 512) throw new Error('This file path is too long.');
    return [new Uint8Array([bytes.length & 255, bytes.length >> 8]), bytes];
  }
  function requestBody(path, extra) { return new Blob([...pathField(path), ...extra]); }
  async function command(operation, path, extra = []) {
    const response = await fetch('/files', {
      method: 'POST',
      headers: {'X-OpenOSC-Operation': operation},
      body: requestBody(path, extra),
      cache: 'no-store'
    });
    if (!response.ok) throw new Error(await response.text() || 'File operation failed.');
    return response;
  }
  function writeFile(name, file) {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/files');
      xhr.setRequestHeader('X-OpenOSC-Operation', 'upload');
      xhr.upload.onprogress = event => {
        if (event.lengthComputable) status.textContent = event.loaded === event.total
          ? 'Saving to SPI flash…' : 'Uploading ' + Math.floor(event.loaded * 100 / event.total) + '%…';
      };
      xhr.onload = () => xhr.status === 200 ? resolve() : reject(new Error(xhr.responseText || 'Could not save the file.'));
      xhr.onerror = () => reject(new Error('The connection was interrupted.'));
      xhr.send(requestBody(child(name), file ? [file] : []));
    });
  }
  function button(label, run, className = 'quiet') {
    const item = document.createElement('button');
    item.type = 'button';
    item.className = className;
    item.textContent = label;
    item.addEventListener('click', run);
    return item;
  }
  function showPath() {
    pathBar.replaceChildren();
    let path = '';
    for (const component of folder.slice(1).split('/')) {
      path += '/' + component;
      const directory = path;
      const item = button(component, () => perform('Loading…', () => browse(directory)));
      if (directory === folder) item.setAttribute('aria-current', 'location');
      pathBar.append(item);
    }
  }
  function sizeLabel(bytes) {
    return bytes < 1024 ? bytes + ' B' : bytes < 1024 * 1024
      ? (bytes / 1024).toFixed(1) + ' KiB' : (bytes / (1024 * 1024)).toFixed(1) + ' MiB';
  }
  async function browse(path) {
    const entries = await (await command('list', path)).json();
    folder = path;
    showPath();
    list.replaceChildren();
    entries.sort((a,b) => Number(b[1]) - Number(a[1]) || a[0].localeCompare(b[0]));
    for (const [name, directory, size] of entries) {
      const row = document.createElement('li');
      const identity = document.createElement('div');
      const label = directory
        ? button(name, () => perform('Loading…', () => browse(child(name))), 'entry-name')
        : document.createElement('span');
      if (!directory) { label.textContent = name; label.className = 'entry-file'; }
      const type = document.createElement('small');
      type.textContent = directory ? 'Folder' : sizeLabel(size);
      identity.append(label, type);
      const actions = document.createElement('div');
      actions.className = 'file-actions';
      if (!directory) actions.append(button('Download', () => perform('Downloading…', () => download(name))));
      actions.append(button('Rename', () => ask('rename', name)), button('Delete', () => ask('remove', name), 'danger'));
      row.append(identity, actions);
      list.append(row);
    }
    status.textContent = entries.length ? '' : 'This folder is empty.';
  }
  async function download(name) {
    const blob = await (await command('download', child(name))).blob();
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = name;
    root.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 30000);
    status.textContent = '';
  }
  async function perform(message, work) {
    if (busy) return;
    busy = true;
    root.setAttribute('aria-busy', 'true');
    status.textContent = message;
    try { await work(); }
    catch (error) { status.textContent = error.message; }
    finally {
      root.removeAttribute('aria-busy');
      busy = false;
    }
  }
  function ask(action, name = '') {
    if (busy) return;
    dialogAction = action;
    selection = name;
    const isDelete = action === 'remove';
    dialog.returnValue = '';
    root.querySelector('#file-dialog-title').textContent = {
      folder: 'New folder', new: 'New file', rename: 'Rename item', remove: 'Delete item'
    }[action];
    const note = root.querySelector('#file-dialog-note');
    note.hidden = !isDelete;
    root.querySelector('#file-dialog-warn').hidden = !isDelete;
    note.textContent = isDelete ? name : '';
    root.querySelector('#file-dialog-label').hidden = isDelete;
    nameInput.required = !isDelete;
    nameInput.value = isDelete ? '' : name;
    nameInput.setCustomValidity('');
    const confirm = root.querySelector('#file-confirm');
    confirm.textContent = isDelete ? 'Delete' : 'Save';
    confirm.className = isDelete ? 'danger-solid' : '';
    dialog.showModal();
    if (isDelete) dialog.querySelector('button[value="cancel"]').focus();
    else { nameInput.focus(); nameInput.select(); }
  }
  dialog.querySelector('form').addEventListener('submit', event => {
    if (event.submitter.value !== 'confirm' || dialogAction === 'remove') return;
    const valid = nameOk(nameInput.value);
    nameInput.setCustomValidity(valid ? '' : 'Enter a valid file or folder name.');
    if (!valid) { event.preventDefault(); nameInput.reportValidity(); }
  });
  nameInput.addEventListener('input', () => nameInput.setCustomValidity(''));
  dialog.addEventListener('close', () => {
    if (dialog.returnValue !== 'confirm') return;
    const name = nameInput.value;
    const action = dialogAction;
    const previous = selection;
    perform('Saving…', async () => {
      if (action === 'new') await writeFile(name);
      if (action === 'folder') await command('mkdir', child(name));
      if (action === 'remove') await command('remove', child(previous));
      if (action === 'rename' && name !== previous) await command('rename', child(previous), pathField(child(name)));
      await browse(folder);
    });
  });
  root.querySelector('[data-file-action="new"]').addEventListener('click', () => ask('new'));
  root.querySelector('[data-file-action="folder"]').addEventListener('click', () => ask('folder'));
  root.querySelector('[data-file-action="refresh"]').addEventListener('click', () => perform('Loading…', () => browse(folder)));
  root.querySelector('[data-file-action="upload"]').addEventListener('click', () => { if (!busy) chooser.click(); });
  chooser.addEventListener('change', () => {
    const file = chooser.files[0];
    chooser.value = '';
    if (!file) return;
    perform('Uploading…', async () => {
      if (!nameOk(file.name)) throw new Error('This filename is not valid on the SPI flash.');
      await writeFile(file.name, file);
      await browse(folder);
    });
  });
  window.addEventListener('openosc:section', event => {
    if (event.detail !== 'files' || initialized) return;
    initialized = true;
    perform('Loading…', () => browse(folder));
  });
})();
</script>
)html";
}
