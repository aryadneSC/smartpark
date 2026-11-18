# Relatório de validação — SmartPark IoT

Data: 13/11/2026. Revisão 1.1: incorporado o sketch original enviado posteriormente pela equipe. Referência: plano T01–T15 do roteiro original.

A suíte executa regras reais do backend usando armazenamento transacional em memória
e dublês controlados de Mercado Pago e Telegram. Ela não representa transações
financeiras, mensagens externas ou evidência de conexão ao banco remoto.

Comando obrigatório executado com ambiente virtual ativo: `npm test`.
Resultado: **17 casos Python aprovados** e **sintaxe JavaScript válida**.

| Teste | Entrada/ação | Esperado | Observado e limite |
|---|---|---|---|
| T01 | Abrir `/`, recursos estáticos e lista pública | Página e pelo menos dois locais com dados da API | Aprovado no teste HTTP e no Chromium; três locais iniciais e marcadores reais. Prints em `evidencias/`. |
| T02 | Saída igual/anterior à chegada, tipo incorreto, passado e duração excessiva | HTTP 400 | Aprovado. |
| T03 | Oito pedidos simultâneos pela única vaga ativa | Uma criação, sete conflitos | Aprovado: um 201 e sete 409. Concorrência remota do Firestore ainda exige ensaio com nova credencial. |
| T04 | Consulta do pagamento retorna rejected | Sem confirmação nem entrada | Aprovado com dublê: PENDING_PAYMENT; allowed=false. |
| T05 | HMAC válido e consulta retorna approved, BRL e valor correto | CONFIRMED uma vez | Aprovado com dublê. Pagamento live rejeitado. Provedor real de teste pendente de configuração. |
| T06 | Reenviar webhook aprovado e depois status pending | Sem duplicação ou regressão | Aprovado: uma confirmação, um pagamento verificado; alerta para reconciliação do evento posterior. |
| T07 | Reserva paga na janela, dispositivo/local corretos | Entrada autorizada e CHECKED_IN | Backend aprovado. Movimento do servo no Wokwi exige ensaio remoto. |
| T08 | Solicitar entrada pendente, duplicar entrada ativa, usar outro local ou chegar cedo | Acesso negado | Aprovado no backend; LEDs/LCD exigem simulador. |
| T09 | Provedor indisponível e persistência com falha | 503, nenhuma aprovação alternativa | Backend aprovado. Falha de rede física do ESP32 segue pendente de ensaio no Wokwi. |
| T10 | Entrada seguida de EXIT | Horários reais, COMPLETED, ticket e vaga livre | Aprovado; valor fixo mantém os R$ 24 do período reservado. |
| T11 | Repetir EXIT com mesmo ID e reutilizar ID para outro tipo | Um ticket, replay sem atuar; conflito no conteúdo diferente | Aprovado: duplicate=true e actuate=false; tipo diferente retorna 409. |
| T12 | Bot falha com resultado conhecido; recuperar e tentar de novo | Ticket salvo e reenvio controlado | Aprovado: PENDING_RETRY, depois SENT; nenhum envio extra em nova rodada. Timeout incerto fica MANUAL_REVIEW. |
| T13 | Enviar preço 0,01, outro user_id e status CONFIRMED | Ignorar campos não confiáveis | Aprovado: servidor calculou R$ 24, proprietário autenticado e PENDING_PAYMENT; valor do pagamento divergente retorna 409. |
| T14 | Bob consulta/abre checkout da reserva de Alice; motorista acessa operador | Sem exposição | Aprovado: 404 para reserva alheia, 403 para operador sem permissão, 401 sem autenticação. |
| T15 | Conferir arquivos do Git, ignore e pacote de entrega | Nenhuma chave privada nas fontes entregues | Nenhum arquivo deste diretório estava rastreado; `git check-ignore` confirmou os arquivos sensíveis. ZIP usa lista permitida, rejeita chaves privadas e valida integridade. A chave original preservada localmente ainda precisa ser revogada no Google Cloud. |

Outros cenários aprovados: expiração com pagamento tardio; cancelamento; vínculo
Telegram por código único e webhook autenticado; limite de telemetria; validação de
cadastro; reutilização de checkout; retorno success sem confirmação; falha do banco
sem troca silenciosa para demonstração.

## Verificação no navegador

Chromium headless via Playwright, instalado somente em `/tmp`, sem dependência nova
no projeto. Servidor local em modo demo com tokens descartáveis de ensaio.

