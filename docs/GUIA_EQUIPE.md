# SmartPark IoT
## Guia oficial da atividade — versão 1.1

Data: 13/11/2026. Público: equipe do curso técnico, com explicações para quem está começando. Documento de referência: SmartPark IoT Roteiro Atividade.pdf, recebido junto do projeto. Este guia documenta a implementação e as decisões do protótipo escolar; não constitui homologação de um sistema de estacionamento.

## 1. O que a equipe está entregando

O motorista consulta estacionamentos fictícios no mapa, escolhe um período, solicita uma reserva e abre o checkout de teste. O backend verifica o pagamento e decide se o ESP32 pode simular a abertura da cancela. Na saída, grava um ticket e agenda seu envio pelo Telegram. O operador acompanha ocupação, reservas e alertas.

O código implementa RF01 a RF10. A execução local foi testada com serviços externos substituídos por dublês controlados. Firebase, pagamento de teste, bot e circuito remoto precisam da configuração e das evidências descritas neste guia. A presença do código de integração não significa que houve transação financeira ou execução do servo no Wokwi nesta entrega.

Entregas locais: backend Flask; interface HTML/CSS/JavaScript; diagrama complementar; firmware ESP32; regras do Firestore; seed de demonstração; testes; guia editável e PDF; relatório de testes; pacote ZIP sem segredos. O link remoto do Wokwi e as evidências dos provedores serão registrados pela equipe após configurar suas contas.

## 2. Continuidade do trabalho da equipe

O projeto recebido tinha a tela com três abas, formulário de login, mapa Leaflet, paleta cinza/amarela, três estacionamentos fictícios no seed e um circuito com ESP32, servo, LCD, dois botões e um ultrassônico. O backend mais avançado estava em backend/app.py, com leitura de locais e estimativa; outras cópias continham rotas incompletas e caminhos Windows fixos.

Foram mantidos o layout e as cores de frontend/css/style.css, a estrutura de frontend/index.html, os nomes de campos e coleções, os cadastros do seed e as ligações existentes. Os caminhos dos recursos da página foram corrigidos para a aplicação Flask. Os marcadores passaram a receber dados da API em vez da lista isolada de Salvador, pois o seed da equipe cadastra unidades fictícias em São Paulo. O mapa se ajusta automaticamente aos locais disponíveis.

app.py tornou-se a entrada única. frontend/services/app.py e frontend/services/seed.py continuam existindo como atalhos para a implementação oficial. O seed principal continua em scripts/seed.py; agora não sobrescreve documentos existentes. Após o recebimento do sketch da equipe, o firmware foi ajustado para preservar também seus nomes PIN_*, enum Cancela_Status_t, funções conectar_wifi, init_pins, init_cancela e mover_cancela e ângulos de 180 graus (aberta) e 60 graus (fechada). O histórico objetivo das correções está em firmware/ALTERACOES.md. O diagrama original wowki/diagram.json.txt está preservado. wowki/diagram.json acrescenta apenas um segundo sensor de distância, em GPIO 25/26, conforme sugestão do roteiro.

Nenhum arquivo de src/auth/ foi modificado. Não havia TypeScript nesta base; foi mantida a tecnologia JavaScript prevista no roteiro, sem introduzir dependências de produção. O comando npm test executa os testes Python e verifica a sintaxe JavaScript.

## 3. Arquitetura e responsabilidades

Fluxo: navegador → Flask → Firestore. Flask → Mercado Pago para criar preferência e consultar pagamento. Mercado Pago → webhook Flask. ESP32 → Flask para entrada, saída e telemetria. Worker Flask → Telegram para notificações. Leaflet usa os tiles do OpenStreetMap apenas como cartografia.

O navegador envia local e horários, nunca determina o preço ou o usuário da reserva. O token de identidade determina o proprietário. O servidor calcula valores, impede conflitos e registra eventos. O ESP32 recebe somente uma decisão mínima; não recebe tokens de pagamento, chave administrativa do Firebase ou token do bot.

Backend/app.py concentra regras e rotas; backend/store.py separa memória de Firestore; backend/providers.py concentra chamadas HTTPS. Esta divisão permite testar as regras sem depender da internet. Um dublê é um objeto controlado pelo teste que responde como o provedor responderia; ele não é usado para aprovar pagamentos pela interface.

No modo demo os dados vivem apenas na memória de um processo. No modo firestore os dados são persistentes. Uma falha do Firestore não muda automaticamente para demo. O operador precisa saber em qual ambiente está; a faixa do cabeçalho identifica o modo.

## 4. Preparação do ambiente

