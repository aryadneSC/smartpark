"""Persistência transacional do protótipo; nunca substitui Firebase silenciosamente."""
import os
from copy import deepcopy
from threading import RLock

COLLECTIONS = ('parking_lots', 'spaces', 'reservations', 'payments', 'access_events',
               'tickets', 'devices', 'notifications', 'users', 'telegram_links')


class MemoryStore:
    def __init__(self):
        self.state = {name: {} for name in COLLECTIONS}
        self.lock = RLock()

    def run(self, operation):
        with self.lock:
            state = deepcopy(self.state)
            result = operation(state)
            self.state = state
            return result


class FirestoreStore:
    def __init__(self):
        import firebase_admin
        from firebase_admin import firestore
        try:
            firebase_admin.get_app()
        except ValueError:
            # No Vercel não existe arquivo de chave: o conteúdo do JSON vem na variável
            # FIREBASE_SERVICE_ACCOUNT_JSON. Localmente, sem ela, continua valendo o
            # GOOGLE_APPLICATION_CREDENTIALS (arquivo fora do projeto).
            import json
            from firebase_admin import credentials
            project = os.getenv('FIREBASE_PROJECT_ID')
            raw = os.getenv('FIREBASE_SERVICE_ACCOUNT_JSON')
            cred = credentials.Certificate(json.loads(raw)) if raw else None
            firebase_admin.initialize_app(cred, options={'projectId': project} if project else None)
        self.db = firestore.client()
        self.firestore = firestore

    def run(self, operation):
        # Todas as mutações passam por este documento: serializa inclusive vagas vazias.
        # Adequado somente ao volume pequeno da atividade (não escalar desta maneira).
        @self.firestore.transactional
        def transact(tx):
            guard = self.db.collection('_locks').document('smartpark')
            version = guard.get(transaction=tx).to_dict() or {}
            state = {name: {d.id: d.to_dict() for d in
                     self.db.collection(name).stream(transaction=tx, timeout=10)}
                     for name in COLLECTIONS}
            original = deepcopy(state)
            result = operation(state)
            changes = [(name, key, value) for name in COLLECTIONS
                       for key, value in state[name].items()
                       if original[name].get(key) != value]
            if changes:
                for name, key, value in changes:
                    tx.set(self.db.collection(name).document(key), value)
                tx.set(guard, {'revision': version.get('revision', 0) + 1})
            return result
        return transact(self.db.transaction(max_attempts=5))