Fluxo executado: carregamento de três locais → login motorista → selecionar Centro →
período de duas horas → estimativa R$ 24 → criar reserva → checkout não configurado
retorna mensagem de indisponibilidade → histórico mantém PENDING_PAYMENT → cancelar
→ login operador → painel de ocupação → cadastrar local fictício com duas vagas →
processar notificações → verificar tela de 390 × 844. Nenhum erro JavaScript; sem
rolagem horizontal na tela móvel. Quatro marcadores ao final, incluindo o cadastro.

Evidências: `evidencias/dashboard-desktop.png` e `evidencias/dashboard-mobile.png`.
O cadastro e as reservas desse ensaio eram dados em memória, não foram enviados ao
Firestore. Tokens usados no ensaio são valores descartáveis exclusivos de teste.

Para repetir, instale Playwright em um ambiente de desenvolvimento separado e execute
`scripts/browser_smoke.cjs` com um servidor demo novo na porta 5000 e as variáveis
DEMO_USER_TOKEN=ui-driver-test e DEMO_OPERATOR_TOKEN=ui-operator-test. Não use esses
valores em uma instância compartilhada. O script pressupõe os três cadastros iniciais.

## PDF e fontes

Guia gerado por ReportLab, com capa, sumário navegável, 21 capítulos e referências.
Capa, sumário e página de configuração do Firebase conferidos com renderização PNG;
texto extraído com pdftotext e metadados com pdfinfo. Arquivo final em A4.

Fontes Python verificadas com py_compile. Diagrama Wokwi JSON validado localmente;
as ligações originais permanecem e o segundo sensor usa TRIG 25/ECHO 26.

Firmware compilado localmente para `esp32:esp32:esp32`, core ESP32 3.3.12,
ArduinoJson 7.4.3, ESP32Servo 3.2.1 e LiquidCrystal I2C 1.1.2.
Resultado: 1.072.391 bytes de programa (81%) e 50.692 bytes de variáveis globais
(15%). Compilação e ligação concluídas com código de saída zero. Houve aviso de
metadados AVR da biblioteca LCD, sem erro de compilação para ESP32. Configuração
usada: `config.example.h`, sem credenciais ou certificado funcional.

A instalação temporária limitou as ferramentas às dependências do ESP32 clássico;
não alterou o core nem as bibliotecas compiladas. O ensaio de movimento, LCD e
rede no simulador remoto continua necessário. Resumo em
`evidencias/firmware-compilacao.txt`.

## Validação externa a registrar pela equipe

| Item | Estado | Procedimento para completar |
|---|---|---|
| Firebase/Firestore e Authentication | Não conectado nesta entrega; credencial recebida estava exposta | Revogar antiga, configurar nova fora do projeto, executar diagnóstico e seed, autenticar duas contas e repetir concorrência. |
| Mercado Pago sandbox | Contrato testado com dublês; conta externa não configurada | Criar aplicação de teste, configurar assinatura e HTTPS, pagar com conta de teste e conferir webhook aprovado/rejeitado/duplicado/tardio. |
| Telegram externo | Fila, vínculo e retries testados com dublê | Configurar bot e webhook, enviar /start, executar worker e registrar confirmação/alerta/ticket sem expor tokens. |
| Wokwi remoto | Firmware e diagrama preparados; link remoto ainda não publicado | Importar arquivos, configurar certificado e segredo do dispositivo e realizar os ensaios de entrada/saída/manutenção/falha. |

Não foram fabricados prints, mensagens ou aprovações de serviços externos.
O guia contém os passos e os pontos de configuração para a equipe completar essas
validações com suas contas.

## Registro de ensaio integrado (preencher após executar)

Data e responsável: ______________________________

Link Wokwi sem segredos: _________________________

Ambiente Firebase de teste: ______________________

Reserva/caso (sem dados pessoais): ________________

Entrada aplicada: ________________________________

Resultado esperado: ______________________________

Resultado observado: _____________________________

Evidência sem credenciais: ________________________

Correção necessária e repetição do teste: __________

## Revisão do sketch da equipe

Preservados os nomes PIN_*, Cancela_Status_t, conectar_wifi, init_pins,
init_cancela e mover_cancela. Corrigidos a chamada inexistente connect_WiFi,
os trechos com Seguranca_t sem definição e a abertura automática sem autorização.
Mantidos 180° para abrir, 60° para fechar e dois segundos de abertura autorizada.
O token do bot enviado com o sketch foi excluído das fontes; a equipe precisa
revogá-lo no BotFather. Firmware recompilado com sucesso após essas alterações.
