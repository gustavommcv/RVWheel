# Prompt 006 — Investigar a perda de acesso exclusivo de Force Feedback no G923

## Prompt para o agente de codificação

[ROLE]

Você é um engenheiro sênior de C++20 especializado em DirectInput, force feedback, drivers/firmware de dispositivos HID USB, e testes controlados em hardware real. Você já testou com sucesso todos os inputs deste projeto (volante, pedais, câmbio sequencial Logitech) e validou a integração completa com o jogo via UE4SS — sua experiência prática com este hardware específico é exatamente o que esta investigação precisa agora.

[CONTEXT]

O RVWheel já possui uma infraestrutura completa de Force Feedback, implementada e testada nesta sessão anterior, mas **nunca validada como funcional em hardware real**:

- `src/ForceFeedback/` (`rvwheel_ffb`): `ForceFeedbackSafetyController` (clamps, sanitização de NaN/Inf, slew-rate, watchdog, máquina de estados `Disabled/Armed/Active/Stopping/Faulted`, `EmergencyStop` instantâneo), `ForceFeedbackMixer`, `SpringDamperSource` (único efeito real implementado, não depende de telemetria), `ForceFeedbackEngine`. 37+ testes unitários, todos passando, sem hardware.
- `src/Devices/DirectInput/src/DirectInputDevice.cpp`: `ApplyConstantForce`/`ApplySpring`/`ApplyDamper`/`ApplyGain`/`StopForceFeedback` chamam `CreateEffect`/`SetParameters`/`Start`/`Stop`/`SendForceFeedbackCommand` reais do DirectInput. Erros agora incluem o HRESULT real (`FormatHresult`).
- `src/Devices/DirectInput/src/DirectInputDeviceEnumerator.cpp`: só pede `DISCL_EXCLUSIVE` quando `requestExclusiveForceFeedbackAccess=true` E o dispositivo reporta `DIDC_FORCEFEEDBACK`; caso contrário usa `DISCL_NONEXCLUSIVE`. A flag usada hoje é `DISCL_EXCLUSIVE | DISCL_BACKGROUND`.
- `tools/device_probe/`: três modos novos de CLI —
  - `--ffb-simulate`: nunca toca o dispositivo real (usa `SimulatedForceFeedbackSink`, que intercepta `ApplyForceFeedback`/`StopForceFeedback` e nunca repassa pro dispositivo real). Sempre seguro.
  - `--ffb-hw-test-stop-only`: pede acesso exclusivo real e chama `StopForceFeedback()` uma vez, sem nunca criar um efeito. Teste real, mas sem força nenhuma.
  - `--ffb-hw-test-weak-effect --effect spring|damper`: aplica UM efeito real fraco (constantes fixas no código, não configuráveis via CLI) por 5 segundos, com contagem regressiva de 3s, rampa suave via o safety controller real, e parada garantida no final.
- `docs/FORCE_FEEDBACK.md`, `docs/FORCE_FEEDBACK_HARDWARE_TEST.md` (com o log de incidentes completo), `docs/research/FORCE_FEEDBACK_FEASIBILITY.md` documentam tudo em detalhe.

### O problema concreto encontrado

Nesta sessão, **5 testes reais no G923** (VID `046D` PID `C266`) foram executados, sempre com autorização explícita do usuário e sem nenhum movimento inseguro relatado:

1. **Passo 4 (stop-only): passou limpo.** Aquisição exclusiva funcionou, input continuou legível, `StopForceFeedback()` retornou `Ok` sem nenhum efeito criado.
2. **Passo 6, tentativa 1 (spring fraco, gain=0.1): revelou 2 bugs reais do próprio RVWheel (já corrigidos)**:
   - `ApplyForceFeedback` chamava incondicionalmente `ApplyConstantForce`/`ApplyDamper` mesmo quando o valor era zero e nunca foi pedido, criando e iniciando efeitos fantasmas. Corrigido: só toca um canal se for genuinamente não-zero ou se o efeito já existir.
   - O estado interno de rampa do `ForceFeedbackSafetyController` inicializava `gain` em `1.0` (default de `ForceFeedbackCommand`) enquanto `spring`/`damper` inicializavam em `0`. Isso fazia o spring atingir o alvo rápido enquanto o gain ainda estava "cheio", aplicando força mais forte que o configurado por até ~1.6s. Corrigido: estado de rampa agora começa totalmente zerado, incluindo gain. Há um teste de regressão que prova que `spring * gain` nunca excede o valor final durante a rampa.
   - Além disso, por volta de t≈2.0s, `SetParameters`/`Stop` começaram a falhar (mensagem genérica na época).
