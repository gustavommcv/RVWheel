# Prompt 003 — Logitech G923 DirectInput Device Probe

## Prompt para o agente de codificação

[ROLE]

Você é um engenheiro sênior de C++20, Win32 e DirectInput 8 responsável por criar a primeira ferramenta de validação em hardware real do RVWheel. Trabalhe sobre a DAL existente já compilada e testada; não reimplemente a arquitetura e não avance para UE4SS ou integração com o jogo.

[CONTEXT]

A Layer 2 do RVWheel está compilando em Windows x64 com Visual Studio 2022 Build Tools/MSVC, Logitech SDK desabilitado e Catch2 via vcpkg. Builds Debug e Release passaram sem warnings, e 22/22 testes unitários passaram.

Hardware real conectado e validado no painel `joy.cpl`:

```text
Device: Logitech G HUB G923 Racing Wheel
Driver/software: Logitech G HUB installed
Windows status: working
Observed controls: wheel axis, accelerator, brake, 25 buttons and one POV hat
```

O dispositivo deve ser testado inicialmente pelo backend DirectInput existente:

```text
RVWHEEL_ENABLE_LOGITECH_SDK=OFF
```

O `LogitechGamingSdkAdapter` ainda é um esqueleto deliberado e não deve ser usado ou completado sem o SDK proprietário real. O objetivo desta tarefa é validar enumeração, polling, normalização, botões, POV e hot-plug pelo DirectInput.

O artefato atual `rvwheel_dal.lib` é uma biblioteca estática e não pode ser executado sozinho. Precisamos de um executável standalone de diagnóstico.

[TASK]

Implemente um executável console Windows chamado `rvwheel_device_probe`, em `tools/device_probe/`, que consuma somente a API pública da DAL e permita validar o Logitech G923 real antes da integração com o jogo.

### 1. Preservação do repositório

Antes de editar:

- inspecione a árvore e os CMake existentes;
- preserve todas as mudanças atuais;
- não use `git reset`, `git checkout --` ou limpeza destrutiva;
- não altere contratos da DAL sem necessidade comprovada;
- não copie arquivos para o diretório do jogo;
- não implemente Layer 3, Layer 4, UE4SS, overlay ou instalador.

### 2. Target e estrutura

Estrutura sugerida:

```text
tools/
└── device_probe/
    ├── CMakeLists.txt
    ├── DeviceProbeApp.hpp
    ├── DeviceProbeApp.cpp
    ├── ConsoleRenderer.hpp
    ├── ConsoleRenderer.cpp
    ├── HiddenWindow.hpp
    ├── HiddenWindow.cpp
    ├── CliOptions.hpp
    ├── CliOptions.cpp
    └── main.cpp
```

Adicione uma opção CMake:

```text
RVWHEEL_BUILD_TOOLS
```

Ela deve ser `ON` por default apenas quando RVWheel for o projeto raiz, ou seguir o padrão já usado no repositório. Crie o target `rvwheel_device_probe` e linke-o a `rvwheel::dal`. Não duplique fontes da DAL no executável.

### 3. Contexto Win32 e ciclo de vida

O probe precisa fornecer `HINSTANCE` e um `HWND` válido ao composition root da DAL.

- crie uma pequena janela Win32 oculta com RAII, classe registrada e destruição determinística;
- não dependa de `GetConsoleWindow()`, pois Windows Terminal/pseudoconsole pode não fornecer uma janela adequada;
- bombeie mensagens Win32 durante o monitoramento para manter o contexto saudável;
- use `GetModuleHandleW(nullptr)` para o módulo quando apropriado;
- mantenha `NOMINMAX` e encoding Win32 explícito;
- não exponha Win32 nos contratos públicos existentes da DAL além do composition root já previsto.

### 4. Interface de linha de comando

Implemente os seguintes modos:

```text
rvwheel_device_probe --help
rvwheel_device_probe --list
rvwheel_device_probe --monitor [--duration <seconds>] [--rate <hz>]
rvwheel_device_probe --capture <path.jsonl> [--duration <seconds>] [--rate <hz>]
```

Defaults:

