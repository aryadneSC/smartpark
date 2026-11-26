"""Diagnóstico somente de leitura: python -m scripts.check_firebase."""
from pathlib import Path
from dotenv import load_dotenv
from backend.store import FirestoreStore

load_dotenv(Path(__file__).resolve().parents[1] / '.env')
if __name__ == '__main__':
    try:
        store = FirestoreStore()
        store.db.collection('parking_lots').document('centro').get(timeout=10)
        print('OK: Firestore respondeu. A ausência do documento é normal antes do seed.')
    except Exception as exc:
        print('Falha de conexão/configuração:', type(exc).__name__)
        print('Confira o caminho da NOVA chave, o projeto, Firestore, permissões IAM e rede. Consulte o guia.')
        raise SystemExit(1) from None
