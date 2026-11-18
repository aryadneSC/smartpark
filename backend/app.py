"""SmartPark: continuação das rotas Flask e regras do roteiro da atividade."""
import hashlib
import hmac
import math
import os
import re
import secrets
import time
from datetime import datetime, timedelta, timezone
from decimal import Decimal, InvalidOperation
from pathlib import Path
from zoneinfo import ZoneInfo

from dotenv import load_dotenv
from flask import Flask, jsonify, request, send_from_directory
from flask_cors import CORS
from werkzeug.exceptions import HTTPException

from backend.providers import MercadoPago, Telegram, ProviderError, http_json
from backend.store import MemoryStore, FirestoreStore

ROOT = Path(__file__).resolve().parents[1]
BLOCKING = {'PENDING_PAYMENT', 'CONFIRMED', 'CHECKED_IN'}
STATES = {'INICIALIZANDO', 'PRONTO', 'VALIDANDO', 'ACESSO_LIBERADO',
          'ACESSO_NEGADO', 'FALHA_REDE', 'MANUTENCAO'}


class RuleError(Exception):
    def __init__(self, message, status=400):
        self.message, self.status = message, status


def now():
    return datetime.now(timezone.utc)


def iso(value):
    return value.isoformat()


def date(value):
    if isinstance(value, datetime):
        return value if value.tzinfo else value.replace(tzinfo=ZoneInfo(os.getenv("APP_TIMEZONE", "America/Bahia")))
    if not isinstance(value, str):
        raise RuleError('Informe datas ISO válidas.')
    try:
        parsed = datetime.fromisoformat(value)
        return parsed.replace(tzinfo=ZoneInfo(os.getenv('APP_TIMEZONE', 'America/Bahia'))) if parsed.tzinfo is None else parsed
    except (ValueError, TypeError):
        raise RuleError('Informe datas ISO válidas.') from None


def money(value):
    try:
        result = Decimal(str(value))
        if not result.is_finite() or result <= 0 or result > 10000:
            raise ValueError()
        return float(result.quantize(Decimal('0.01')))
    except (ValueError, InvalidOperation):
        raise RuleError('Tarifa inválida (0,01 a 10.000 reais).') from None


def identifier(value):
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,100}', value):
        raise RuleError('Identificador inválido.')
    return value


def expire(state):
    for reservation in state['reservations'].values():
        if reservation['status'] == 'PENDING_PAYMENT' and date(reservation['expires_at']) <= now():
            reservation['status'] = 'EXPIRED'


def free_spaces(state, lot_id, start, end):
    spaces = [key for key, space in state['spaces'].items()
              if space['parking_lot_id'] == lot_id and space.get('active')]
    busy = {r['space_id'] for r in state['reservations'].values()
            if r['parking_lot_id'] == lot_id and r['status'] in BLOCKING
            and (r['status'] == 'CHECKED_IN' or
                 (date(r['arrival_expected']) < end and date(r['departure_expected']) > start))}
    return spaces, [key for key in spaces if key not in busy]


def queue(state, key, kind, reservation_id=None, text=None):
    state['notifications'].setdefault(key, {'type': kind, 'reservation_id': reservation_id,
        'text': text, 'status': 'PENDING_RETRY', 'attempts': 0, 'next_attempt': iso(now())})


def demo_seed(state):
    # Mesmos estacionamentos e vagas do seed escrito pela equipe.
    lots = [('centro', 'Centro', -23.5614, -46.6559, 12, 12, 'A'),
            ('pinheiros', 'Pinheiros', -23.5668, -46.6925, 9, 10, 'B'),
            ('vila', 'Vila Madalena', -23.5546, -46.6914, 10, 8, 'C')]
    for key, name, lat, lng, rate, count, prefix in lots:
        state['parking_lots'][key] = {'name': 'Unidade ' + name, 'address': 'Local fictício — ' + name,
            'latitude': lat, 'longitude': lng, 'hourly_rate': rate, 'active': True,
            'opening_hours': {'open': '00:00', 'close': '23:59'}, 'is_demo': True}
        for i in range(1, count + 1):
            label = f'{prefix}-{i:02}'
            state['spaces'][f'{key}_{label}'] = {'parking_lot_id': key, 'label': label,
                'active': True, 'occupancy_state': 'FREE', 'is_demo': True}
        state['devices'][f'esp32-{key}-01'] = {'parking_lot_id': key, 'status': 'UNKNOWN'}


