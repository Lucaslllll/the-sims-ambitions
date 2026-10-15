# Arquitetura do port

## Visão geral

Não recompilamos o jogo (não temos o código-fonte). Em vez disso, o APK Android contém um
**loader nativo** que carrega o executável iOS original (fatia armv7 ou arm64) na memória e o executa
diretamente na CPU do celular. Tudo que o jogo esperava do iOS é reimplementado por nós
("HLE" — *high-level emulation*), em C/C++, por cima de APIs Android.

```
┌──────────────────────────────────────────────────────────────┐
│ sims3dp_iphone (ARMv7/ARM64 original da EA, sem modificação) │
└───────────────┬──────────────────────────────────────────────┘
│ chamadas para os símbolos importados (411 na arm64)
┌───────────────▼──────────────────────────────────────────────┐
│ Camada HLE (nosso código, libts3a.so)                         │
│  libSystem · libstdc++ · runtime ObjC · CoreFoundation        │
│  Foundation · UIKit · CoreGraphics · OpenGLES/EAGL · OpenAL   │
│  AudioToolbox · MediaPlayer · QuartzCore                      │
└───────────────┬──────────────────────────────────────────────┘
                │
┌───────────────▼──────────────────────────────────────────────┐
│ Android: SDL2 (janela, EGL, toque, teclado, ciclo de vida),   │
│ GLES 3 (Mali/Adreno), OpenAL Soft (AAudio/OpenSL),            │
│ AMediaCodec (AAC), JNI → android.graphics (texto)             │
└──────────────────────────────────────────────────────────────┘
```

O usuário fornece o próprio IPA ou um OBB convertido da sua cópia; o app importa o
executável e os assets na primeira execução. Não redistribuímos nada da EA.

## Componentes (`android/app/src/main/cpp/`)

| Pasta | Conteúdo |
|---|---|
| `loader/` | Parser Mach-O, mapeamento de segmentos, rebase/bind, `mod_init_func`, chamada de `main` |
| `loader/symbols` | Registro `nome → endereço` dos símbolos HLE; stubs automáticos que registram no log chamadas não implementadas |
| `objc/` | Runtime Objective-C compatível com o layout ObjC2 da Apple (`objc_msgSend` em assembly) |
| `hle/` | Uma unidade por framework iOS |
| `platform/` | Integração SDL2: laço principal, eventos, apresentação de quadros |

## Carregamento do Mach-O

1. Ler o executável, escolher `CPU_TYPE_ARM64` no processo de 64 bits ou ARM / subtipo
   armv7 (9) no processo de 32 bits. O Android escolhe a biblioteca nativa adequada do APK.
2. Reservar com `mmap` um bloco do tamanho de `__TEXT..__LINKEDIT` (≈2 MB);
   `slide = base − vmaddr(__TEXT)` (`0x100000000` na arm64, `0x4000` na armv7).
3. Copiar os segmentos; aplicar **rebase** (somar `slide`) e **bind** (gravar endereços dos nossos
   símbolos). Binds *lazy* são resolvidos de imediato — `dyld_stub_binder` nunca é chamado.
4. `mprotect`: `__TEXT` → R‑X, `__DATA` → RW; limpar cache de instruções.
5. Inicializar metadados ObjC da imagem (classes, categorias, selrefs, classrefs, CFStrings constantes).
6. Executar os construtores estáticos (`__mod_init_func`) e chamar `main(argc, argv)`.
   `main` chama `UIApplicationMain`, que é nosso.

Páginas de 16 KB do iOS são compatíveis com Android de páginas de 4 KB e de 16 KB.

O parser usa cabeçalhos, segmentos, seções e slots de fixup da largura nativa (4/8 bytes).
Entradas e IMPs armv7 mantêm o bit Thumb. O relatório de crash usa registradores `r0–r15`
e endereços originais de 32 bits nesse caminho, ou `x0–x30` e endereços arm64 no outro.

## Diferenças de ABI Apple arm64 × Linux/Android arm64

O código do jogo segue o ABI da Apple; nossa camada é compilada com o ABI AAPCS64 do
Android. Quase tudo coincide, exceto:

| Diferença | Tratamento |
|---|---|
| **Varargs**: Apple passa *todos* os argumentos variádicos na pilha (slots de 8 bytes) | Trampolins em assembly (`mov xN, sp; b impl`) para `printf`, `snprintf`, `NSLog`, `CFStringCreateWithFormat`, `stringWithFormat:` etc.; `va_list` da Apple é um `char*` simples |
| `x18` reservado na Apple | Código da Apple não usa x18 → sem conflito |
| `char` é *signed* na Apple | Cuidado em funções HLE que recebem `char` |
| `FILE` do Darwin é acessado inline (`getc` → `__srget`) | `fopen` devolve um `FILE` falso com layout Darwin e `_r = 0`, forçando toda leitura a passar por `__srget` |
| `struct stat` diferente | `fstat` converte para o layout Darwin |
| Constantes `PTHREAD_CREATE_*` diferentes (Darwin: 1/2, bionic: 0/1) | Tradução em `pthread_attr_setdetachstate` |
| Argumentos <8 bytes na pilha são compactados na Apple | Só afeta funções com >8 argumentos inteiros; nenhuma importada tem esse caso com tipos pequenos |

