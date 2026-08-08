# Prompt 002 — Build, Test and Stabilization Gate

## Prompt para o agente de codificação

[ROLE]

Você é um engenheiro sênior de C++/Windows responsável pela etapa de estabilização da Layer 2 (DAL) do RVWheel. Sua tarefa não é adicionar features: é transformar a implementação existente em uma entrega comprovadamente configurável, compilável e testada no ambiente Windows disponível.

[CONTEXT]

A Layer 2 já foi implementada no repositório. A resposta anterior declarou corretamente que não havia sido compilada no ambiente original. O ambiente Windows agora foi preparado e verificado por outro agente.

Ferramentas confirmadas nesta máquina:

```text
Repository:      <RVWHEEL_REPOSITORY_ROOT>
Git:             2.53.0.windows.2
CMake:           4.4.2
Build Tools:     Visual Studio 2022 Build Tools
MSBuild:         17.14.51.32402
MSVC toolset:    14.44.35207
C++ compiler:    MSVC 19.44.35228.0
Windows SDK:     10.0.26100.0
vcpkg root:      %VCPKG_ROOT%
Catch2 config:   %VCPKG_ROOT%\installed\x64-windows\share\catch2\Catch2Config.cmake
```

O compilador, linker e Windows SDK funcionam. Um programa C++ mínimo gerado pelo CMake foi compilado com sucesso. A configuração completa com `NMake Makefiles`, Logitech desabilitado e testes habilitados também terminou com sucesso em aproximadamente 2 segundos.

A build real avançou por vários arquivos e encontrou o primeiro erro de código em:

```text
src/Devices/DirectInput/src/DirectInputDeviceEnumerator.cpp:135
error C2039: 'DeviceCapabilitiesA': is not a member of 'rvwheel::dal'
```

A linha original é equivalente a:

```cpp
dal::DeviceCapabilities capabilities{};
```

O Windows SDK define `DeviceCapabilities` como macro ANSI/Unicode. Macros são expandidas mesmo quando o token está qualificado por namespace, portanto o preprocessador transforma o identificador em `dal::DeviceCapabilitiesA`. Isso é uma colisão real entre a API pública da DAL e Win32, não uma falha de instalação.

Também foram observados três headers públicos que devem ser verificados quanto a dependências diretas:

- `DeviceId.hpp` usa `std::size_t` e deve incluir `<cstddef>` diretamente;
- `Diagnostics.hpp` usa `std::uint8_t` e deve incluir `<cstdint>` diretamente;
- `Status.hpp` usa `std::uint8_t` e deve incluir `<cstdint>` diretamente.

[TASK]

Inspecione o estado atual do repositório, corrija todos os problemas de compilação e testes encontrados e valide a entrega de ponta a ponta. Trabalhe iterativamente: configure, compile, leia o primeiro erro acionável, corrija a causa raiz e repita até obter build e testes verdes.

### 1. Preservação e inspeção inicial

Antes de editar:

- execute `git status --short` e inspecione o diff existente;
- trate todos os arquivos e mudanças atuais como trabalho do usuário/agente anterior;
- não use `git reset`, `git checkout --`, limpeza destrutiva ou reescrita ampla;
- não apague diretórios de build que você não criou;
- não faça buscas recursivas em todo o disco: os paths de ferramentas estão fornecidos acima;
- não reimplemente a DAL do zero.

### 2. Corrigir a colisão Win32 conhecida

Renomeie o tipo público `DeviceCapabilities` para um nome que não colida com Win32, preferencialmente:

```text
WheelDeviceCapabilities
```

Atualize consistentemente:

- declaração do tipo;
- `DeviceInfo`;
- backends DirectInput e Logitech;
- enumeradores;
- fakes e testes;
- documentação e exemplos que mencionem o símbolo.

Não use `#undef DeviceCapabilities` como solução permanente e não esconda a falha atrás de ordem de includes. A API pública deve poder coexistir com `<windows.h>` em qualquer ordem razoável.

