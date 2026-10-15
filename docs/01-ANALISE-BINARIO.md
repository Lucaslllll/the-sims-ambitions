# Análise do binário — The Sims 3 Ambitions (iOS 1.1.84)

Documento vivo com tudo que descobrimos sobre o IPA original. Gerado no Sprint 0.

## Pacote

| Item | Valor |
|---|---|
| Arquivo | `The_Sims_3_Ambitions_1.1.84.ipa` (211 MB, versão 1.1.84) |
| Bundle ID | `com.ea.sims3ambition.bv` |
| Executável | `Payload/sims3dp_iphone.app/sims3dp_iphone` |
| SDK | iOS 8.3 (Xcode 6.3), `MinimumOSVersion` 6.1 |
| Orientação | Paisagem (`UIInterfaceOrientationLandscapeRight`) |
| Dispositivos | iPhone + iPad (`UIDeviceFamily = [1, 2]`) |
| NIB principal | `MainWindow` (só cria `UIWindow` + `RuntimeAppDelegate`) |

## Executável (Mach-O universal)

| Fatia | Offset | Tamanho | `cryptid` |
|---|---|---|---|
| **armv7** | 0x4000 | 2,0 MB | 0 — usado no caminho Android de 32 bits a partir do release 0.2.0 |
| armv7s | 0x208000 | 2,0 MB | 0 |
| **arm64** | 0x40c000 | 1,9 MB | 0 — usado no caminho Android de 64 bits |

Layout da fatia arm64 (base `0x100000000`, PIE):

```
__PAGEZERO 0x000000000 .. 0x100000000
__TEXT     0x100000000  size 0x1c4000  r-x   (__text = 1,6 MB de código)
__DATA     0x1001c4000  size 0x038000  rw-
__LINKEDIT 0x1001fc000  size 0x010000
```

- Fixups clássicos via `LC_DYLD_INFO_ONLY` (rebase/bind/weak/lazy opcodes), **sem** chained fixups.
- 598 binds normais, 323 lazy, 4 weak (`operator new/delete`).
- `LC_MAIN` → `main` em `0x10015f7ec`.
- 12 construtores estáticos C++ em `__mod_init_func`.

### Fatia armv7 — adicionada ao port em 12/10/2026

- Mach-O de 32 bits (`0xfeedface`), CPU ARM / subtipo armv7 (9), código Thumb.
- Base original `0x4000`; `__TEXT` em `0x4000`, `__DATA` em `0x1c8000` e
  `__LINKEDIT` em `0x20c000`. `LC_MAIN` tem entryoff `0xfdf15` (bit Thumb incluído),
  endereço original de `main` = `0x101f15`. Há 13 construtores estáticos.
- A fatia armv7 contém código adicional de serviços EA, RestKit/CoreData e classes UIKit
  que não está presente na arm64. O inventário de 411 símbolos abaixo é específico da arm64.
- Metadados de 32 bits em `analysis/objc_armv7.json`; `tools/macho_objc.py` agora interpreta
  ponteiros de 4 ou 8 bytes. A decompilação `decomp_arm64.c` e `tools/fn.sh` continuam
  específicos da arm64.
- A ABI armv7 Apple difere da Android no alinhamento de argumentos de 64 bits e na
  preservação de `r9`; ver arquitetura. O caminho de jogo foi executado nativamente no A10.

## Motor do jogo

O jogo usa o motor **"midp" da EA Mobile**: uma reimplementação em C++ da plataforma
J2ME (MIDP + JSR-184/M3G). Os nomes de tipos nos metadados ObjC entregam isso:
`midp::Display`, `midp::Displayable`, `midp::Graphics`, `midp::Image`, `midp::Player`,
`midp::PlayerListener`, `midp::TextBox`, `Timer`/`TimerTask`, `javax.microedition.lcdui.Font`.
Os assets 3D são arquivos `.m3g` (formato padrão JSR-184).

A camada específica de iOS é **fina** — exatamente o que precisamos reimplementar:

