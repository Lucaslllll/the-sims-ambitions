# Como compilar

## Requisitos

| Ferramenta | Versão usada |
|---|---|
| JDK | 17 |
| Android SDK | platform 33, build-tools 35 |
| Android NDK | 27.2.12479018 (r27c) |
| CMake (do SDK) | 3.22.1 |
| Python | 3.8+ (registros de ABI gerados pelo CMake e conversor de OBB) |
| Gradle | 8.10.2 (via `./gradlew`) |
| Android Gradle Plugin | 8.7.3 |

Dependências de terceiros ficam em `android/third_party/` (código-fonte, compiladas junto):
SDL2 2.30.9, OpenAL Soft 1.23.1, stb_image 2.30.

```bash
# instalar NDK e CMake pelo sdkmanager
sdkmanager "ndk;27.2.12479018" "cmake;3.22.1"

# baixar as dependências (se a pasta third_party estiver vazia)
cd android/third_party
curl -L -o sdl.tgz https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz
curl -L -o oal.tgz https://github.com/kcat/openal-soft/archive/refs/tags/1.23.1.tar.gz
curl -L -o stb_image.h https://raw.githubusercontent.com/nothings/stb/f75e8d1cad7d90d72ef7a4661f1b994ef78b4e31/stb_image.h
tar xzf sdl.tgz && tar xzf oal.tgz && rm *.tgz
```

As classes Java do SDL (`app/src/main/java/org/libsdl/app/`) são copiadas de
`SDL2-2.30.9/android-project`.

## Compilar

```bash
cd android
echo "sdk.dir=$HOME/Android/Sdk" > local.properties
./gradlew assembleDebug      # app/build/outputs/apk/debug/app-debug.apk
./gradlew assembleRelease    # app/build/outputs/apk/release/app-release.apk
```

As bibliotecas nativas são alinhadas em 16 KB (`ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES`), exigência
de aparelhos com Android 15+ que usam páginas de 16 KB.

O release 0.2.0 inclui `arm64-v8a` e `armeabi-v7a` no mesmo APK. Para compilar só uma ABI
durante o desenvolvimento, use `./gradlew assembleRelease -Pts3aAbi=armeabi-v7a` ou
`-Pts3aAbi=arm64-v8a`. Sem a propriedade, ambas são compiladas. O mínimo continua Android 8.0.

O release é assinado com a chave descrita em `android/keystore.properties`
(`storeFile`, `storePassword`, `keyAlias`, `keyPassword`). Esse arquivo e a pasta
`android/keystore/` **não** vão para o repositório; sem eles o release é assinado com a
chave de debug. Guarde a chave: atualizações do APK precisam da mesma assinatura.

## Testar no aparelho

Se o aparelho já estiver com o release assinado pela chave local, atualize usando
`adb install -r app/build/outputs/apk/release/app-release.apk` (a partir de `android/`).
O debug usa outra assinatura e exige desinstalação; antes disso, faça backup de
`/sdcard/Android/data/com.ts3ambitions.port/files/home`, pois desinstalar apaga saves e jogo importado.

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk

# sem passar pelo importador: copiar a pasta .app extraída do IPA
unzip The_Sims_3_Ambitions_1.1.84.ipa -d extracted
adb shell mkdir -p /sdcard/Android/data/com.ts3ambitions.port/files
adb push extracted/Payload/sims3dp_iphone.app /sdcard/Android/data/com.ts3ambitions.port/files/

