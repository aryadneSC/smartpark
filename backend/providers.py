"""Clientes HTTPS sem novas dependências de produção."""
import json
import os
from urllib.request import Request, urlopen
from urllib.error import HTTPError, URLError


class ProviderError(Exception):
    def __init__(self, uncertain=False):
        self.uncertain = uncertain


def http_json(url, payload=None, headers=None):
    data = None if payload is None else json.dumps(payload).encode()
    req = Request(url, data=data, headers={'Content-Type': 'application/json', **(headers or {})})
    try:
        with urlopen(req, timeout=10) as response:
            return json.load(response)
    except HTTPError as exc:
        raise ProviderError(uncertain=exc.code >= 500) from None
    except (URLError, TimeoutError, OSError, ValueError):
        raise ProviderError(uncertain=True) from None


class MercadoPago:
    def headers(self, key=None):
        token = os.environ.get('MERCADOPAGO_ACCESS_TOKEN')
        if not token:
            raise ProviderError()
        return {'Authorization': f'Bearer {token}', **({'X-Idempotency-Key': key} if key else {})}

    def checkout(self, reservation_id, reservation, base):
        if not base.startswith('https://'):
            raise ProviderError()
        data = http_json('https://api.mercadopago.com/checkout/preferences', {
            'external_reference': reservation_id,
            'items': [{'id': reservation_id, 'title': 'SmartPark — reserva de teste',
                       'quantity': 1, 'currency_id': 'BRL',
                       'unit_price': reservation['price_estimate']}],
            'notification_url': base + '/webhooks/mercadopago?source_news=webhooks',
            'back_urls': {s: base + '/?payment_return=' + s for s in ('success', 'failure', 'pending')},
            'auto_return': 'approved',
            'expires': True, 'expiration_date_to': reservation['expires_at'],
        }, self.headers(reservation_id))
        # Apenas ambiente de testes nesta atividade.
        if not data.get('sandbox_init_point'):
            raise ProviderError()
        return {'preference_id': data['id'], 'checkout_url': data['sandbox_init_point']}

    def payment(self, payment_id):
        from urllib.parse import quote
        return http_json('https://api.mercadopago.com/v1/payments/' + quote(payment_id, safe=''),
                         headers=self.headers())


class Telegram:
    def send(self, chat_id, text):
        token = os.environ.get('TELEGRAM_BOT_TOKEN')
        if not token:
            raise ProviderError()
        result = http_json(f'https://api.telegram.org/bot{token}/sendMessage',
                           {'chat_id': chat_id, 'text': text})
        if not result.get('ok'):
            raise ProviderError()
        return result['result']['message_id']
