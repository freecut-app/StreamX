# Integração StreamX → FreeCut (contrato v1)

O botão **"Editar no FreeCut"** do StreamX envia gravações e clipes para o editor [FreeCut](https://github.com/freecut-app/FreeCut).

## Situação atual (FreeCut 1.0.0)

O FreeCut 1.0.0 **ainda não lê** argumentos de linha de comando nem o arquivo de entrega. O StreamX não altera o FreeCut, então hoje o botão:

1. Localiza o FreeCut na seguinte ordem: caminho configurado em `%APPDATA%\StreamX\StreamX\integration.ini` (`[freecut] path=`), depois a instalação por usuário `%LOCALAPPDATA%\Programs\FreeCut\FreeCut.exe`, depois o registro de desinstalação (`DisplayName` começando com "FreeCut").
2. Abre o FreeCut com os argumentos do contrato abaixo, que a versão atual ignora.
3. Seleciona o arquivo no Explorer e copia o caminho para a área de transferência, para o usuário arrastar o arquivo para a área de mídia do FreeCut.
4. Grava o arquivo de entrega.

Quando o FreeCut implementar o contrato, a importação passa a ser automática, sem nenhuma mudança no StreamX.

## Contrato

### 1. Argumentos de inicialização

```
FreeCut.exe --import "<caminho absoluto>" [--import "<outro arquivo>" ...]
```

- Os caminhos estão no formato do Windows (`C:\...`), cada um precedido de `--import`.
- O FreeCut deve importar cada arquivo para a biblioteca de mídia (como `importPaths`) e, se o projeto estiver vazio, inseri-lo na linha do tempo principal.
- Se o FreeCut **já estiver aberto**, ele deve usar `app.requestSingleInstanceLock()` e o evento `second-instance` para receber os argumentos na janela existente.

### 2. Arquivo de entrega (alternativa/compatibilidade)

`%APPDATA%\StreamX\StreamX\handoff\freecut-import.json`

```json
{
  "version": 1,
  "source": "StreamX",
  "createdAt": "2026-10-15T16:30:00",
  "files": ["C:\\Users\\...\\Vídeos\\StreamX\\Clipes\\2026-10\\Fortnite - Clip 2026-10-15 16-29-58.mp4"]
}
```

O FreeCut pode ler este arquivo ao iniciar, ou ao receber `--import-handoff`. Depois de importar, deve apagá-lo para não importar duas vezes.

### 3. Implementação sugerida no FreeCut (main.js)

```js
const gotLock = app.requestSingleInstanceLock();
if (!gotLock) app.quit();
const importsFrom = argv => argv.flatMap((a, i) => a === '--import' && argv[i + 1] ? [argv[i + 1]] : []);
app.on('second-instance', (e, argv) => { win.webContents.send('app:import', importsFrom(argv)); win.focus(); });
// ao criar a janela: enviar importsFrom(process.argv) depois do 'did-finish-load'
```

No renderizador: `fc.on('app:import', paths => importPaths(paths))`.