def create_app(store=None, payment_provider=None, telegram_provider=None, auth_verifier=None):
    load_dotenv(ROOT / '.env')
    app = Flask(__name__, static_folder=str(ROOT / 'frontend'), static_url_path='/static')
    app.config['MAX_CONTENT_LENGTH'] = 16384
    CORS(app, origins=os.getenv('FRONTEND_ORIGIN', 'http://127.0.0.1:5000').split(','))
    demo = os.getenv('SMARTPARK_MODE', 'firestore') == 'demo'
    storage = store if store is not None else (MemoryStore() if demo else FirestoreStore())
    mp, telegram = payment_provider or MercadoPago(), telegram_provider or Telegram()
    app.extensions['store'] = storage
    if demo and store is None:
        storage.run(demo_seed)

    def atomic(operation):
        def run(state):
            expire(state)
            return operation(state)
        return storage.run(run)

    def body():
        data = request.get_json(silent=True)
        if not isinstance(data, dict):
            raise RuleError('Envie um objeto JSON.')
        return data

    def user(operator=False):
        authorization = request.headers.get('Authorization', '')
        token = authorization[7:] if authorization.startswith('Bearer ') else ''
        if not token:
            raise RuleError('Entre para continuar.', 401)
        if auth_verifier:
            claims = auth_verifier(token)
        elif demo:
            regular, admin = os.getenv('DEMO_USER_TOKEN'), os.getenv('DEMO_OPERATOR_TOKEN')
            if admin and hmac.compare_digest(token, admin):
                claims = {'uid': 'demo-operator', 'operator': True}
            elif regular and hmac.compare_digest(token, regular):
                claims = {'uid': 'demo-user'}
            else:
                raise RuleError('Token de demonstração inválido.', 401)
        else:
            from firebase_admin import auth
            try:
                claims = auth.verify_id_token(token, check_revoked=True)
            except Exception:
                raise RuleError('Sessão inválida ou expirada.', 401) from None
        if not claims or not claims.get('uid'):
            raise RuleError('Sessão inválida.', 401)
        if operator and not claims.get('operator'):
            raise RuleError('Somente operador autorizado.', 403)
        return claims

    def owned(state, key, claims):
        reservation = state['reservations'].get(key)
        if not reservation or (reservation['user_id'] != claims['uid'] and not claims.get('operator')):
            raise RuleError('Reserva não encontrada.', 404)
        return reservation

    def device(data):
        key = identifier(data.get('device_id'))
        secret = os.getenv('DEVICE_SHARED_SECRET', '')
        provided = request.headers.get('X-Device-Secret', '')
        if not secret or not hmac.compare_digest(provided, secret) or key != os.getenv('DEVICE_ID', 'esp32-centro-01'):
            raise RuleError('Dispositivo não autorizado.', 401)
        return key

    def period(data):
        start, end = date(data.get('arrival')), date(data.get('departure'))
        if start < now() - timedelta(minutes=1) or end <= start or end - start > timedelta(hours=24):
            raise RuleError('Período inválido: chegada futura e permanência de até 24 horas.')
        return start, end

    def lot(state, key):
        found = state['parking_lots'].get(key)
        if not found or not found.get('active'):
            raise RuleError('Estacionamento não encontrado.', 404)
        return found

    def estimate(state, key, start, end):
        place = lot(state, key)
        hours = math.ceil((end - start).total_seconds() / 3600)
        # O horário cadastrado é local; não aceitar intervalos que atravessem fechamento.
        opening = place.get('opening_hours', {'open': '00:00', 'close': '23:59'})
        local = ZoneInfo(os.getenv('APP_TIMEZONE', 'America/Bahia'))
        a, b = start.astimezone(local), end.astimezone(local)
        if opening != {'open': '00:00', 'close': '23:59'}:
            if a.date() != b.date() or a.strftime('%H:%M') < opening['open'] or b.strftime('%H:%M') > opening['close']:
                raise RuleError('Período fora do horário de funcionamento.')
        spaces, free = free_spaces(state, key, start, end)
        return {'parking_lot_id': key, 'total_spaces': len(spaces), 'free_spaces': len(free),
                'available': bool(free), 'billable_hours': hours,
                'price_estimate': float(Decimal(str(place['hourly_rate'])) * hours)}

    @app.get('/')
    def index():
        return send_from_directory(ROOT / 'frontend', 'index.html')

    @app.get('/api/config')
    def config():
        return jsonify({'mode': 'demo' if demo else 'firestore',
            'firebase_api_key': os.getenv('FIREBASE_WEB_API_KEY', ''),
            'telegram_bot': os.getenv('TELEGRAM_BOT_USERNAME', '')})

    @app.post('/api/session')
    def session():
        data = body()
        if demo:
            raise RuleError('No modo demo use o token local configurado.', 400)
        key = os.getenv('FIREBASE_WEB_API_KEY')
        if not key:
            raise RuleError('Configure FIREBASE_WEB_API_KEY no servidor.', 503)
        from urllib.parse import quote
        result = http_json('https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=' + quote(key),
            {'email': data.get('email'), 'password': data.get('password'), 'returnSecureToken': True})
        return jsonify({'token': result['idToken']})

    @app.get('/api/me')
    def me():
        claims = user()
        return jsonify({'uid': claims['uid'], 'operator': bool(claims.get('operator'))})

    @app.get('/api/parking-lots')
    def list_parking_lots():
        def operation(state):
            result = []
            for key, place in state['parking_lots'].items():
                if not place.get('active'):
                    continue
                spaces, free = free_spaces(state, key, now(), now() + timedelta(hours=1))
                fields = ('name', 'address', 'latitude', 'longitude', 'hourly_rate', 'opening_hours')
                result.append({'id': key, **{k: place.get(k) for k in fields},
                               'total_spaces': len(spaces), 'free_spaces': len(free)})
            return result
        return jsonify(atomic(operation))

    @app.get('/api/parking-lots/<key>/availability')
    def availability(key):
        start, end = period(request.args)
        return jsonify(atomic(lambda s: estimate(s, key, start, end)))

    @app.post('/api/reservations')
    def reserve():
        claims, data = user(), body()
        start, end = period(data)
        key, reservation_id = identifier(data.get('parking_lot_id')), 'SP-' + secrets.token_hex(8)
        def operation(state):
            quote = estimate(state, key, start, end)
            _, free = free_spaces(state, key, start, end)
            if not free:
                raise RuleError('Nenhuma vaga disponível neste período.', 409)
            reservation = {'user_id': claims['uid'], 'parking_lot_id': key, 'space_id': free[0],
                'arrival_expected': iso(start), 'departure_expected': iso(end),
                'status': 'PENDING_PAYMENT', 'price_estimate': quote['price_estimate'],
                'created_at': iso(now()), 'expires_at': iso(min(now() + timedelta(minutes=15), end)),
                'tariff_policy': 'FIXED_PREPAID', 'payment_status': 'pending'}
            state['reservations'][reservation_id] = reservation
            return {'id': reservation_id, **reservation}
        return jsonify(atomic(operation)), 201

    @app.get('/api/reservations')
    def reservations():
        claims = user()
        return jsonify(atomic(lambda s: [{'id': key, **r} for key, r in s['reservations'].items()
                                        if r['user_id'] == claims['uid']]))

    @app.get('/api/reservations/<key>')
    def reservation(key):
        claims = user()
        def operation(state):
            result = {'id': key, **owned(state, key, claims)}
            result['ticket'] = state['tickets'].get(key)
            return result
        return jsonify(atomic(operation))

    @app.post('/api/reservations/<key>/cancel')
    def cancel(key):
        claims = user()
        def operation(state):
            r = owned(state, key, claims)
            if r['status'] not in {'PENDING_PAYMENT', 'CONFIRMED'}:
                raise RuleError('Reserva não pode ser cancelada.', 409)
            if r['status'] == 'CONFIRMED':
                queue(state, 'cancel-' + key, 'ALERT', key, 'Cancelamento pago: reconciliação manual, sem estorno automático.')
                r['refund_status'] = 'MANUAL_REVIEW'
            r['status'] = 'CANCELLED'
            return {'status': r['status']}
        return jsonify(atomic(operation))

    @app.post('/api/reservations/<key>/checkout')
    def checkout(key):
        claims = user()
        def prepare(state):
            r = owned(state, key, claims)
            if r['status'] != 'PENDING_PAYMENT':
                raise RuleError('Reserva não está pendente de pagamento.', 409)
            if not r.get('checkout_url'):
                if r.get('checkout_state') in {'CREATING', 'MANUAL_REVIEW'}:
                    raise RuleError('Checkout em processamento ou aguardando revisão do operador.', 409)
                r['checkout_state'] = 'CREATING'
            return dict(r)
        r = atomic(prepare)
        if r.get('checkout_url'):
            return jsonify({'checkout_url': r['checkout_url']})
        try:
            result = mp.checkout(key, r, os.getenv('APP_BASE_URL', '').rstrip('/'))
        except ProviderError as exc:
            def failed(state):
                current = state['reservations'][key]
                current['checkout_state'] = 'MANUAL_REVIEW' if exc.uncertain else 'RETRY'
                if exc.uncertain:
                    queue(state, 'checkout-' + key, 'ALERT', key, 'Resultado do checkout desconhecido: revisão manual.')
            atomic(failed)
            raise
        def save(state):
            current = owned(state, key, claims)
            if current['status'] != 'PENDING_PAYMENT':
                raise RuleError('Reserva expirou durante o checkout.', 409)
            current.update(result, checkout_state='READY')
            state['payments'].setdefault('preference-' + key, {'reservation_id': key,
                'provider': 'mercadopago', 'preference_id': result['preference_id'],
                'status': 'pending', 'amount': r['price_estimate'], 'currency': 'BRL'})
            return {'checkout_url': current['checkout_url']}
        return jsonify(atomic(save))

    @app.post('/webhooks/mercadopago')
    def payment_webhook():
        data = body()
        payment_id = request.args.get('data.id', '')
        req_id = request.headers.get('x-request-id', '')
        signature = dict(part.strip().split('=', 1) for part in request.headers.get('x-signature', '').split(',') if '=' in part)
        ts, supplied = signature.get('ts', ''), signature.get('v1', '')
        secret = os.getenv('MERCADOPAGO_WEBHOOK_SECRET', '')
        try:
            timestamp = int(ts)
            fresh = abs(time.time() - (timestamp / 1000 if timestamp > 10**12 else timestamp)) <= 300
        except ValueError:
            fresh = False
        manifest = f'id:{payment_id.lower()};request-id:{req_id};ts:{ts};'
        expected = hmac.new(secret.encode(), manifest.encode(), hashlib.sha256).hexdigest()
        if not secret or not payment_id or not req_id or not fresh or not hmac.compare_digest(expected, supplied):
            raise RuleError('Assinatura inválida.', 401)
        if data.get('type') != 'payment':
            return jsonify({'ignored': True})
        payment = mp.payment(payment_id)
        if str(payment.get('id')) != payment_id or payment.get('live_mode') is not False:
            raise RuleError('Somente pagamentos de teste verificados.', 400)
        key = payment.get('external_reference')
        def operation(state):
            r = state['reservations'].get(key)
            if not r:
                raise RuleError('Referência de pagamento desconhecida.', 404)
            if payment.get('currency_id') != 'BRL' or Decimal(str(payment.get('transaction_amount', 0))) != Decimal(str(r['price_estimate'])):
                raise RuleError('Pagamento não corresponde à reserva.', 409)
            previous = state['payments'].get(payment_id)
            if previous and previous.get('status') == 'approved' and payment['status'] != 'approved':
                queue(state, 'reconcile-' + payment_id, 'ALERT', key, 'Pagamento alterado após aprovação: revisão manual.')
                return {'received': True, 'reconciliation': True}
            state['payments'][payment_id] = {'reservation_id': key, 'provider': 'mercadopago',
                'payment_id': payment_id, 'status': payment['status'], 'amount': r['price_estimate'],
                'currency': 'BRL', 'verified_at': iso(now())}
            if payment['status'] == 'approved':
                if r.get('payment_id') and r['payment_id'] != payment_id:
                    queue(state, 'duplicate-payment-' + payment_id, 'ALERT', key, 'Segundo pagamento: revisão manual.')
                elif r['status'] == 'PENDING_PAYMENT':
                    _, free = free_spaces({**state, 'reservations': {k: v for k, v in state['reservations'].items() if k != key}},
                                          r['parking_lot_id'], date(r['arrival_expected']), date(r['departure_expected']))
                    if r['space_id'] not in free:
                        r['status'] = 'EXPIRED'
                        queue(state, 'late-' + key, 'ALERT', key, 'Pagamento sem vaga: revisão manual.')
                    else:
                        r.update(status='CONFIRMED', payment_status='approved', payment_id=payment_id)
                        queue(state, 'confirm-' + key, 'CONFIRMATION', key)
                elif r['status'] in {'EXPIRED', 'CANCELLED'}:
                    r['refund_status'] = 'MANUAL_REVIEW'
                    queue(state, 'late-' + key, 'ALERT', key, 'Pagamento tardio: revisão manual; entrada negada.')
            return {'received': True}
        return jsonify(atomic(operation))

    @app.post('/api/iot/access-request')
    def access():
        data = body()
        dev, event = device(data), identifier(data.get('event_id'))
        key, kind = identifier(data.get('reservation_id')), data.get('type')
        if kind not in {'ENTRY', 'EXIT'}:
            raise RuleError('type deve ser ENTRY ou EXIT.')
        def operation(state):
            registered = state['devices'].get(dev)
            if not registered:
                raise RuleError('Dispositivo não cadastrado.', 403)
            old = state['access_events'].get(event)
            if old:
                if (old['device_id'], old['reservation_id'], old['type']) != (dev, key, kind):
                    raise RuleError('event_id já utilizado para outra operação.', 409)
                # Um replay nunca movimenta o servo novamente.
                return {**old['response'], 'duplicate': True, 'actuate': False}
            window = registered.get('access_window')
            if not window or now() - date(window) >= timedelta(minutes=1):
                registered.update(access_window=iso(now()), access_count=0)
            if registered.get('access_count', 0) >= 30:
                raise RuleError('Limite de 30 novos eventos por minuto.', 429)
            registered['access_count'] = registered.get('access_count', 0) + 1
            r = state['reservations'].get(key)
            allowed, reason = False, 'Reserva ou estado incompatível.'
            if registered.get('status') == 'MANUTENCAO':
                reason = 'Dispositivo em manutenção.'
            elif r and r['parking_lot_id'] == registered['parking_lot_id']:
                if kind == 'ENTRY':
                    start = date(r['arrival_expected']) - timedelta(minutes=15)
                    end = min(date(r['arrival_expected']) + timedelta(minutes=30), date(r['departure_expected']))
                    occupied = state['spaces'].get(r['space_id'], {}).get('occupancy_state') == 'OCCUPIED'
                    allowed = r['status'] == 'CONFIRMED' and r.get('payment_status') == 'approved' and start <= now() <= end and not occupied
                    if allowed:
                        r.update(status='CHECKED_IN', arrival_real=iso(now()))
                        state['spaces'][r['space_id']]['occupancy_state'] = 'OCCUPIED'
                else:
                    allowed = r['status'] == 'CHECKED_IN'
                    if allowed:
                        r.update(status='COMPLETED', departure_real=iso(now()))
                        duration = max(0, math.ceil((date(r['departure_real']) - date(r['arrival_real'])).total_seconds() / 60))
                        overdue = date(r['departure_real']) > date(r['departure_expected'])
                        state['tickets'].setdefault(key, {'ticket_id': 'TK-' + key, 'reservation_id': key,
                            'parking_lot_id': r['parking_lot_id'], 'space_id': r['space_id'],
                            'arrival_real': r['arrival_real'], 'departure_real': r['departure_real'],
                            'duration_minutes': duration, 'final_amount': r['price_estimate'],
                            'payment_status': r['payment_status'], 'telegram_status': 'PENDING_RETRY'})
                        state['spaces'][r['space_id']]['occupancy_state'] = 'FREE'
                        queue(state, 'ticket-' + key, 'TICKET', key)
                        if overdue:
                            queue(state, 'overdue-' + key, 'ALERT', key, 'Permanência excedida: sem cobrança automática adicional.')
                if allowed:
                    reason = 'Autorizado pelo backend.'
            response = {'allowed': bool(allowed), 'actuate': bool(allowed), 'reason': reason, 'event_id': event}
            state['access_events'][event] = {'event_id': event, 'reservation_id': key, 'device_id': dev,
                'type': kind, 'timestamp': iso(now()), 'result': 'ALLOWED' if allowed else 'DENIED', 'response': response}
            registered['last_seen'] = iso(now())
            if not allowed:
                queue(state, 'access-' + event, 'ALERT', key if r else None, 'Acesso negado no dispositivo ' + dev)
            return response
        return jsonify(atomic(operation))

    @app.post('/api/iot/telemetry')
    def telemetry():
        data = body()
        dev = device(data)
        status = data.get('status')
        if status not in STATES:
            raise RuleError('Estado de controlador inválido.')
        def operation(state):
            registered = state['devices'].get(dev)
            if not registered:
                raise RuleError('Dispositivo não cadastrado.', 403)
            last = registered.get('last_telemetry')
            if last and now() - date(last) < timedelta(seconds=5):
                raise RuleError('Aguarde cinco segundos entre telemetrias.', 429)
            registered.update(status=status, last_seen=iso(now()), last_telemetry=iso(now()), firmware_version='1.0.0')
            if status in {'FALHA_REDE', 'MANUTENCAO'}:
                queue(state, 'device-' + dev + '-' + status, 'ALERT', text='Dispositivo ' + dev + ': ' + status)
            return {'received': True}
        return jsonify(atomic(operation))

    @app.post('/api/telegram/link')
    def telegram_link():
        claims = user()
        code = secrets.token_urlsafe(24)
        atomic(lambda s: s['telegram_links'].update({code: {'user_id': claims['uid'], 'expires_at': iso(now() + timedelta(minutes=10)), 'used': False}}))
        username = os.getenv('TELEGRAM_BOT_USERNAME', '')
        return jsonify({'url': f'https://t.me/{username}?start={code}' if username else None,
                        'instruction': 'Configure o bot e abra o link para enviar /start.'})

    @app.post('/webhooks/telegram')
    def telegram_webhook():
        secret = os.getenv('TELEGRAM_WEBHOOK_SECRET', '')
        if not secret or not hmac.compare_digest(request.headers.get('X-Telegram-Bot-Api-Secret-Token', ''), secret):
            raise RuleError('Webhook Telegram não autorizado.', 401)
        data = body()
        message = data.get('message') or {}
        chat = message.get('chat') or {}
        text = message.get('text', '')
        if chat.get('type') != 'private' or not isinstance(text, str) or not text.startswith('/start '):
            return jsonify({'ignored': True})
        code = text.split(' ', 1)[1]
        def operation(state):
            link = state['telegram_links'].get(code)
            if link and not link['used'] and date(link['expires_at']) > now():
                state['users'].setdefault(link['user_id'], {})['telegram_chat_id'] = str(chat['id'])
                link['used'] = True
            return {'received': True}
        return jsonify(atomic(operation))

    @app.get('/api/admin/dashboard')
    def dashboard():
        user(operator=True)
        def operation(state):
            for dev, d in state['devices'].items():
                if d.get('last_seen') and now() - date(d['last_seen']) > timedelta(minutes=2):
                    queue(state, 'offline-' + dev, 'ALERT', text='Dispositivo sem comunicação: ' + dev)
            return {'lots': [{'id': key, 'name': p['name'],
                'total': sum(s['parking_lot_id'] == key and s.get('active', False) for s in state['spaces'].values()),
                'occupied': sum(s['parking_lot_id'] == key and s.get('occupancy_state') == 'OCCUPIED' for s in state['spaces'].values())}
                for key, p in state['parking_lots'].items()],
                'reservations': [{'id': key, **r} for key, r in state['reservations'].items()],
                'alerts': [{'id': key, **n} for key, n in state['notifications'].items() if n['type'] == 'ALERT'],
                'notifications': [{'id': key, **n} for key, n in state['notifications'].items()]}
        return jsonify(atomic(operation))

    @app.post('/api/admin/parking-lots')
    def add_lot():
        user(operator=True)
        data = body()
        name, address = data.get('name'), data.get('address')
        if not isinstance(name, str) or not 1 <= len(name.strip()) <= 80 or not isinstance(address, str) or not 1 <= len(address.strip()) <= 120:
            raise RuleError('Nome e endereço são obrigatórios.')
        try:
            lat, lng = float(data.get('latitude')), float(data.get('longitude'))
            count = data.get('spaces', 1)
            if not -90 <= lat <= 90 or not -180 <= lng <= 180 or type(count) is not int or not 1 <= count <= 50:
                raise ValueError()
        except (ValueError, TypeError):
            raise RuleError('Coordenadas ou quantidade de vagas inválidas.') from None
        rate, key = money(data.get('hourly_rate')), 'lot-' + secrets.token_hex(6)
        def operation(state):
            state['parking_lots'][key] = {'name': name.strip(), 'address': address.strip(),
                'latitude': lat, 'longitude': lng, 'hourly_rate': rate, 'active': True,
                'opening_hours': {'open': '00:00', 'close': '23:59'}}
            for i in range(count):
                state['spaces'][f'{key}-A-{i+1:02}'] = {'parking_lot_id': key, 'label': f'A-{i+1:02}', 'active': True, 'occupancy_state': 'FREE'}
            return {'id': key}
        return jsonify(atomic(operation)), 201

    def deliver_notifications():
        def claim(state):
            jobs = []
            for key, n in state['notifications'].items():
                if n['status'] == 'SENDING':
                    # Resultado desconhecido: nunca reenviar automaticamente após queda do processo.
                    if now() - date(n['claimed_at']) > timedelta(minutes=2):
                        n['status'] = 'MANUAL_REVIEW'
                    continue
                if n['status'] != 'PENDING_RETRY' or n['attempts'] >= 3 or date(n['next_attempt']) > now():
                    continue
                r = state['reservations'].get(n.get('reservation_id'), {})
                chat = os.getenv('TELEGRAM_DEFAULT_CHAT_ID') if n['type'] == 'ALERT' else state['users'].get(r.get('user_id'), {}).get('telegram_chat_id')
                if not chat:
                    continue
                place = state['parking_lots'].get(r.get('parking_lot_id'), {})
                text = n.get('text') or f"SMARTPARK {n['type']} | {n['reservation_id']} | {place.get('name')} | vaga {r.get('space_id')} | {r.get('arrival_expected')} até {r.get('departure_expected')} | pagamento {r.get('payment_status')}"
                if n['type'] == 'TICKET':
                    t = state['tickets'][n['reservation_id']]
                    text = f"SMARTPARK — COMPROVANTE {t['ticket_id']} | {place.get('name')} | vaga {t['space_id']} | chegada {t['arrival_real']} | saída {t['departure_real']} | {t['duration_minutes']} min | R$ {t['final_amount']:.2f} | pagamento {t['payment_status']}"
                n.update(status='SENDING', claimed_at=iso(now()), attempts=n['attempts'] + 1)
                jobs.append((key, str(chat), text))
                if len(jobs) == 10:
                    break
            return jobs
        jobs = atomic(claim)
        for key, chat, text in jobs:
            message_id, status = None, 'SENT'
            try:
                message_id = telegram.send(chat, text)
            except ProviderError as exc:
                status = 'MANUAL_REVIEW' if exc.uncertain else 'PENDING_RETRY'
            def finish(state):
                n = state['notifications'][key]
                n.update(status='FAILED' if status == 'PENDING_RETRY' and n['attempts'] >= 3 else status,
                         message_id=message_id, next_attempt=iso(now() + timedelta(seconds=60 * 2 ** n['attempts'])))
                if status == 'SENT':
                    n['sent_at'] = iso(now())
                if n['type'] == 'TICKET':
                    state['tickets'][n['reservation_id']]['telegram_status'] = n['status']
            atomic(finish)
        return {'processed': len(jobs)}

    app.extensions['deliver_notifications'] = deliver_notifications

    @app.post('/api/admin/notifications/retry')
    def retry():
        user(operator=True)
        return jsonify(deliver_notifications())

    @app.cli.command('send-notifications')
    def notify_command():
        print(deliver_notifications())

    @app.errorhandler(RuleError)
    def rule_error(exc):
        return jsonify({'error': exc.message}), exc.status

    @app.errorhandler(ProviderError)
    def provider_error(_):
        return jsonify({'error': 'Serviço externo indisponível ou não configurado. Consulte o guia.'}), 503

    @app.errorhandler(Exception)
    def error(exc):
        if isinstance(exc, HTTPException):
            return jsonify({'error': exc.description}), exc.code
        app.logger.error('Falha na operação: %s', type(exc).__name__)
        return jsonify({'error': 'Falha de persistência ou configuração. Consulte o guia; não houve fallback para demo.'}), 503

    @app.after_request
    def headers(response):
        response.headers['X-Content-Type-Options'] = 'nosniff'
        # OSM exige Referer nos tiles; envie somente a origem entre sites.
        response.headers['Referrer-Policy'] = 'strict-origin-when-cross-origin'
        if request.path.startswith('/api/'):
            response.headers['Cache-Control'] = 'no-store'
        return response

    return app


if __name__ == '__main__':
    create_app().run(host='127.0.0.1', port=5000)
