# Space Racer

Ferrari 296 GT3 com física de simulador, rodando no navegador. Por enquanto não tem pista, só um chão de asfalto infinito.

## Rodar

```bash
npm start          # ou: python3 -m http.server 8080
```
Abra http://localhost:8080. Precisa de um servidor local, porque abrir o `index.html` direto (`file://`) bloqueia o carregamento dos módulos e do GLB. Three.js, N8AO e postprocessing vêm da CDN jsDelivr, então é preciso internet.

`npm test` roda os testes da física no Node, sem navegador (aceleração, frenagem, aderência lateral, estabilidade).

## Telas

- **Garagem (tela inicial):** estúdio branco infinito com piso de porcelanato polido. O reflexo do carro no piso é real (reflexo planar). Embaixo fica a lista de carros na horizontal: clique num card para carregar o carro, arraste para girá-lo e use **Pilotar** (ou Enter) para entrar na pista. Quando os carros não cabem, a lista rola: arraste-a para os lados, use a rodinha do mouse em cima dela ou as setas/direcional (ela acompanha o carro selecionado). As bordas com mais carros somem num degradê, mostrando um pedaço do próximo.
- **Pista:** **Esc** volta para a garagem. O mundo só é carregado uma vez; da segunda vez em diante a entrada é instantânea.

Para adicionar um carro, inclua uma entrada em `js/cars.js` com o GLB, a miniatura (`assets/cars/`), o spec de física (`js/physics/carSpec.js`) e o bloco `visual`, que diz quais materiais do GLB são pneus, faróis, lanternas, discos, volante e vidros. A escala e a orientação do modelo são corrigidas automaticamente pelo entre-eixos real.

## Gráficos

- **Iluminação:** céu HDRI real de 4K (`assets/hdri`). O sol direcional é posicionado automaticamente onde o sol está na foto, e os reflexos vêm do próprio céu (image-based lighting).
- **Asfalto:** texturas PBR escaneadas (cor, normal e rugosidade), misturadas em duas escalas com ruído para não aparecer repetição.
- **Pós-processamento:** MSAA, oclusão de ambiente N8AO, bloom HDR, tone mapping ACES, gradação de cor, vinheta, aberração cromática, blur radial em alta velocidade e grão de filme.
- **Detalhes do carro:** pintura com verniz (clearcoat), lanternas que acendem ao frear, discos que ficam incandescentes acima de ~450 °C (a física calcula a temperatura), volante girando com o esterço, fumaça e marcas de pneu.
- **Câmeras:** no cockpit a cabeça do piloto é jogada pelas forças G; a câmera de perseguição vibra em alta velocidade.
- **Qualidade:** tecla **G** alterna entre Baixo, Médio, Alto e Ultra. O jogo começa no Alto e baixa sozinho se o fps ficar abaixo de ~42.

## Física (`js/physics/`, JS puro e sem dependências)

| Parte | Modelo |
|---|---|
| Carroceria | Corpo rígido 6DOF, passo fixo de 1000 Hz, tensor de inércia, efeito giroscópico |
| Suspensão | Raycast por roda, mola + amortecedor com bump/rebound separados, batente, barras estabilizadoras |
| Pneus | Pacejka Magic Formula, escorregamento combinado (elipse de atrito), sensibilidade à carga, integração semi-implícita da rotação da roda |
| Motor | V6 biturbo com curva de torque, atraso do turbo, freio-motor, limitador, marcha lenta |
| Transmissão | Embreagem resolvida implicitamente (com patinação), sequencial de 6 marchas com corte e blip automático, proteção contra sobregiro, diferencial autoblocante |
| Aerodinâmica | Arrasto e downforce dividido entre os eixos |
| Auxílios | ABS e TC por escorregamento (desligáveis), câmbio automático/manual |

Os parâmetros do carro ficam todos em `js/physics/carSpec.js`. O entre-eixos, as bitolas e os raios das rodas são medidos do próprio modelo 3D ao carregar.

Para adicionar uma pista depois, troque o `FlatGround` por um objeto com os métodos `raycast(origem, direção, distMax)` e `heightAt(x, z)`. O campo `mu` permite definir a aderência por superfície (zebra, grama).

Pelo console do navegador dá para mexer ao vivo, por exemplo: `game.car.spec.tire.mu = 1.4`.

## Versão nativa (C++)

A pasta `cpp/` tem o jogo inteiro em C++20 com OpenGL 4.1 e SDL3, rodando numa janela própria. Ele usa os mesmos assets da raiz (`assets/`, os GLB dos carros) e não precisa de navegador nem de internet para jogar.

```bash
cpp/build.sh       # ou: npm run native
./space_racer
```

No Ubuntu/Debian, instale antes os pacotes de som (sem eles o jogo compila e abre, mas fica mudo):

```bash
sudo apt install libasound2-dev libpulse-dev
```

O `build.sh` usa o CMake do sistema e, se não houver, baixa um CMake portátil para `cpp/.tools`. O SDL3 vem do sistema (`libsdl3-dev`) ou é baixado pelo CMake. Na primeira compilação é preciso internet. `./sr_physics_test` roda os mesmos testes de `npm test` e dá os mesmos números da versão JS. **F11** alterna a tela cheia. O resto dos controles é igual.