3. **Passo 6, tentativa 2 (spring fraco, gain=0.1, com HRESULT real capturado): falha reproduzida com código exato.** `0x80040205` = **`DIERR_NOTEXCLUSIVEACQUIRED`** — o dispositivo perdeu a aquisição exclusiva, sempre por volta do mesmo tempo decorrido. `StopForceFeedback()` final também falhou pelo mesmo motivo (`Stop`/`SendForceFeedbackCommand` exigem exclusividade).
4. **Passo 6, tentativa 3 (mesmo teste, com `lghub_agent.exe` e `lghub_system_tray.exe` do G HUB encerrados): hipótese do G HUB descartada.** Falha idêntica, mesmo tempo, sem G HUB rodando (`lghub_updater.exe`, um serviço de atualização, não pôde ser encerrado e ficou rodando).
5. **Passo 6/7, tentativa 4 (gain subido pra 0.2, com log de diagnóstico no próprio `Poll()`): hipótese da reaquisição própria descartada.** Zero eventos de "input poll lost" apareceram durante a falha — a leitura de input (`GetDeviceState`) nunca falhou, só o FFB. Isso prova que **não é o próprio código de reaquisição do `Poll()` que causa isso**.
6. **Passo 7 (damper fraco, gain=0.2): achado novo, hipótese revisada.** O usuário relatou que a resistência **padrão** do volante (que existe independente de qualquer software, já que o G HUB estava fechado) **diminuiu** por ~1s enquanto nosso efeito estava ativo, e voltou ao normal quando a falha aconteceu. Como `DIPROP_FFGAIN` é uma propriedade do dispositivo inteiro (não só do nosso efeito), isso sugere que **o G923 já roda algum efeito de FFB ambiente/padrão independente do PC**, e nosso gain baixo temporariamente suprimiu esse efeito enquanto tivemos acesso exclusivo.

**Hipótese de trabalho atual, não confirmada**: o G923 (ou seu driver) pode ter um watchdog de firmware que retoma o controle/comportamento padrão de FFB, incluindo a exclusividade, se nenhum aplicativo "renovar" a atividade de FFB dentro de ~2 segundos. Isso seria uma explicação tranquilizadora (o hardware teria sua própria rede de segurança), mas não foi confirmada — não há documentação oficial do G923 nesse nível, e confirmar exigiria rastreamento USB/HID que este projeto não tem hoje.

### Uma hipótese concreta ainda não testada

A documentação oficial da Microsoft ("Cooperative Levels") e o sample oficial do DirectX SDK para force feedback (`FFConst.cpp`) usam `DISCL_EXCLUSIVE | DISCL_FOREGROUND`, **não** `DISCL_EXCLUSIVE | DISCL_BACKGROUND` (que é o que `DirectInputDeviceEnumerator.cpp` usa hoje para FFB). A janela usada por este projeto (`HiddenWindow`) é invisível e nunca é colocada em foreground. **Isso nunca foi testado** e é uma pista concreta, real, ainda em aberto — ver `docs/research/FORCE_FEEDBACK_FEASIBILITY.md`, questão aberta 4/5.

[TASK]

Investigue por que a aquisição exclusiva de force feedback é perdida após ~2 segundos, priorizando a hipótese `DISCL_FOREGROUND` antes de qualquer outra, e sem nunca perder o rigor de segurança já estabelecido nesta sessão.

### 1. Inspeção e preservação

Antes de editar:

- leia `docs/FORCE_FEEDBACK.md`, `docs/FORCE_FEEDBACK_HARDWARE_TEST.md` (log de incidentes completo) e `docs/research/FORCE_FEEDBACK_FEASIBILITY.md` na íntegra;
- inspecione `src/ForceFeedback/`, `src/Devices/DirectInput/`, e os três modos de CLI em `tools/device_probe/`;
- preserve todos os 194 testes existentes;
- não altere o comportamento padrão de input (`DISCL_NONEXCLUSIVE | DISCL_BACKGROUND`), que já funciona e coexiste com G HUB/jogo — qualquer mudança de cooperative level deve ficar restrita ao caminho de FFB exclusivo;
- não enfraqueça o safety controller (clamps, watchdog, `EmergencyStop`, estados) de forma alguma;
- não hardcode nenhuma lógica específica de G923/VID/PID no código genérico do backend DirectInput — comportamento específico de dispositivo pertence ao profile JSON.

### 2. Testar a hipótese DISCL_FOREGROUND (prioridade 1)

- Adicione um jeito de solicitar `DISCL_EXCLUSIVE | DISCL_FOREGROUND` em vez de `DISCL_EXCLUSIVE | DISCL_BACKGROUND` especificamente para o caminho de FFB (mantendo `DISCL_NONEXCLUSIVE | DISCL_BACKGROUND` como está para o caminho de input puro).
- Primeiro teste **sem criar nenhum efeito**: veja se `Acquire()` com `DISCL_FOREGROUND` sequer tem sucesso com uma janela invisível/sem foco. Se falhar imediatamente, isso já responde a pergunta (FOREGROUND exige que a janela realmente tenha foco) e indica o próximo passo: descobrir como trazer a janela do processo para foreground de forma seguraa (`SetForegroundWindow`, ou tornar a `HiddenWindow` minimamente visível/focável só durante o uso de FFB) sem perturbar a janela do jogo.
- Use o modo `--ffb-hw-test-stop-only` (já existe, seguro, sem efeito real) como base para esse experimento antes de tentar qualquer coisa com `--ffb-hw-test-weak-effect`.

