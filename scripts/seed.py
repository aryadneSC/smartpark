"""
seed.py: popula o Firestore com dados FICTÍCIOS de demonstração.

Uso (na raiz, com o ambiente virtual ativo):
    python scripts/seed.py            # cria/atualiza os dados (pode rodar várias vezes)
    python scripts/seed.py --clear    # remove apenas os documentos marcados como demo

Requisitos:
    pip install firebase-admin python-dotenv
    .env com GOOGLE_APPLICATION_CREDENTIALS apontando para a chave da conta de serviço
    (o arquivo da chave fica FORA do repositório e nunca é versionado).
"""
import os
import sys
from pathlib import Path

import firebase_admin
from dotenv import load_dotenv
from firebase_admin import firestore

load_dotenv(Path(__file__).resolve().parents[1] / ".env")

if not os.environ.get("GOOGLE_APPLICATION_CREDENTIALS"):
    sys.exit("Defina GOOGLE_APPLICATION_CREDENTIALS no .env (caminho da chave da conta de serviço).")

firebase_admin.initialize_app()
db = firestore.client()

# IDs fixos tornam o script idempotente: rodar de novo atualiza em vez de duplicar.
PARKING_LOTS = [
    {
        "id": "centro",
        "name": "Unidade Centro",
        "address": "Região da Av. Paulista (aprox.)",
        "latitude": -23.5614,
        "longitude": -46.6559,
        "hourly_rate": 12.00,
        "opening_hours": {"open": "06:00", "close": "23:00"},
        "prefix": "A",
        "spaces": 12,
        "device_id": "esp32-centro-01",
    },
    {
        "id": "pinheiros",
        "name": "Unidade Pinheiros",
        "address": "Região do Largo da Batata (aprox.)",
        "latitude": -23.5668,
        "longitude": -46.6925,
        "hourly_rate": 9.00,
        "opening_hours": {"open": "00:00", "close": "23:59"},
        "prefix": "B",
        "spaces": 10,
        "device_id": "esp32-pinheiros-01",
    },
    {
        "id": "vila",
        "name": "Unidade Vila Madalena",
        "address": "Região da Rua Aspicuelta (aprox.)",
        "latitude": -23.5546,
        "longitude": -46.6914,
        "hourly_rate": 10.00,
        "opening_hours": {"open": "07:00", "close": "22:00"},
        "prefix": "C",
        "spaces": 8,
        "device_id": "esp32-vila-01",
    },
]

# Coleções que recebem dados de demonstração (todas com o campo is_demo=True).
DEMO_COLLECTIONS = ["parking_lots", "spaces", "devices"]


def seed():
    batch = db.batch()
    ops = 0

    for lot in PARKING_LOTS:
        lot_ref = db.collection("parking_lots").document(lot["id"])
        if not lot_ref.get().exists:
            batch.set(lot_ref, {
                "name": lot["name"],
                "address": lot["address"],
                "latitude": lot["latitude"],
                "longitude": lot["longitude"],
                "hourly_rate": lot["hourly_rate"],
                "opening_hours": lot["opening_hours"],
                "active": True,
                "is_demo": True,
            })
            ops += 1

        for i in range(1, lot["spaces"] + 1):
            label = f"{lot['prefix']}-{i:02d}"
            space_ref = db.collection("spaces").document(f"{lot['id']}_{label}")
            if space_ref.get().exists:
                continue
            batch.set(space_ref, {
                "parking_lot_id": lot["id"],
                "label": label,
                "active": True,
                "occupancy_state": "FREE",  # FREE | OCCUPIED (reservas são tratadas na coleção reservations)
                "is_demo": True,
            })
            ops += 1

        # Cadastro do dispositivo. Nenhum segredo é guardado aqui.
        device_ref = db.collection("devices").document(lot["device_id"])
        if not device_ref.get().exists:
            batch.set(device_ref, {
                "parking_lot_id": lot["id"],
                "status": "UNKNOWN",
                "last_seen": None,
                "firmware_version": "0.1.0",
                "is_demo": True,
            })
            ops += 1

        # O limite de um batch do Firestore é 500 operações.
        if ops >= 400:
            batch.commit()
            batch = db.batch()
            ops = 0

    if ops:
        batch.commit()

    total_spaces = sum(l["spaces"] for l in PARKING_LOTS)
    print(f"OK: {len(PARKING_LOTS)} estacionamentos, {total_spaces} vagas e "
          f"{len(PARKING_LOTS)} dispositivos conferidos (existentes preservados).")


def clear():
    removed = 0
    for name in DEMO_COLLECTIONS:
        for doc in db.collection(name).where("is_demo", "==", True).stream():
            doc.reference.delete()
            removed += 1
    print(f"OK: {removed} documentos de demonstração removidos.")


if __name__ == "__main__":
    if "--clear" in sys.argv:
        clear()
    else:
        seed()