| Classe ObjC do jogo | Base | Função |
|---|---|---|
| `RuntimeAppDelegate` | UIResponder | delegate do app, ciclo de vida |
| `EAGLView` | UIView | superfície OpenGL ES + toques |
| `RVController` | UIViewController | rotação |
| `CocoaTimer` | NSObject | `midp::Timer` sobre `NSTimer` (laço principal) |
| `TextBoxDelegate` | NSObject | entrada de texto (`UITextField`) |
| `AudioQueueStarter/Action/Closer`, `PlayerObject`, `QueueObject`, `AudioQueueStructObject` | NSObject | música via AudioQueue |
| `PlayerIPhone2` | NSObject | vídeo (`MPMoviePlayerController`) |
| `iPhoneAccelerometer` | NSObject | acelerômetro |
| `WebViewImpl` | UIViewController | links/ajuda em `UIWebView` |
| `PhotoPickerImpl` | UIImagePickerController | escolher foto |
| `MediaPickerImpl` | UIViewController | música do iPod |
| `EASWManagerImpl` | NSObject | (EA SW manager) |
| categoria `UIDevice (Hardware)` | — | detecta modelo via `sysctl hw.machine` |

Lista completa (ivars, métodos, seletores): `analysis/objc.txt`, gerada por
`tools/macho_objc.py`.

## Superfície de importação (arm64) — 411 símbolos

| Biblioteca | Nº | Observações |
|---|---|---|
| OpenGLES | 118 | ES 1.1 **e** ES 2.0 (o jogo tem os dois caminhos: `OpenGLES1`/`OpenGLES2`), `EAGLContext` |
| CoreFoundation | 47 | CFString, CFBundle, CFURL, CFLocale |
| CoreGraphics | 39 | bitmap context, PNG, caminhos, texto (desenho 2D de UI) |
| AudioToolbox | 30 | `AudioFile*` + `AudioQueue*` (música), `AudioSession*` |
| UIKit | 29 | janela, view, toque, fonte, `UITextField`, `UIWebView` |
| libSystem | 52 | libc básica, pthread, `sysctl`, `__srget` (getc inline do Darwin!) |
| OpenAL | 22 | efeitos sonoros |
| libstdc++ | 21 | `_Rb_tree_*`, guards, new/delete (sem `__cxa_throw`: o jogo não lança exceções) |
| Foundation | 15 | NSString, NSBundle, NSFileManager, NSTimer… |
| libobjc | 7 | `objc_msgSend`, `objc_msgSendSuper2`, `objc_setProperty_nonatomic` |
| libz | 5 | `inflate` (assets compactados) |
| MediaPlayer | 4 | vídeo / música do usuário |
| QuartzCore | 2 | `CAEAGLLayer`, `CATransaction` |
| AVFoundation | 1 | `AVAudioSession` |

Lista completa em `analysis/imports_arm64.txt`. Observações importantes:

- **241 seletores** e **51 classes referenciadas** — runtime ObjC mínimo é viável.
- Bibliotecas linkadas mas sem símbolos usados: StoreKit, CoreData, Security, CFNetwork,
  CoreTelephony, AdSupport, SystemConfiguration, libxml2, libsqlite3 → nada a implementar.
- Não há TLS, nem `__cxa_throw`, nem blocks além de `_NSConcreteGlobalBlock`.

## Assets (`res/`, 413 MB, 1482 arquivos)

| Tipo | Qtde | Formato |
|---|---|---|
| `.png` | 639 | texturas/UI (verificar se são PNG "CgBI" da Apple) |
| `.m3g` | 546 | cenas/modelos JSR-184 — lidos pelo próprio motor |
| `.wav` | 253 | efeitos sonoros (OpenAL) |
| `.caf` | 24 | música, **AAC** 44,1 kHz estéreo (`caff`/`desc`/`kuki`/`pakt`) |
| `.bin` | 10 | dados do jogo |
| `baked_pvrtc/` | — | texturas **PVRTC** pré-compiladas — GPUs não-PowerVR (Mali/Adreno) não suportam → decodificar em software |
| `*.lproj` | 10 | idiomas: en, fr, it, de, es, ja, ko, **pt**, zh_CN, zh_TW |

## Implicações para o port

1. Usar **arm64** nativamente em Android de 64 bits e **armv7** em Android de 32 bits.
2. Implementar um **loader Mach-O** (mapear segmentos, aplicar rebase/bind).
3. Implementar em C++ (HLE) os 411 símbolos, um **runtime Objective-C compatível com o
   ABI da Apple** e as ~50 classes de framework usadas.
4. Cuidados de ABI Apple × Linux arm64 (ver `02-ARQUITETURA.md`): varargs na pilha,
   `FILE`/`struct stat` diferentes, constantes de pthread diferentes.
5. PVRTC → RGBA em software; AAC → `AMediaCodec` do Android.
