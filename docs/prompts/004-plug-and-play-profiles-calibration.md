# Prompt 004 — Plug-and-Play Device Profiles and Guided Calibration

## Prompt para o agente de codificação

[ROLE]

Você é um arquiteto e engenheiro sênior de C++20 especializado em DirectInput, dispositivos HID de corrida, sistemas de perfis versionados, calibração de eixos e ferramentas CLI. Sua tarefa é implementar a base plug-and-play do RVWheel sem acoplar o projeto a um único fabricante ou modelo.

[CONTEXT]

A Layer 2 (DAL) e o executável `rvwheel_device_probe` já compilam em Debug/Release com MSVC e passam 63/63 testes. O probe foi validado em hardware real com um Logitech G923 pelo backend DirectInput, sem Logitech SDK e sem force feedback.

Hardware verificado:

```text
Device name: Logitech G HUB G923 Racing Wheel for PlayStation 4 and PC (USB)
Backend: DirectInput
VID: 0x046D
PID: 0xC266
Buttons: 25
POVs: 1
FFB capability reported: true (not exercised)
```

Evidência completa:

```text
docs/hardware/G923_DIRECTINPUT_CAPTURE.md
g923-capture.jsonl (local capture artifact)
```

Resultados reais da captura:

- steering: `-1.0` a `+1.0`, centro final aproximadamente `+0.000854` — correto;
- throttle: solto `1.0`, pressionado `0.0` — semanticamente invertido;
- brake: solto `1.0`, pressionado `0.0` — semanticamente invertido;
- clutch: solto `1.0`, pressionado `0.0` — semanticamente invertido;
- os três pedais são independentes e foram fisicamente movimentados;
- durante aproximadamente os primeiros 2.05 segundos após aquisição, os três pedais reportaram simultaneamente `0.499992`, depois estabilizaram em `1.0` sem falha de poll;
- 1,679 amostras, zero failed polls, todas connected/valid/Ok;
- botões e oito direções do POV foram observados;
- nenhum FFB foi aplicado.

O objetivo do RVWheel não é apenas suportar o G923. O sistema deve atender Logitech G29/G27/G920/G923, Moza, Thrustmaster, Fanatec e dispositivos DirectInput genéricos.

Limitação inevitável: DirectInput não padroniza de forma confiável qual eixo físico representa throttle, brake, clutch, handbrake ou steering em todos os fabricantes. “Plug-and-play” deve significar:

1. dispositivos conhecidos: perfil embutido selecionado automaticamente, zero configuração;
2. dispositivos desconhecidos: fallback seguro e um calibrador guiado executado uma vez;
3. perfil gerado salvo e reaplicado automaticamente nas próximas conexões;
4. nenhuma lógica específica de modelo espalhada pelo backend.

[TASK]

Implemente um sistema versionado de perfis de dispositivo e uma calibração guiada no `rvwheel_device_probe`. Use o G923 como primeiro perfil verificado, mas desenhe contratos, matching e persistência para múltiplos fabricantes/backends.

### 1. Preservação e escopo

Antes de editar:

- inspecione a árvore, CMake e mudanças atuais;
- preserve o código e os 63 testes existentes;
- não use operações Git destrutivas;
- não reimplemente a DAL ou o probe do zero;
- não implemente UE4SS, hooks do jogo, UI in-game, curvas, force feedback ou instalador;
- não invente VID/PID, nomes de eixos ou características de modelos não testados;
- não hardcode `if (vid == 0x046D && pid == 0xC266)` dentro do polling/backend.

### 2. Limites arquiteturais

Separe responsabilidades:

```text
DAL / DirectInput backend
  - enumera dispositivo e eixos
  - lê valores brutos
  - aplica um layout/calibração já resolvido
  - publica WheelState normalizado

Profile system
  - carrega/valida JSON
  - faz matching por identidade/capacidades
  - resolve axis roles, direction and startup policy
  - não conhece HWND, COM ou estruturas DirectInput

Calibration workflow
  - observa eixos brutos
  - orienta ações do usuário
  - identifica eixos/direções/ranges
  - gera um perfil JSON

Probe
  - composition root e CLI
  - seleciona perfil automaticamente
  - oferece --calibrate e validação/captura
```

A DAL não deve depender do parser JSON. Defina tipos de configuração pequenos e backend-agnósticos (por exemplo `WheelInputLayout`, `AxisBinding`, `AxisCalibrationConfig`, `DeviceReadinessPolicy`) que possam ser produzidos pelo sistema de perfis e consumidos pela DAL.