## Runtime Objective-C

### Caminho armv7 (Android de 32 bits)

- Apple armv7 alinha argumentos `double`/`int64_t` a 4 bytes; Android AAPCS os alinha
  a 8 bytes. A pilha de entrada do jogo também pode estar alinhada somente a 4 bytes.
- `hle/abi.*` gera adaptadores ARM para organizar os registradores/argumentos de pilha
  e alinhar a pilha antes de chamar Android/C++. O caminho inverso é usado para chamar
  código do jogo: construtores, `main`, callbacks de áudio/imagem/threads e mensagens ObjC.
- `r9` é volátil na ABI Darwin, mas deve ser preservado para os chamadores Android.
  Os adaptadores o salvam; a falta disso inicialmente corrompeu o contador de construtores.
- As assinaturas dos métodos HLE são deduzidas de seus tipos C++, com registros gerados
  por `tools/gen_method_abi.py` durante a configuração CMake. Adaptadores são cacheados;
  a arm64 mantém chamadas diretas.
- `objc_msgSend_stret` trata retornos de estruturas com ponteiro de resultado em `r0`,
  receptor em `r1` e seletor em `r2`. Os helpers `objc::send` selecionam esse caminho.
- `CGFloat` é `float`, `BOOL` é `signed char`; `NSInteger`, `NSUInteger` e ponteiros têm
  4 bytes. `class_ro_t` não possui o padding da versão de 64 bits. Referências de classes,
  seletores e categorias são percorridas com `sizeof(void*)`.
- Foundation/CoreGraphics usam os layouts dessa ABI: CFString constante de 16 bytes,
  `FILE` Darwin de 88 bytes, `stat` de 108 bytes, `AudioQueueBuffer` de 28 bytes e tabela
  CRC com elementos de 4 bytes. UIKit usa `UIView=60`, `UIViewController=162` e
  `UIImagePickerController=204` bytes, conforme os metadados armv7.
- Varargs armv7 são reunidos dos registradores e da pilha em palavras de 4 bytes;
  valores de 64 bits consomem duas palavras. A arm64 mantém os slots de 8 bytes na pilha.

Os testes de ABI usam funções sintéticas em assembly (sem código do jogo), executadas
no Android de 32 bits. O caminho foi verificado no A10; outros aparelhos e as rotinas
adicionais de serviços online presentes na armv7 ainda precisam de validação.

- Estruturas idênticas às da Apple (`objc_class`, `class_ro_t`, `method_t`, `ivar_t`), porque o
  binário contém as classes do jogo estaticamente nesse formato.
- `SEL` = ponteiro para string única (internada). `selrefs` e listas de métodos são corrigidos
  na carga.
- O campo `cache` de cada classe aponta para nossa estrutura C++ (`ClassRT`) com a tabela de
  métodos e o cache de busca.
- *Ivars não-frágeis*: se a superclasse HLE tiver tamanho diferente do esperado pelo jogo, os
  offsets são deslizados (como o runtime da Apple faz). Mesmo assim, nossas classes de framework
  declaram o mesmo `instanceSize` do iOS 8 (ex.: `UIView` = 104 bytes na arm64, 60 na armv7)
  para não haver surpresa.
- Contagem de referências (MRC — o jogo não usa ARC) num cabeçalho oculto de 16 bytes antes de
  cada objeto alocado por nós; objetos estáticos (CFString constantes, blocks globais) são imortais.
- Mensagem para seletor desconhecido: registra no log e devolve `nil`/0 (modo leniente para
  descobrir rapidamente o que falta implementar).

## Threads

O jogo usa três tipos de thread, e o port respeita isso:

| Thread | Quem cria | O que faz |
|---|---|---|
| "main" (iOS) | nosso `game_thread` (pilha de 16 MB) | `main()`, `UIApplicationMain`, run loop: `NSTimer`, `performSelectorOnMainThread`, layout, eventos SDL |
| render | o jogo (`pthread_create`) | todo o OpenGL ES e a apresentação dos quadros |
| áudio | uma por `AudioQueue` | decodifica AAC e chama o callback de buffer do jogo |