```text
duration: 30 seconds
rate: 60 Hz
hardware refresh interval: 5 seconds
force feedback: disabled
```

Regras:

- argumentos inválidos retornam exit code diferente de zero e mostram uso;
- `--rate` deve ser limitado a range seguro/documentado, por exemplo 1–250 Hz;
- `--duration` deve aceitar valor finito e positivo, com limite razoável;
- `--list` enumera, imprime e encerra;
- `--monitor` atualiza a tela sem gerar milhares de linhas;
- `--capture` grava JSON Lines com uma amostra por linha e flush periódico, não por frame;
- paths e mensagens devem lidar corretamente com Unicode no Windows;
- `Ctrl+C` deve encerrar de forma limpa.

### 5. Saída de enumeração

Para cada dispositivo, mostre:

- `DeviceId` em representação hexadecimal estável;
- nome;
- manufacturer quando disponível, ou `unknown`;
- backend (`DirectInput`/`Logitech`);
- VID/PID em hexadecimal quando disponíveis;
- conectado/desconectado;
- capacidades: steering, throttle, brake, clutch, force feedback;
- número de botões e POVs.

Não trate ausência de manufacturer, VID/PID ou clutch como crash. Não invente valores ausentes.

O resultado esperado para esta máquina é pelo menos um dispositivo cujo nome corresponda ao Logitech G923/G HUB. Não hardcode nome, VID, PID, número de botões ou comportamento específico do G923: apenas reporte o que o backend realmente descobrir.

### 6. Monitor de estado normalizado

Em `--monitor`, apresente uma linha/bloco atualizado contendo:

```text
steering  [-1.000, +1.000]
throttle  [ 0.000,  1.000]
brake     [ 0.000,  1.000]
clutch    [ 0.000,  1.000] or N/A when unsupported
buttons   pressed indices
POV       centered or direction
sample counter
connected/valid
poll frequency and dropped/failed polls
```

Não aplique deadzone, curvas ou smoothing. O objetivo é observar exatamente a normalização da Layer 2.

Use `std::chrono::steady_clock` para scheduling. Evite busy-spin: durma até a próxima amostra com tolerância simples. Não prometa precisão real-time.

Se o dispositivo desconectar:

- não crashe;
- mostre estado disconnected/invalid;
- continue chamando o refresh conforme a política de cinco segundos;
- detecte a reconexão quando o backend a reportar;
- registre transições uma única vez, sem log spam.

### 7. Captura JSONL

Cada linha de captura deve ser um objeto JSON autocontido, com schema documentado e estável para esta versão, contendo ao menos:

```text
schemaVersion
elapsedMilliseconds
deviceId
backend
connected
valid
sampleCounter
steering
throttle
brake
clutch or null
pressedButtons
povs
pollStatus
```

Não adicione uma biblioteca JSON ao projeto apenas para esse formato simples se serialização segura e pequena puder ser implementada localmente. Escape strings corretamente. Se usar dependência existente, justifique.

Grave em arquivo temporário/sidecar e finalize de forma segura quando for prático, para reduzir risco de captura parcialmente corrompida. Uma interrupção pode deixar JSONL parcial, mas todas as linhas já gravadas devem permanecer válidas individualmente.

### 8. Force feedback fora do escopo inicial

Não aplique nenhum efeito de force feedback no `--list`, `--monitor` ou `--capture`.

Não adicione ainda `--ffb-test` nesta tarefa. Primeiro precisamos confirmar eixos, normalização, hot-plug e estabilidade. Qualquer FFB real será uma tarefa separada, com confirmação humana, limite baixo de força, duração curta e cleanup garantido.

### 9. Testes automatizados

Extraia componentes puros para que possam ser testados sem hardware:

- parsing de argumentos;
- validação/limites de duration e rate;
- formatação de VID/PID, `DeviceId`, botões e POV;
- serialização de uma amostra JSONL, incluindo escaping;
- comportamento de seleção de dispositivo quando há zero, um ou vários dispositivos;
- renderer não deve acessar índice de botão/POV fora da capacidade reportada.

Adicione esses testes ao target Catch2 existente ou a um target de testes apropriado, sem duplicar `main`. Não torne o build dos testes dependente de volante conectado.