Não exponha `GUID`, `DIDEVICEOBJECTINSTANCE`, `DIJOYSTATE2` ou outros tipos DirectInput nos headers públicos do Core. Um eixo deve possuir um identificador opaco/serializável estável para o backend, por exemplo string/token documentado (`X`, `Y`, `Z`, `Rx`, `Ry`, `Rz`, `Slider0`, `Slider1`) ou representação equivalente sem tipos Win32.

### 3. Descoberta de eixos brutos

O calibrador não pode aprender novos dispositivos usando apenas `WheelState`, pois esse estado já contém quatro roles previamente mapeados. Adicione uma API de diagnóstico/calibração separada da interface de gameplay:

- enumeração de todos os eixos realmente presentes;
- identificador estável do eixo dentro daquele backend;
- nome reportado quando disponível;
- raw min/max reportados pela API;
- valor bruto atual;
- tipo/hint do backend quando disponível, sem tratá-lo como verdade absoluta;
- polling de um snapshot bruto sem alocação desnecessária por frame.

Opções aceitáveis incluem uma interface opcional `ICalibratableWheelDevice`, um serviço de discovery separado ou composição equivalente. Não polua `IWheelDevice` com métodos específicos de ferramenta se uma interface segregada resolver melhor.

Reutilize o mesmo objeto DirectInput/RAII sempre que possível; não mantenha duas aquisições exclusivas concorrentes do mesmo volante.

### 4. Schema de perfil JSON

Crie um schema versionado, documentado e estritamente validado. Exemplo conceitual (ajuste nomes se melhorar a arquitetura):

```json
{
  "schemaVersion": 1,
  "profileId": "logitech-g923-ps-pc-directinput",
  "displayName": "Logitech G923 (PlayStation/PC, DirectInput)",
  "match": {
    "backend": "DirectInput",
    "vendorId": "0x046D",
    "productId": "0xC266"
  },
  "axes": {
    "steering": {
      "source": "X",
      "direction": "normal",
      "center": "rangeMidpoint"
    },
    "throttle": {
      "source": "Y",
      "direction": "inverted"
    },
    "brake": {
      "source": "Rz",
      "direction": "inverted"
    },
    "clutch": {
      "source": "Slider0",
      "direction": "inverted"
    }
  },
  "readiness": {
    "minimumWarmupMilliseconds": 2200,
    "stableSampleMilliseconds": 250,
    "maximumWaitMilliseconds": 5000
  }
}
```

O exemplo de sources reflete o mapping MVP atual, mas confirme os identificadores reais usados pelo backend antes de gravar o perfil. Não grave raw range `0..65535` como verdade do modelo: continue consultando ranges reais do DirectInput em runtime e use `direction` para ordenar `rawAtReleased`/`rawAtPressed`.

Validação obrigatória:

- `schemaVersion` suportada;
- IDs não vazios e únicos;
- backend conhecido;
- VID/PID válidos e normalizados;
- roles sem source duplicado, salvo se explicitamente permitido no futuro;
- source precisa existir no dispositivo correspondente;
- direction enumerada, sem booleanos ambíguos;
- tempos finitos, não negativos e limitados;
- perfil inválido gera erro acionável com path/campo; não faça fallback silencioso depois de um match exato inválido.

### 5. Dependência JSON e setup simples

Use `nlohmann_json` através do vcpkg, não um parser improvisado e não uma cópia avulsa sem versionamento.

Adicione manifest mode ao repositório (`vcpkg.json`, e baseline/configuração quando apropriado) com pelo menos:

```text
catch2
nlohmann-json
```

Preserve o build com `RVWHEEL_BUILD_TESTS=OFF`. Dependências exclusivas de testes não devem contaminar consumers desnecessariamente; organize features/dependencies do manifest quando útil.

O objetivo é que um novo desenvolvedor com `VCPKG_ROOT` configurado possa configurar o projeto sem executar `vcpkg install ...` manualmente para cada pacote.

### 6. Localização e precedência de perfis

Implemente duas origens:

