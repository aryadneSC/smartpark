"""Casos do roteiro: regras reais, serviços externos substituídos por dublês."""
import hashlib
import hmac
import os
import time
import unittest
from concurrent.futures import ThreadPoolExecutor
from datetime import timedelta
from unittest.mock import patch

from backend.app import create_app, demo_seed, now, iso
from backend.store import MemoryStore
from backend.providers import ProviderError


class Payments:
    def __init__(self):
        self.data = {}
        self.checkouts = 0

    def checkout(self, key, r, base):
        self.checkouts += 1
        return {'preference_id': 'pref-' + key, 'checkout_url': 'https://sandbox.mercadopago.com.br/test'}

    def payment(self, key):
        return self.data[key]


class Bot:
    def __init__(self):
        self.sent = []
        self.fail = False
        self.uncertain = False

    def send(self, chat, text):
        if self.fail:
            raise ProviderError(self.uncertain)
        self.sent.append((chat, text))
        return len(self.sent)


class SmartParkTests(unittest.TestCase):
    def setUp(self):
        self.env = patch.dict(os.environ, {'SMARTPARK_MODE': 'demo', 'DEVICE_ID': 'esp32-centro-01',
            'DEVICE_SHARED_SECRET': 'test-device', 'MERCADOPAGO_WEBHOOK_SECRET': 'test-webhook',
            'TELEGRAM_WEBHOOK_SECRET': 'test-telegram', 'TELEGRAM_DEFAULT_CHAT_ID': 'operator',
            'APP_TIMEZONE': 'America/Bahia'}, clear=False)
        self.env.start()
        self.addCleanup(self.env.stop)
        self.store, self.mp, self.bot = MemoryStore(), Payments(), Bot()
        self.store.run(demo_seed)
        self.app = create_app(self.store, self.mp, self.bot,
                             lambda token: {'uid': token, 'operator': token == 'admin'})
        self.client = self.app.test_client()
        self.auth = {'Authorization': 'Bearer alice'}
        self.device = {'X-Device-Secret': 'test-device'}
        self.start = now() + timedelta(minutes=1)
        self.period = {'parking_lot_id': 'centro', 'arrival': iso(self.start),
                       'departure': iso(self.start + timedelta(hours=2))}

    def reserve(self, headers=None, **extra):
        result = self.client.post('/api/reservations', json={**self.period, **extra}, headers=headers or self.auth)
        self.assertEqual(result.status_code, 201, result.json)
        return result.json

    def webhook(self, key, status='approved', payment_id='123', amount=None, live=False):
        r = self.store.state['reservations'][key]
        self.mp.data[payment_id] = {'id': payment_id, 'external_reference': key, 'status': status,
            'currency_id': 'BRL', 'transaction_amount': amount if amount is not None else r['price_estimate'], 'live_mode': live}
        ts = str(int(time.time()))
        digest = hmac.new(b'test-webhook', f'id:{payment_id};request-id:test;ts:{ts};'.encode(), hashlib.sha256).hexdigest()
        return self.client.post('/webhooks/mercadopago?data.id=' + payment_id,
            json={'type': 'payment', 'data': {'id': payment_id}},
            headers={'x-request-id': 'test', 'x-signature': f'ts={ts},v1={digest}'})

    def access(self, key, kind, event, device='esp32-centro-01'):
        return self.client.post('/api/iot/access-request', json={'reservation_id': key,
            'device_id': device, 'event_id': event, 'type': kind}, headers=self.device)

    def test_T01_map_and_team_assets(self):
        for path in ['/', '/static/css/style.css', '/static/scripts/map.js']:
            with self.client.get(path) as response:
                self.assertEqual(response.status_code, 200)
                self.assertEqual(response.headers['Referrer-Policy'], 'strict-origin-when-cross-origin')
        lots = self.client.get('/api/parking-lots').json
        self.assertGreaterEqual(len(lots), 2)
        self.assertEqual(lots[0]['total_spaces'], 12)
        self.assertNotIn('private_key', str(lots))
        self.assertEqual(self.client.get('/projeto-teste-57250-firebase-adminsdk-fbsvc-3764a9b6fe.json').status_code, 404)

    def test_T02_invalid_dates(self):
        for data in [{'departure': self.period['arrival']}, {'arrival': 7}, {'arrival': None},
                     {'arrival': iso(now() - timedelta(days=1))}, {'departure': iso(now() + timedelta(days=3))}]:
            self.assertEqual(self.client.post('/api/reservations', json={**self.period, **data}, headers=self.auth).status_code, 400)

    def test_T03_concurrent_last_space(self):
        self.store.run(lambda s: s.update(spaces={key: value for key, value in s['spaces'].items() if key == 'centro_A-01'}))
        def reserve_once(_):
            with self.app.test_client() as client:
                return client.post('/api/reservations', json=self.period, headers=self.auth).status_code
        with ThreadPoolExecutor(max_workers=8) as pool:
            results = list(pool.map(reserve_once, range(8)))
        self.assertEqual(results.count(201), 1)
        self.assertEqual(results.count(409), 7)

    def test_T04_rejected_pending_and_missing_signature(self):
        r = self.reserve()
        self.assertEqual(self.webhook(r['id'], 'rejected').status_code, 200)
        self.assertFalse(self.access(r['id'], 'ENTRY', 'entry-unpaid').json['allowed'])
        self.assertEqual(self.store.state['reservations'][r['id']]['status'], 'PENDING_PAYMENT')
        self.assertEqual(self.client.post('/webhooks/mercadopago?data.id=123', json={}).status_code, 401)

    def test_T05_T06_approved_idempotent_webhook(self):
        r = self.reserve()
        self.assertEqual(self.webhook(r['id']).status_code, 200)
        self.assertEqual(self.webhook(r['id']).status_code, 200)
        self.assertEqual(self.store.state['reservations'][r['id']]['status'], 'CONFIRMED')
        self.assertEqual(len(self.store.state['notifications']), 1)
        self.assertEqual(len(self.store.state['payments']), 1)
        self.webhook(r['id'], 'pending')
        self.assertEqual(self.store.state['reservations'][r['id']]['status'], 'CONFIRMED')

    def test_T07_T08_T10_T11_entry_exit_and_replays(self):
        r = self.reserve()
        self.assertFalse(self.access(r['id'], 'ENTRY', 'unpaid').json['allowed'])
        self.webhook(r['id'])
        entry = self.access(r['id'], 'ENTRY', 'entry')
        self.assertTrue(entry.json['allowed'])
        replay = self.access(r['id'], 'ENTRY', 'entry').json
        self.assertTrue(replay['duplicate'])
        self.assertFalse(replay['actuate'])
        self.assertFalse(self.access(r['id'], 'ENTRY', 'entry2').json['allowed'])
        self.assertEqual(self.access(r['id'], 'EXIT', 'exit').status_code, 200)
        self.assertTrue(self.access(r['id'], 'EXIT', 'exit').json['duplicate'])
        ticket = self.store.state['tickets'][r['id']]
        self.assertEqual(ticket['final_amount'], r['price_estimate'])
        self.assertEqual(len(self.store.state['tickets']), 1)
        self.assertEqual(self.store.state['reservations'][r['id']]['status'], 'COMPLETED')
        self.assertEqual(self.store.state['spaces'][r['space_id']]['occupancy_state'], 'FREE')
        self.assertEqual(self.access(r['id'], 'ENTRY', 'exit').status_code, 409)

    def test_T09_provider_failure_and_safe_defaults(self):
        r = self.reserve()
        with patch.object(self.mp, 'checkout', side_effect=ProviderError()):
            self.assertEqual(self.client.post(f"/api/reservations/{r['id']}/checkout", headers=self.auth).status_code, 503)
        self.assertFalse(self.access(r['id'], 'ENTRY', 'failclosed').json['allowed'])
        self.assertFalse(self.store.state['tickets'])

    def test_T12_telegram_retry_and_no_duplicate_ticket(self):
        r = self.reserve()
        self.store.run(lambda s: s['users'].update({'alice': {'telegram_chat_id': '123'}}))
        self.webhook(r['id'])
        self.access(r['id'], 'ENTRY', 'entry')
        self.access(r['id'], 'EXIT', 'exit')
        self.bot.fail = True
        self.app.extensions['deliver_notifications']()
        self.assertEqual(self.store.state['tickets'][r['id']]['telegram_status'], 'PENDING_RETRY')
        self.assertEqual(len(self.store.state['tickets']), 1)
        self.bot.fail = False
        self.store.run(lambda s: [n.update(next_attempt=iso(now() - timedelta(seconds=1))) for n in s['notifications'].values()])
        self.app.extensions['deliver_notifications']()
        self.app.extensions['deliver_notifications']()
        self.assertEqual(len(self.bot.sent), 2)
        self.assertEqual(self.store.state['tickets'][r['id']]['telegram_status'], 'SENT')

    def test_telegram_uncertain_delivery_manual_review(self):
        r = self.reserve()
        self.store.run(lambda s: s['users'].update({'alice': {'telegram_chat_id': '123'}}))
        self.webhook(r['id'])
        self.bot.fail = self.bot.uncertain = True
        self.app.extensions['deliver_notifications']()
        self.assertEqual(self.store.state['notifications']['confirm-' + r['id']]['status'], 'MANUAL_REVIEW')
        self.app.extensions['deliver_notifications']()
        self.assertEqual(self.store.state['notifications']['confirm-' + r['id']]['attempts'], 1)

    def test_T13_tampered_price(self):
        r = self.reserve(price_estimate=.01, user_id='mallory', status='CONFIRMED')
        self.assertEqual(r['price_estimate'], 24)
        self.assertEqual(r['user_id'], 'alice')
        self.assertEqual(r['status'], 'PENDING_PAYMENT')
        self.assertEqual(self.webhook(r['id'], amount=.01).status_code, 409)
        self.assertEqual(self.webhook(r['id'], live=True).status_code, 400)

    def test_T14_ownership_and_admin(self):
        r = self.reserve()
        other = {'Authorization': 'Bearer bob'}
        self.assertEqual(self.client.get(f"/api/reservations/{r['id']}", headers=other).status_code, 404)
        self.assertEqual(self.client.post(f"/api/reservations/{r['id']}/checkout", headers=other).status_code, 404)
        self.assertEqual(self.client.get('/api/admin/dashboard', headers=self.auth).status_code, 403)
        self.assertEqual(self.client.get('/api/admin/dashboard').status_code, 401)
        self.assertEqual(self.client.get('/api/admin/dashboard', headers={'Authorization': 'Bearer admin'}).status_code, 200)

    def test_expiry_late_payment_and_cancellation(self):
        r = self.reserve()
        self.store.run(lambda s: s['reservations'][r['id']].update(expires_at=iso(now() - timedelta(seconds=1))))
        self.webhook(r['id'])
        self.assertEqual(self.store.state['reservations'][r['id']]['status'], 'EXPIRED')
        self.assertFalse(self.access(r['id'], 'ENTRY', 'late').json['allowed'])
        self.assertEqual(self.store.state['reservations'][r['id']]['refund_status'], 'MANUAL_REVIEW')
        r2 = self.reserve()
        self.assertEqual(self.client.post(f"/api/reservations/{r2['id']}/cancel", headers=self.auth).status_code, 200)
        self.assertEqual(self.store.state['reservations'][r2['id']]['status'], 'CANCELLED')

    def test_wrong_device_and_window(self):
        r = self.reserve()
        self.webhook(r['id'])
        self.assertEqual(self.access(r['id'], 'ENTRY', 'wrong', 'esp32-pinheiros-01').status_code, 401)
        self.store.run(lambda s: s['devices']['esp32-centro-01'].update(parking_lot_id='pinheiros'))
        self.assertFalse(self.access(r['id'], 'ENTRY', 'wronglot').json['allowed'])
        self.store.run(lambda s: s['devices']['esp32-centro-01'].update(parking_lot_id='centro'))
        with patch('backend.app.now', return_value=self.start - timedelta(minutes=20)):
            self.assertFalse(self.access(r['id'], 'ENTRY', 'early').json['allowed'])

    def test_telegram_link_requires_validated_private_start(self):
        link = self.client.post('/api/telegram/link', headers=self.auth)
        self.assertEqual(link.status_code, 200)
        code = next(iter(self.store.state['telegram_links']))
        payload = {'message': {'text': '/start ' + code, 'chat': {'id': 42, 'type': 'private'}}}
        self.assertEqual(self.client.post('/webhooks/telegram', json=payload).status_code, 401)
        headers = {'X-Telegram-Bot-Api-Secret-Token': 'test-telegram'}
        self.assertEqual(self.client.post('/webhooks/telegram', json=payload, headers=headers).status_code, 200)
        self.assertEqual(self.store.state['users']['alice']['telegram_chat_id'], '42')
        payload['message']['chat']['id'] = 99
        self.client.post('/webhooks/telegram', json=payload, headers=headers)
        self.assertEqual(self.store.state['users']['alice']['telegram_chat_id'], '42')

    def test_admin_validation_and_telemetry_rate_limit(self):
        data = {'name': 'Teste', 'address': 'Fictício', 'latitude': -12, 'longitude': -38, 'hourly_rate': 8, 'spaces': 2}
        self.assertEqual(self.client.post('/api/admin/parking-lots', json=data, headers=self.auth).status_code, 403)
        admin = {'Authorization': 'Bearer admin'}
        self.assertEqual(self.client.post('/api/admin/parking-lots', json={**data, 'hourly_rate': -1}, headers=admin).status_code, 400)
        self.assertEqual(self.client.post('/api/admin/parking-lots', json=data, headers=admin).status_code, 201)
        telemetry = {'device_id': 'esp32-centro-01', 'status': 'PRONTO'}
        self.assertEqual(self.client.post('/api/iot/telemetry', json=telemetry, headers=self.device).status_code, 200)
        self.assertEqual(self.client.post('/api/iot/telemetry', json=telemetry, headers=self.device).status_code, 429)

    def test_checkout_reused_and_return_does_not_confirm(self):
        r = self.reserve()
        for _ in range(2):
            self.assertEqual(self.client.post(f"/api/reservations/{r['id']}/checkout", headers=self.auth).status_code, 200)
        self.assertEqual(self.mp.checkouts, 1)
        with self.client.get('/?payment_return=success') as response:
            self.assertEqual(response.status_code, 200)
        self.assertEqual(self.store.state['reservations'][r['id']]['status'], 'PENDING_PAYMENT')

    def test_database_failure_no_fallback(self):
        with patch.object(self.store, 'run', side_effect=RuntimeError('offline')):
            result = self.client.get('/api/parking-lots')
        self.assertEqual(result.status_code, 503)
        self.assertNotIn('offline', str(result.json))


if __name__ == '__main__':
    unittest.main()
