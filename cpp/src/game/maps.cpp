#include "track.h"

const std::vector<MapEntry>& mapCatalog() {
  static const std::vector<MapEntry> maps = [] {
    std::vector<MapEntry> m;
    {
      MapEntry e;
      e.id = "teste";
      e.name = "Pista de teste";
      e.country = "Área livre";
      e.category = "Teste";
      e.thumb = "assets/maps/teste.jpg";
      e.flat = true;
      e.figures = {{"Superfície", "Asfalto"}, {"Extensão", "Ilimitada"}, {"Uso", "Testes"}};
      e.city = "Asfalto infinito";
      e.length = "Ilimitada";
      e.turns = "Sem traçado";
      e.description = "Uma área de asfalto sem limites para testar o carro, o acerto da oficina, frenagens, drift e manobras à vontade.";
      m.push_back(e);
    }
    {
      MapEntry e;
      e.id = "spa";
      e.name = "Spa-Francorchamps";
      e.country = "Bélgica";
      e.category = "Circuito";
      e.model = "mapas/circuit-de-spa-francorchamps-2022-layout/source/spa.glb";
      e.thumb = "assets/maps/spa.jpg";
      e.figures = {{"Extensão", "7,004 km"}, {"Curvas", "19"}, {"Sentido", "Horário"}};
      e.city = "Stavelot, Ardenas";
      e.length = "7,004 km";
      e.turns = "19 curvas";
      e.outline = "assets/maps/spa-outline.png";
      e.description = "O circuito mais longo do calendário, nas florestas das Ardenas: a lendária subida da Eau Rouge e "
                      "Raidillon, a longa reta Kemmel e curvas de alta como Pouhon e Blanchimont, com clima que muda a cada volta.";
      MapCfg& c = e.cfg;
      c.textureDir = "mapas/circuit-de-spa-francorchamps-2022-layout/textures";
      // aderência por superfície (μ relativo ao asfalto) e resistência de rolagem extra
      c.surfaces = {
          {{"asph*", "line*", "doted_line*", "groove*", "asph_patch_joint*", "texts_new*", "diss2*", "carpet-blue*"}, 1.0, 0},
          {{"kerb_new*", "CURB_B*"}, 0.92, 0},
          {{"road-ext-tile*", "Parking*"}, 0.95, 0},
          {{"carpet*"}, 0.85, 0.01},
          {{"sand*", "construction_dirt*"}, 0.42, 0.14},
          {{"GRASS*", "grass-side*", "grass-ext-shad*", "sbancamento*"}, 0.55, 0.03},
          {{"top-ext*"}, 0.6, 0.03},
      };
      c.walls = {"barriers*", "jersey*", "walls*", "concrete-barrie*", "grails*", "metal_beam*", "stone-wall*", "ivy-wall*",
                 "fences*", "fence-special*", "fence2_at*", "box1*", "box-door*", "houses*", "structures*", "marshal-post*",
                 "main-gstand*", "raidillon-grandstand*"};
      c.alphaTest = {"trees1_2022*", "trees2_2022*", "treesline*", "bushesB*", "hedge*", "fences*", "fence2_at*",
                     "grille-alpha_test*", "transp-alpha_test*", "structures*", "metal_beam*"};
      // faixas e bordas com transparência desenhadas por cima de outra superfície
      // (sand.001 é a borda da caixa de brita; a brita em si é sand_new)
      c.decals = {"line*", "doted_line*", "groove*", "asph_patch_joint*", "sand.001", "sbancamento_D*", "new_AO*",
                  "misc*", "stones_detail1*", "CURB_B*", "Parking*", "road-ext-tile*"};
      c.glass = {"Glass*", "water*"};
      // grama: base clara (variação e trilhas) × detalhe repetido a cada 5 m
      c.details = {{{"GRASS*"}, "grass-detail19.png", 5.0f, 1.2f},
                   {{"grass-ext-shad*"}, "grass-detail19.png", 7.0f, 1.8f}};
      c.noShadow = {"asph*", "line*", "doted_line*", "groove*", "kerb_new*", "CURB_B*", "GRASS*", "grass-*", "top-ext*",
                    "sand*", "carpet*", "road-ext-tile*", "Parking*", "sbancamento*", "texts_new*", "diss2*", "water*"};
      c.spawnMarker = {"spa-start-lights-emissive*"};
      c.spawnRoad = {"asph.001", "asph_new*"};
      c.clockwise = false;  // (com true os carros largavam virados para trás, contra o sentido da corrida)
      c.gridOffset = 18;
      c.treeReplace = {"trees1_2022*", "trees2_2022*"};
      c.treeModel = "tree/real_tree_models.glb";
      c.treeReplaceDist = 15;
      m.push_back(e);
    }
    return m;
  }();
  return maps;
}
