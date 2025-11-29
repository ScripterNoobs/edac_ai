const API_BASE = 'http://localhost:8080';

const statusEl = document.getElementById('healthStatus');
const chatForm = document.getElementById('chatForm');
const chatResult = document.getElementById('chatResult');
const audioForm = document.getElementById('audioForm');
const audioResult = document.getElementById('audioResult');
const intentLabel = document.getElementById('intentLabel');
const toolGrid = document.getElementById('toolGrid');
const sessionResult = document.getElementById('sessionResult');
const diagnosticsResult = document.getElementById('diagnosticsResult');
const healthStatusLarge = document.getElementById('healthStatusLarge');
const sessionCount = document.getElementById('sessionCount');
const diagTimestamp = document.getElementById('diagTimestamp');

async function checkHealth() {
  try {
    const res = await fetch(`${API_BASE}/health`);
    if (res.ok) {
      const data = await res.json();
      statusEl.textContent = `API aktif • ${data.timestamp}`;
      statusEl.style.color = '#10b981';
      healthStatusLarge.textContent = 'OK';
    } else {
      statusEl.textContent = 'API yanıt vermiyor';
      statusEl.style.color = '#f97316';
      healthStatusLarge.textContent = 'Hata';
    }
  } catch (err) {
    statusEl.textContent = 'API bağlantısı kurulamadı';
    statusEl.style.color = '#ef4444';
    healthStatusLarge.textContent = 'Kapalı';
  }
}

chatForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  chatResult.textContent = 'İstek gönderiliyor...';

  const payload = {
    text: document.getElementById('inputText').value,
    mode: document.getElementById('mode').value,
    session_id: document.getElementById('sessionId').value || 'demo',
    stream: document.getElementById('stream').checked,
  };

  try {
    const res = await fetch(`${API_BASE}/api/chat`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });

    if (!res.ok) throw new Error('Sunucu hatası');
    const data = await res.json();
    chatResult.textContent = JSON.stringify(data, null, 2);
    intentLabel.textContent = data.intent_label || data.intent || 'intent?';
  } catch (err) {
    chatResult.textContent = `Hata: ${err.message}`;
  }
});

async function fileToBase64(file) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(reader.result.split(',')[1]);
    reader.onerror = reject;
    reader.readAsDataURL(file);
  });
}

audioForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  audioResult.textContent = 'Ses hazırlanıyor...';
  const file = document.getElementById('audioFile').files[0];
  if (!file) {
    audioResult.textContent = 'Lütfen bir ses dosyası seçin.';
    return;
  }

  try {
    const audio_b64 = await fileToBase64(file);
    const payload = {
      audio_b64,
      transcript_hint: document.getElementById('transcript').value,
    };

    const res = await fetch(`${API_BASE}/api/audio/analyze`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload),
    });

    if (!res.ok) throw new Error('Sunucu hatası');
    const data = await res.json();
    audioResult.textContent = JSON.stringify(data, null, 2);
  } catch (err) {
    audioResult.textContent = `Hata: ${err.message}`;
  }
});

async function fetchTools() {
  try {
    const res = await fetch(`${API_BASE}/api/tools`);
    if (!res.ok) throw new Error('Araç listesi alınamadı');
    const data = await res.json();
    toolGrid.innerHTML = data.tools
      .map(
        (tool) => `
        <div class="tool-card">
          <div class="name">${tool.name}</div>
          <div class="desc">${tool.description}</div>
        </div>`
      )
      .join('');
  } catch (err) {
    toolGrid.innerHTML = `<div class="tool-card">${err.message}</div>`;
  }
}

async function fetchDiagnostics() {
  try {
    const res = await fetch(`${API_BASE}/api/diagnostics`);
    if (!res.ok) throw new Error('Diagnostik alınamadı');
    const data = await res.json();
    diagnosticsResult.textContent = JSON.stringify(data, null, 2);
    sessionCount.textContent = data.sessions;
    diagTimestamp.textContent = data.timestamp;
  } catch (err) {
    diagnosticsResult.textContent = err.message;
  }
}

async function fetchSession(sessionId) {
  try {
    const res = await fetch(`${API_BASE}/api/session/${sessionId}`);
    if (!res.ok) throw new Error('Oturum getirilemedi');
    const data = await res.json();
    sessionResult.textContent = JSON.stringify(data, null, 2);
  } catch (err) {
    sessionResult.textContent = err.message;
  }
}

document.getElementById('loadSession').addEventListener('click', async () => {
  const id = document.getElementById('sessionLookup').value || 'default';
  sessionResult.textContent = 'Yükleniyor...';
  await fetchSession(id);
});

document.querySelectorAll('.nav-btn').forEach((btn) => {
  btn.addEventListener('click', () => {
    document.querySelectorAll('.nav-btn').forEach((b) => b.classList.remove('active'));
    btn.classList.add('active');
    const target = btn.getAttribute('data-target');
    document.querySelectorAll('.panel').forEach((panel) => {
      panel.classList.toggle('show', panel.id === target);
    });
  });
});

checkHealth();
fetchTools();
fetchDiagnostics();
