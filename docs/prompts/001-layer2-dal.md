# Prompt 001 — Layer 2: Device Abstraction Layer (DAL)

## Prompt para o agente de codificação

[ROLE]

Você é um engenheiro de software sênior especializado em C++20, Windows Input, DirectInput 8, Logitech Gaming SDK, APIs COM, arquitetura orientada a interfaces, CMake e testes unitários. Você está contribuindo para o RVWheel, um mod open-source sob licença MIT que adiciona suporte profissional a volantes no jogo *RV There Yet?* sem modificar o executável do jogo.

[CONTEXT]

O RVWheel será organizado em quatro camadas desacopladas:

1. Device Drivers/APIs: DirectInput, Logitech Gaming SDK e, futuramente, HID/SDL2.
2. Device Abstraction Layer (escopo desta tarefa): interface uniforme, enumeração, ciclo de vida, normalização e hot-plug.
3. Input Mapper/Profiles: deadzones, curvas, sensibilidade, inversões e mapeamentos configuráveis.
4. Game Integration: hooks UE4SS e aplicação de inputs no jogo.

A DAL não pode depender de UE4SS, Unreal Engine, Lua, UI, perfis JSON ou lógica específica do jogo. Ela será consumida posteriormente pela integração UE4SS. O alvo atual é Windows 64-bit, C++20 e biblioteca estática. O caminho de execução por frame deve ser previsível, sem alocações desnecessárias e com impacto muito inferior a 1 ms. A enumeração de hardware pode ser mais cara, mas deve ocorrer no máximo uma vez a cada cinco segundos.

O MVP deve suportar:

- volantes genéricos via DirectInput 8;
- Logitech G29/G920 via Logitech Gaming SDK quando o SDK estiver disponível;
- volante, acelerador, freio e embreagem como eixos independentes;
- botões e POV/hats;
- comandos básicos de force feedback;
- conexão e desconexão detectadas sem reiniciar o processo.

O Logitech Gaming SDK é proprietário e não deve ser baixado, copiado ou redistribuído pelo projeto. A integração Logitech deve ser opcional no build e isolada atrás de uma abstração testável. O projeto deve continuar compilando e funcionando somente com DirectInput quando o SDK não estiver instalado.

[TASK]

Implemente a primeira versão da Device Abstraction Layer do RVWheel como uma biblioteca estática chamada `rvwheel_dal`.

Antes de editar, inspecione o repositório e preserve arquivos, convenções e mudanças existentes. Se a árvore ainda não existir, crie somente a estrutura necessária para esta tarefa. Não implemente Layers 3 ou 4 e não adicione código UE4SS.

### 1. Contratos públicos da DAL

Crie uma interface pura virtual `IWheelDevice`, com destrutor virtual e sem expor tipos DirectInput ou Logitech na API pública. Ela deve oferecer, no mínimo:

- metadados imutáveis do dispositivo (`DeviceInfo`): identificador estável, nome, fabricante, backend, VID/PID quando conhecidos e capacidades;
- estado de conexão;
- `Poll()` para atualizar um snapshot completo do dispositivo;
- acesso ao último `WheelState` normalizado;
- aplicação e interrupção de force feedback;
- ausência de cópia; ownership e tempo de vida explícitos.

Modele os tipos públicos necessários, preferencialmente em headers pequenos:

- `DeviceBackend`: `DirectInput` e `Logitech`;
- `DeviceId`: valor opaco e comparável, estável entre refreshes na mesma máquina sempre que a API permitir;
- `DeviceCapabilities`: presença de steering, throttle, brake, clutch, FFB, número de botões e POVs;
- `WheelState`: steering em `[-1.0f, 1.0f]`; throttle, brake e clutch em `[0.0f, 1.0f]`; botões em representação sem alocação por frame; POVs em formato documentado; timestamp ou contador de amostra; flag de validade/conexão;
- `ForceFeedbackCommand`: pelo menos força constante normalizada em `[-1.0f, 1.0f]`, spring, damper e ganho global em ranges documentados. Se um efeito não for suportado pelo backend, retorne um resultado explícito em vez de falhar silenciosamente;
- um tipo de resultado/status pequeno e claro para diferenciar sucesso, dispositivo desconectado, recurso não suportado e erro de backend.

