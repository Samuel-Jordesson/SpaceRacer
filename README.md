# Space Racer

Ferrari 296 GT3 com física de simulador, rodando no navegador. Por enquanto não tem pista, só um chão de asfalto infinito.

## Rodar

```bash
npm start          # ou: python3 -m http.server 8080
```
Abra http://localhost:8080. Precisa de um servidor local, porque abrir o `index.html` direto (`file://`) bloqueia o carregamento dos módulos e do GLB. Three.js, N8AO e postprocessing vêm da CDN jsDelivr, então é preciso internet.

`npm test` roda os testes da física no Node, sem navegador (aceleração, frenagem, aderência lateral, estabilidade).

## Telas

- **Garagem (tela inicial):** estúdio branco infinito com piso de porcelanato polido. O reflexo do carro no piso é real (reflexo planar). Embaixo fica a lista de carros na horizontal: clique num card para carregar o carro, arraste para girá-lo e use **Pilotar** (ou Enter) para entrar na pista.
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

## Créditos

- Modelo: "Ferrari 296 GT3" por vecarz (Sketchfab), licença CC BY-NC 4.0. O crédito é obrigatório e o uso comercial não é permitido.
- Modelo: "2024 Porsche 992 GT3 R" por Dave Love (Sketchfab), licença CC BY 4.0. O crédito é obrigatório.
- Céu `kloofendal_48d_partly_cloudy_puresky` e asfalto `asphalt_02`: Poly Haven, CC0.
- Oclusão de ambiente: N8AO (MIT).