O `EAGLContext` do iOS é corrente *por thread*; no EGL um contexto só pode estar ativo
numa thread por vez. `+[EAGLContext setCurrentContext:]` transfere a posse do contexto EGL
para a thread que pede, pedindo à thread principal que o solte quando necessário
(`hle/gles.cpp`).

## Gráficos

- Contexto **OpenGL ES 1.1** (o caminho que a EA lançou: o `Info.plist` não define
  `EAGLRenderingAPI`). Usa `GL_OES_matrix_palette` para a animação dos Sims.
- `renderbufferStorage:fromDrawable:` aloca o renderbuffer e, na hora de anexá-lo ao FBO do
  jogo (`glFramebufferRenderbuffer`), troca por uma **textura** do mesmo tamanho.
- `presentRenderbuffer:` cria uma fence EGL, troca para um **segundo contexto compartilhado**
  (o de apresentação), espera a fence no GPU e desenha a textura **girada 90°** e com
  *letterbox* na janela paisagem; depois `eglSwapBuffers`. O contexto separado evita ter de
  salvar/restaurar o estado do pipeline fixo do jogo.
- O jogo pensa estar num iPhone "retrato" de `largura × altura` pontos (por padrão a
  resolução do aparelho limitada a 720 pontos no lado curto, mantendo a proporção da tela)
  e desenha a cena paisagem já rotacionada, como no iOS. Isso mantém os controles com tamanho
  relativo semelhante em celulares 720p e Full HD+. A resolução lógica é também a resolução
  de renderização (`screen_scale = 1`); o quadro é ampliado para a janela Android, com alguma
  perda de nitidez em telas de alta resolução. `short_side`/`long_side` substituem esse padrão.
- Texturas PVRTC são decodificadas para RGBA8 em software (`hle/pvrtc.cpp`).
- O caminho ES 2.0 do jogo existe mas tem shaders inválidos (tipos `int4`/`float4` de HLSL);
  `glShaderSource` os corrige, mas esse caminho não é o padrão.

## Toque e orientação

Coordenada da janela Android (paisagem) → coordenada paisagem do jogo (removendo o
letterbox) → ponto "retrato" do iOS: `px = largura − Y`, `py = X`. O próprio jogo desfaz a
rotação em `-[EAGLView touchesBegan:withEvent:]`.

## Texto e UI 2D

- O jogo desenha textos e imagens de UI com CoreGraphics em bitmaps que depois viram texturas.
  `hle/cg.cpp` é um renderizador 2D em software (transformações afins, imagens com filtragem
  bilinear, caminhos com antialiasing, sombras, modos de mistura).
- O texto é rasterizado pelo Android (`TextRenderer.java`, via JNI) com as fontes do sistema,
  o que cobre todos os idiomas do jogo, inclusive chinês, japonês e coreano.
- `UITextField`: no iOS é uma view nativa sobre o jogo. No port, quando o campo vira
  *first responder* abrimos um diálogo nativo do Android; ao confirmar, disparamos os eventos
  `EditingChanged` e `EditingDidEndOnExit`, que é como o jogo recebe o texto.

## Áudio

- Efeitos: OpenAL → OpenAL Soft (mesma API e ABI).
- Música: `AudioFile` lê WAV e CAF (com tabela de pacotes); cada `AudioQueue` tem uma thread
  que consome os buffers no ritmo da reprodução, decodifica AAC com `AMediaCodec` e envia PCM
  para um mixer próprio sobre o áudio do SDL (AAudio), com conversão de taxa por voz.
- `AudioQueueGetCurrentTime` é calculado pelos quadros efetivamente tocados.
- Em segundo plano, o mixer e o dispositivo OpenAL são pausados.

## Distribuição

`LauncherActivity` verifica se os arquivos do jogo existem; se não, pede o `.ipa` ou `.obb`
pelo seletor de documentos do Android. Ambos usam ZIP com a estrutura
`Payload/sims3dp_iphone.app/`. A importação extrai o bundle para uma pasta `.partial` no
armazenamento do app, valida o Mach-O (fatia apropriada ao processo presente, limites dos comandos válidos e
`cryptid == 0`) e só então publica a pasta. Os saves em `home/` ficam separados do bundle.
O pacote pode ser escolhido em Downloads; o app não depende da pasta `Android/obb` nem do
nome `main.<versionCode>.<package>.obb` usado por arquivos de expansão tradicionais.

`tools/ipa_to_obb.py` reempacota somente os arquivos do bundle da cópia local do IPA,
sem alterar o executável ou os assets. Valida as fatias armv7/arm64 disponíveis e FairPlay,
e verifica CRC e SHA-256 de
cada arquivo na saída. O resultado continua compactado e é extraído pelo app na primeira
execução; não há leitura direta dos assets de dentro do OBB. Nada da EA é empacotado no APK.
