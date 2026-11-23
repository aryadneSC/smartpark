# Pendências da entrega SmartPark

Este ZIP corrige a disponibilização dos arquivos estáticos no Vercel e remove o segredo do firmware. Ele não substitui as evidências e documentos que não estavam no pacote recebido.

Antes da entrega final, a equipe ainda precisa:

- Anexar `GUIA_EQUIPE.md` e/ou `GUIA_EQUIPE.pdf` com a documentação técnica real.
- Preencher `RELATORIO_TESTES.md` com os resultados efetivamente observados para T01–T15 (entrada, resultado esperado, resultado observado e correção). Não registrar testes não executados como aprovados.
- Incluir o link público do projeto Wokwi e validar a simulação integrada.
- Anexar apresentação técnica de 5 a 10 minutos e reflexão individual.
- Comprovar checkout de teste, webhook, notificações, falhas e reintentos sem expor credenciais.

## Segurança obrigatória

O arquivo original `firmware/config.h` continha um segredo de dispositivo. Gere outro segredo e atualize `DEVICE_SHARED_SECRET` no ambiente do Vercel. Copie `firmware/config.example.h` para `firmware/config.h` apenas no ambiente local/Wokwi e preencha `DEVICE_SECRET` com o mesmo valor. Nunca inclua o `config.h` preenchido no ZIP ou no Git.