1. perfis built-in distribuídos com RVWheel, sob `configs/default_profiles/`;
2. perfis do usuário sob `%LOCALAPPDATA%\RVWheel\profiles\`.

Precedência:

- override do usuário com mesmo `profileId` vence o built-in;
- exact match backend+VID+PID vence qualquer fallback;
- critérios adicionais podem aumentar especificidade, mas nome de exibição sozinho nunca deve ser identidade principal;
- perfil genérico tem prioridade menor;
- empate na maior prioridade é erro/diagnóstico, não escolha aleatória.

Permita injetar diretórios em testes e oferecer `--profiles-dir <path>` no probe. Não faça testes dependerem de `%LOCALAPPDATA%` real.

Resolva built-in profiles de forma robusta em relação ao executable/install root; não dependa apenas do current working directory. Adicione uma opção explícita para tests/development se necessário.

### 7. Primeiro perfil verificado: G923

Crie somente um perfil específico inicialmente:

```text
Logitech G923 PlayStation/PC DirectInput
VID 046D
PID C266
```

Dados permitidos porque foram verificados:

- steering normal;
- throttle, brake e clutch invertidos;
- 25 botões e um POV podem ser usados como sanity checks, não como requisito absoluto de matching;
- startup transient observado por aproximadamente 2.05 s.

Não adicione perfis G29/G920/G27/Moza/Thrustmaster/Fanatec com dados estimados. Crie documentação/template para contribuições futuras e exija evidência de captura para novos perfis.

### 8. Readiness/startup state machine

Implemente uma política explícita para impedir que o midpoint transitório do G923 seja consumido como meio pedal.

Requisitos:

- após connect/acquire, estado inicia como warming up;
- durante `minimumWarmupMilliseconds`, poll pode ocorrer, mas o input não deve ser publicado como gameplay-valid;
- depois do mínimo, exija estabilidade dos eixos relevantes por `stableSampleMilliseconds` dentro de tolerância documentada;
- ao ficar ready, publique estado normalizado usando o perfil;
- em `maximumWaitMilliseconds`, retorne status/diagnóstico explícito em vez de ficar preso indefinidamente;
- disconnect/reacquire reinicia a state machine;
- clock injetável para testes determinísticos;
- não confunda “pedais soltos” com “valor 0.5”: readiness usa estabilidade/política, não hardcode de um raw value específico no backend genérico;
- o perfil pode fornecer warmup mínimo específico; fallback genérico deve ser conservador e documentado.

Defina claramente a semântica de `WheelState.valid` durante warming up. `connected` pode ser true enquanto `valid` é false. Propague um diagnóstico/status que permita ao probe exibir `WarmingUp` em vez de `BackendError`.

### 9. Matching e fallback plug-and-play

Fluxo esperado ao conectar:

```text
Enumerate identity and raw axes
        ↓
Load built-in + user profiles
        ↓
Resolve unique best match
        ↓
Known profile? ── yes → apply automatically → warmup → ready
        │
        no
        ↓
Safe generic heuristic available?
        │ yes → mark provisional and report it
        │ no
        ↓