Você pode ajustar nomes e assinaturas se justificar a melhoria no documento de implementação, mas preserve a semântica acima. Não lance exceções através da fronteira pública durante `Poll()` ou FFB; converta falhas dos SDKs em status e logs/callback de diagnóstico injetável.

### 2. Normalização de eixos

Implemente a normalização em componente puro e independente de hardware (`AxisNormalizer` ou equivalente), para que seja testável sem um volante conectado.

Regras obrigatórias:

- steering: mapear `[rawMin, rawCenter]` para `[-1, 0]` e `[rawCenter, rawMax]` para `[0, 1]`, suportando ranges assimétricos;
- pedais: mapear `[rawReleased, rawPressed]` para `[0, 1]`, inclusive quando o eixo físico estiver invertido (`rawPressed < rawReleased`);
- sempre aplicar clamp ao range de saída;
- tratar ranges inválidos/degenerados sem divisão por zero e retornar estado de erro determinístico;
- não aplicar deadzone, curva de sensibilidade, linearidade ou smoothing: essas responsabilidades pertencem à Layer 3;
- não depender de constantes fixas como `0..65535`; cada eixo deve carregar/calcular sua calibração a partir dos metadados do dispositivo, com defaults explícitos apenas quando a API não fornecer dados.

Evite `NaN`, infinito e comportamento indefinido. Documente as fórmulas e a política para valor central exato.

### 3. Backend DirectInput 8

Implemente `DirectInputDevice` e os componentes internos necessários usando DirectInput 8:

- encapsule objetos COM/DirectInput com RAII e liberação correta;
- enumere apenas game controllers relevantes e filtre dispositivos sem eixos úteis;
- configure data format e cooperative level de forma explícita; receba `HINSTANCE` e `HWND` através de um contexto de inicialização, sem buscar globals;
- adquira/readquira o dispositivo quando necessário;
- trate `DIERR_INPUTLOST`, `DIERR_NOTACQUIRED`, desconexão e falhas sem crash;
- leia eixos, botões e POVs em um único poll quando possível;
- descubra ranges/capacidades via propriedades do DirectInput e converta-os pelos normalizadores puros;
- mantenha mapeamento de eixos separado da matemática de normalização. Não esconda inversão/curvas de perfil dentro do backend;
- exponha force feedback através dos contratos genéricos da DAL e gerencie efeitos DirectInput com RAII. Atualize efeitos existentes quando possível, em vez de recriá-los a cada frame;
- documente a escolha de cooperative level e quaisquer implicações para execução em background.

Não use XInput como substituto. Não faça hooks, injeção de DLL ou acesso à memória do jogo nesta tarefa.

### 4. Backend Logitech

Implemente `LogitechDevice` usando o Logitech Gaming SDK, respeitando estas regras:

- compile somente quando `RVWHEEL_ENABLE_LOGITECH_SDK=ON`;
- não faça commit de headers, DLLs ou bibliotecas proprietárias;
- adicione variáveis CMake cache documentadas para include directory, import library e, quando aplicável, runtime DLL;
- quando a opção estiver `OFF`, não inclua headers Logitech e não deixe símbolos não resolvidos;
- isole chamadas do fornecedor atrás de `ILogitechSdk`/adapter equivalente, permitindo fake em testes e evitando contaminar `IWheelDevice` com tipos proprietários;
- inicialização e shutdown do SDK devem ter ownership único e ordem determinística;
- enumere índices/dispositivos reportados pelo SDK, leia o estado, normalize e traduza FFB para os contratos comuns;
- reporte efeitos não suportados explicitamente;
- não presuma assinaturas ou arquivos do SDK: use somente a API realmente presente nos headers configurados pelo usuário. Se houver variações entre versões, documente a versão suportada e mantenha o adapter como ponto único de compatibilidade.

### 5. `DeviceManager` e hot-plug

Implemente um `DeviceManager` responsável por inicialização, enumeração e ownership dos dispositivos:

