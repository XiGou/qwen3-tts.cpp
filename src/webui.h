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
  .subtitle { color: #94a3b8; font-size: 0.9rem; margin-bottom: 24px; text-align: center; }
  .app {
    width: 100%;
    max-width: 880px;
    display: flex;
    flex-direction: column;
    gap: 20px;
  }
  .card {
    background: #1e2130;
    border: 1px solid #2d3448;
    border-radius: 12px;
    padding: 24px;
  }
  .card h2 {
    font-size: 1rem;
    color: #94a3b8;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    margin-bottom: 16px;
  }
  .card h3 {
    font-size: 0.98rem;
    margin-bottom: 10px;
    color: #cbd5e1;
  }
  .tabs {
    display: flex;
    flex-wrap: wrap;
    gap: 10px;
    margin-bottom: 18px;
  }
  .tab-btn {
    background: #131826;
    border: 1px solid #2d3448;
    color: #cbd5e1;
    padding: 10px 14px;
    border-radius: 999px;
    font-size: 0.92rem;
    cursor: pointer;
  }
  .tab-btn.active {
    background: #1d4ed8;
    border-color: #3b82f6;
    color: #fff;
  }
  .tab-panel { display: none; }
  .tab-panel.active { display: block; }
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
  textarea { resize: vertical; min-height: 110px; }
  textarea.mono {
    font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    min-height: 200px;
  }
  .row { display: flex; gap: 16px; flex-wrap: wrap; }
  .row > .field { flex: 1; min-width: 140px; }
  .field { margin-bottom: 16px; }
  button {
    background: #3b82f6;
    color: #fff;
    border: none;
    border-radius: 8px;
    padding: 10px 18px;
    font-size: 0.95rem;
    cursor: pointer;
    transition: background .2s;
  }
  button:hover:not(:disabled) { background: #2563eb; }
  button:disabled { background: #374151; cursor: not-allowed; }
  .secondary-btn { background: #334155; }
  .secondary-btn:hover:not(:disabled) { background: #475569; }
  #status {
    font-size: 0.88rem;
    color: #94a3b8;
    min-height: 1.4em;
    white-space: pre-wrap;
  }
  #status.error { color: #f87171; }
  #status.ok { color: #86efac; }
  audio { width: 100%; margin-top: 12px; border-radius: 8px; }
  .hidden { display: none !important; }
  .tip { font-size: 0.78rem; color: #64748b; margin-top: 4px; line-height: 1.45; }
  .mode-note {
    margin-bottom: 16px;
    color: #a5b4fc;
    font-size: 0.85rem;
    line-height: 1.5;
  }
  .actions { display: flex; gap: 12px; flex-wrap: wrap; align-items: center; }
  input[type=file] {
    background: #0f1117;
    border: 1px dashed #2d3448;
    border-radius: 8px;
    color: #94a3b8;
    padding: 8px 12px;
    width: 100%;
    cursor: pointer;
  }
  .result-link {
    display: inline-block;
    margin-top: 12px;
    color: #7dd3fc;
    text-decoration: none;
  }
  .result-link:hover { text-decoration: underline; }
</style>
</head>
<body>
<h1>🎙️ Qwen3-TTS WebUI</h1>
<p class="subtitle">与当前 C++ 实现对齐：基础合成、参考音色克隆、预计算 Speaker Embedding 三种入口。</p>

<div class="app">
  <div class="card">
    <h2>Common Parameters</h2>

    <div class="field">
      <label for="text">Text</label>
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
        <p class="tip">0 = greedy；值越高随机性越强。</p>
      </div>
      <div class="field">
        <label for="topk">Top-k</label>
        <input type="number" id="topk" value="50" min="0" max="200">
      </div>
      <div class="field">
        <label for="topp">Top-p</label>
        <input type="number" id="topp" value="1.0" min="0" max="1" step="0.01">
      </div>
    </div>

    <div class="row">
      <div class="field">
        <label for="max_tokens">Max audio tokens</label>
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

  <div class="card">
    <h2>Modes</h2>
    <div class="tabs">
      <button class="tab-btn active" data-tab="basic">Tab 1 · 基础合成</button>
      <button class="tab-btn" data-tab="clone">Tab 2 · 参考音频克隆</button>
      <button class="tab-btn" data-tab="embedding">Tab 3 · Embedding 模式</button>
    </div>

    <div class="tab-panel active" data-panel="basic">
      <p class="mode-note">对应 <code>synthesize(text, params)</code>：不提供参考音频，使用默认音色。</p>
      <div class="actions">
        <button class="synth-btn" data-mode="basic">▶ 开始基础合成</button>
      </div>
    </div>

    <div class="tab-panel" data-panel="clone">
      <p class="mode-note">对应 <code>synthesize_with_voice(text, reference_audio, params)</code>：上传参考音频后直接进行音色克隆。</p>
      <div class="field">
        <label for="ref_audio">Reference audio</label>
        <input type="file" id="ref_audio" accept=".wav,audio/*">
        <p class="tip">建议使用 24kHz 单声道 WAV；如果采样率不同，后端会自动重采样。</p>
      </div>
      <div class="actions">
        <button class="synth-btn" data-mode="clone">▶ 开始音色克隆</button>
      </div>
    </div>

    <div class="tab-panel" data-panel="embedding">
      <p class="mode-note">对应 <code>extract_speaker_embedding(...)</code> + <code>synthesize_with_embedding(...)</code>：适合复用同一个说话人 embedding，避免重复做 speaker encoder。</p>
      <div class="field">
        <label for="embedding_audio">Reference audio for embedding extraction</label>
        <input type="file" id="embedding_audio" accept=".wav,audio/*">
        <p class="tip">可先上传参考音频并点击“提取 Embedding”，提取结果会填入下方文本框，可复制保存后重复使用。</p>
      </div>
      <div class="actions" style="margin-bottom: 16px;">
        <button id="btn_extract_embedding" class="secondary-btn">⤵ 提取 Embedding</button>
      </div>
      <div class="field">
        <label for="embedding_text">Speaker embedding</label>
        <textarea id="embedding_text" class="mono" placeholder="Paste JSON array or comma/space-separated float values here."></textarea>
        <p class="tip">支持 JSON 数组，或以逗号 / 空格 / 换行分隔的 float 列表。通常长度应为 1024。</p>
      </div>
      <div class="actions">
        <button class="synth-btn" data-mode="embedding">▶ 使用 Embedding 合成</button>
      </div>
    </div>
  </div>

  <div class="card">
    <h2>Result</h2>
    <p id="status"></p>
    <audio id="player" controls class="hidden"></audio>
    <a id="dl_link" class="result-link hidden" download="output.wav">⬇ Download WAV</a>
  </div>
</div>

<script>
(function () {
  const tabButtons = Array.from(document.querySelectorAll('.tab-btn'));
  const panels = Array.from(document.querySelectorAll('.tab-panel'));
  const synthButtons = Array.from(document.querySelectorAll('.synth-btn'));
  const statusEl = document.getElementById('status');
  const playerEl = document.getElementById('player');
  const dlLink = document.getElementById('dl_link');
  const extractBtn = document.getElementById('btn_extract_embedding');
  const embeddingTextEl = document.getElementById('embedding_text');

  function setStatus(msg, cls) {
    statusEl.textContent = msg;
    statusEl.className = cls || '';
  }

  function activeTab() {
    const current = document.querySelector('.tab-btn.active');
    return current ? current.dataset.tab : 'basic';
  }

  function setBusyState(busy) {
    synthButtons.forEach((btn) => { btn.disabled = busy; });
    extractBtn.disabled = busy;
  }

  function resetAudioResult() {
    playerEl.classList.add('hidden');
    dlLink.classList.add('hidden');
    if (playerEl.src) {
      URL.revokeObjectURL(playerEl.src);
      playerEl.removeAttribute('src');
    }
  }

  function buildCommonFormData() {
    const text = document.getElementById('text').value.trim();
    if (!text) {
      throw new Error('Please enter some text.');
    }

    const formData = new FormData();
    formData.append('text', text);
    formData.append('language', document.getElementById('language').value);
    formData.append('temperature', document.getElementById('temperature').value);
    formData.append('top_k', document.getElementById('topk').value);
    formData.append('top_p', document.getElementById('topp').value);
    formData.append('max_tokens', document.getElementById('max_tokens').value);
    formData.append('rep_penalty', document.getElementById('rep_penalty').value);
    formData.append('threads', document.getElementById('threads').value);
    return formData;
  }

  async function runSynthesis(mode) {
    resetAudioResult();

    let formData;
    try {
      formData = buildCommonFormData();
    } catch (err) {
      setStatus(err.message, 'error');
      return;
    }

    formData.append('mode', mode);

    if (mode === 'clone') {
      const refFile = document.getElementById('ref_audio').files[0];
      if (!refFile) {
        setStatus('Voice cloning 模式需要上传参考音频。', 'error');
        return;
      }
      formData.append('ref_audio', refFile);
    }

    if (mode === 'embedding') {
      const embeddingText = embeddingTextEl.value.trim();
      if (!embeddingText) {
        setStatus('Embedding 模式需要先提取或粘贴 speaker embedding。', 'error');
        return;
      }
      formData.append('speaker_embedding', embeddingText);
    }

    setBusyState(true);
    setStatus('Synthesizing… (this may take a while)');

    try {
      const resp = await fetch('/api/synthesize', { method: 'POST', body: formData });
      if (!resp.ok) {
        const err = await resp.json().catch(() => ({ error: resp.statusText }));
        setStatus('Error: ' + (err.error || resp.statusText), 'error');
        return;
      }

      const blob = await resp.blob();
      const url = URL.createObjectURL(blob);
      playerEl.src = url;
      playerEl.classList.remove('hidden');
      dlLink.href = url;
      dlLink.classList.remove('hidden');

      const duration = parseFloat(resp.headers.get('X-Audio-Duration') || '0');
      const timing = resp.headers.get('X-Timing-Ms') || '';
      const modeName = resp.headers.get('X-Synthesis-Mode') || mode;
      setStatus(
        'Done! Mode: ' + modeName +
        ' | Duration: ' + (duration > 0 ? duration.toFixed(2) + 's' : 'unknown') +
        (timing ? ' | Inference: ' + timing + ' ms' : ''),
        'ok'
      );
    } catch (err) {
      setStatus('Network error: ' + err.message, 'error');
    } finally {
      setBusyState(false);
    }
  }

  async function extractEmbedding() {
    const refFile = document.getElementById('embedding_audio').files[0];
    if (!refFile) {
      setStatus('请先上传用于提取 speaker embedding 的参考音频。', 'error');
      return;
    }

    setBusyState(true);
    setStatus('Extracting speaker embedding…');

    try {
      const formData = new FormData();
      formData.append('ref_audio', refFile);
      const resp = await fetch('/api/extract_embedding', { method: 'POST', body: formData });
      if (!resp.ok) {
        const err = await resp.json().catch(() => ({ error: resp.statusText }));
        setStatus('Error: ' + (err.error || resp.statusText), 'error');
        return;
      }

      const data = await resp.json();
      embeddingTextEl.value = JSON.stringify(data.embedding);
      setStatus('Embedding extracted successfully. Dimension: ' + data.embedding_size, 'ok');
    } catch (err) {
      setStatus('Network error: ' + err.message, 'error');
    } finally {
      setBusyState(false);
    }
  }

  tabButtons.forEach((btn) => {
    btn.addEventListener('click', () => {
      const target = btn.dataset.tab;
      tabButtons.forEach((item) => item.classList.toggle('active', item === btn));
      panels.forEach((panel) => panel.classList.toggle('active', panel.dataset.panel === target));
      setStatus('当前模式：' + btn.textContent, '');
    });
  });

  synthButtons.forEach((btn) => {
    btn.addEventListener('click', () => runSynthesis(btn.dataset.mode));
  });

  extractBtn.addEventListener('click', extractEmbedding);
  setStatus('当前模式：' + tabButtons.find((btn) => btn.classList.contains('active')).textContent, '');
})();
</script>
</body>
</html>
)WEBUI";

} // namespace webui
