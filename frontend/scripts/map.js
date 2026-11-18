// Interface da equipe: Leaflet e OpenStreetMap, agora alimentados pela API Flask.
const $ = id => document.getElementById(id);
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const brl = value => Number(value).toLocaleString('pt-BR', { style: 'currency', currency: 'BRL' });
const when = value => new Date(value).toLocaleString('pt-BR');
let token = ''; // A sessão termina ao recarregar a página; nenhum segredo fica no armazenamento.
let profile = null;
let config = {};
let lots = [];
let selected = '';
let quoteSequence = 0;
let busy = false;
let map = null;
let markers = null;

async function api(path, options = {}) {
  const response = await fetch(path, {
    ...options,
    headers: { 'Content-Type': 'application/json', ...(token ? { Authorization: `Bearer ${token}` } : {}), ...options.headers }
  });
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || `Falha HTTP ${response.status}`);
  return data;
}
const post = (path, data = {}) => api(path, { method: 'POST', body: JSON.stringify(data) });
function message(id, error) { $(id).textContent = error.message || error; }
function dates() { return { arrival: $('arrival').value, departure: $('departure').value }; }
function tab(id) {
  for (const key of ['reservar', 'minhas-reservas', 'operador']) {
    $(key).classList.toggle('hidden', id !== key);
    $(`tab-${key}`).setAttribute('aria-current', String(id === key));
  }
  if (id === 'minhas-reservas') history().catch(e => message('msgRetorno', e));
  if (id === 'operador') admin().catch(e => message('adminAlertas', e));
}
for (const key of ['reservar', 'minhas-reservas', 'operador']) $(`tab-${key}`).onclick = () => tab(key);

function initMap() {
  if (!globalThis.L) {
    $('map').textContent = 'Mapa indisponível. Verifique sua internet; a seleção de locais continua disponível.';
    return;
  }
  map = L.map('map').setView([-12.99, -38.52], 12);
  L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19, attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>'
  }).addTo(map);
  markers = L.layerGroup().addTo(map);
}
function choose(key) {
  selected = key;
  $('lotSelect').value = key;
  const lot = lots.find(l => l.id === key);
  $('lotCard').innerHTML = lot ? `<strong>${esc(lot.name)}</strong><p>${esc(lot.address)}</p><p>${brl(lot.hourly_rate)}/h · ${lot.free_spaces}/${lot.total_spaces} vagas na próxima hora</p>` : 'Nenhum estacionamento selecionado.';
  quote().catch(e => message('msgReserva', e));
}
function renderLots() {
  const query = $('lotFilter').value.toLocaleLowerCase('pt-BR');
  const filtered = lots.filter(l => `${l.name} ${l.address}`.toLocaleLowerCase('pt-BR').includes(query));
  $('lotSelect').innerHTML = '<option value="">Selecione um local</option>' + filtered.map(l => `<option value="${esc(l.id)}">${esc(l.name)}</option>`).join('');
  $('lotSelect').value = selected;
  if (markers) {
    markers.clearLayers();
    for (const lot of filtered) {
      L.marker([lot.latitude, lot.longitude]).addTo(markers).bindPopup(`<strong>${esc(lot.name)}</strong><br>${esc(lot.address)}<br>${brl(lot.hourly_rate)}/h<br>Vagas: ${lot.free_spaces} de ${lot.total_spaces}<br>Clique no marcador para selecionar.`).on('click', () => choose(lot.id));
    }
  }
}
async function refreshLots() { lots = await api('/api/parking-lots'); renderLots(); }
$('lotFilter').oninput = renderLots;
$('lotSelect').onchange = () => choose($('lotSelect').value);
async function quote() {
  const sequence = ++quoteSequence;
  $('btnPagar').disabled = true;
  $('priceEstimate').textContent = '—';
  $('spacesAvailable').textContent = '—';
  message('msgReserva', '');
  if (!selected || !dates().arrival || !dates().departure) return;
  try {
    const data = await api(`/api/parking-lots/${encodeURIComponent(selected)}/availability?${new URLSearchParams(dates())}`);
    if (sequence !== quoteSequence) return;
    $('priceEstimate').textContent = brl(data.price_estimate);
    $('spacesAvailable').textContent = data.free_spaces;
    $('btnPagar').disabled = !data.available || !profile || busy;
    if (!profile) message('msgReserva', 'Entre para reservar.');
  } catch (e) { if (sequence === quoteSequence) message('msgReserva', e); }
}
$('arrival').onchange = quote;
$('departure').onchange = quote;
$('formReserva').onsubmit = async event => {
  event.preventDefault();
  if (busy) return;
  busy = true;
  $('btnPagar').disabled = true;
  try {
    const r = await post('/api/reservations', { parking_lot_id: selected, ...dates() });
    message('msgReserva', `Reserva ${r.id} criada. Aguarda pagamento; expira em 15 minutos. Se o checkout falhar, retome em Minhas reservas.`);
    await refreshLots();
    const checkout = await post(`/api/reservations/${r.id}/checkout`);
    location.assign(checkout.checkout_url);
  } catch (e) { message('msgReserva', e); }
  finally { busy = false; }
};

