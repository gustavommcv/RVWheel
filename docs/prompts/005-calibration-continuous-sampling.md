# Prompt 005 — Robust Continuous Sampling for Guided Calibration

## Prompt para o agente de codificação

[ROLE]

Você é um engenheiro sênior de C++20 especializado em DirectInput, aquisição de dispositivos HID, ferramentas CLI interativas, concorrência segura e calibração baseada em séries temporais.

[CONTEXT]

O RVWheel já possui DAL, perfis JSON, readiness, `rvwheel_device_probe` e um wizard de calibração. Os testes automatizados existentes passam, mas o primeiro teste manual do wizard com o Logitech G923 revelou um defeito real.

Saída observada:

```text
Discovered axes (source: rawMin..rawMax, current):
  X: 0..65535, 32767
  Y: 0..65535, 32767
  Rz: 0..65535, 32767
  Slider0: 0..65535, 32767

Release ALL controls ... Enter
Keep steering centered ... Enter
Turn the wheel FULLY LEFT ... Enter
  -> More than one axis moved; move ONLY the requested control and try again.
```

O usuário moveu somente o volante. O comportamento é reproduzível.

Existe evidência anterior de hardware mostrando que, após aquisição, o G923 pode começar com todos os pedais em `32767` e depois publicar os estados reais soltos, próximos de `65535`, simultaneamente. Isso é startup settling, não ação do usuário.

A causa já foi localizada no código atual:

- `DeviceProbeApp::RunCalibrate()` bloqueia em `std::getline` enquanto aguarda Enter;
- durante esse bloqueio não executa `window.PumpMessages()` nem `PollRawAxes()`;
- após Enter, coleta apenas um `RawAxisSnapshot` instantâneo;
- `CalibrationWizard::SubmitSnapshot()` compara snapshots únicos;
- assim, a transição tardia dos três pedais entre `32767` e `65535` é interpretada como múltiplos eixos movimentados durante o passo do volante;
- os testes atuais fornecem snapshots já estabilizados e não exercitam esse fluxo temporal.

Este problema não deve ser corrigido com um caso especial para G923, VID/PID, `32767` ou `65535`. Outros dispositivos e drivers também podem demorar para estabilizar ou apresentar ruído.

[TASK]

Corrija o workflow de calibração para fazer aquisição contínua e selecionar amostras estáveis de uma janela temporal. Preserve a state machine pura do wizard, separando aquisição/interação de inferência.

### 1. Inspeção e preservação

Antes de editar:

- inspecione `DeviceProbeApp`, `CalibrationWizard`, DirectInput raw polling, readiness/profile policy, testes e CMake;
- preserve todos os testes existentes;
- não reimplemente a DAL, profiles ou probe do zero;
- não faça operações Git destrutivas;
- não introduza UE4SS, hooks do jogo, FFB ou Logitech SDK;
- mantenha a solução genérica para qualquer volante DirectInput.

### 2. Aquisição contínua obrigatória

Enquanto qualquer prompt interativo de calibração estiver aguardando o usuário:

- bombeie mensagens Win32 continuamente;
- faça `PollRawAxes()` numa frequência limitada, próxima de 60 Hz;
- mantenha uma janela limitada das amostras recentes com timestamps de `std::chrono::steady_clock`;
- não use busy loop;
- preserve resposta a `Ctrl+C` e encerre de forma limpa;
- erros de poll devem produzir diagnóstico e política explícita, não virar amostras válidas;
- não deixe thread destacada nem lifetime inseguro.

Não é obrigatório usar uma thread. Uma implementação de input de console não bloqueante, eventos Win32 ou uma thread RAII/joinable são aceitáveis. Escolha a opção mais simples e segura para Windows/MSVC e documente brevemente o trade-off.

### 3. Captura estável, não snapshot único

Extraia um componente puro e testável, com nome como `StableRawAxisSampler` ou equivalente, que receba amostras timestamped e seja capaz de:

