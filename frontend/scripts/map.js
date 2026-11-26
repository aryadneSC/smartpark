// Interface da equipe: Leaflet e OpenStreetMap, agora alimentados pela API Flask.
const $ = id => document.getElementById(id);
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const brl = value => Number(value).toLocaleString('pt-BR', { style: 'currency', currency: 'BRL' });
const when = value => new Date(value).toLocaleString('pt-BR');
let token = ''; // A sessão termina ao recarregar a página; nenhum segredo fica no armazenamento.
let profile = null;
let config = { mode: 'firestore' };
let lots = [];
let selected = '';
let quoteSequence = 0;
let busy = false;
let map = null;
let markers = null;
let demoActive = false;
let demoPaymentReservation = '';
const demoLots = [
  { id: 'centro', name: 'Unidade Centro', address: 'Local fictício — Centro', latitude: -23.5614, longitude: -46.6559, hourly_rate: 12, total_spaces: 12, prefix: 'A' },
  { id: 'pinheiros', name: 'Unidade Pinheiros', address: 'Local fictício — Pinheiros', latitude: -23.5668, longitude: -46.6925, hourly_rate: 9, total_spaces: 10, prefix: 'B' },
  { id: 'vila', name: 'Unidade Vila Madalena', address: 'Local fictício — Vila Madalena', latitude: -23.5546, longitude: -46.6914, hourly_rate: 10, total_spaces: 8, prefix: 'C' }
];
const demoState = { reservations: [], alerts: [], messages: [], telegramLinked: false, nextReservation: 1 };

