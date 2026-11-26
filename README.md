# SmartPark IoT — atividade prática

Continuação do trabalho da equipe conforme **SmartPark IoT Roteiro Atividade.pdf**.
Flask + Firestore + HTML/CSS/JavaScript + Leaflet/OpenStreetMap + ESP32/Wokwi.
A paleta, as telas, os cadastros fictícios e a pinagem original foram aproveitados.

A pasta `public/static/` contém as cópias dos arquivos CSS e JavaScript servidas pela CDN do Vercel. As versões de desenvolvimento continuam em `frontend/`.

**Pendências documentais:** o guia da equipe (`docs/GUIA_EQUIPE.pdf` e `.md`) e o relatório de execução (`docs/RELATORIO_TESTES.md`) não vieram no ZIP original e não foram inventados nesta correção. Consulte `docs/PENDENCIAS_ENTREGA.md`.

## Executar

Python 3.11+ e Node/npm para o comando de testes.

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r backend/requirements.txt
cp .env.example .env
```

Preencha `.env`. Para apresentação sem Firebase, escolha `SMARTPARK_MODE=demo`
e defina `DEMO_USER_TOKEN` e `DEMO_OPERATOR_TOKEN` diferentes e aleatórios.
No login demo, use o token correspondente. Não há pagamento fictício aprovado pela interface:
a integração externa continua exigindo configuração, e os testes usam dublês isolados.

```bash
python app.py
# Abra http://127.0.0.1:5000
npm test
```

No Windows, ative com `.venv\Scripts\activate` e use `python` no lugar de `python3`.
O ambiente `venv/` recebido da equipe contém caminhos de outra máquina; crie o seu.

Para Firebase: use uma **nova** chave fora da pasta do projeto, configure
`GOOGLE_APPLICATION_CREDENTIALS`, `FIREBASE_PROJECT_ID`, `FIREBASE_WEB_API_KEY` e
`SMARTPARK_MODE=firestore`; habilite Authentication por e-mail/senha e Firestore.
Execute `python scripts/seed.py` uma vez com o servidor parado. Consulte o guia para
operadores, Mercado Pago de teste, Telegram e certificado HTTPS do ESP32.

### Deploy na Vercel

Em **Settings → Environment Variables**, configure `FIREBASE_SERVICE_ACCOUNT_JSON`
como segredo de **Production** com o conteúdo completo do JSON de uma conta de
serviço válida. Configure `FIREBASE_PROJECT_ID` e `FIREBASE_WEB_API_KEY` do mesmo
projeto Firebase e defina `SMARTPARK_MODE=firestore`. Faça um novo deploy para
aplicar as variáveis. `GOOGLE_APPLICATION_CREDENTIALS` aponta para um arquivo
local e deve ser omitida na Vercel ao usar o segredo JSON.
Se o segredo já estiver cadastrado como `FIREBASE_CREDENTIALS`, o backend
aceita esse nome quando `FIREBASE_SERVICE_ACCOUNT_JSON` estiver ausente.

Worker de notificações no modo Firestore, em outro terminal com o mesmo ambiente
(no modo demo, use o botão do operador no mesmo processo):

```bash
python -m scripts.notifications
```

**Credencial encontrada:** o JSON de conta de serviço na raiz e `wowki/codigo.txt`
vieram com a mesma chave administrativa. Foram preservados localmente e excluídos do
Git e do pacote de entrega. Revogue a chave antiga no Google Cloud; não a copie para
Wokwi nem a compartilhe. `.gitignore` não revoga uma chave já divulgada.

Nenhuma dependência de produção foi adicionada. ReportLab é usado somente para
gerar o guia (`pip install -r docs/requirements.txt` e `python scripts/build_guide.py`).
