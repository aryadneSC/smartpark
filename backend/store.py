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
        from firebase_admin import firestore, credentials

        try:
            firebase_admin.get_app()
        except ValueError:
            project = os.getenv('FIREBASE_PROJECT_ID')
            credentials_json = os.getenv('FIREBASE_CREDENTIALS')

            if credentials_json:
                import json
                certificate = credentials.Certificate(
                    json.loads(credentials_json)
                )
                firebase_admin.initialize_app(
                    certificate,
                    options={'projectId': project} if project else None
                )
            else:
                raise RuntimeError(
                    'FIREBASE_CREDENTIALS não configurada no ambiente.'
                )

        self.db = firestore.client()
        self.firestore = firestore

    def run(self, operation):
        @self.firestore.transactional
        def transact(tx):
            guard = self.db.collection('_locks').document('smartpark')
            version = guard.get(transaction=tx).to_dict() or {}
            state = {
                name: {
                    d.id: d.to_dict()
                    for d in self.db.collection(name).stream(
                        transaction=tx, timeout=10
                    )
                }
                for name in COLLECTIONS
            }
            original = deepcopy(state)
            result = operation(state)

            changes = [
                (name, key, value)
                for name in COLLECTIONS
                for key, value in state[name].items()
                if original[name].get(key) != value
            ]

            if changes:
                for name, key, value in changes:
                    tx.set(self.db.collection(name).document(key), value)

                tx.set(
                    guard,
                    {'revision': version.get('revision', 0) + 1}
                )

            return result
          
        return transact(self.db.transaction(max_attempts=5))