- exigir um número mínimo de amostras e uma janela mínima;
- determinar se todos os eixos observados estão estáveis dentro de tolerância relativa ao range reportado;
- produzir um snapshot agregado representativo por eixo, preferencialmente mediana para tolerar spikes;
- rejeitar eixo ausente, range degenerado e janela insuficiente com resultado tipado/diagnóstico;
- manter capacidade e alocações limitadas;
- não conhecer console, Win32, DirectInput, VID/PID ou nomes de modelos.

Use tolerâncias relativas ao range de cada eixo. Não hardcode valores brutos ou axis sources específicos.

Valores iniciais razoáveis, ajustáveis e documentados:

```text
poll rate:                 60 Hz
minimum acquisition time: 2500 ms
stable window:             500 ms
stable jitter tolerance:   0.5% do range
step capture timeout:      10 s
movement threshold:        manter o atual 5% ou justificar mudança
```

O tempo mínimo sozinho não basta: depois dele ainda é necessário observar uma janela estável. Reaproveite a readiness policy já existente quando fizer sentido, mas não force dependência de um profile conhecido para calibrar um dispositivo desconhecido.

### 4. Semântica da interação

Implemente um fluxo claro:

1. ao entrar em `--calibrate`, iniciar polling imediatamente;
2. mostrar `Warming up / waiting for stable input` e somente prosseguir quando a aquisição mínima e a estabilidade forem satisfeitas;
3. solicitar todos os controles soltos e centralizados;
4. enquanto o usuário prepara o controle, continuar coletando;
5. ao pressionar Enter, não usar um poll instantâneo: selecionar/agregar uma janela estável recente ou coletar uma pequena janela estável imediatamente após a confirmação;
6. entregar somente o snapshot agregado ao `CalibrationWizard`;
7. se a posição ainda estiver oscilando, mostrar `Hold the control steady` e continuar, sem avançar;
8. reiniciar/rebasear corretamente a janela após uma tentativa inválida, para samples antigos não contaminarem o retry.

O baseline usado para detectar movimento deve ser capturado somente depois do startup settling. Uma mudança que ocorreu durante warmup nunca deve ser atribuída à ação posterior do usuário.

Não exija que um controle esteja em um raw value específico para considerá-lo estável. Um pedal em midpoint estável pode ser um estado legítimo em outro dispositivo; estabilidade e instrução do usuário são conceitos separados.

### 5. Responsabilidades arquiteturais

Mantenha a separação:

```text
Continuous acquisition loop
  -> pumps messages, polls, timestamps, handles cancellation/errors

Stable sampler
  -> consumes a time series and returns a stable aggregated snapshot

CalibrationWizard
  -> compares confirmed stable snapshots and infers roles/directions

DeviceProbeApp
  -> owns CLI prompts and composes the components
```

O `CalibrationWizard` deve continuar testável sem hardware e sem sleeps. Se precisar alterar sua API, preserve semântica clara e atualize todos os callers/testes.

### 6. Feedback de CLI

Durante warmup/captura, mostre informação suficiente sem inundar o terminal:

- fase atual (`warming up`, `waiting for stability`, `ready`, `capturing`);
- tempo decorrido/necessário;
- opcionalmente valores crus atuais numa linha atualizável, se isso funcionar de forma confiável no console;
- erro específico se algum eixo não estabilizar dentro do timeout;
- instrução acionável para tentar novamente.

Evite depender de reposicionamento de cursor quando stdout não for um terminal. Redirecionamento de output e testes não podem falhar por handle inválido.

### 7. Testes automatizados obrigatórios

Além de preservar todos os testes atuais, adicione testes determinísticos, sem sleep real, para:

- janela insuficiente não fica ready;
- número insuficiente de samples não fica ready;
- série estável gera mediana correta;
- spike isolado não contamina o valor agregado;
- jitter acima da tolerância não fica estável;
- timestamps fora de ordem são tratados explicitamente;
- eixo ausente numa amostra invalida/reinicia a janela conforme contrato;
- range degenerado retorna erro claro;
- poll failure não entra na janela estável;
- timeout retorna diagnóstico;
- retry não reutiliza samples da tentativa anterior;
- cancelamento termina o loop;
- output não interativo não tenta manipular cursor do console.

