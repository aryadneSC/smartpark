# Publicação corrigida no Vercel

## O que mudou

- Os arquivos de produção foram copiados para `public/static/css/style.css` e `public/static/scripts/map.js`. O Vercel publica arquivos de `public/` pela CDN.
- O HTML continua referenciando `/static/css/style.css` e `/static/scripts/map.js`.
- O Flask mantém a pasta `frontend/` para desenvolvimento local. Quando executado no Vercel (`VERCEL` definido), ele não registra a rota de estáticos do Flask; os arquivos vêm de `public/`.
- A função Python inclui `frontend/index.html` e o código do backend, mas não precisa empacotar a pasta inteira de desenvolvimento do frontend.

## Publicar

1. Na raiz do repositório, confirme que `public/static/css/style.css` e `public/static/scripts/map.js` existem.
2. Faça commit e push das alterações para o repositório conectado ao Vercel, ou importe esta pasta como projeto.
3. Confirme que o Root Directory do projeto Vercel aponta para a pasta que contém `vercel.json`.
4. Aguarde o novo deployment ficar pronto.
5. Abra estes endereços no navegador:
   - `https://SEU-DOMINIO/static/css/style.css` — deve retornar CSS, não uma página de erro.
   - `https://SEU-DOMINIO/static/scripts/map.js` — deve retornar JavaScript, não `text/plain` nem 404.
   - `https://SEU-DOMINIO/` — deve carregar a página.
6. Faça um hard refresh (Ctrl+Shift+R) e confira o Console e a aba Network do DevTools.

## Segredo do ESP32

O `firmware/config.h` preenchido não deve ser enviado ao Git nem ao ZIP. Copie `firmware/config.example.h` para `firmware/config.h` apenas no ambiente local/Wokwi. Gere um segredo novo e configure o mesmo valor em `DEVICE_SHARED_SECRET` nas Environment Variables do Vercel e em `DEVICE_SECRET` no arquivo local do ESP32. Faça um novo deployment depois de alterar as variáveis. O segredo antigo foi incluído no pacote original e deve ser considerado comprometido.

> A alteração do código não publica automaticamente o projeto. É necessário enviar os arquivos ao repositório conectado ou fazer um novo deployment no Vercel. Depois, valide os três endereços acima.