Request one-time guided calibration
```

Nunca publique um eixo desconhecido como throttle/brake com confiança silenciosa. O probe deve mostrar se o layout veio de `BuiltInProfile`, `UserProfile`, `GeneratedProfile`, `ProvisionalFallback` ou `Unconfigured`.

### 10. Guided calibration no probe

Adicione CLI:

```text
rvwheel_device_probe --profiles
rvwheel_device_probe --calibrate [--output <profile.json>]
rvwheel_device_probe --monitor ... [--profile <profileId-or-path>]
rvwheel_device_probe --capture ... [--profile <profileId-or-path>]
```

O wizard deve:

1. listar dispositivo e eixos brutos;
2. aguardar warmup/estabilidade;
3. capturar baseline com todos os controles soltos;
4. pedir steering ao centro;
5. pedir steering full left e full right;
6. pedir throttle solto/pressionado;
7. pedir brake solto/pressionado;
8. pedir clutch se presente ou permitir skip explícito;
9. detectar qual eixo teve maior delta em cada etapa;
10. inferir direção pelos endpoints observados;
11. rejeitar ação ambígua quando múltiplos eixos se movem acima do threshold;
12. mostrar resumo e pedir confirmação antes de salvar;
13. salvar atomicamente em user profiles;
14. recarregar o perfil e fazer uma validação curta.

Use thresholds relativos ao range reportado, não deltas absolutos hardcoded. Inclua tolerância de ruído configurável/documentada. Nunca sobrescreva perfil existente sem confirmação explícita/flag.

O wizard deve ser testável: extraia uma state machine pura que recebe snapshots/ações confirmadas, sem ler console diretamente.

### 11. Probe e captura após perfil

Atualize `--list` para mostrar:

- perfil selecionado e origem;
- status `WarmingUp`, `Ready`, `Unconfigured` ou erro;
- axis role → source → direction;
- motivo do match e score/prioridade quando em modo verbose.

Atualize JSONL para nova schema version ou extensão backward-compatible documentada. Inclua:

- `profileId`;
- `profileOrigin`;
- `readinessState`;
- `valid` coerente com readiness.

Depois do perfil G923 ser aplicado, a próxima captura deve produzir:

```text
released throttle/brake/clutch ≈ 0.0
fully pressed throttle/brake/clutch ≈ 1.0
steering unchanged at -1..+1
no valid half-pedal samples during initial warmup
```

### 12. Testes automatizados

Preserve os 63 testes existentes e adicione testes para:

- parse/serialize/round-trip de perfil;
- schema/version e mensagens de erro por campo;
- precedence user vs built-in;
- matching exato backend+VID+PID;
- empate ambíguo;
- dispositivo sem VID/PID;
- source ausente;
- source duplicado;
- direction normal/inverted usando ranges runtime;
- G923 released raw max → output 0;
- G923 pressed raw min → output 1;
- steering permanece normal;
- readiness: minimum warmup;
- readiness: estabilidade;
- readiness: timeout;
- disconnect/reconnect reinicia;
- `connected=true` com `valid=false` durante warmup;
- synthetic trace equivalente a midpoint transitório → settled released;
- calibration wizard escolhe eixo de maior delta;
- wizard detecta inversão;
- wizard rejeita múltiplos eixos ambíguos;
- wizard skip clutch;
- atomic/profile overwrite policy;
- paths injetados, sem tocar `%LOCALAPPDATA%` real.

Adicione um fixture sintético compacto baseado nos fatos verificados do G923; não versione as 1,679 linhas da captura inteira só para testes se poucos keyframes cobrem o comportamento.

### 13. Build e validação

Use:

```powershell
$toolchain = "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake -S . -B build-profiles `
  -G "Visual Studio 17 2022" `
  -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
  -DRVWHEEL_ENABLE_LOGITECH_SDK=OFF `
  -DRVWHEEL_BUILD_TESTS=ON `
  -DRVWHEEL_BUILD_TOOLS=ON

cmake --build build-profiles --config Debug
ctest --test-dir build-profiles -C Debug -N
ctest --test-dir build-profiles -C Debug --output-on-failure

cmake --build build-profiles --config Release
ctest --test-dir build-profiles -C Release --output-on-failure
```

Também valide library-only/tests-off quando aplicável.

Após testes verdes, execute apenas operações sem FFB:

```powershell
.\build-profiles\tools\device_probe\Release\rvwheel_device_probe.exe --profiles
.\build-profiles\tools\device_probe\Release\rvwheel_device_probe.exe --list
```

Se o sandbox não acessar DirectInput, forneça comandos para o usuário. Não declare a validação física final sem nova captura real.

[CONSTRAINTS]

- C++20, Windows x64, MSVC, CMake target-based.
- JSON via nlohmann_json/vcpkg manifest.
- DAL sem dependência de JSON.
- Perfis conhecidos em arquivos, não condicionais espalhadas.
- Nenhum dado inventado para outros volantes.
- Unknown devices devem ter fallback explícito ou wizard; nunca mapping silencioso inseguro.
- Sem deadzones, curves, sensitivity e button mapping completo nesta tarefa.
- Sem FFB.
- Sem UE4SS/jogo.
- Código e comentários em inglês.
- Não reduzir warnings nem remover testes para obter verde.

[OUTPUT]

Ao concluir, responda em português com:

1. arquivos e targets criados/alterados;
2. arquitetura e limites DAL/Profile/Calibration/Probe;
3. schema JSON final e exemplo G923;
4. algoritmo de matching e precedence;
5. readiness state machine;
6. funcionamento do wizard para dispositivo desconhecido;
7. comandos reais executados;
8. build Debug/Release e warnings;
9. testes descobertos/aprovados;
10. saída de `--profiles` e `--list`;
11. comando exato para nova captura G923;
12. limitações e dados que ainda exigem captura de outros modelos.

[ACCEPTANCE GATE]

A tarefa só está concluída quando:

- os 63 testes anteriores continuam passando;
- novos testes de profiles, matching, readiness e calibration passam;
- Debug e Release compilam sem warnings novos;
- G923 é reconhecido por perfil JSON exact-match, sem `if VID/PID` no backend;
- pedais G923 são configurados como inverted pelo perfil;
- startup transient é marcado invalid/warming-up até readiness;
- unknown device não recebe mapping silencioso definitivo;
- wizard gera perfil reutilizável;
- user profile override e ambiguity são testados;
- nenhum perfil fictício de outro volante é adicionado;
- nenhum FFB ou integração com o jogo é introduzido.
