const API_BASE = 'http://localhost:8080';

const statusEl = document.getElementById('healthStatus');
const chatForm = document.getElementById('chatForm');
const chatResult = document.getElementById('chatResult');
const audioForm = document.getElementById('audioForm');
const audioResult = document.getElementById('audioResult');

async function checkHealth() {
  try {
    const res = await fetch(`${API_BASE}/health`);
    if (res.ok) {
      const data = await res.json();
      statusEl.textContent = `API aktif • ${data.timestamp}`;
      statusEl.style.color = '#10b981';
    } else {
      statusEl.textContent = 'API yanıt vermiyor';
      statusEl.style.color = '#f97316';
    }
  } catch (err) {
    statusEl.textContent = 'API bağlantısı kurulamadı';
    statusEl.style.color = '#ef4444';
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

checkHealth();