**Controles de videogame:** Xbox, PlayStation e controles genéricos funcionam no jogo todo, e os ícones das dicas trocam sozinhos para o aparelho em uso. Nos menus: direcional ou analógico para escolher, A/✕ para confirmar, B/○ para voltar e LB/RB (L1/R1) para trocar de aba. Na pista: RT/LT acelera e freia, o analógico esterça, A/✕ é o freio de mão, RB/LB troca de marcha, Y/△ troca a câmera, Start/Options reposiciona o carro, ↑ mostra a ajuda e View/Share volta para a garagem.

Na garagem, a aba **Configurações** (ao lado de Garagem) ajusta qualidade (Baixo a Ultra), resolução, anti-serrilhado, oclusão de ambiente, sombras, bloom, efeitos de lente, desfoque de velocidade, qualidade automática, modo de exibição (tela cheia ou janela), tamanho da janela (1280×720, 1600×900, 1920×1080 ou maximizada), VSync e volume. Em janela também dá para arrastar a borda e deixar do tamanho que quiser (mínimo 480×270); o HUD e os menus escalam junto com a janela, sempre na mesma proporção da tela; o jogo lembra o modo, o tamanho e a posição da janela na próxima vez que abrir. As mudanças aparecem na hora e ficam salvas em `~/.local/share/SpaceRacer/SpaceRacer/settings.ini`.

| JS | C++ |
|---|---|
| `js/physics/*` | `cpp/src/physics/*`, port linha a linha em `double`, cerca de 12× mais rápido |
| Three.js (PBR, sombras, PMREM) | `cpp/src/render/renderer.cpp` e `environment.cpp`, com as mesmas fórmulas do `MeshPhysicalMaterial` |
| EffectComposer, N8AO, bloom, Bokeh | `cpp/src/render/postfx.cpp` |
| GLTFLoader | `cpp/src/core/gltf_loader.cpp` (cgltf, texturas decodificadas em paralelo) |
| HTML/CSS (garagem, HUD, cinemática) | `cpp/src/ui/ui.cpp`, `cpp/src/main.cpp`, `cpp/src/game/hud.cpp` |
| Web Audio | `cpp/src/game/audio.cpp` (mesmo grafo, sintetizado no callback do SDL) |

### Pistas (versão C++)

Na garagem, **PILOTAR** abre a escolha da pista. As opções são a **Pista de teste** (asfalto infinito) e o **Spa-Francorchamps** (`mapas/circuit-de-spa-francorchamps-2022-layout`). **CORRER** (ou Enter) carrega e entra; **VOLTAR** (ou Esc) volta. O jogo lembra a última pista escolhida.

Em Spa, cada superfície tem sua aderência: asfalto 100%, zebra 92%, grama 55%, brita 42% com resistência extra. Muros, barreiras, guard-rails e prédios têm colisão. O carro larga no grid, embaixo do pórtico de semáforos, virado para a La Source. As texturas em alta resolução da pasta `textures/` substituem as reduzidas do GLB, e as que vieram de cabeça para baixo são corrigidas sozinhas.

Para adicionar outra pista: coloque o GLB em `mapas/<nome>/` e crie uma entrada em `cpp/src/game/maps.cpp`. A entrada diz quais materiais são asfalto, zebra, grama, brita e muro, quais têm transparência (árvores, cercas) ou são decalques (faixas, marcas de pneu), e qual objeto marca a largada. A miniatura fica em `assets/maps/`.

Fontes em `assets/fonts`: Barlow e Barlow Condensed (OFL), Noto Sans (OFL) e DejaVu Sans Mono (licença livre do Bitstream Vera).

Para testes automatizados, `SR_SCRIPT="sel 0;ready;play;ready;down W;wait 3;shot foto.png;quit" ./space_racer` executa comandos e tira screenshots.

## Créditos

- Modelo: "Ferrari 296 GT3" por vecarz (Sketchfab), licença CC BY-NC 4.0. O crédito é obrigatório e o uso comercial não é permitido.
- Modelo: "2024 Porsche 992 GT3 R" por Dave Love (Sketchfab), licença CC BY 4.0. O crédito é obrigatório.
- Modelo: "Lamborghini Fenomeno 2026" por VTX (Sketchfab), licença CC BY-NC-SA 4.0. Crédito obrigatório, sem uso comercial.
- Modelo: "BMW M4 GT3 EVO" por VTX (Sketchfab), licença CC BY-NC-SA 4.0. Crédito obrigatório, sem uso comercial.
- Modelo: "Honda Civic Type R FL5 Custom" por blakebella (Sketchfab), licença CC BY-NC-ND 4.0. Crédito obrigatório, sem uso comercial e sem distribuir versões modificadas do modelo.
- Brasões das marcas: ver `assets/brands/CREDITS.txt`.
- Céu `kloofendal_48d_partly_cloudy_puresky` e asfalto `asphalt_02`: Poly Haven, CC0.
- Oclusão de ambiente: N8AO (MIT).