function resetDemoState() {
  demoState.reservations = []; demoState.alerts = []; demoState.messages = [];
  demoState.telegramLinked = false; demoState.nextReservation = 1;
}
function demoAvailability(lot, start, end) {
  const now = Date.now();
  const blocking = demoState.reservations.filter(r => r.parking_lot_id === lot.id &&
    ['PENDING_PAYMENT', 'CONFIRMED', 'CHECKED_IN'].includes(r.status) &&
    (r.status === 'CHECKED_IN' || (r.status !== 'PENDING_PAYMENT' || new Date(r.expires_at).getTime() > now) &&
      new Date(r.arrival_expected) < end && new Date(r.departure_expected) > start));
  const used = new Set(blocking.map(r => r.space_id));
  const spaces = Array.from({ length: lot.total_spaces }, (_, i) => `${lot.id}_${lot.prefix}-${String(i + 1).padStart(2, '0')}`);
  return { free: spaces.filter(id => !used.has(id)), spaces };
}
function demoLotList() {
  const start = new Date(), end = new Date(start.getTime() + 60 * 60 * 1000);
  return demoLots.map(lot => ({ ...lot, free_spaces: demoAvailability(lot, start, end).free.length }));
}
function demoPushMessage(text) {
  demoState.messages.unshift({ text, at: new Date().toISOString() });
  renderDemoMessages();
}
function renderDemoMessages() {
  $('demoMessages').classList.toggle('hidden', !demoActive);
  $('demoMessageList').innerHTML = demoState.messages.length
    ? demoState.messages.map(item => `<article class="demo-message"><strong>SmartPark Bot · simulado</strong><p>${esc(item.text)}</p><small>${esc(when(item.at))}</small></article>`).join('')
    : '<p>Nenhuma mensagem ainda.</p>';
}
function demoBody(options) {
  try { return options.body ? JSON.parse(options.body) : {}; } catch { return {}; }
}
async function demoApi(path, options = {}) {
  const url = new URL(path, location.origin);
  const method = options.method || 'GET';
  const body = demoBody(options);
  if (url.pathname === '/api/config') return { mode: 'demo', firebase_api_key: '', telegram_bot: '' };
  if (url.pathname === '/api/parking-lots' && method === 'GET') return demoLotList();
  const availability = url.pathname.match(/^\/api\/parking-lots\/([^/]+)\/availability$/);
  if (availability && method === 'GET') {
    const lot = demoLots.find(item => item.id === decodeURIComponent(availability[1]));
    if (!lot) throw new Error('Estacionamento não encontrado.');
    const start = new Date(url.searchParams.get('arrival'));
    const end = new Date(url.searchParams.get('departure'));
    if (!Number.isFinite(start.getTime()) || !Number.isFinite(end.getTime()) || end <= start || start < new Date()) throw new Error('Informe um período futuro válido.');
    const available = demoAvailability(lot, start, end).free.length;
    const hours = Math.ceil((end - start) / 3600000);
    return { available: available > 0, free_spaces: available, price_estimate: lot.hourly_rate * hours };
  }
  if (url.pathname === '/api/reservations' && method === 'GET') {
    if (!profile) throw new Error('Entre para consultar suas reservas.');
    return demoState.reservations.filter(r => r.user_id === profile.uid).map(item => ({ ...item }));
  }
  if (url.pathname === '/api/reservations' && method === 'POST') {
    if (!profile || profile.operator) throw new Error('Entre como motorista para reservar.');
    const lot = demoLots.find(item => item.id === body.parking_lot_id);
    const start = new Date(body.arrival), end = new Date(body.departure);
    if (!lot || !Number.isFinite(start.getTime()) || !Number.isFinite(end.getTime()) || end <= start || start < new Date()) throw new Error('Estacionamento ou período inválido.');
    const { free } = demoAvailability(lot, start, end);
    if (!free.length) throw new Error('Nenhuma vaga disponível neste período.');
    const id = `SP-DEMO-${String(demoState.nextReservation++).padStart(4, '0')}`;
    const reservation = { id, user_id: profile.uid, parking_lot_id: lot.id, space_id: free[0], arrival_expected: start.toISOString(), departure_expected: end.toISOString(), created_at: new Date().toISOString(), expires_at: new Date(Date.now() + 15 * 60 * 1000).toISOString(), status: 'PENDING_PAYMENT', price_estimate: lot.hourly_rate * Math.ceil((end - start) / 3600000), payment_status: 'pending' };
    demoState.reservations.push(reservation);
    return { ...reservation };
  }
  const checkout = url.pathname.match(/^\/api\/reservations\/([^/]+)\/checkout$/);
  if (checkout && method === 'POST') return { demo: true, reservation_id: decodeURIComponent(checkout[1]) };
  const cancellation = url.pathname.match(/^\/api\/reservations\/([^/]+)\/cancel$/);
  if (cancellation && method === 'POST') {
    const reservation = demoState.reservations.find(r => r.id === decodeURIComponent(cancellation[1]) && r.user_id === profile?.uid);
    if (!reservation || !['PENDING_PAYMENT', 'CONFIRMED'].includes(reservation.status)) throw new Error('Reserva não pode ser cancelada.');
    reservation.status = 'CANCELLED';
    return { status: reservation.status };
  }
  const oneReservation = url.pathname.match(/^\/api\/reservations\/([^/]+)$/);
  if (oneReservation && method === 'GET') {
    const reservation = demoState.reservations.find(r => r.id === decodeURIComponent(oneReservation[1]) && r.user_id === profile?.uid);
    if (!reservation) throw new Error('Reserva não encontrada.');
    return { ...reservation, ticket: null };
  }
  if (url.pathname === '/api/telegram/link' && method === 'POST') {
    if (!profile) throw new Error('Entre para vincular o Telegram.');
    demoState.telegramLinked = true;
    demoPushMessage('Conta vinculada à conversa fictícia do SmartPark. Esta mensagem não foi enviada pelo Telegram real.');
    return { demo: true, instruction: 'Telegram vinculado no modo de demonstração. As mensagens aparecerão abaixo.' };
  }
  if (url.pathname === '/api/admin/dashboard' && method === 'GET') {
    if (!profile?.operator) throw new Error('Somente operador autorizado.');
    const start = new Date(), end = new Date(start.getTime() + 60 * 60 * 1000);
    return { lots: demoLots.map(lot => ({ id: lot.id, name: lot.name, total: lot.total_spaces, occupied: demoState.reservations.filter(r => r.parking_lot_id === lot.id && r.status === 'CHECKED_IN').length })), reservations: demoState.reservations.map(item => ({ ...item })), alerts: demoState.alerts.map(item => ({ ...item })), devices: [] };
  }
  throw new Error('Ação não disponível na demonstração local.');
}

async function api(path, options = {}) {
  if (demoActive) return demoApi(path, options);
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
  if (id === 'reservar') refreshLots().then(quote).catch(e => message('msgReserva', e));
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
    if (demoActive) { openDemoPayment(r.id); return; }
    const checkout = await post(`/api/reservations/${r.id}/checkout`);
    location.assign(checkout.checkout_url);
  } catch (e) { message('msgReserva', e); }
  finally { busy = false; }
};

