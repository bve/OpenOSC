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
.tools{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:.4rem;padding:.6rem .9rem}
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
.explorer{display:grid;border-top:1px solid var(--line)}
.folder-pane,.file-main{min-width:0}
.folder-pane{background:var(--field);border-bottom:1px solid var(--line)}
.folder-pane summary{padding:.45rem .75rem;color:var(--muted);font-size:.85rem;font-weight:650;cursor:pointer}
.folder-tree,.folder-tree ul{list-style:none;margin:0;padding:0}
#file-tree{max-height:11rem;overflow:auto;padding:.1rem .45rem .45rem}
.folder-tree ul{margin-left:.55rem;padding-left:.32rem;border-left:1px solid var(--line)}
.tree-row{display:grid;grid-template-columns:1.65rem minmax(0,1fr);align-items:stretch;gap:.05rem;margin:.1rem 0}
.tree-row button{min-width:0;min-height:36px;padding:0 .35rem;border:1px solid transparent;background:none;color:var(--muted);border-radius:4px}
.tree-row button:hover{color:var(--ink);background:var(--panel)}
.tree-toggle::before{content:"";width:6px;height:6px;border:solid currentColor;border-width:1px 1px 0 0;transform:rotate(45deg)}
.tree-toggle[aria-expanded="true"]::before{transform:translateY(-1px) rotate(135deg)}
.tree-row .tree-link{grid-column:2;justify-content:flex-start;color:var(--ink);font-size:.9rem;font-weight:550}
.tree-link span:last-child{min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.tree-row .tree-link[aria-current]{background:#183638;border-color:#2c5554;color:#9ee9e0}
.item-icon{display:inline-block;position:relative;flex:0 0 .8rem;width:.8rem;height:.92rem;border:1px solid #8ba2b5;border-radius:2px;background:#8ba2b51a}
.item-icon.directory{height:.65rem;margin-top:.12rem;border-color:#cfaa70;background:#cfaa7022}
.item-icon.directory::before{position:absolute;content:"";top:-4px;left:-1px;width:6px;height:4px;border:1px solid #cfaa70;border-bottom:0;border-radius:2px 2px 0 0;background:var(--field)}
.file-columns,#file-list li{display:grid;grid-template-columns:minmax(0,1fr) auto;align-items:center;gap:.35rem .75rem;padding:.5rem .9rem}
.file-columns{color:var(--muted);font-size:.8rem;font-weight:650;background:var(--field)}
.file-columns span:last-child{display:none}
#file-list{list-style:none;margin:0;padding:0}
#file-list li{border-top:1px solid var(--line)}
.entry-identity{display:flex;align-items:center;gap:.55rem;min-width:0;overflow-wrap:anywhere}
#file-list li>.file-actions{grid-column:1/-1;justify-content:flex-end}
.entry-file{font-weight:550}
#file-list small{color:var(--muted);font-size:.8rem;text-align:right;white-space:nowrap}
#file-list .danger{margin-left:.25rem}
.entry-name{display:flex;min-height:0;max-width:100%;padding:8px 0 7px;margin:-8px 0 -7px;border:0;background:none;justify-content:flex-start;font-weight:600;color:var(--accent);text-align:left;white-space:normal;overflow-wrap:anywhere}
.entry-name:hover{background:none;text-decoration:underline}
#files[aria-busy] #file-list{opacity:.55}
@media(min-width:42rem){
  .explorer{grid-template-columns:minmax(10rem,26%) minmax(0,1fr);min-height:20rem}
  .folder-pane{border-right:1px solid var(--line);border-bottom:0;border-radius:0 0 0 8px}
  #file-tree{max-height:60vh}
}
@media(min-width:52rem){
  .file-columns,#file-list li{grid-template-columns:minmax(0,1fr) 4.5rem 16.5rem}
  .file-columns span:last-child{display:block;text-align:right}
  .file-columns span:nth-child(2){text-align:right}
  #file-list li>.file-actions{grid-column:auto}
  #file-list .file-actions button{min-height:36px;font-size:.875rem}
}
@media(max-width:23rem){#file-list .file-actions{gap:.3rem}#file-list .file-actions button{padding:.3rem .5rem}}
@media(pointer:coarse){.file-actions button,#file-path button,.tree-row button{min-height:44px}.entry-name{padding:12px 0 10px;margin:-12px 0 -10px}}
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
  <div class="explorer">
    <aside class="folder-pane" aria-label="Folder navigation">
      <details open>
        <summary>Folders</summary>
        <ul id="file-tree" class="folder-tree" aria-label="Folder tree"></ul>
      </details>
    </aside>
    <div class="file-main">
      <div class="file-columns" aria-hidden="true"><span>Name</span><span>Size</span><span>Actions</span></div>
      <p id="file-status" class="hint" role="status" aria-live="polite"></p>
      <ul id="file-list" aria-label="Directory contents"></ul>
    </div>
  </div>
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
  const tree = root.querySelector('#file-tree');
  const pathBar = root.querySelector('#file-path');
  const status = root.querySelector('#file-status');
  const chooser = root.querySelector('#file-upload');
  const dialog = root.querySelector('#file-dialog');
  const nameInput = root.querySelector('#file-name');
  const encoder = new TextEncoder();
  const home = '/openosc';
  const folders = new Map();
  const expanded = new Set([home]);
  let folder = home;
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
  function icon(directory) {
    const item = document.createElement('span');
    item.className = directory ? 'item-icon directory' : 'item-icon';
    item.setAttribute('aria-hidden', 'true');
    return item;
  }
  function openFolder(path) { perform('Loading…', () => browse(path)); }
  function showPath() {
    pathBar.replaceChildren();
    let path = '';
    for (const component of folder.slice(1).split('/')) {
      path += '/' + component;
      const directory = path;
      const item = button(component, () => openFolder(directory));
      if (directory === folder) item.setAttribute('aria-current', 'location');
      pathBar.append(item);
    }
  }
  function sizeLabel(bytes) {
    return bytes < 1024 ? bytes + ' B' : bytes < 1024 * 1024
      ? (bytes / 1024).toFixed(1) + ' KiB' : (bytes / (1024 * 1024)).toFixed(1) + ' MiB';
  }
  function forgetFolder(path) {
    for (const name of folders.get(path) || []) forgetFolder(path + '/' + name);
    folders.delete(path);
    expanded.delete(path);
  }
  async function readFolder(path) {
    const entries = await (await command('list', path)).json();
    entries.sort((a,b) => Number(b[1]) - Number(a[1]) || a[0].localeCompare(b[0]));
    const directories = new Set(entries.filter(entry => entry[1]).map(entry => entry[0]));
    for (const previous of folders.get(path) || []) {
      if (!directories.has(previous)) forgetFolder(path + '/' + previous);
    }
    folders.set(path, directories);
    return entries;
  }
  function toggleFolder(path) {
    if (busy) return;
    if (expanded.delete(path)) { showTree(); return; }
    perform('Loading folders…', async () => {
      if (!folders.has(path)) await readFolder(path);
      expanded.add(path);
      showTree();
      status.textContent = list.childElementCount ? '' : 'This folder is empty.';
    });
  }
  function treeBranch(path, name) {
    const branch = document.createElement('li');
    const row = document.createElement('div');
    row.className = 'tree-row';
    const directories = folders.get(path);
    const isOpen = expanded.has(path);
    const toggle = button('', () => toggleFolder(path), 'tree-toggle');
    toggle.dataset.path = path;
    toggle.setAttribute('aria-label', (isOpen ? 'Collapse ' : 'Expand ') + name);
    toggle.setAttribute('aria-expanded', String(isOpen));
    toggle.hidden = directories?.size === 0;
    const select = button('', () => openFolder(path), 'tree-link');
    select.dataset.path = path;
    select.title = path;
    if (path === folder) select.setAttribute('aria-current', 'location');
    const caption = document.createElement('span');
    caption.textContent = name;
    select.append(icon(true), caption);
    row.append(toggle, select);
    branch.append(row);
    if (isOpen && directories?.size) {
      const group = document.createElement('ul');
      for (const child of directories) group.append(treeBranch(path + '/' + child, child));
      branch.append(group);
    }
    return branch;
  }
  function showTree() {
    const current = tree.contains(document.activeElement) ? document.activeElement : null;
    tree.replaceChildren(treeBranch(home, home.slice(1)));
    if (current) {
      const path = current.dataset.path;
      const candidates = [...tree.querySelectorAll('button')].filter(item => item.dataset.path === path);
      const target = candidates.find(item => item.className === current.className && !item.hidden) || candidates.at(-1);
      target?.focus({preventScroll:true});
    }
  }
  async function browse(path) {
    const entries = await readFolder(path);
    folder = path;
    for (let visible = path; visible.length >= home.length; visible = visible.slice(0, visible.lastIndexOf('/')))
      expanded.add(visible);
    showPath();
    showTree();
    list.replaceChildren();
    for (const [name, directory, size] of entries) {
      const row = document.createElement('li');
      const identity = document.createElement('div');
      identity.className = 'entry-identity';
      const label = directory
        ? button(name, () => openFolder(child(name)), 'entry-name')
        : document.createElement('span');
      if (!directory) { label.textContent = name; label.className = 'entry-file'; }
      const type = document.createElement('small');
      type.textContent = directory ? '—' : sizeLabel(size);
      identity.append(icon(directory), label);
      const actions = document.createElement('div');
      actions.className = 'file-actions';
      if (!directory) actions.append(button('Download', () => perform('Downloading…', () => download(name))));
      actions.append(button('Rename', () => ask('rename', name)), button('Delete', () => ask('remove', name), 'danger'));
      row.append(identity, type, actions);
      list.append(row);
    }
    status.textContent = entries.length ? '' : 'This folder is empty.';
    initialized = true;
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
  showTree();
  window.addEventListener('openosc:section', event => {
    if (event.detail !== 'files' || initialized) return;
    openFolder(folder);
  });
})();
</script>
)html";
}