- aceite dependências/contexto por construtor ou factory; evite singletons;
- use `std::chrono::steady_clock`;
- ofereça `Update()`/`RefreshIfDue()` não bloqueante no fluxo normal;
- enumere novamente os backends a cada cinco segundos, com intervalo configurável para testes e default de cinco segundos;
- preserve instâncias de dispositivos ainda conectados para que referências/IDs não mudem a cada refresh;
- marque/remova dispositivos desconectados de forma segura e documente a política de validade dos handles retornados;
- não crie thread interna nesta versão; a integração futura chamará `Update()` no game thread;
- permita injetar clock e enumeradores/factories para testes determinísticos;
- forneça acesso read-only à coleção de dispositivos sem transferir ownership indevidamente;
- deduplicate dispositivos vistos simultaneamente pelo Logitech SDK e pelo DirectInput. Prefira o backend Logitech para hardware Logitech suportado quando ele estiver habilitado e utilizável; use DirectInput como fallback. Baseie a deduplicação em identificadores/VID/PID/GUID disponíveis, nunca apenas no nome de exibição, e documente limitações.

O polling de estado por frame e o refresh de hardware são operações diferentes. Não reenumere a cada `Poll()`.

### 6. Estrutura de diretórios sugerida

Adapte a estrutura se o repositório já tiver convenções equivalentes:

```text
RVWheel/
├── CMakeLists.txt
├── cmake/
│   └── FindLogitechSteeringWheelSDK.cmake
├── src/
│   ├── Core/
│   │   ├── include/rvwheel/dal/
│   │   │   ├── IWheelDevice.hpp
│   │   │   ├── WheelTypes.hpp
│   │   │   ├── AxisNormalizer.hpp
│   │   │   └── DeviceManager.hpp
│   │   └── src/
│   │       ├── AxisNormalizer.cpp
│   │       └── DeviceManager.cpp
│   └── Devices/
│       ├── DirectInput/
│       │   ├── include/rvwheel/devices/DirectInputDevice.hpp
│       │   └── src/DirectInputDevice.cpp
│       └── Logitech/
│           ├── include/rvwheel/devices/LogitechDevice.hpp
│           └── src/LogitechDevice.cpp
└── tests/
    └── unit/
        ├── CMakeLists.txt
        ├── AxisNormalizerTests.cpp
        └── DeviceManagerTests.cpp
```

Headers específicos de backend podem ser privados se não forem necessários para consumidores. Prefira manter somente os contratos DAL em includes públicos.

### 7. CMake e portabilidade do build

Forneça CMake moderno baseado em targets:

- versão mínima de CMake justificada;
- target estático `rvwheel_dal`;
- C++20 via `target_compile_features`;
- include paths por `BUILD_INTERFACE`/`INSTALL_INTERFACE`;
- warnings rigorosos para MSVC sem promover warnings de SDKs externos a erros;
- link privado de `dinput8`, `dxguid` e outras bibliotecas Windows estritamente necessárias;
- `RVWHEEL_ENABLE_LOGITECH_SDK` default `OFF`;
- `RVWHEEL_BUILD_TESTS` default `ON` apenas no projeto raiz ou conforme padrão existente;
- mensagem de configuração clara quando o SDK Logitech for solicitado mas não encontrado;
- nenhum path absoluto, nome de usuário ou localização de SDK hardcoded;
- build DirectInput funcional sem Logitech;
- falha clara em plataforma não Windows para os backends atuais, mantendo tipos puros/normalizador com o mínimo de acoplamento possível.

Use `find_package(Catch2 3 CONFIG)` para testes. Se optar por `FetchContent`, torne o download opt-in ou preserve uma rota de build offline; não esconda dependência de rede na configuração padrão.

### 8. Testes unitários Catch2

Implemente testes de normalização sem acessar hardware. Cubra ao menos:

- steering nos valores mínimo, centro e máximo;
- steering em range simétrico e assimétrico;
- range DirectInput comum `0..65535` com centro coerente;
- range assinado `-32768..32767`;
- clamp abaixo e acima dos limites;
- pedal normal e invertido;
- pedal solto e totalmente pressionado;
- valores intermediários com tolerância de ponto flutuante;
- ranges degenerados (`min == center`, `center == max`, `released == pressed`);
- garantia de que nenhum caso inválido retorna `NaN` ou infinito.

