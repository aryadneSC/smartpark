"""Ponto de entrada do Vercel: expõe a aplicação Flask do SmartPark como variável `app`."""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if ROOT not in sys.path:
    sys.path.insert(0, ROOT)

from app import app  # noqa: E402,F401  (app.py da raiz cria a aplicação com create_app())