Use Python 3.11 ou superior e Node com npm para executar o comando de verificação. Abra um terminal na raiz do projeto. O venv recebido tem referências de outra máquina; crie um ambiente novo.

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r backend/requirements.txt
cp .env.example .env
```

No Windows: python -m venv .venv, depois .venv\Scripts\activate. Use python no lugar de python3. O npm test usa python3; caso seu Windows não tenha esse alias, execute python -m unittest discover -s tests -v e node --check frontend/scripts/map.js.

O arquivo .env é local. Preencha os valores sem publicá-lo. Não copie segredos para README, slides, prints, mensagens ou Wokwi. O arquivo .env.example só contém marcadores.

```bash
python app.py
# Abra http://127.0.0.1:5000
npm test
```

Não abra index.html por file://. Flask serve a página em / e os recursos em /static. Assim, as chamadas /api usam a mesma origem. Não é necessário um segundo servidor de frontend.

## 5. Demonstração sem Firebase

No .env, configure SMARTPARK_MODE=demo. Defina DEMO_USER_TOKEN e DEMO_OPERATOR_TOKEN com valores diferentes e aleatórios. Para gerar um valor local, execute o comando a seguir duas vezes e copie cada resultado apenas para seu .env.

```bash
python -c "import secrets; print(secrets.token_urlsafe(32))"
```

Inicie python app.py. No diálogo Entrar, informe o token local do motorista ou do operador. O token do operador representa uma conta autorizada somente nesta demonstração. Ele não se torna um usuário Firebase e não funciona quando SMARTPARK_MODE=firestore.

Há três locais com 30 vagas ao todo. Reservar cria PENDING_PAYMENT. Se o Mercado Pago não estiver configurado, o checkout informa indisponibilidade e a reserva continua pendente; ela não é confirmada por simulação no navegador. Retome o pagamento em Minhas reservas após configurar o serviço. A reserva expira em até 15 minutos.

Reiniciar o processo apaga os dados demo. Use apenas um processo. Para processar notificações demo, use o botão do painel do operador. Um worker separado teria outra memória e, por isso, scripts/notifications.py recusa esse modo.

## 6. Variáveis de configuração

SMARTPARK_MODE: firestore por padrão; demo somente quando explicitamente selecionado. APP_TIMEZONE: America/Bahia; datas sem fuso são interpretadas nesse fuso. APP_BASE_URL: origem HTTPS pública do túnel, sem barra final. FRONTEND_ORIGIN: origem autorizada para CORS; ao usar o túnel, configure sua origem HTTPS.

GOOGLE_APPLICATION_CREDENTIALS: caminho absoluto da nova chave administrativa, fora do projeto. FIREBASE_PROJECT_ID: identificador do projeto. FIREBASE_WEB_API_KEY: chave de configuração Web usada para login por e-mail/senha, distinta da chave privada da conta de serviço.

MERCADOPAGO_ACCESS_TOKEN: credencial do ambiente de teste. MERCADOPAGO_WEBHOOK_SECRET: segredo de assinatura fornecido pelo painel da aplicação. Não é o mesmo token de acesso. Pagamentos live_mode=true são rejeitados nesta atividade.

TELEGRAM_BOT_TOKEN: token do BotFather. TELEGRAM_BOT_USERNAME: nome do bot sem @. TELEGRAM_WEBHOOK_SECRET: segredo aleatório para autenticar as chamadas recebidas do Telegram. TELEGRAM_DEFAULT_CHAT_ID: conversa operacional validada, destinada a alertas; não substitui o vínculo pessoal do motorista.

DEVICE_ID: esp32-centro-01 por padrão. DEVICE_SHARED_SECRET: segredo próprio do dispositivo simulado; deve coincidir com config.h. Esta versão aceita um dispositivo configurado por instância. Os outros dispositivos do seed são exemplos de cadastro. Para demonstrar outra unidade, use seu ID e a reserva correspondente.

DEMO_USER_TOKEN e DEMO_OPERATOR_TOKEN: usados exclusivamente em demo. ROOT_CA e API_BASE ficam no config.h local do firmware; ROOT_CA contém certificado público da autoridade certificadora, nunca chave privada.

## 7. Firebase: configuração passo a passo

Antes de conectar, substitua a credencial encontrada na raiz e em wowki/codigo.txt. O conteúdo desse TXT é uma conta de serviço, não código ESP32. No Google Cloud Console, acesse IAM e administrador → Contas de serviço → conta correspondente → Chaves; exclua/revogue a chave antiga. Gere outra somente se necessário ao ambiente didático. Salve fora do repositório, por exemplo em uma pasta pessoal de credenciais com acesso restrito. A chave antiga não foi usada para autenticar serviços nesta implementação.

No Firebase Console, crie ou selecione o projeto da atividade. Habilite Cloud Firestore no banco padrão (default). Em Authentication, habilite o provedor E-mail/senha e cadastre contas fictícias para os participantes. Registre um aplicativo Web para obter a configuração pública e a API key. Não coloque o JSON administrativo no frontend.

Preencha GOOGLE_APPLICATION_CREDENTIALS, FIREBASE_PROJECT_ID e FIREBASE_WEB_API_KEY no .env. Configure SMARTPARK_MODE=firestore. A conta de serviço precisa de permissão IAM para ler/escrever no Firestore e verificar usuários Firebase. O Admin SDK ignora as regras de acesso direto: as verificações de usuário e operador continuam obrigatórias no Flask.

Publique no console as regras de firestore.rules. Elas negam acesso direto às coleções pelo navegador; a aplicação usa o backend para todas as operações de negócio. Authentication continua sendo usado para identidade. Não abra as regras para corrigir erros de permissão do Admin SDK: ajuste o IAM da conta de serviço.

Com o ambiente virtual ativo, execute um diagnóstico somente de leitura. Ele não revela o conteúdo da chave.

```bash
python -m scripts.check_firebase
python scripts/seed.py
python app.py
```

Execute o seed com o servidor parado. Ele cria documentos ausentes, mantém os IDs da equipe e preserva os existentes. O parâmetro --clear apaga apenas documentos marcados is_demo nas coleções parking_lots, spaces e devices; ele não limpa reservas, pagamentos ou tickets. Use-o somente antes de começar os testes, em um banco descartável. Não o execute com reservas em andamento.

Para autorizar um operador, use o Admin SDK em um terminal privado com a conta administrativa configurada. Substitua UID pela identidade do usuário de teste. Depois, saia e entre novamente para obter um token contendo a permissão.

```python
import firebase_admin
from firebase_admin import auth
firebase_admin.initialize_app()
user = auth.get_user('UID')
auth.set_custom_user_claims(user.uid, {**(user.custom_claims or {}), 'operator': True})
```

O backend aceita Authorization: Bearer ID_TOKEN e verifica assinatura, validade e revogação. A sessão da interface permanece apenas em memória; ao recarregar ou expirar o token, entre novamente. Digitar um código de reserva de outra pessoa não dá acesso ao documento. Alterar o botão do painel no navegador também não concede a permissão de operador.

## 8. Firebase indisponível: como proceder

Se a inicialização falhar antes de abrir o servidor, confira se o caminho da nova chave existe e se o JSON pertence ao projeto correto. Não reutilize o caminho C:\Users\aluno.fsa da implementação antiga. Verifique GOOGLE_APPLICATION_CREDENTIALS e se o ambiente virtual está ativo.

Se o diagnóstico responder DefaultCredentialsError, revise a variável e o acesso ao arquivo. Se responder PermissionDenied, revise IAM e a habilitação do Firestore. Se houver DeadlineExceeded ou ServiceUnavailable, verifique internet, DNS, proxy/firewall e disponibilidade do serviço. Se o login falhar, confira E-mail/senha habilitado, a conta cadastrada e FIREBASE_WEB_API_KEY do mesmo projeto.

Não exponha a mensagem completa de bibliotecas se ela contiver dados da conta. O diagnóstico informa apenas a classe do erro. A API devolve 503 para falhas de persistência e não afirma que uma operação foi concluída sem confirmação do banco.

Para continuar a parte escolar sem conexão, pare o servidor, selecione SMARTPARK_MODE=demo e configure os tokens locais. Reinicie. Registre no relatório que a persistência remota ainda não foi validada. Os dados da memória não são migrados automaticamente. Ao recuperar a conexão, volte a firestore e repita o diagnóstico, seed se necessário e os testes integrados.

## 9. Regras de reserva e disponibilidade

Arrival e departure são datas ISO 8601; a saída deve ser posterior à chegada, com duração máxima de 24 horas. A chegada não pode estar no passado, salvo tolerância de um minuto para diferenças entre preenchimento e envio. O horário de funcionamento cadastrado pelo seed é validado no fuso local. Locais criados no painel desta versão funcionam 24 horas na simulação.

O cálculo usa horas iniciadas: ceil(duração em horas) × tarifa cadastrada. Exemplo: 1h20 a R$ 12/h resulta em R$ 24. Campos price_estimate, status e user_id enviados pelo cliente são ignorados na criação; o servidor define os valores confiáveis.

Intervalos de reserva usam [chegada, saída): uma termina exatamente quando outra começa sem sobreposição. PENDING_PAYMENT bloqueia a vaga até expirar; CONFIRMED bloqueia o período reservado; CHECKED_IN bloqueia a vaga fisicamente, inclusive após o fim previsto, até a saída. Uma vaga já ocupada não autoriza uma segunda entrada.

A reserva pendente expira em 15 minutos ou no fim previsto, o que ocorrer primeiro. A expiração é recalculada a cada operação de negócio. Cancelamento e saída liberam o bloqueio correspondente. O mapa consulta a próxima hora; ao escolher datas, a estimativa apresenta disponibilidade específica daquele período. A consulta não garante a vaga: a criação ainda pode responder 409 se outra pessoa reservou antes.

## 10. Concorrência e persistência

Concorrência ocorre quando duas solicitações chegam quase juntas. Consultar vagas e depois gravar sem proteção permitiria que ambas escolhessem a última vaga. Na memória, um bloqueio de processo protege a operação inteira; no Firestore, uma transação lê os documentos, verifica a regra e grava as alterações atomicamente.

O documento _locks/smartpark é o ponto de coordenação das mutações. Se duas transações partirem da mesma revisão, o Firestore repete uma delas; na nova leitura, a vaga já estará bloqueada e a segunda reserva recebe 409. Todas as leituras são feitas antes das gravações. Chamadas a pagamento e Telegram ficam fora da transação, pois uma repetição automática não deve repetir efeitos externos.

Esta estratégia é intencionalmente pequena e didática: lê as coleções da atividade e serializa mutações em um ponto. Não foi projetada para um banco grande. Não faça alterações manuais no console ou execute o seed durante reservas, porque essas ações não seguem o protocolo de coordenação. Um sistema maior exigiria bloqueios por local/período e consultas limitadas.

## 11. Pagamento e política de tarifa

Foi escolhida a Opção A recomendada no roteiro: preço fixo antecipado. O pagamento de teste cobre o intervalo previsto. A saída registra a duração real, mas o valor final mantém price_estimate. Não há débito complementar ou reembolso automático nesta versão.

Entrada permitida de 15 minutos antes até 30 minutos depois da chegada prevista, limitada pelo fim previsto. Chegar fora dessa janela não autoriza entrada; o operador analisa a situação. Permanência excedente não impede a saída da simulação e gera alerta operacional, mantendo o valor fixo. Não há nova entrada após completar a estadia.

Cancelar uma reserva pendente libera a vaga. Cancelar uma reserva paga libera o período, marca refund_status=MANUAL_REVIEW e gera alerta para eventual reembolso fora da aplicação. Pagamento recebido após EXPIRED ou CANCELLED nunca reativa a reserva; gera revisão manual. Um segundo pagamento aprovado para a mesma reserva também gera alerta, sem conceder novo acesso.

Configure a aplicação Mercado Pago no ambiente de testes, suas contas de teste e credenciais. Use APP_BASE_URL HTTPS público e configure o tópico de pagamentos para /webhooks/mercadopago no painel. URLs de retorno apontam para /?payment_return=success, failure ou pending. Estes parâmetros só exibem orientação; não alteram a reserva.

O backend cria uma preferência em /checkout/preferences com external_reference igual ao ID interno. O preço vem do banco. A interface recebe apenas checkout_url, correspondente ao sandbox_init_point. O resultado é armazenado e reutilizado. Enquanto a criação está em andamento, outra tentativa recebe 409. Falha com resultado desconhecido exige revisão, evitando criar uma segunda preferência sem saber o que ocorreu.

No webhook, a aplicação exige data.id na query, x-request-id e x-signature contendo ts e v1. Monta o manifesto id:ID;request-id:REQUEST_ID;ts:TS; com ID em minúsculas e compara HMAC SHA-256 usando o segredo. A assinatura tem tolerância local de cinco minutos. Mantenha o relógio do servidor sincronizado; uma repetição antiga exige nova notificação assinada pelo provedor.

Após validar a assinatura, consulta /v1/payments/ID com o token do servidor. Confere ID, referência interna, moeda BRL, valor e live_mode=false. Somente status approved confirma uma reserva ainda válida e com vaga compatível. Não se usa o status recebido no navegador como comprovação. Uma notificação atrasada de pending depois de approved não rebaixa a reserva; cria alerta de reconciliação.

No painel do provedor, confira o formato vigente da integração Checkout Pro baseada em preferências e pagamentos. Esta implementação não processa notificações do modelo Orders. Antes da apresentação integrada, execute pagamento aprovado, rejeitado, pendente, webhook duplicado e aprovação após expiração. Guarde prints sem tokens e registre IDs fictícios ou parcialmente ocultos.

## 12. Entrada, saída e idempotência

Idempotência significa que repetir o mesmo evento não repete seu efeito. O ESP32 envia device_id, event_id, reservation_id e type (ENTRY ou EXIT). O servidor autentica X-Device-Secret, verifica o cadastro e associa o dispositivo ao estacionamento. Há limite de 30 novos eventos por minuto; telemetria aceita no máximo uma chamada a cada cinco segundos.

Para entrada, exige CONFIRMED, pagamento aprovado, janela válida e vaga não ocupada. O servidor registra arrival_real e CHECKED_IN, marcando a vaga OCCUPIED. Na saída, exige CHECKED_IN, grava departure_real, calcula minutos arredondados para cima, marca COMPLETED e cria um ticket único com a política fixa.

O mesmo event_id com conteúdo igual retorna o resultado armazenado e duplicate=true, mas actuate=false. O firmware só abre quando allowed=true, actuate=true e event_id coincide. Isso evita nova abertura no replay. Se a resposta original se perder após o banco confirmar a entrada, a repetição preserva os dados mas não abre novamente: a equipe deve reconciliar o estado na simulação. É uma escolha conservadora documentada, não uma garantia de entrega física exatamente uma vez.

Reusar event_id com outra reserva, dispositivo ou tipo retorna 409. Repetir uma saída com novo ID também não cria outro ticket, pois a reserva já está COMPLETED. O servidor usa seu próprio relógio; não confia no timestamp do ESP32 para o valor final.

## 13. Modelo de dados adotado

parking_lots: name, address, latitude, longitude, hourly_rate, opening_hours, active e is_demo nos exemplos. spaces: parking_lot_id, label, active, occupancy_state. IDs do seed seguem centro_A-01 etc. Estados da vaga: FREE ou OCCUPIED; reserva futura é consultada separadamente.

reservations: user_id, parking_lot_id, space_id, arrival_expected, departure_expected, created_at, expires_at, status, price_estimate, tariff_policy e payment_status. Após entrada/saída: arrival_real e departure_real. Integração adiciona preference_id, checkout_url, checkout_state, payment_id e, quando necessário, refund_status.

payments: reservation_id, provider, preference_id ou payment_id, status, amount, currency e verified_at. O ID preference-RESERVA representa a preferência; o ID do pagamento representa a consulta verificada. Não há dados de cartão. access_events: event_id, reservation_id, device_id, type, timestamp, result e response.

tickets: documento com ID igual ao da reserva; ticket_id, reservation_id, parking_lot_id, space_id, arrival_real, departure_real, duration_minutes, final_amount, payment_status e telegram_status. Um documento por estadia concluída.

devices: parking_lot_id, status, last_seen, last_telemetry, firmware_version, access_window e access_count. O segredo não é salvo na coleção. notifications: type, reservation_id, text quando necessário, status, attempts, next_attempt, claimed_at, message_id e sent_at.

users: vínculo privado telegram_chat_id pelo UID. telegram_links: código aleatório de uso único, user_id, expires_at e used. _locks: revisão da transação. Datas novas são strings ISO com fuso; datas Firestore nativas recebidas pela camada de regras também são aceitas. Os documentos não devem ser editados manualmente durante o uso.

## 14. Contrato da API

Respostas usam JSON. Erros seguem {"error":"mensagem"}. Códigos: 200 sucesso, 201 criação, 400 dados inválidos, 401 ausência/falha de autenticação, 403 permissão insuficiente, 404 recurso não encontrado ou sem acesso, 409 conflito de estado, 429 limite de frequência, 503 serviço não configurado ou indisponível. Corpo JSON limitado a 16 KiB.

GET /api/config informa modo e configuração pública. POST /api/session recebe email e password no modo Firebase e devolve token; no modo demo, use os tokens locais. GET /api/me devolve uid e permissão operator a partir do token verificado.

GET /api/parking-lots é público e retorna id, nome, endereço, coordenadas, tarifa, horário, total_spaces e free_spaces. GET /api/parking-lots/ID/availability?arrival=ISO&departure=ISO retorna available, free_spaces, total_spaces, billable_hours e price_estimate.

POST /api/reservations exige Bearer e recebe parking_lot_id, arrival e departure. GET /api/reservations retorna apenas as reservas do usuário autenticado. GET /api/reservations/ID permite proprietário ou operador e inclui ticket quando houver. POST /api/reservations/ID/checkout devolve checkout_url. POST /api/reservations/ID/cancel cancela conforme a política.

POST /webhooks/mercadopago recebe notificação assinada, consulta o provedor e processa pagamento. POST /api/iot/access-request exige X-Device-Secret. POST /api/iot/telemetry recebe device_id e status válido, atualiza last_seen e registra alertas de falha/manutenção.

POST /api/telegram/link exige usuário e cria vínculo de dez minutos. POST /webhooks/telegram exige X-Telegram-Bot-Api-Secret-Token e consome /start em chat privado. POST /api/admin/parking-lots exige operator e recebe name, address, latitude, longitude, hourly_rate e spaces (1 a 50). GET /api/admin/dashboard retorna ocupação, reservas, alertas e estados de envio. POST /api/admin/notifications/retry processa até dez envios elegíveis.

```json
{
  "parking_lot_id": "centro",
  "arrival": "2026-11-20T14:00:00-03:00",
  "departure": "2026-11-20T16:00:00-03:00"
}
```

Exemplo de corpo de reserva acima. Ajuste a data para o futuro e respeite o horário da unidade. Para solicitar entrada, use o exemplo abaixo com ID retornado pela sua reserva e um event_id novo.

```json
{
  "device_id": "esp32-centro-01",
  "event_id": "ensaio-entrada-001",
  "reservation_id": "SP-ID-DA-SUA-RESERVA",
  "type": "ENTRY"
}
```

## 15. Telegram: vínculo e envio

Crie o bot no BotFather, configure TELEGRAM_BOT_TOKEN e TELEGRAM_BOT_USERNAME. Gere um segredo aleatório para TELEGRAM_WEBHOOK_SECRET. Registre no Telegram um webhook HTTPS que aponte para APP_BASE_URL/webhooks/telegram, enviando esse segredo como secret_token. Exemplo em Python abaixo; executá-lo altera o webhook do seu próprio bot.

```python
import os
from dotenv import load_dotenv
from backend.providers import http_json
load_dotenv()
http_json('https://api.telegram.org/bot' + os.environ['TELEGRAM_BOT_TOKEN'] + '/setWebhook', {
    'url': os.environ['APP_BASE_URL'].rstrip('/') + '/webhooks/telegram',
    'secret_token': os.environ['TELEGRAM_WEBHOOK_SECRET'],
    'allowed_updates': ['message']
})
```

No dashboard, entre e clique Vincular Telegram. Abra o link e pressione Iniciar para enviar /start CODIGO. O código aleatório vence em dez minutos e funciona uma única vez. O servidor só associa o chat privado proveniente do webhook autenticado; não aceita chat_id arbitrário do navegador.

Para alertas operacionais, inicie uma conversa com o bot e obtenha o ID da conversa a partir de uma atualização validada em seu ambiente. Configure-o em TELEGRAM_DEFAULT_CHAT_ID. Evite grupos públicos. Não coloque tokens nos prints.

No modo Firestore, execute um worker com o mesmo .env em outro terminal. Para uma rodada manual, use Flask CLI.

```bash
python -m scripts.notifications
# alternativa, uma rodada:
flask --app app send-notifications
```

O worker tenta até dez mensagens por rodada, a cada 15 segundos. Falhas com resposta conhecida são adiadas: após a primeira tentativa, aguarda 120 segundos; depois, 240; máximo três tentativas. Sem vínculo pessoal ou chat operacional configurado, a mensagem fica pendente sem consumir tentativas. No modo demo, processe pelo botão do operador na mesma aplicação.

Eventos de confirmação e ticket usam chaves determinísticas confirm-RESERVA e ticket-RESERVA. Uma transação marca SENDING antes do envio para impedir dois workers de enviar a mesma mensagem. Se o envio funcionou, salva SENT e message_id. Se a resposta se perder ou o processo cair, o estado passa a MANUAL_REVIEW; não há reenvio automático de resultado incerto. Isso evita prometer uma garantia que a API sendMessage não oferece. O ticket permanece salvo mesmo se o Telegram falhar.

Exemplo de confirmação: SMARTPARK CONFIRMATION | SP-... | Unidade Centro | vaga centro_A-01 | período previsto | pagamento approved. Exemplo de alerta: Dispositivo sem comunicação: esp32-centro-01. Exemplo de ticket: SMARTPARK — COMPROVANTE TK-SP-... | local e vaga | chegada/saída reais | duração | valor | pagamento approved.

A verificação de dispositivo sem comunicação ocorre ao consultar o painel do operador; após dois minutos sem last_seen, cria alerta. Esta versão não possui serviço separado de monitoramento contínuo. Um dispositivo que nunca enviou telemetria aparece UNKNOWN e deve ser conferido na configuração.

## 16. Wokwi: circuito e firmware

Crie um projeto ESP32 DevKit no Wokwi. Importe wowki/diagram.json no editor de diagrama. Copie firmware/sketch.ino para o sketch e firmware/libraries.txt para a lista de bibliotecas. Crie config.h local a partir de firmware/config.example.h. Não use wowki/codigo.txt: ele é uma credencial administrativa exposta. O sketch enviado posteriormente pela equipe foi incorporado em firmware/sketch.ino; veja firmware/ALTERACOES.md para a relação das correções. O token do Telegram que estava nesse sketch precisa ser revogado no BotFather e substituído somente no backend.

Bibliotecas do sketch: ArduinoJson versão com JsonDocument (linha 7), ESP32Servo e LiquidCrystal I2C. Não são dependências do backend. O endereço do LCD é 0x27. A pinagem original da equipe foi preservada. O segundo sensor foi adicionado sem rearranjar os demais componentes.

Pinagem: LED vermelho GPIO 4; LED verde GPIO 5; servo GPIO 18; LCD SDA 21 e SCL 22; botão de entrada 27; botão de saída 14; HC-SR04 de entrada TRIG 33 e ECHO 34; HC-SR04 de saída TRIG 25 e ECHO 26. Botões usam INPUT_PULLUP e fecham para GND. LEDs mantêm resistores de 1 kΩ do circuito original. Sensor e servo compartilham referência de GND.

Na simulação, Echo está conectado diretamente conforme o diagrama. Em hardware físico, o HC-SR04 pode retornar 5 V e o ESP32 requer adaptação para entrada de 3,3 V. A atividade não fornece projeto elétrico para uma cancela real.

Configure API_BASE com o túnel HTTPS, DEVICE_ID e DEVICE_SECRET exclusivo da simulação. O firmware conecta à Wokwi-GUEST, senha vazia, canal 6. Localhost do computador não é acessível automaticamente pelo simulador público. Use túnel HTTPS ou gateway compatível com sua instalação, conforme a documentação Wokwi.

O TLS valida o certificado. Em ROOT_CA, cole o certificado público PEM da autoridade raiz que valida o domínio do túnel. Consulte a cadeia de certificação no navegador e a página oficial da autoridade para obter a raiz. Não copie chave privada e não use setInsecure(). O relógio é sincronizado por NTP. Se a cadeia, horário ou certificado estiverem incorretos, a chamada falha e a cancela fica fechada.

Não publique o projeto Wokwi com seu segredo de dispositivo. Para uma demonstração pública, use um ambiente descartável, segredo exclusivo e troque-o após a apresentação. O arquivo config.h está no .gitignore e fica fora do ZIP. Registre o link do circuito sem segredos no relatório da equipe.

No Monitor Serial em 115200 baud, digite o código SP-... da sua reserva e Enter. Isso representa a leitura de um código no protótipo, sem adicionar um leitor não exigido pelo roteiro. Primeiro teste sem pagamento: entrada deve ser negada. Após pagamento aprovado, aproxime o sensor de entrada para menos de 15 cm ou pressione o botão correspondente.

## 17. Estados e ensaio do controlador

INICIALIZANDO: configura pinos, mantém servo fechado e conecta ao Wi-Fi. PRONTO: aguarda evento. VALIDANDO: solicita autorização com timeout. ACESSO_LIBERADO: abre servo a 180 graus e acende verde; fecha a 60 graus após dois segundos, preservando os valores do sketch da equipe. ACESSO_NEGADO: mantém fechado e indica recusa no LCD. FALHA_REDE: mantém fechado e tenta reconectar. MANUTENCAO: ignora pedidos de acesso.

Pressionar os dois botões juntos alterna manutenção. Solte ambos antes de repetir. Para trocar de reserva, digite outro código no Serial quando não houver requisição pendente ou abertura em andamento. O estado é mostrado no LCD e as transições aparecem no Serial sem segredos.

As chamadas possuem limites de conexão e leitura; uma falha pode ter até três tentativas espaçadas, com o mesmo event_id. O dispositivo não inventa uma aprovação se o backend não responder. A telemetria é enviada a cada 15 segundos. Sensores e botões geram eventos por transição, evitando repetir enquanto permanecem acionados.

Ensaio: reserve para poucos minutos à frente; valide o pagamento; informe o código; acione entrada; confira CHECKED_IN e vaga ocupada no painel; acione saída; confira COMPLETED, ticket único, vaga livre e notificação. Desligue o backend e repita um pedido: o servo deve permanecer fechado. Repita um event_id pela API: não deve haver nova abertura/ticket.

O firmware foi compilado localmente com Arduino CLI para esp32:esp32:esp32, core ESP32 3.3.12, ArduinoJson 7.4.3, ESP32Servo 3.2.1 e LiquidCrystal I2C 1.1.2. Resultado: 1.072.391 bytes de programa (81%) e 50.692 bytes de variáveis globais (15%). A biblioteca do LCD emitiu um aviso de metadados de arquitetura AVR, mas a compilação e a ligação para ESP32 concluíram com sucesso. A execução remota e o link no Wokwi ainda devem ser registrados pela equipe após configurar o ambiente. O diagrama JSON também foi validado localmente, e os contratos de entrada/saída foram testados no backend.

## 18. Testes e evidências da atividade

O comando obrigatório é npm test, com o ambiente virtual ativo. A suíte usa unittest da biblioteca padrão e dublês de Mercado Pago/Telegram; não adiciona bibliotecas de produção. O JavaScript também passa por node --check. O relatório separado registra cada cenário T01–T15, o resultado observado e as etapas externas ainda pendentes.

Foram executados 17 casos automatizados cobrindo rotas públicas, datas inválidas, oito pedidos concorrentes pela última vaga, autenticação e proprietário, pagamento rejeitado e aprovado, webhook repetido, preço adulterado, expiração, cancelamento, entrada/saída, repetição de evento, indisponibilidade de provedores, vínculo Telegram, retries e estados incertos.

O fluxo da interface também foi executado no Chromium: login, mapa com marcadores, estimativa, reserva pendente com falha de checkout, cancelamento, cadastro pelo operador e tela móvel de 390 × 844. Não houve erro JavaScript ou rolagem horizontal. As capturas estão em docs/evidencias.

T09 no teste automatizado verifica a regra de falha fechada e indisponibilidade do provedor. A perda real de comunicação do ESP32, a posição do servo, as cores dos LEDs e o conteúdo físico do LCD exigem ensaio no simulador. T15 verifica o pacote de entrega e os arquivos ignorados; a chave encontrada no material recebido ainda precisa ser revogada pela equipe.

Para cada ensaio externo, registre data, responsável, entrada, resultado esperado, resultado observado e evidência sem segredos. Não marque como aprovado um teste que apenas foi descrito. Use docs/RELATORIO_TESTES.md como ponto inicial para completar a comprovação remota.

## 19. Solução de problemas

Página sem estilo: abra pela URL do Flask, confirme /static/css/style.css e não altere a pasta frontend. Mapa vazio: confira internet, CDN do Leaflet e tiles; a seleção de locais por lista permanece disponível se Leaflet não carregar. Nenhum local: execute o seed no projeto correto e veja se active=true.

Reserva retorna 400: revise fuso, chegada futura, saída posterior, duração e funcionamento. Retorna 409: a vaga foi ocupada/reservada ou a reserva não está no estado exigido; consulte disponibilidade novamente. Botão desabilitado: escolha local, informe datas e entre. Checkout retorna 503: configure token de teste, HTTPS e verifique o provedor. Checkout em revisão: investigue a preferência já criada antes de tentar novamente; não altere o preço.

Pagamento permanece pendente: conferir entrega do webhook, tópico payment, query data.id, segredo de assinatura, horário sincronizado e consulta /v1/payments. A página de sucesso não confirma pagamento. Pagamento live ou valor divergente é recusado. Reserva expirada não é reativada; a correção financeira é manual.

Entrada negada: confira conta paga, status CONFIRMED, janela de tolerância, local do dispositivo, manutenção e ocupação. Replay com actuate=false é esperado. FALHA_REDE no ESP32: conferir túnel, Wi-Fi, ROOT_CA, relógio, segredo e logs de classe de erro no servidor, sem imprimir tokens.

Telegram pendente: usuário iniciou /start pelo link? Bot e webhook configurados? Worker ativo no modo Firestore? Next_attempt já venceu? Após três falhas, FAILED exige intervenção. MANUAL_REVIEW exige checar a conversa antes de decidir um reenvio; a aplicação não reenfileira automaticamente um resultado incerto.

## 20. Entrega, manutenção e apresentação

Compartilhe SmartPark-Entrega.zip e docs/GUIA_EQUIPE.pdf. O ZIP é montado por scripts/build_delivery.py com uma lista explícita de fontes e rejeita chaves privadas. Não envie a pasta inteira recebida: nela ainda há arquivos originais contendo credencial e um ambiente virtual local. O .gitignore evita novos rastreamentos, mas não remove segredos de históricos anteriores nem revoga credenciais.

Para gerar novamente o documento, instale somente a ferramenta de documentação e execute os comandos abaixo. O conteúdo oficial editável é este Markdown; o script aplica a paleta do projeto, numera páginas e preserva exemplos de código no PDF.

```bash
pip install -r docs/requirements.txt
python scripts/build_guide.py
python scripts/build_delivery.py
```

Roteiro de apresentação de 5–10 minutos: explicar o problema e a arquitetura; mostrar seleção no mapa e estimativa; criar reserva e explicar o pagamento verificado; demonstrar entrada/saída no circuito; mostrar ticket e alerta; terminar com o teste de falha e as decisões de tarifa e idempotência. Cada integrante deve explicar a parte que executou e registrar uma reflexão individual, conforme o roteiro.

Antes de marcar o trabalho integrado como concluído na apresentação, registre o link Wokwi sem segredos, teste Firestore com a nova chave, valide pelo menos um pagamento aprovado e um rejeitado em sandbox, confirme webhook duplicado e mostre confirmação/ticket reais do bot de teste. Esses itens dependem das contas da equipe; esta entrega deixa os pontos de conexão e o procedimento definidos.

## 21. Referências oficiais

Roteiro original: SmartPark IoT Roteiro Atividade.pdf, material fornecido pela equipe. As referências abaixo foram consultadas para conferir contratos e configuração; o código continua limitado ao escopo educacional.

Firebase Admin SDK: https://firebase.google.com/docs/admin/setup

Firebase autenticação de tokens: https://firebase.google.com/docs/reference/admin/python/firebase_admin.auth

Firestore transações: https://firebase.google.com/docs/firestore/manage-data/transactions

Mercado Pago, documentação oficial de validação de webhook: https://github.com/mercadopago/sdk-nodejs/blob/master/docs/webhooks.md

Mercado Pago, notificações: https://www.mercadopago.com.br/developers/pt/docs/split-payments/additional-content/your-integrations/notifications/webhooks

Wokwi Wi-Fi: https://docs.wokwi.com/guides/esp32-wifi

Wokwi HC-SR04: https://docs.wokwi.com/parts/wokwi-hc-sr04

Telegram Bot API: https://core.telegram.org/bots/api

Leaflet: https://leafletjs.com/examples/quick-start/

OpenStreetMap, política de tiles: https://operations.osmfoundation.org/policies/tiles/