Inclua também testes pequenos para o intervalo de refresh do `DeviceManager` usando fake clock/enumerator: refresh inicial, nenhuma reenumeração antes de cinco segundos, reenumeração ao atingir o prazo e preservação de instância para o mesmo `DeviceId`.

### 9. Exemplo de uso esperado

Inclua no README técnico ou comentário de exemplo um fluxo equivalente a este pseudocódigo, ajustado à API final:

```cpp
DeviceManager manager({
    .instance = moduleInstance,
    .window = gameWindow,
    .refreshInterval = std::chrono::seconds{5}
});

if (auto status = manager.Initialize(); !status) {
    Report(status);
}

// Chamado no loop de integração; não enumera hardware em todo frame.
manager.Update();

for (const auto& device : manager.Devices()) {
    if (device->Poll()) {
        const WheelState& state = device->State();
        ConsumeNormalizedInput(
            state.steering,
            state.throttle,
            state.brake,
            state.clutch,
            state.buttons);

        device->ApplyForceFeedback({
            .constantForce = ComputeForce(),
            .spring = 0.25f,
            .damper = 0.10f,
            .gain = 0.80f
        });
    }
}
```

O exemplo deve ilustrar o contrato, não introduzir dependências da integração do jogo na DAL.

[CONSTRAINTS]

- C++20, Windows x64, MSVC como toolchain primária.
- Biblioteca estática; nenhuma DLL de injeção nesta tarefa.
- API pública independente de DirectInput, Logitech, UE4SS e Unreal.
- RAII para COM, handles, efeitos e ciclo de vida dos SDKs.
- Sem globals mutáveis, singletons obrigatórios ou threads destacadas.
- Sem alocações, locks ou enumeração de hardware desnecessários no caminho de `Poll()`.
- Sem valores de calibração ou bindings de modelo espalhados/hardcoded.
- Sem deadzones, curvas, perfis, UI, Lua ou hooks do jogo.
- Não redistribuir componentes do Logitech SDK.
- Compatibilidade com builds sem Logitech obrigatória.
- Código e comentários em inglês; documentação de usuário pode ser em inglês.
- Formatação consistente, nomes expressivos, includes mínimos e nenhuma função monolítica.
- Preserve mudanças existentes no repositório; não reformate arquivos alheios à tarefa.

[OUTPUT]

Entregue uma implementação completa e compilável, não apenas snippets. Ao terminar, responda com:

1. resumo dos arquivos criados/alterados;
2. decisões arquiteturais importantes e respectivos trade-offs;
3. comandos exatos para configurar, compilar e testar sem Logitech;
4. comandos/variáveis para compilar com o SDK Logitech fornecido localmente pelo usuário;
5. resultados reais de build e testes executados, distinguindo claramente o que não pôde ser validado por ausência de Windows SDK, Catch2, hardware ou Logitech SDK;
6. limitações conhecidas e próximos passos estritamente relacionados à DAL.

Não afirme que compilou, testou hardware ou validou o SDK se isso não ocorreu. Não invente GUIDs, VID/PIDs, nomes de bibliotecas ou assinaturas do Logitech SDK.

[TESTS]

Todos os testes Catch2 listados devem ser descobertos pelo CTest. Execute, quando o ambiente permitir:

```text
cmake -S . -B build -DRVWHEEL_ENABLE_LOGITECH_SDK=OFF -DRVWHEEL_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Critérios de aceitação:

- `rvwheel_dal` compila como biblioteca estática no Windows x64 sem o Logitech SDK;
- nenhum header proprietário é necessário quando a integração Logitech está desabilitada;
- implementações concretas satisfazem `IWheelDevice` sem tipos de fornecedor na interface;
- normalização respeita exatamente os ranges definidos e passa todos os testes, inclusive inversão, clamp e degeneração;
- `DeviceManager` não reenumera antes do intervalo configurado, preserva dispositivos existentes e detecta adições/remoções no refresh;
- o mesmo dispositivo Logitech não aparece duplicado pelos dois backends quando há informação suficiente para deduplicação;
- desconexão, reacquire e FFB não suportado retornam status explícito, sem crash;
- não há dependência de UE4SS, Unreal, Lua, JSON ou lógica específica do jogo nesta entrega.