$('btnLogin').onclick = async () => {
  if (token) {
    token = ''; profile = null;
    $('btnLogin').textContent = 'Entrar'; $('userName').textContent = '';
    $('listaReservas').textContent = 'Entre para consultar suas reservas.';
    $('adminContent').classList.add('hidden'); $('adminLocked').classList.remove('hidden');
    await quote();
  } else $('dlgLogin').showModal();
};
$('loginCancel').onclick = () => $('dlgLogin').close();
$('formLogin').onsubmit = async event => {
  event.preventDefault();
  try {
    token = config.mode === 'demo' ? $('demoToken').value : (await post('/api/session', { email: $('loginEmail').value, password: $('loginPass').value })).token;
    profile = await api('/api/me');
    $('userName').textContent = profile.operator ? 'Operador' : 'Motorista';
    $('btnLogin').textContent = 'Sair';
    $('loginPass').value = ''; $('demoToken').value = '';
    $('dlgLogin').close();
    await quote();
    if (!$('operador').classList.contains('hidden')) await admin();
    if (!$('minhas-reservas').classList.contains('hidden')) await history();
  } catch (e) { token = ''; profile = null; message('msgLogin', e); }
};
async function history() {
  if (!profile) { $('listaReservas').textContent = 'Entre para consultar suas reservas.'; return; }
  const reservations = await api('/api/reservations');
  $('listaReservas').innerHTML = reservations.length ? reservations.map(r => `<article class="res"><header><strong>${esc(r.id)}</strong><span class="tag">${esc(r.status)}</span></header><p>${esc(lots.find(l => l.id === r.parking_lot_id)?.name || r.parking_lot_id)} · vaga ${esc(r.space_id)}</p><p>${when(r.arrival_expected)} até ${when(r.departure_expected)}</p><p>Estimado: ${brl(r.price_estimate)}</p>${r.status === 'PENDING_PAYMENT' ? `<button type="button" data-pay="${esc(r.id)}">Pagar agora</button>` : ''}${['PENDING_PAYMENT', 'CONFIRMED'].includes(r.status) ? `<button type="button" data-cancel="${esc(r.id)}">Cancelar reserva</button>` : ''}${r.status === 'COMPLETED' ? `<button type="button" data-ticket="${esc(r.id)}">Ver ticket</button>` : ''}</article>`).join('') : '<p>Nenhuma reserva.</p>';
}
$('listaReservas').onclick = async event => {
  const button = event.target.closest('button');
  if (!button) return;
  button.disabled = true;
  try {
    if (button.dataset.pay) location.assign((await post(`/api/reservations/${button.dataset.pay}/checkout`)).checkout_url);
    if (button.dataset.cancel) { await post(`/api/reservations/${button.dataset.cancel}/cancel`); await history(); await refreshLots(); await quote(); }
    if (button.dataset.ticket) {
      const { ticket } = await api(`/api/reservations/${button.dataset.ticket}`);
      message('msgRetorno', ticket ? `Ticket ${ticket.ticket_id} · ${when(ticket.arrival_real)} até ${when(ticket.departure_real)} · ${ticket.duration_minutes} min · ${brl(ticket.final_amount)} · pagamento ${ticket.payment_status} · Telegram ${ticket.telegram_status}` : 'Ticket indisponível.');
    }
  } catch (e) { message('msgRetorno', e); }
  finally { button.disabled = false; }
};
$('btnTelegram').onclick = async () => {
  try {
    const data = await post('/api/telegram/link');
    $('msgTelegram').replaceChildren();
    if (data.url) {
      const a = document.createElement('a'); a.href = data.url; a.textContent = 'Abrir bot e vincular minha conta'; a.target = '_blank'; a.rel = 'noopener'; $('msgTelegram').append(a);
    } else message('msgTelegram', data.instruction);
  } catch (e) { message('msgTelegram', e); }
};
async function admin() {
  const allowed = Boolean(profile?.operator);
  $('adminLocked').classList.toggle('hidden', allowed); $('adminContent').classList.toggle('hidden', !allowed);
  if (!allowed) return;
  const data = await api('/api/admin/dashboard');
  $('adminOcupacao').innerHTML = data.lots.map(l => `<p>${esc(l.name)} <meter min="0" max="${l.total || 1}" value="${l.occupied}"></meter> ${l.occupied}/${l.total} ocupadas</p>`).join('');
  $('adminReservas').innerHTML = '<table><thead><tr><th>Reserva</th><th>Local</th><th>Chegada</th><th>Status</th></tr></thead><tbody>' + data.reservations.map(r => `<tr><td>${esc(r.id)}</td><td>${esc(r.parking_lot_id)}</td><td>${when(r.arrival_expected)}</td><td>${esc(r.status)}</td></tr>`).join('') + '</tbody></table>';
  $('adminAlertas').innerHTML = data.alerts.length ? data.alerts.map(a => `<p>${esc(a.text)} · ${esc(a.status)}</p>`).join('') : '<p>Nenhum alerta.</p>';
}
$('formLot').onsubmit = async event => {
  event.preventDefault();
  try {
    const data = Object.fromEntries(new FormData(event.target));
    data.latitude = Number(data.latitude); data.longitude = Number(data.longitude); data.hourly_rate = Number(data.hourly_rate); data.spaces = Number(data.spaces);
    await post('/api/admin/parking-lots', data);
    message('msgLot', 'Estacionamento e vagas cadastrados.');
    event.target.reset(); await refreshLots(); await admin();
  } catch (e) { message('msgLot', e); }
};
$('btnRetry').onclick = async () => {
  try { const data = await post('/api/admin/notifications/retry'); message('msgRetry', `${data.processed} notificações processadas.`); await admin(); }
  catch (e) { message('msgRetry', e); }
};
async function init() {
  initMap();
  config = await api('/api/config');
  $('modeBadge').textContent = config.mode === 'demo' ? 'Demonstração local · dados temporários' : 'Estacionamento inteligente · Firebase';
  if (config.mode === 'demo') {
    for (const id of ['demoLabel', 'demoToken']) $(id).classList.remove('hidden');
    for (const id of ['loginEmail', 'loginPass']) { $(id).required = false; $(id).classList.add('hidden'); document.querySelector(`label[for="${id}"]`).classList.add('hidden'); }
  }
  await refreshLots();
  if (map && lots.length) map.fitBounds(lots.map(l => [l.latitude, l.longitude]), { padding: [40, 40], maxZoom: 13 });
  if (location.search.includes('payment_return')) message('msgRetorno', 'Retorno do checkout recebido. Entre e consulte o status: somente o webhook verificado confirma o pagamento.');
  setInterval(async () => {
    try { await refreshLots(); if (!$('minhas-reservas').classList.contains('hidden')) await history(); if (!$('operador').classList.contains('hidden')) await admin(); }
    catch (e) { message('msgReserva', e); }
  }, 30000);
}
init().catch(e => message('msgReserva', e));
