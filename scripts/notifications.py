"""Worker local: python -m scripts.notifications (interrompa com Ctrl+C)."""
import time
import os
from backend.app import create_app

if __name__ == '__main__':
    app = create_app()
    if os.getenv('SMARTPARK_MODE') == 'demo':
        raise SystemExit('Modo demo usa memória por processo: processe pelo botão do operador na mesma aplicação.')
    try:
        while True:
            try:
                app.extensions['deliver_notifications']()
            except Exception as exc:
                print('Notificações: tentativa adiada —', type(exc).__name__)
            time.sleep(15)
    except KeyboardInterrupt:
        pass
