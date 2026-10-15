# Continuidade do desenvolvimento

## Documentação de referência

1. [Histórico e tarefas pendentes](SPRINTS.md).
2. [Arquitetura](02-ARQUITETURA.md).
3. [Compilação, ferramentas e depuração](03-COMPILAR.md).
4. [Análise do binário](01-ANALISE-BINARIO.md).
5. [Guia do jogador](04-GUIA-DO-JOGADOR.md).

## Estado de referência

- Release 0.2.1: APK universal com `arm64-v8a` e `armeabi-v7a`, ícone adaptativo e
  importação de IPA/OBB pelo seletor do Android.
- A10: renderização GLES 1.1 a aproximadamente 60 FPS, salvar/carregar, teclado nativo,
  pausa/retomada e música/efeitos verificados no histórico. Culinária e pesca confirmadas
  pelo jogador no caminho de 64 bits.
- Caminho armv7 executado no A10, com testes específicos de ABI e save compartilhado com
  arm64. Instalação e execução no Galaxy J6 confirmadas pelo jogador.
- Ainda faltam sessões prolongadas, validação em outras GPUs e cobertura dos imports
  adicionais dos serviços EA/RestKit/CoreData presentes na fatia armv7.

## Organização do código

| Local | Responsabilidade |
|---|---|
| `android/app/src/main/cpp/loader/` | Mapeamento Mach-O, rebase/bind e stubs de imports |
| `android/app/src/main/cpp/objc/` | Runtime ObjC e dispatchers armv7/arm64 |
| `android/app/src/main/cpp/hle/` | APIs compatíveis com o iOS, gráficos, áudio e adaptadores ABI |
| `android/app/src/main/cpp/platform/` | SDL/EGL, eventos, JNI, apresentação e relatório de crash |
| `android/app/src/main/java/com/ts3ambitions/port/` | Importador, diálogos nativos, foto e texto |
| `tools/` | Conversor de OBB, geração de código/ícone, capturas e testes |

`analysis/`, `extracted/`, os pacotes do jogo, builds, saves e chaves de assinatura são
dados locais, fora do controle de versão. Cada desenvolvedor prepara sua própria cópia
do jogo e seu próprio ambiente de análise. Os caminhos nos comandos são relativos à
raiz do projeto, exceto quando indicado.

## Fluxo de verificação

1. Faça backup de `files/home` do aparelho antes de trocar builds ou repetir importações.
2. Compile o release conforme o guia e atualize com `adb install -r`, usando a mesma
   assinatura da instalação anterior. A chave local não faz parte do repositório.
3. Use `tools/run.sh` para reiniciar e capturar o log; o script abre a `LauncherActivity`.
4. Procure chamadas `UNIMPLEMENTED`, `unrecognized selector` e mensagens `TS3A-crash`.
   A contagem de binds pendentes na armv7 também inclui caminhos ainda não exercitados.
5. Confira a imagem com `tools/shot.sh` e preserve evidências úteis em `docs/img/`.
6. Atualize `SPRINTS.md` com o que foi efetivamente testado, problemas e soluções.

As coordenadas de toque dos roteiros existentes foram medidas em 1520×720. Para outro
aparelho ou orientação, use as dimensões reais da captura. O fator de escala de uma
prévia para toques é a largura original dividida pela largura reduzida.

Para evitar que a tela apague durante um teste, registre antes os valores de
`screen_off_timeout` e `stay_on_while_plugged_in`. Restaure ambos após a sessão.

## Detalhes importantes de implementação

- A renderização parte de uma pthread criada pelo jogo. `setCurrentContext:` precisa
  transferir a posse do EGL para ela; o contexto não pode ficar corrente em duas threads.
- O caminho padrão é GLES 1.1, com `GL_OES_matrix_palette`. ES2 é experimental: os shaders
  originais contêm tipos HLSL que precisam de adaptação.
- O quadro do jogo é uma textura compartilhada, girada 90° por um contexto de apresentação.
  Fence EGL é necessária para sincronizar a amostragem no driver Mali.
- A tela iOS emulada é retrato. O toque paisagem é convertido por
  `px = largura − Y`, `py = X`, após retirar o letterbox.
- O nome digitado no diálogo Android é entregue por `UIControlEventEditingDidEndOnExit`
  (`0x80000`). Apenas alterar o texto do `UITextField` não é suficiente.
- A tabela CRC acompanha `unsigned long`: 8 bytes por entrada em arm64, 4 em armv7.
- Há PNGs normais e CgBI premultiplicados. Música AAC usa CAF; efeitos WAV usam OpenAL.
- Apple arm64 passa varargs na pilha. Armv7 usa registradores/pilha com alinhamento
  diferente do Android, além de tratar `r9` como volátil. Use os adaptadores existentes
  também ao adicionar callbacks que entram no código do jogo.

## Convenções

- C++20; comentários curtos em inglês; documentação em português.
- Use `LOGI`/`LOGW`/`LOGE`, `HLE_EXPORT` e `objc::define_class`/`add_method` conforme
  o padrão dos arquivos existentes.
- As tabelas GL e os registros ABI são gerados por ferramentas do projeto.
- Não adicione IPAs, OBBs, executáveis extraídos, assets do jogo, saves ou chaves ao git.