Adicione um smoke check de compilação que inclua `<windows.h>` antes dos headers públicos da DAL e use o novo tipo. Esse check pode ser uma pequena translation unit adicionada ao target de testes; seu propósito é impedir regressão da colisão com macros Win32.

### 3. Autossuficiência dos headers

Garanta que todo header público inclua diretamente os headers padrão dos símbolos que utiliza. Corrija pelo menos os três casos já identificados (`<cstddef>` e `<cstdint>`), mas faça uma revisão objetiva dos demais headers públicos.

Não dependa de includes transitivos de STL ou Windows. Não use um header agregador apenas para mascarar dependências ausentes.

### 4. Configuração do ambiente de build

Prefira inicialmente o gerador Visual Studio:

```powershell
$toolchain = "$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

cmake -S . -B build-claude-validation `
  -G "Visual Studio 17 2022" `
  -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
  -DRVWHEEL_ENABLE_LOGITECH_SDK=OFF `
  -DRVWHEEL_BUILD_TESTS=ON
```

Em alguns executores sandboxados, o gerador Visual Studio pode ficar parado após imprimir apenas a seleção do Windows SDK. Se não houver progresso por aproximadamente 60 segundos, não faça buscas globais nem espere indefinidamente. Use o fallback já comprovado:

1. inicialize o ambiente com:

```cmd
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64
```

2. na mesma sessão `cmd`, configure com:

```cmd
"C:\Program Files\CMake\bin\cmake.exe" -S "%CD%" -B "%CD%\build-claude-nmake" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DRVWHEEL_ENABLE_LOGITECH_SDK=OFF -DRVWHEEL_BUILD_TESTS=ON
```

Para NMake, não use `--parallel`; NMake ignora essa opção e emite warning. Se precisar criar um `.cmd` temporário para manter `VsDevCmd` e CMake na mesma sessão, remova-o ao finalizar.

Use um diretório de build novo criado por você. Não reutilize caches parcialmente configurados de tentativas anteriores.

### 5. Ciclo obrigatório de compilação e correção

Execute build Debug completo. Corrija todos os erros e warnings acionáveis introduzidos pelo projeto, um por vez, pela causa raiz.

Restrições durante correções:

- não reduza `/W4` ou `/permissive-`;
- não desabilite arquivos problemáticos no CMake;
- não comente implementações ou testes para fazer a build passar;
- não transforme erros em casts cegos ou `#pragma warning(disable)` sem justificativa técnica;
- preserve os limites arquiteturais: API pública sem tipos DirectInput/Logitech, DAL sem UE4SS/Unreal/Lua/UI;
- não implemente o adapter proprietário Logitech sem os headers reais;
- mantenha `RVWHEEL_ENABLE_LOGITECH_SDK=OFF` na validação principal;
- não altere comportamento apenas para satisfazer um teste incorreto; valide contrato e teste juntos.

Após Debug verde, faça uma configuração/build Release independente ou, se estiver usando gerador multi-config, compile explicitamente Release.

### 6. Descoberta e execução de testes

Não aceite simplesmente “build passou”. Confirme que Catch2 foi encontrado e que os testes foram registrados:

```powershell
ctest --test-dir build-claude-validation -C Debug -N
```

ou, para NMake:

```cmd
"C:\Program Files\CMake\bin\ctest.exe" --test-dir "%CD%\build-claude-nmake" -N
```

O resultado não pode ser `Total Tests: 0`. Verifique que há cobertura registrada para:

- normalização de steering e pedais;
- ranges simétricos, assimétricos, invertidos e degenerados;
- clamp e valores extremos sem overflow/NaN/infinito;
- refresh de cinco segundos do `DeviceManager` com fake clock;
- preservação de instância por `DeviceId`;
- deduplicação DirectInput/Logitech;
- comportamento do `LogitechDevice` via fake SDK;
- smoke check de headers públicos junto de `<windows.h>`.