Inclua obrigatoriamente este trace sintético de regressão:

```text
t=0.0 s: X=32767, Y=32767, Rz=32767, Slider0=32767
t=0.0..2.0 s: valores permanecem no midpoint inicial
t≈2.05 s: Y, Rz e Slider0 mudam juntos para 65535
t=2.05..2.55+ s: X=32767 e pedais=65535 estáveis
baseline/center confirmado somente após estabilidade
usuário move apenas X gradualmente até 0 e segura
captura stable: X=0 e pedais=65535
```

Resultado esperado: o wizard identifica somente `X` como steering; não retorna `Ambiguous`. O teste deve falhar com a implementação antiga de snapshot único e passar com a nova aquisição estável.

Adicione também um cenário genérico em que dois eixos realmente são movidos entre snapshots estáveis; esse cenário deve continuar retornando `Ambiguous`.

### 8. Build e validação

Execute, sem FFB:

```powershell
$toolchain = "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake -S . -B build-calibration-fix `
  -G "Visual Studio 17 2022" `
  -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
  -DRVWHEEL_ENABLE_LOGITECH_SDK=OFF `
  -DRVWHEEL_BUILD_TESTS=ON `
  -DRVWHEEL_BUILD_TOOLS=ON

cmake --build build-calibration-fix --config Debug
ctest --test-dir build-calibration-fix -C Debug --output-on-failure

cmake --build build-calibration-fix --config Release
ctest --test-dir build-calibration-fix -C Release --output-on-failure
```

Depois dos testes verdes, forneça o comando de teste manual, mas não declare sucesso em hardware sem o usuário executá-lo:

```powershell
& ".\build-calibration-fix\tools\device_probe\Release\rvwheel_device_probe.exe" --calibrate
```

[CONSTRAINTS]

- C++20, Windows x64, MSVC, CMake target-based.
- Sem hardcode de G923, VID/PID, raw midpoint, raw maximum ou source de eixos no algoritmo genérico.
- Sem sleeps reais em testes.
- Clock/tempo injetável ou timestamps fornecidos aos componentes puros.
- Sem polling ilimitado, busy waiting, detached threads ou data races.
- Sem FFB.
- Sem alterar silenciosamente o significado dos profiles existentes.
- Código, comentários e mensagens técnicas no código em inglês.
- Não reduzir warnings nem remover testes para obter verde.

[OUTPUT]

Ao concluir, responda em português com:

1. causa raiz confirmada e arquivos alterados;
2. desenho do loop contínuo e estratégia de input de console;
3. contrato e algoritmo do stable sampler;
4. tolerâncias/defaults finais e justificativa;
5. tratamento de timeout, erros, retry e cancelamento;
6. testes novos, incluindo resultado do trace regressivo;
7. comandos realmente executados;
8. resultados Debug e Release, contagem total de testes e warnings;
9. comando exato para o teste manual no G923;
10. limitações remanescentes.

[ACCEPTANCE GATE]

A tarefa só está concluída quando:

- todos os testes anteriores continuam passando;
- novos testes temporais e o trace regressivo passam;
- Debug e Release compilam sem warnings novos;
- mensagens Win32 e raw axes continuam sendo processados enquanto a CLI espera o usuário;
- nenhum passo usa um snapshot cru isolado obtido somente depois de `std::getline` bloqueante;
- baseline é aceito apenas depois de aquisição mínima e janela estável;
- transição simultânea de startup dos pedais não causa falso `Ambiguous` no passo do volante;
- movimento real de dois eixos ainda causa `Ambiguous`;
- retry descarta a janela contaminada;
- `Ctrl+C`, poll failure e timeout encerram/recuperam de forma explícita;
- não existe lógica específica de G923 na implementação genérica;
- nenhum FFB ou integração com o jogo foi introduzido.