### 3. Instrumentação adicional, se necessário

Se a hipótese do FOREGROUND não resolver, considere:

- chamar `IDirectInputDevice8::GetForceFeedbackState` periodicamente durante o teste fraco e logar as flags (`DIGFFS_ACTUATORSON/OFF`, `DIGFFS_SAFETYSWITCHON/OFF`, `DIGFFS_USERFFSWITCHON/OFF`, `DIGFFS_DEVICELOST`) — pode revelar o que muda pouco antes da falha;
- verificar se `lghub_updater.exe` (não pôde ser encerrado) tem algum papel, se houver forma de investigar sem depender de encerrá-lo;
- documentar qualquer nova hipótese com a mesma honestidade do log de incidentes atual — não declare causa raiz sem reprodução real.

### 4. Se e somente se a exclusividade for resolvida

Só depois de confirmar que o acesso exclusivo se mantém estável por bem mais que 2 segundos (ex.: os 5 segundos completos do teste fraco, sem `DIERR_NOTEXCLUSIVEACQUIRED`), considere avançar para:

- telemetria do veículo: `docs/research/FORCE_FEEDBACK_FEASIBILITY.md` §4 documenta que **somente steering e marcha são confirmados acessíveis via Lua** hoje; velocidade/suspensão/slip nunca foram consultados no jogo real. Isso exigiria uma sondagem de reflection pontual e única (não por tick — este projeto já teve instabilidade real com reflection pesada) contra o jogo rodando, o que exige autorização explícita do usuário para abrir o jogo;
- conectar o `ForceFeedbackEngine` ao loop real do `--bridge` (hoje só é alcançável pelos modos de diagnóstico) — só depois de tudo acima estar validado, nunca antes.

Não é obrigatório chegar até aqui nesta tarefa. Resolver ou entender melhor a causa raiz do `DIERR_NOTEXCLUSIVEACQUIRED` já é o objetivo principal.

[CONSTRAINTS]

- C++20, Windows x64, MSVC, CMake target-based.
- **Nenhuma força real deve ser enviada ao hardware sem autorização explícita do usuário, a cada tentativa** — mesma regra desta sessão. Prefira sempre o modo mais seguro disponível (`--ffb-hw-test-stop-only` antes de `--ffb-hw-test-weak-effect`; `--ffb-simulate` para qualquer teste de lógica que não precise de hardware real).
- Qualquer novo teste físico deve seguir o protocolo gated em `docs/FORCE_FEEDBACK_HARDWARE_TEST.md`: volante fixado, mãos livres, cabo USB acessível para desconectar, confirmação do usuário antes e depois de cada tentativa.
- Não assuma que uma força baixa é automaticamente segura. Não teste ganho alto sem antes validar ganho baixo repetidamente.
- Não declare o problema "resolvido" sem reprodução real mostrando a falha desaparecendo por pelo menos 2-3 execuções consecutivas.
- Preserve os 194 testes existentes; adicione novos testes para qualquer lógica pura nova (ex.: seleção de cooperative level).
- Não faça `git push` sem apresentar os resultados e receber autorização explícita.
- Código, comentários e mensagens técnicas no código em inglês; a resposta final ao usuário em português.

[OUTPUT]

Ao concluir (ou ao pausar por precisar de decisão do usuário), responda com:

1. o que foi tentado e por quê;
2. se a hipótese `DISCL_FOREGROUND` foi confirmada, refutada, ou ficou inconclusiva, com os dados exatos (HRESULT, tempo decorrido, contagem de execuções);
3. arquivos alterados;
4. testes novos e resultado real de build/test (Debug e Release, contagem total, warnings);
5. se algum teste físico foi realizado: comando exato, resultado real, confirmação de segurança do usuário — nunca declare um teste físico como feito sem tê-lo executado de fato;
6. atualização honesta de `docs/FORCE_FEEDBACK_HARDWARE_TEST.md` (log de incidentes) e `docs/research/FORCE_FEEDBACK_FEASIBILITY.md`;
7. limitações remanescentes e próximo passo recomendado;
8. pergunta explícita pedindo autorização antes de qualquer novo teste físico, se ainda houver algo pendente.

[ACCEPTANCE GATE]

Esta etapa estará concluída quando:

- a hipótese `DISCL_FOREGROUND` tiver sido genuinamente testada (não apenas discutida) e o resultado registrado com dados reais;
- todos os testes anteriores continuarem passando, mais quaisquer novos;
- Debug e Release compilarem sem warnings novos;
- o comportamento de input não-exclusivo (`DISCL_NONEXCLUSIVE | DISCL_BACKGROUND`) permanecer exatamente como está;
- nenhuma força real tiver sido enviada sem autorização explícita do usuário em cada ocasião;
- `docs/FORCE_FEEDBACK_HARDWARE_TEST.md` refletir com precisão cada tentativa real feita, incluindo falhas;
- não existir lógica específica de G923 na implementação genérica do backend;
- o usuário tiver recebido um relatório claro do que foi descoberto e uma pergunta explícita sobre os próximos passos.