$('btnLogin').onclick = async () => {
  if (token) {
    if (demoActive) { await exitDemo(); return; }
    token = ''; profile = null;
    $('btnLogin').textContent = 'Entrar'; $('userName').textContent = '';
    $('listaReservas').textContent = 'Entre para consultar suas reservas.';
    $('adminContent').classList.add('hidden'); $('adminLocked').classList.remove('hidden');
    await quote();
  } else { $('dlgLogin').showModal(); }
};
$('loginCancel').onclick = () => $('dlgLogin').close();
$('formLogin').onsubmit = async event => {
  event.preventDefault();
  if (demoActive) return;
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
async function enterDemo(role) {
  profile = { uid: role === 'operator' ? 'dev-demo-operator' : 'dev-demo-driver', operator: role === 'operator' };
  token = `dev-demo-${role}`;
  $('userName').textContent = role === 'operator' ? 'Operador · DEV' : 'Motorista · DEV';
  $('btnLogin').textContent = 'Sair';
  $('dlgDemo').close();
  renderDemoMessages();
  try {
    await refreshLots();
    await quote();
    if (!$('operador').classList.contains('hidden')) await admin();
    if (!$('minhas-reservas').classList.contains('hidden')) await history();
  } catch (e) { message('msgLogin', e); }
}
$('btnDemoDriver').onclick = () => enterDemo('driver');
$('btnDemoOperator').onclick = () => enterDemo('operator');
$('dlgDemo').addEventListener('cancel', event => { event.preventDefault(); exitDemo(); });
async function exitDemo() {
  demoActive = false; token = ''; profile = null;
  selected = ''; demoPaymentReservation = '';
  resetDemoState(); renderDemoMessages();
  $('dlgDemo').close(); $('dlgDemoPayment').close();
  $('modeBadge').textContent = config.mode === 'demo' ? 'Demonstração local · dados temporários' : 'Estacionamento inteligente · Firebase';
  $('btnDemoMode').textContent = 'Demonstração (DEV)';
  $('btnLogin').textContent = 'Entrar'; $('userName').textContent = '';
  $('adminContent').classList.add('hidden'); $('adminLocked').classList.remove('hidden');
  $('listaReservas').textContent = 'Entre para consultar suas reservas.';
  for (const key of ['reservar', 'minhas-reservas', 'operador']) {
    $(key).classList.toggle('hidden', key !== 'reservar');
    $(`tab-${key}`).setAttribute('aria-current', String(key === 'reservar'));
  }
  try { await refreshLots(); await quote(); } catch (e) { message('msgReserva', e); }
}
$('btnCancelDemo').onclick = () => exitDemo();
$('btnDemoMode').onclick = async () => {
  if (demoActive) { await exitDemo(); return; }
  demoActive = true;
  resetDemoState();
  token = ''; profile = null;
  selected = '';
  $('modeBadge').textContent = 'Demonstração DEV · dados fictícios';
  $('btnDemoMode').textContent = 'Sair da demonstração';
  $('btnLogin').textContent = 'Entrar'; $('userName').textContent = '';
  lots = demoLotList(); renderLots(); renderDemoMessages();
  for (const key of ['reservar', 'minhas-reservas', 'operador']) {
    $(key).classList.toggle('hidden', key !== 'reservar');
    $(`tab-${key}`).setAttribute('aria-current', String(key === 'reservar'));
  }
  $('dlgLogin').close();
  $('dlgDemo').showModal();
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
    if (button.dataset.pay) {
      if (demoActive) openDemoPayment(button.dataset.pay);
      else location.assign((await post(`/api/reservations/${button.dataset.pay}/checkout`)).checkout_url);
    }
    if (button.dataset.cancel) { await post(`/api/reservations/${button.dataset.cancel}/cancel`); await history(); await refreshLots(); await quote(); }
    if (button.dataset.ticket) {
      const { ticket } = await api(`/api/reservations/${button.dataset.ticket}`);
      message('msgRetorno', ticket ? `Ticket ${ticket.ticket_id} · ${when(ticket.arrival_real)} até ${when(ticket.departure_real)} · ${ticket.duration_minutes} min · ${brl(ticket.final_amount)} · pagamento ${ticket.payment_status} · Telegram ${ticket.telegram_status}` : 'Ticket indisponível.');
    }
  } catch (e) { message('msgRetorno', e); }
  finally { button.disabled = false; }
};
function openDemoPayment(reservationId) {
  const reservation = demoState.reservations.find(r => r.id === reservationId && r.user_id === profile?.uid);
  if (!reservation) { message('msgReserva', 'Reserva de demonstração não encontrada.'); return; }
  demoPaymentReservation = reservationId;
  $('demoPaymentSummary').textContent = `Reserva ${reservation.id} · ${brl(reservation.price_estimate)} · ${demoLots.find(l => l.id === reservation.parking_lot_id)?.name || reservation.parking_lot_id}`;
  $('dlgDemoPayment').showModal();
}
async function settleDemoPayment(status) {
  const reservation = demoState.reservations.find(r => r.id === demoPaymentReservation && r.user_id === profile?.uid);
  if (!reservation) return;
  reservation.payment_status = status;
  if (status === 'approved') {
    reservation.status = 'CONFIRMED';
    reservation.payment_id = `PAY-DEMO-${reservation.id}`;
    if (demoState.telegramLinked) demoPushMessage(`Pagamento aprovado. Reserva ${reservation.id} confirmada para ${demoLots.find(l => l.id === reservation.parking_lot_id)?.name || reservation.parking_lot_id}, vaga ${reservation.space_id}.`);
  } else if (demoState.telegramLinked) {
    demoPushMessage(`Pagamento da reserva ${reservation.id} recusado. Você pode tentar novamente em Minhas reservas.`);
  }
  $('dlgDemoPayment').close();
  demoPaymentReservation = '';
  message('msgReserva', status === 'approved' ? 'Pagamento simulado como aprovado; a reserva foi confirmada.' : 'Pagamento simulado como recusado; a reserva continua pendente.');
  await refreshLots(); await quote();
  if (!$('minhas-reservas').classList.contains('hidden')) await history();
  if (!$('operador').classList.contains('hidden') && profile?.operator) await admin();
}
$('btnApproveDemoPayment').onclick = () => settleDemoPayment('approved').catch(e => message('msgReserva', e));
$('btnRejectDemoPayment').onclick = () => settleDemoPayment('rejected').catch(e => message('msgReserva', e));
$('btnCancelDemoPayment').onclick = () => $('dlgDemoPayment').close();
$('btnTelegram').onclick = async () => {
  try {
    const data = await post('/api/telegram/link');
    $('msgTelegram').replaceChildren();
    if (data.demo) {
      message('msgTelegram', data.instruction);
      renderDemoMessages();
      return;
    }
    if (data.url) {
      const a = document.createElement('a'); a.href = data.url; a.textContent = 'Abrir bot e vincular minha conta'; a.target = '_blank'; a.rel = 'noopener'; $('msgTelegram').append(a);
    } else message('msgTelegram', data.instruction);
  } catch (e) { message('msgTelegram', e); }
};
function renderAdminReservations(data) {
  const statusNames = {
    PENDING_PAYMENT: 'Aguardando pagamento', CONFIRMED: 'Confirmada', CHECKED_IN: 'No local',
    COMPLETED: 'Concluída', EXPIRED: 'Expirada', CANCELLED: 'Cancelada',
    approved: 'Aprovado', pending: 'Pendente', rejected: 'Recusado', refunded: 'Reembolsado'
  };
  const statusName = value => statusNames[value] || value || '—';
  const lotName = id => data.lots.find(l => l.id === id)?.name || lots.find(l => l.id === id)?.name || id || '—';
  const reservations = data.reservations.slice()
    .sort((a, b) => new Date(b.created_at || b.arrival_expected) - new Date(a.created_at || a.arrival_expected));
  $('adminReservas').innerHTML = reservations.length ? reservations.map(r => `<article class="reservation-card"><div class="reservation-card-heading"><strong>${esc(r.id)}</strong><span class="status-pill status-${esc(r.status)}">${esc(statusName(r.status))}</span></div><p><strong>${esc(lotName(r.parking_lot_id))}</strong> · vaga ${esc(r.space_id || 'não atribuída')}</p><p>${when(r.arrival_expected)} até ${when(r.departure_expected)}</p><small>Pagamento: ${esc(statusName(r.payment_status))}</small></article>`).join('') : '<p class="empty-state">Nenhuma reserva registrada.</p>';
}
async function admin() {
  const allowed = Boolean(profile?.operator);
  $('adminLocked').classList.toggle('hidden', allowed); $('adminContent').classList.toggle('hidden', !allowed);
  if (!allowed) return;
  const data = await api('/api/admin/dashboard');
  $('adminSyncStatus').textContent = `Dados atualizados às ${new Date().toLocaleTimeString('pt-BR')} · próxima atualização em até 1 minuto.`;
  const statusNames = {
    PENDING_PAYMENT: 'Aguardando pagamento', CONFIRMED: 'Confirmada', CHECKED_IN: 'No local',
    COMPLETED: 'Concluída', EXPIRED: 'Expirada', CANCELLED: 'Cancelada',
    approved: 'Aprovado', pending: 'Pendente', rejected: 'Recusado', refunded: 'Reembolsado',
    PENDING_RETRY: 'Aguardando nova tentativa', SENDING: 'Enviando', MANUAL_REVIEW: 'Revisão manual',
    SENT: 'Enviada', FAILED: 'Falhou',
    PRONTO: 'Pronto', INICIALIZANDO: 'Inicializando', VALIDANDO: 'Validando',
    ACESSO_LIBERADO: 'Acesso liberado', ACESSO_NEGADO: 'Acesso negado',
    FALHA_REDE: 'Falha de rede', MANUTENCAO: 'Em manutenção'
  };
  const statusName = value => statusNames[value] || value || '—';
  $('adminOcupacao').innerHTML = data.lots.length ? data.lots.map(l => {
    const percent = l.total ? Math.round(l.occupied / l.total * 100) : 0;
    return `<article class="occupancy-card"><div class="occupancy-card-heading"><strong>${esc(l.name)}</strong><span>${l.occupied}/${l.total} ocupadas</span></div><div class="occupancy-track" role="meter" aria-label="Ocupação de ${esc(l.name)}" aria-valuemin="0" aria-valuemax="${l.total}" aria-valuenow="${l.occupied}"><span style="width:${percent}%"></span></div><small>${l.total - l.occupied} vagas disponíveis · ${percent}% ocupado</small></article>`;
  }).join('') : '<p>Nenhum estacionamento cadastrado.</p>';
  renderAdminReservations(data);
  $('adminAlertas').innerHTML = data.alerts.length ? data.alerts.slice().reverse().map(a => `<article class="alert-item"><span class="alert-indicator" aria-hidden="true"></span><div><strong>${esc(a.text || statusName(a.type))}</strong><p>${esc(statusName(a.status))} · ${a.attempts || 0} tentativa(s)${a.reservation_id ? ' · reserva ' + esc(a.reservation_id) : ''}</p></div></article>`).join('') : '<p class="empty-state">Nenhum alerta registrado.</p>';
}
$('btnRefreshAdmin').onclick = async () => {
  const button = $('btnRefreshAdmin');
  button.disabled = true; button.textContent = 'Atualizando…';
  try { await refreshLots(); await admin(); }
  catch (e) { message('adminAlertas', e); }
  finally { button.disabled = false; button.textContent = 'Atualizar painel'; }
};
async function init() {
  initMap();
  config = await api('/api/config');
  $('modeBadge').textContent = config.mode === 'demo' ? 'Demonstração local · dados temporários' : 'Estacionamento inteligente · Firebase';
  if (config.mode === 'demo') {
    for (const id of ['demoLabel', 'demoToken']) $(id).classList.remove('hidden');
    for (const id of ['loginEmail', 'loginPass']) { $(id).required = false; $(id).classList.add('hidden'); document.querySelector(`label[for="${id}"]`).classList.add('hidden'); }
  }
  try { await refreshLots(); } catch (e) { message('msgReserva', e); }
  if (map && lots.length) map.fitBounds(lots.map(l => [l.latitude, l.longitude]), { padding: [40, 40], maxZoom: 13 });
  if (location.search.includes('payment_return')) message('msgRetorno', 'Retorno do checkout recebido. Entre e consulte o status: somente o webhook verificado confirma o pagamento.');
  setInterval(() => {
    if (document.visibilityState === 'visible' && !$('operador').classList.contains('hidden') && profile?.operator) {
      admin().catch(e => message('adminAlertas', e));
    }
  }, 60000);
}
init().catch(e => message('msgReserva', e));
