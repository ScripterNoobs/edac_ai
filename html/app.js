const API_BASE = 'http://localhost:8080';

const statusEl = document.getElementById('healthStatus');
const chatForm = document.getElementById('chatForm');
const chatResult = document.getElementById('chatResult');
const audioForm = document.getElementById('audioForm');
const audioResult = document.getElementById('audioResult');
const intentLabel = document.getElementById('intentLabel');
const searchForm = document.getElementById('searchForm');
const searchResult = document.getElementById('searchResult');
const healthForm = document.getElementById('healthForm');
const healthResult = document.getElementById('healthResult');
const toolGrid = document.getElementById('toolGrid');
const subscriptionGrid = document.getElementById('subscriptionGrid');
const accountForm = document.getElementById('accountForm');
const planForm = document.getElementById('planForm');
const accountResult = document.getElementById('accountResult');
const planSelect = document.getElementById('planSelect');
const planSelectUpdate = document.getElementById('planSelectUpdate');
const sessionResult = document.getElementById('sessionResult');
const diagnosticsResult = document.getElementById('diagnosticsResult');
const healthStatusLarge = document.getElementById('healthStatusLarge');
const sessionCount = document.getElementById('sessionCount');
const accountCount = document.getElementById('accountCount');
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

searchForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  searchResult.textContent = 'Aranıyor...';
  const query = document.getElementById('searchQuery').value;
  if (!query) {
    searchResult.textContent = 'Lütfen bir sorgu girin';
    return;
  }
  try {
    const res = await fetch(`${API_BASE}/api/search`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ query }),
    });
    if (!res.ok) throw new Error('Arama başarısız');
    const data = await res.json();
    searchResult.textContent = JSON.stringify(data, null, 2);
  } catch (err) {
    searchResult.textContent = err.message;
  }
});

healthForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  healthResult.textContent = 'Belirtiler değerlendiriliyor...';
  const symptoms = document.getElementById('symptoms').value;
  if (!symptoms) {
    healthResult.textContent = 'Belirti giriniz';
    return;
  }
  const duration_days = document.getElementById('duration').value;
  const age = document.getElementById('age').value;
  try {
    const res = await fetch(`${API_BASE}/api/health/diagnose`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ symptoms, duration_days, age }),
    });
    if (!res.ok) throw new Error('Sağlık değerlendirmesi başarısız');
    const data = await res.json();
    healthResult.textContent = JSON.stringify(data, null, 2);
  } catch (err) {
    healthResult.textContent = err.message;
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
    if (accountCount) accountCount.textContent = data.accounts ?? 0;
    diagTimestamp.textContent = data.timestamp;
  } catch (err) {
    diagnosticsResult.textContent = err.message;
  }
}

async function fetchSubscriptions() {
  try {
    const res = await fetch(`${API_BASE}/api/subscriptions`);
    if (!res.ok) throw new Error('Planlar alınamadı');
    const data = await res.json();
    const options = data.plans
      .map((p) => `<option value="${p.id}">${p.name} (${p.price})</option>`)
      .join('');
    planSelect.innerHTML = options;
    planSelectUpdate.innerHTML = options;
    subscriptionGrid.innerHTML = data.plans
      .map(
        (p) => `
        <div class="plan-card ${p.popular ? 'popular' : ''}">
          <div class="pill">${p.name}</div>
          <div class="price">${p.price}<small>/mo</small></div>
          <ul>
            ${p.features.map((f) => `<li>${f}</li>`).join('')}
          </ul>
          <button type="button" data-plan="${p.id}" class="plan-btn">${p.popular ? 'Upgrade' : 'Seç'}</button>
        </div>`
      )
      .join('');

    document.querySelectorAll('.plan-btn').forEach((btn) => {
      btn.addEventListener('click', () => {
        planSelect.value = btn.dataset.plan;
        planSelectUpdate.value = btn.dataset.plan;
        accountResult.textContent = `Plan seçildi: ${btn.dataset.plan}. Formdan gönderin.`;
      });
    });
  } catch (err) {
    subscriptionGrid.innerHTML = `<div class="plan-card">${err.message}</div>`;
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

accountForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  accountResult.textContent = 'Hesap oluşturuluyor...';
  const name = document.getElementById('accountName').value;
  const email = document.getElementById('accountEmail').value;
  if (!name || !email) {
    accountResult.textContent = 'İsim ve e-posta zorunlu';
    return;
  }
  try {
    const res = await fetch(`${API_BASE}/api/account/create`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name, email, plan_id: planSelect.value }),
    });
    if (!res.ok) throw new Error('Hesap oluşturulamadı');
    const data = await res.json();
    accountResult.textContent = JSON.stringify(data, null, 2);
    document.getElementById('accountId').value = data.account_id;
  } catch (err) {
    accountResult.textContent = err.message;
  }
});

planForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  accountResult.textContent = 'Abonelik güncelleniyor...';
  const account_id = document.getElementById('accountId').value;
  if (!account_id) {
    accountResult.textContent = 'Önce hesap ID girin';
    return;
  }
  try {
    const res = await fetch(`${API_BASE}/api/subscriptions/select`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ account_id, plan_id: planSelectUpdate.value }),
    });
    if (!res.ok) throw new Error('Güncelleme hatası');
    const data = await res.json();
    accountResult.textContent = JSON.stringify(data, null, 2);
  } catch (err) {
    accountResult.textContent = err.message;
  }
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
fetchSubscriptions();
