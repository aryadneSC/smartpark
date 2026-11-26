"""Persistência transacional do protótipo; nunca substitui Firebase silenciosamente."""
import json
import os
from copy import deepcopy
from threading import Lock, RLock

COLLECTIONS = ('parking_lots', 'spaces', 'reservations', 'payments', 'access_events',
               'tickets', 'devices', 'notifications', 'users', 'telegram_links')


class MemoryStore:
    def __init__(self):
        self.state = {name: {} for name in COLLECTIONS}
        self.lock = RLock()

    def run(self, operation, collections=None, filters=None, document_ids=None, serialize=True):
        with self.lock:
            original = deepcopy(self.state)
            selected = tuple(dict.fromkeys(collections or COLLECTIONS))
            filters = filters or {}
            document_ids = document_ids or {}
            state = {name: {} for name in COLLECTIONS}
            for name in selected:
                if name in document_ids:
                    requested = document_ids[name]
                    keys = requested(state) if callable(requested) else requested
                    state[name] = {key: deepcopy(original[name][key]) for key in dict.fromkeys(keys)
                                   if key in original[name]}
                    continue
                docs = original[name]
                for field, operator, value in filters.get(name, ()):
                    if operator != '==':
                        raise ValueError(f'Filtro de memória não suportado: {operator}')
                    docs = {key: item for key, item in docs.items() if item.get(field) == value}
                state[name] = deepcopy(docs)
            loaded_ids = {name: set(state[name]) for name in selected}
            result = operation(state)
            for name in selected:
                for key in loaded_ids[name] - set(state[name]):
                    self.state[name].pop(key, None)
                self.state[name].update(state[name])
            return result


class LazyFirestoreStore:
    """Inicializa Firebase apenas quando uma rota realmente precisar dele."""
    def __init__(self):
        self._store = None
        self._lock = Lock()

    def run(self, operation, collections=None, filters=None, document_ids=None, serialize=True):
        if self._store is None:
            with self._lock:
                if self._store is None:
                    self._store = FirestoreStore()
        return self._store.run(operation, collections=collections, filters=filters,
                                document_ids=document_ids, serialize=serialize)


class FirestoreStore:
    def __init__(self):
        import firebase_admin
        from firebase_admin import credentials, firestore
        try:
            firebase_admin.get_app()
        except ValueError:
            project = os.getenv('FIREBASE_PROJECT_ID')
            options = {'projectId': project} if project else None
            credential = None
            service_account = os.getenv('FIREBASE_SERVICE_ACCOUNT_JSON')
            if service_account is None:
                service_account = os.getenv('FIREBASE_CREDENTIALS')
            if service_account is not None:
                try:
                    info = json.loads(service_account)
                    if not isinstance(info, dict):
                        raise ValueError
                    credential = credentials.Certificate(info)
                except ValueError:
                    raise ValueError(
                        'Configure FIREBASE_SERVICE_ACCOUNT_JSON com um JSON válido de conta de serviço do Firebase.'
                    ) from None
            firebase_admin.initialize_app(credential, options=options)
        self.db = firestore.client()
        self.firestore = firestore

    def run(self, operation, collections=None, filters=None, document_ids=None, serialize=True):
        # A trava global preserva a exclusão de reservas simultâneas; cada operação
        # declara apenas as coleções/documentos que precisa ler.
        selected = tuple(dict.fromkeys(collections or COLLECTIONS))
        unknown = set(selected) - set(COLLECTIONS)
        if unknown:
            raise ValueError(f'Coleções desconhecidas: {sorted(unknown)}')
        filters = filters or {}
        document_ids = document_ids or {}
        @self.firestore.transactional
        def transact(tx):
            guard = self.db.collection('_locks').document('smartpark')
            version = {}
            if serialize:
                version = guard.get(transaction=tx).to_dict() or {}
            state = {name: {} for name in COLLECTIONS}
            for name in selected:
                collection = self.db.collection(name)
                if name in document_ids:
                    requested = document_ids[name]
                    keys = requested(state) if callable(requested) else requested
                    for key in dict.fromkeys(keys):
                        snapshot = collection.document(key).get(transaction=tx)
                        if snapshot.exists:
                            state[name][key] = snapshot.to_dict()
                    continue
                query = collection
                for field, operator, value in filters.get(name, ()):
                    query = query.where(field, operator, value)
                state[name] = {doc.id: doc.to_dict() for doc in
                               query.stream(transaction=tx, timeout=10)}
            original = deepcopy(state)
            result = operation(state)
            omitted_changes = [name for name in COLLECTIONS if name not in selected and state[name] != original[name]]
            if omitted_changes:
                raise ValueError(f'Operação alterou coleções não carregadas: {omitted_changes}')
            changes = [(name, key, value) for name in selected
                       for key, value in state[name].items()
                       if original[name].get(key) != value]
            if changes:
                for name, key, value in changes:
                    tx.set(self.db.collection(name).document(key), value)
                if serialize:
                    tx.set(guard, {'revision': version.get('revision', 0) + 1})
            return result
        return transact(self.db.transaction(max_attempts=5))