Preserve e execute os 22 testes existentes; a contagem final deve ser maior que 22.

### 10. Build e validação automática

Use o ambiente já validado:

```powershell
$toolchain = "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake -S . -B build-device-probe `
  -G "Visual Studio 17 2022" `
  -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
  -DRVWHEEL_ENABLE_LOGITECH_SDK=OFF `
  -DRVWHEEL_BUILD_TESTS=ON `
  -DRVWHEEL_BUILD_TOOLS=ON

cmake --build build-device-probe --config Debug
ctest --test-dir build-device-probe -C Debug -N
ctest --test-dir build-device-probe -C Debug --output-on-failure

cmake --build build-device-probe --config Release
ctest --test-dir build-device-probe -C Release --output-on-failure
```

Depois valide sem interação:

```powershell
.\build-device-probe\tools\device_probe\Debug\rvwheel_device_probe.exe --help
.\build-device-probe\tools\device_probe\Debug\rvwheel_device_probe.exe --list
```

Adapte apenas o path final se o CMake organizar o runtime em outro diretório; reporte o path real.

Se o sandbox impedir acesso DirectInput ao dispositivo, não altere a implementação para mascarar isso. Registre o erro e forneça ao usuário o comando exato para executar em um PowerShell normal.

### 11. Protocolo manual para o usuário

Ao finalizar, forneça estes comandos com o path real do executável:

1. listar dispositivo;
2. monitorar por 30 segundos;
3. capturar por 30 segundos em `g923-capture.jsonl`.

Durante monitoramento/captura, instrua o usuário a executar lentamente, nesta ordem:

1. deixar todos os controles soltos por 3 segundos;
2. girar volante totalmente à esquerda, manter 1 segundo e retornar ao centro;
3. girar totalmente à direita, manter 1 segundo e retornar ao centro;
4. pressionar acelerador de 0% a 100% e soltar;
5. pressionar freio de 0% a 100% e soltar;
6. pressionar embreagem se reportada;
7. acionar os dois paddles;
8. pressionar alguns botões identificáveis;
9. mover o POV em quatro direções e soltar;
10. não desconectar durante a primeira captura.

Uma segunda execução poderá validar hot-plug: desconectar e reconectar somente o USB, nunca alimentação em movimento e nunca durante FFB (FFB estará desabilitado nesta tarefa).

[CONSTRAINTS]

- C++20, Windows x64, MSVC, CMake target-based.
- Backend principal nesta tarefa: DirectInput, Logitech SDK OFF.
- Sem UE4SS, jogo, hooks, profiles, curves, deadzones ou UI gráfica.
- Sem force feedback real.
- Sem paths absolutos commitados; paths absolutos acima são apenas comandos locais de validação.
- Sem globals mutáveis; RAII para janela, console state e arquivos.
- Sem hardware obrigatório nos testes Catch2.
- Não afirmar que o hardware foi testado se `--list`/`--monitor` não foram realmente executados contra o G923.
- Não modificar o jogo nem copiar DLLs para sua instalação.

[OUTPUT]

Ao concluir, responda em português com:

1. arquivos criados/alterados;
2. decisões arquiteturais da ferramenta;
3. resultado real de build Debug/Release;
4. número de testes descobertos e resultado CTest;
5. path real do executável;
6. saída real de `--help`;
7. saída real de `--list`, se o sandbox permitiu hardware;
8. comandos exatos para o usuário realizar monitoramento e captura;
9. schema JSONL e local esperado da captura;
10. limitações e qualquer diferença observada entre capacidades esperadas e reportadas.

[ACCEPTANCE GATE]

A tarefa só está concluída quando:

- `rvwheel_device_probe.exe` compila em Debug e Release;
- todos os testes antigos continuam passando;
- novos testes puros do probe passam;
- `ctest -N` registra mais de 22 testes;
- `--help` funciona sem hardware;
- `--list` enumera com segurança ou reporta erro operacional claro;
- monitor/capture não aplicam FFB;
- a ferramenta não depende de UE4SS, do jogo ou do SDK Logitech;
- o usuário recebe comandos executáveis para a primeira captura do G923.