Execute:

```powershell
ctest --test-dir build-claude-validation -C Debug --output-on-failure
ctest --test-dir build-claude-validation -C Release --output-on-failure
```

Adapte apenas `-C` para gerador single-config. Em NMake, Debug e Release exigem diretórios de build separados ou uma reconfiguração explícita; prefira diretórios separados para não misturar artefatos.

Se um teste falhar, reproduza o teste individual com output detalhado, corrija e execute a suíte completa novamente.

### 7. Validações adicionais obrigatórias

Confirme ainda:

- o artefato `rvwheel_dal` é uma biblioteca estática `.lib`;
- o build principal não inclui nem exige headers/libs do Logitech SDK quando a opção está `OFF`;
- não há símbolos ou tipos proprietários em headers públicos da DAL;
- todos os arquivos listados no CMake existem e todos os fontes relevantes são realmente compilados;
- os testes não acessam hardware real e são determinísticos;
- o projeto não depende de rede após Catch2 já estar instalado pelo vcpkg;
- não existe código temporário, arquivo `.cmd` de diagnóstico ou path absoluto novo commitado ao repositório;
- `git diff --check` não reporta whitespace errors.

Faça também uma configuração com testes desabilitados para confirmar que consumidores conseguem compilar apenas a biblioteca:

```text
RVWHEEL_ENABLE_LOGITECH_SDK=OFF
RVWHEEL_BUILD_TESTS=OFF
```

Não é obrigatório habilitar Logitech nesta tarefa. Sem o SDK proprietário real, apenas confirme que o modo `OFF` está limpo e documente que o modo `ON` não foi validado.

[CONSTRAINTS]

- Escopo exclusivo: build, testes, correções de compilação e pequenas correções necessárias para satisfazer contratos existentes.
- Não adicionar novas features, Layer 3, Layer 4, UE4SS, perfis, overlay ou instalador de mod.
- Não inventar hardware disponível, resultado de FFB ou validação Logitech.
- Não instalar dependências adicionais sem necessidade comprovada.
- Não declarar sucesso com testes não descobertos, pulados ou não executados.
- Não esconder erros de compilação com exclusão de fontes, macros globais perigosas ou redução de warnings.
- Código, comentários e nomes de símbolos em inglês.
- Preserve compatibilidade Windows x64 e C++20.

[OUTPUT]

Ao terminar, responda em português com evidências reais:

1. arquivos alterados e motivo de cada alteração;
2. causa raiz de cada erro relevante encontrado;
3. comandos exatos realmente executados;
4. configuração utilizada: gerador, MSVC, Windows SDK, Debug/Release e flags Logitech/testes;
5. resultado do build Debug e Release;
6. saída resumida de `ctest -N`, incluindo número de testes descobertos;
7. saída resumida de CTest Debug e Release: total, aprovados e falhos;
8. path do `.lib` gerado;
9. limitações que não puderam ser verificadas, especialmente hardware e SDK Logitech;
10. `git status --short` final, distinguindo código da implementação e artefatos de build não versionados.

Se algo continuar falhando, não responda “implementação completa”. Informe o primeiro bloqueio reproduzível, inclua comando, arquivo, linha e mensagem de erro e proponha a correção seguinte.

[ACCEPTANCE GATE]

A tarefa só está concluída quando todos os itens abaixo forem verdadeiros:

- configuração CMake concluída com MSVC x64 e Logitech OFF;
- `rvwheel_dal` compilada integralmente em Debug e Release;
- Catch2 encontrado e mais de zero testes registrados;
- todos os testes Debug e Release aprovados;
- colisão `DeviceCapabilities`/`DeviceCapabilitiesA` eliminada pela renomeação robusta da API;
- smoke check Win32 compila;
- headers públicos usam dependências diretas;
- build sem testes também compila;
- nenhum resultado de hardware/Logitech é inventado;
- relatório final contém resultados reais, não inferências.