tools/run.sh 25 tela.png        # reinicia o app, salva log em /tmp/ts3a_log.txt
tools/shot.sh tela.png          # screenshot reduzida
```

### Configuração opcional

`/sdcard/Android/data/com.ts3ambitions.port/files/config.txt` (uma chave por linha):

| Chave | Padrão | Efeito |
|---|---|---|
| `language` | idioma do Android | código de 2 letras enviado ao jogo |
| `short_side` / `long_side` | lado curto até 720 pontos, lado longo proporcional à tela | resolução lógica do "iPhone" emulado (menor = controles maiores e menor custo de renderização) |
| `gl_api` | `OpenGLES1` | `OpenGLES2` usa o caminho de shaders do jogo (experimental) |
| `device_model` | `iPhone7,2` | modelo reportado por `sysctl hw.machine` |
| `system_version` | `8.4` | versão do iOS reportada |

Em uma tela 2340×1080 (Galaxy A50), o padrão é 1560×720; no A10 de 1520×720,
continua 1520×720. Para experimentar controles ainda maiores, use `short_side=600`
e omita `long_side` para preservar a proporção. Para priorizar nitidez no A50,
`short_side=1080` restaura a resolução nativa e os controles menores. Reinicie o app
após alterar o arquivo. Uma configuração manual existente prevalece sobre o novo padrão.

## Converter o IPA local em OBB

A partir da raiz do projeto, com Python 3.8+ (somente biblioteca padrão):

```bash
python3 tools/ipa_to_obb.py The_Sims_3_Ambitions_1.1.84.ipa dist/TS3Ambitions-1.1.84.obb
python3 -m unittest discover -s tools/tests -v
```

A pasta de saída precisa existir. A ferramenta não sobrescreve um OBB existente e não
altera o IPA. Ela seleciona os arquivos de `Payload/sims3dp_iphone.app/`, valida as fatias armv7/arm64 disponíveis e
ausência de FairPlay, reempacota em ZIP/Deflate e relê a saída verificando CRC e SHA-256
de cada arquivo contra o conteúdo original. O arquivo final só é publicado após a verificação.

O OBB da cópia local testada tem 1.824 arquivos, 209,6 MiB compactados e 421,9 MiB extraídos.
Para testar o fluxo de primeira instalação, copie o OBB para Downloads e selecione-o no app:

```bash
adb push dist/TS3Ambitions-1.1.84.obb /sdcard/Download/
```

Quando o jogo já está importado, o launcher abre o jogo diretamente. Em testes de
reimportação, preserve `files/home` e mova temporariamente apenas o bundle
`files/sims3dp_iphone.app`, com o app parado; assim o importador aparece sem apagar saves.
Após verificar a nova importação, remova apenas a cópia temporária do bundle.

OBBs gerados são conteúdo do jogo e estão excluídos pelo `.gitignore`, assim como IPAs.
O OBB não é vinculado ao versionCode do APK: este port o importa pelo seletor, sem depender
do esquema tradicional de arquivos de expansão em `Android/obb`.

### Verificar a ABI armv7 no aparelho

O A10 suporta as duas ABIs, embora normalmente execute apps em 64 bits. Para verificar
o mesmo APK universal em cada processo, sem desinstalar:

```bash
adb install -r --abi armeabi-v7a dist/TS3Ambitions-0.2.0.apk
tools/run.sh 20
bash tools/test_armv7_abi.sh
adb install -r --abi arm64-v8a dist/TS3Ambitions-0.2.0.apk
```

Execute a partir da raiz do projeto. Faça backup de `files/home` antes de trocar builds.
O log de inicialização identifica `armv7, 32-bit` ou `arm64, 64-bit`. O teste nativo de ABI
verifica argumentos de 64 bits em posições diferentes, pilha alinhada a 4 bytes na entrada,
preservação de `r9` e alternância de destinos no cache de chamadas. Usa NDK r27c no caminho
padrão `~/Android/Sdk` ou em `ANDROID_NDK_HOME`; o aparelho precisa oferecer `armeabi-v7a`.

`tools/macho_objc.py` aceita também `analysis/sims3dp_armv7`; `tools/fn.sh` continua usando
a decompilação arm64. Endereços de crash armv7 começam na base original `0x4000` e podem
conter o bit Thumb. Serviços online/RestKit/CoreData adicionais dessa fatia ainda possuem
binds em stubs/blocos de dados: procure chamadas efetivas a `UNIMPLEMENTED`, além da contagem
de binds não resolvidos no carregamento.

## Regenerar os ícones do launcher

Os vetores autorais estão em `app/src/main/res/drawable/ic_launcher_{background,foreground}.xml`.
Para atualizar os PNGs de compatibilidade e `docs/img/icone.png`, a partir da raiz do projeto:

```bash
python3 tools/gen_icon.py  # requer Pillow 9.1+; não é necessário para compilar o APK
```

O ícone adaptativo usa diretamente os vetores. Após alterar o desenho, regenere os PNGs
para manter a mesma identidade em launchers que usam a versão legada.

## Ferramentas de engenharia reversa

```bash
python3 -m venv .venv && .venv/bin/pip install lief capstone
.venv/bin/python -I tools/macho_objc.py analysis/sims3dp_arm64 > analysis/objc.txt

# Ghidra 11.1.2 (funciona com JDK 17) — decompila tudo para um arquivo C
analyzeHeadless analysis/ghidra_proj sims3 -import analysis/sims3dp_arm64 \
    -scriptPath tools/ghidra -postScript ExportDecomp.py analysis/decomp_arm64.c
tools/fn.sh 10015c42c           # imprime a função decompilada nesse endereço

# wrappers OpenGL ES (após mudar a tabela de assinaturas)
python3 tools/gen_gl_wrappers.py android/app/src/main/cpp/hle
```

## Depuração

- Se o jogo travar, o handler de crash grava em logcat (tag `TS3A-crash`) e em
  `files/crash.txt` o sinal, os registradores e a pilha, com endereços do executável
  original (`game 0x1000xxxxx`) prontos para `tools/fn.sh`. `crash_test=1` no `config.txt`
  provoca um crash proposital para testar isso.

- Chamadas a funções iOS ainda não implementadas aparecem no logcat como
  `UNIMPLEMENTED call: _nome`; mensagens ObjC sem implementação como
  `unrecognized selector -[Classe seletor]`. Ambas devolvem 0/nil em vez de travar.
- `TS3A_STRICT=1` no ambiente aborta na primeira chamada não implementada.

- `tools/run.sh` grava o logcat continuamente em `/tmp/ts3a_log.txt`, incluindo
  `TS3A-crash`, para preservar também o início de sessões que excedem o buffer circular
  do aparelho. O script reinicia o app; para acompanhar alguém já jogando, use
  `adb logcat -s TS3A:* TS3A-crash:* TS3A-NSLog:* TS3A-printf:* DEBUG:* libc:* AndroidRuntime:* > /tmp/ts3a_sessao.txt`
  e encerre com Ctrl+C.
- Crashes no código do jogo aparecem no tombstone como `<anonymous:...>`; subtraia a base
  impressa em `mapped image at 0x...` para obter o offset e use `tools/fn.sh 1000xxxxx`.
