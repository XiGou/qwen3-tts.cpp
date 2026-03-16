#pragma once
/*
 * webui.h — HTML/CSS/JS for the qwen3-tts WebUI, embedded as a C string
 * constant so the server binary has zero runtime file dependencies.
 */

namespace webui {

static const char * INDEX_HTML = R"WEBUI(<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Qwen3-TTS WebUI</title>
<style>
  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    background: #0f1117;
    color: #e2e8f0;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 32px 16px;
  }
  h1 { font-size: 1.8rem; font-weight: 700; margin-bottom: 4px; color: #7dd3fc; }
  .subtitle { color: #94a3b8; font-size: 0.9rem; margin-bottom: 32px; }
  .card {
    background: #1e2130;
    border: 1px solid #2d3448;
    border-radius: 12px;
    padding: 24px;
    width: 100%;
    max-width: 720px;
    margin-bottom: 20px;
  }
  .card h2 { font-size: 1rem; color: #94a3b8; text-transform: uppercase;
             letter-spacing: 0.05em; margin-bottom: 16px; }
  label { display: block; font-size: 0.85rem; color: #94a3b8; margin-bottom: 6px; }
  textarea, input[type=text], input[type=number], select {
    width: 100%;
    background: #0f1117;
    border: 1px solid #2d3448;
    border-radius: 8px;
    color: #e2e8f0;
    padding: 10px 12px;
    font-size: 0.95rem;
    outline: none;
    transition: border-color .2s;
  }
  textarea:focus, input:focus, select:focus { border-color: #7dd3fc; }
  textarea { resize: vertical; min-height: 100px; }
  .row { display: flex; gap: 16px; flex-wrap: wrap; }
  .row > .field { flex: 1; min-width: 140px; }
  .field { margin-bottom: 16px; }
  button {
    background: #3b82f6;
    color: #fff;
    border: none;
    border-radius: 8px;
    padding: 10px 24px;
    font-size: 0.95rem;
    cursor: pointer;
    transition: background .2s;
    margin-top: 4px;
  }
  button:hover:not(:disabled) { background: #2563eb; }
  button:disabled { background: #374151; cursor: not-allowed; }
  #status {
    font-size: 0.85rem;
    color: #94a3b8;
    margin-top: 10px;
    min-height: 1.4em;
  }
  #status.error { color: #f87171; }
  #status.ok    { color: #86efac; }
  audio { width: 100%; margin-top: 12px; border-radius: 8px; }
  .hidden { display: none !important; }
  .tip { font-size: 0.78rem; color: #64748b; margin-top: 4px; }
  input[type=file] {
    background: #0f1117;
    border: 1px dashed #2d3448;
    border-radius: 8px;
    color: #94a3b8;
    padding: 8px 12px;
    width: 100%;
    cursor: pointer;
  }
</style>
</head>
<body>
<h1>🎙️ Qwen3-TTS WebUI</h1>
<p class="subtitle">Text-to-Speech — powered by qwen3-tts.cpp</p>

<!-- Synthesis card -->
<div class="card">
  <h2>Synthesize</h2>

  <div class="field">
    <label for="text">Text (supports Chinese / English and more)</label>
    <textarea id="text" placeholder="Enter text to synthesize…&#10;输入要合成的文字…"></textarea>
  </div>

  <div class="row">
    <div class="field">
      <label for="language">Language</label>
      <select id="language">
        <option value="en">English (en)</option>
        <option value="zh" selected>Chinese (zh)</option>
        <option value="ja">Japanese (ja)</option>
        <option value="ko">Korean (ko)</option>
        <option value="de">German (de)</option>
        <option value="fr">French (fr)</option>
        <option value="es">Spanish (es)</option>
        <option value="ru">Russian (ru)</option>
        <option value="it">Italian (it)</option>
        <option value="pt">Portuguese (pt)</option>
      </select>
    </div>
    <div class="field">
      <label for="temperature">Temperature</label>
      <input type="number" id="temperature" value="0.9" min="0" max="2" step="0.05">
      <p class="tip">0 = greedy; higher = more varied</p>
    </div>
    <div class="field">
      <label for="topk">Top-k</label>
      <input type="number" id="topk" value="50" min="0" max="200">
    </div>
  </div>

  <div class="row">
    <div class="field">
      <label for="max_tokens">Max tokens</label>
      <input type="number" id="max_tokens" value="4096" min="64" max="16384">
    </div>
    <div class="field">
      <label for="rep_penalty">Repetition penalty</label>
      <input type="number" id="rep_penalty" value="1.05" min="1" max="3" step="0.01">
    </div>
    <div class="field">
      <label for="threads">Threads</label>
      <input type="number" id="threads" value="4" min="1" max="64">
    </div>
  </div>
</div>

<!-- Voice cloning card -->
<div class="card">
  <h2>Voice Cloning (optional)</h2>
  <div class="field">
    <label for="ref_audio">Reference audio (WAV, 24 kHz mono recommended)</label>
    <input type="file" id="ref_audio" accept=".wav,audio/*">
    <p class="tip">Leave empty to use the default voice.</p>
  </div>
</div>

<!-- Action & result card -->
<div class="card">
  <button id="btn_synthesize">▶ Synthesize</button>
  <p id="status"></p>
  <audio id="player" controls class="hidden"></audio>
  <a id="dl_link" class="hidden" download="output.wav">⬇ Download WAV</a>
</div>

<script>
(function () {
  const btnSynth  = document.getElementById('btn_synthesize');
  const statusEl  = document.getElementById('status');
  const playerEl  = document.getElementById('player');
  const dlLink    = document.getElementById('dl_link');

  function setStatus(msg, cls) {
    statusEl.textContent = msg;
    statusEl.className   = cls || '';
  }

  btnSynth.addEventListener('click', async () => {
    const text = document.getElementById('text').value.trim();
    if (!text) { setStatus('Please enter some text.', 'error'); return; }

    btnSynth.disabled = true;
    playerEl.classList.add('hidden');
    dlLink.classList.add('hidden');
    setStatus('Synthesizing… (this may take a while)');

    try {
      const formData = new FormData();
      formData.append('text',        text);
      formData.append('language',    document.getElementById('language').value);
      formData.append('temperature', document.getElementById('temperature').value);
      formData.append('top_k',       document.getElementById('topk').value);
      formData.append('max_tokens',  document.getElementById('max_tokens').value);
      formData.append('rep_penalty', document.getElementById('rep_penalty').value);
      formData.append('threads',     document.getElementById('threads').value);

      const refFile = document.getElementById('ref_audio').files[0];
      if (refFile) formData.append('ref_audio', refFile);

      const resp = await fetch('/api/synthesize', { method: 'POST', body: formData });

      if (!resp.ok) {
        const err = await resp.json().catch(() => ({ error: resp.statusText }));
        setStatus('Error: ' + (err.error || resp.statusText), 'error');
        return;
      }

      const blob = await resp.blob();
      const url  = URL.createObjectURL(blob);
      playerEl.src = url;
      playerEl.classList.remove('hidden');
      dlLink.href  = url;
      dlLink.classList.remove('hidden');

      const duration = parseFloat(resp.headers.get('X-Audio-Duration') || '0');
      const timing   = resp.headers.get('X-Timing-Ms') || '';
      setStatus(
        'Done! Duration: ' + (duration > 0 ? duration.toFixed(2) + 's' : 'unknown') +
        (timing ? '  |  Inference: ' + timing + ' ms' : ''),
        'ok'
      );
    } catch (e) {
      setStatus('Network error: ' + e.message, 'error');
    } finally {
      btnSynth.disabled = false;
    }
  });
})();
</script>
</body>
</html>
)WEBUI";

} // namespace webui
