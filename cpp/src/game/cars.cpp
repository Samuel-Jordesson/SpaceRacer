#include "cars.h"

const std::vector<CarEntry>& carCatalog() {
  static const std::vector<CarEntry> cars = [] {
    std::vector<CarEntry> c;
    {
      CarEntry e;
      e.id = "ferrari-296-gt3";
      e.maker = "Ferrari";
      e.name = "296 GT3";
      e.year = 2024;
      e.category = "GT3";
      e.model = "ferrari-488-gt3/source/ferrari_488_gt3.glb";
      e.thumb = "assets/cars/ferrari-296-gt3.jpg";
      e.logo = "assets/brands/ferrari.png";
      e.spec = &phys::FERRARI_296_GT3;
      e.visual.tires = {"mat_235"};
      e.visual.headlights = {"AR3DMat_PBR_Light_Cool_White"};
      e.visual.tailLights = {"mat_50"};
      e.visual.brakeGlow = {"mat_57"};
      e.visual.steeringWheel = {"mat_225"};
      e.visual.glass = {"Window"};
      e.visual.fixBodyMetal = true;  // o GLB deixa a carroceria 100% metálica
      e.visual.cockpit = VisualCfg::Cockpit{{-0.33f, 0.93f, -0.14f}, {-0.33f, 0.9f, 0.07f}};
      e.visual.hoodEye = vec3(0, 1.0f, -0.75f);
      // números medidos na própria simulação (sr_physics_test)
      e.figures = {{"Potência", "610 cv"}, {"Peso", "1.310 kg"}, {"0–100 km/h", "3,1 s"},
                   {"Velocidade máx.", "289 km/h"}, {"Motor", "V6 2.9 biturbo"}, {"Tração", "Traseira"}};
      e.ratings = {{"Velocidade", 7.9f}, {"Aceleração", 8.4f}, {"Frenagem", 9.1f}, {"Curva", 8.8f}};
      c.push_back(e);
    }
    {
      CarEntry e;
      e.id = "porsche-992-gt3-r";
      e.maker = "Porsche";
      e.name = "911 GT3 R";
      e.year = 2024;
      e.category = "GT3";
      e.model = "2024_porsche_992_gt3_r/2024_porsche_992_gt3_r.glb";
      e.thumb = "assets/cars/porsche-992-gt3-r.jpg";
      e.logo = "assets/brands/porsche.png";
      e.spec = &phys::PORSCHE_992_GT3_R;
      e.visual.wheelbase = 2.507f;  // o GLB vem em escala reduzida
      e.visual.tires = {"MI_Tyre_Flex_Dry_R*"};
      e.visual.headlights = {"EXT_Emissive_Light_Front*"};
      e.visual.headlightGlow = 1.2f;
      e.visual.tailLights = {"EXT_Emissive_Light_Rear*"};
      e.visual.tailLightUseMap = true;
      e.visual.discs = {"EXT_Disc*"};
      e.visual.rimBlur = {"EXT_RIM_BLUR*"};
      e.visual.steeringWheel = {"INT_Steer", "INT_Steer_Carbon", "STEER_PLASTIC"};
      e.visual.glass = {"EXT_Windows*", "INT_Windows", "INT_Windshield"};
      e.visual.paint = {"EXT_Carpaint_Inst"};
      e.figures = {{"Potência", "565 cv"}, {"Peso", "1.330 kg"}, {"0–100 km/h", "3,8 s"},
                   {"Velocidade máx.", "284 km/h"}, {"Motor", "Boxer 6 4.2 aspirado"}, {"Tração", "Traseira"}};
      e.ratings = {{"Velocidade", 7.7f}, {"Aceleração", 7.9f}, {"Frenagem", 9.1f}, {"Curva", 8.7f}};
      c.push_back(e);
    }
    {
      CarEntry e;
      e.id = "lamborghini-fenomeno";
      e.maker = "Lamborghini";
      e.name = "Fenomeno";
      e.year = 2026;
      e.category = "Hypercar";
      e.model = "lamborghini_fenomeno_2026/lamborghini_fenomeno_2026.glb";
      e.thumb = "assets/cars/lamborghini-fenomeno.jpg";
      e.logo = "assets/brands/lamborghini.png";
      e.spec = &phys::LAMBORGHINI_FENOMENO;
      e.visual.wheelbase = 2.779f;  // o GLB vem ~23% maior que o carro real
      // as rodas esquerda e direita vêm na mesma malha: o carregador divide ao meio
      e.visual.splitWheels = {"front_tires.001", "rear_tires*", "rims_1*", "carpaint_rims*", "Caliper*",
                              "rotors*", "metal*", "ext_plastics*", "Engexttex1Mtl*", "redbodylight*"};
      e.visual.tires = {"front_tires.001", "rear_tires*"};
      e.visual.frontHint = {"dash"};  // os faróis atravessam o carro; o painel indica a frente
      e.visual.tailLights = {"przemo_l.001"};
      e.visual.tailLightUseMap = true;
      e.visual.discs = {"rotors*"};
      e.visual.steeringWheelNodes = {"Object_133", "Object_134", "Object_135", "Object_136", "Object_137", "Object_138", "Object_139"};
      e.visual.glass = {"glass_1", "CAR_GLASS*"};
      e.figures = {{"Potência", "1.080 cv"}, {"Peso", "1.690 kg"}, {"0–100 km/h", "2,5 s"},
                   {"Velocidade máx.", "353 km/h"}, {"Motor", "V12 6.5 híbrido"}, {"Tração", "Integral"}};
      e.ratings = {{"Velocidade", 9.8f}, {"Aceleração", 9.7f}, {"Frenagem", 8.3f}, {"Curva", 7.6f}};
      c.push_back(e);
    }
    {
      CarEntry e;
      e.id = "bmw-m4-gt3-evo";
      e.maker = "BMW";
      e.name = "M4 GT3 EVO";
      e.year = 2025;
      e.category = "GT3";
      e.model = "bmw_m4_gt3_evo/bmw_m4_gt3_evo.glb";
      e.thumb = "assets/cars/bmw-m4-gt3-evo.jpg";
      e.logo = "assets/brands/bmw.png";
      e.spec = &phys::BMW_M4_GT3_EVO;
      e.visual.wheelbase = 2.917f;  // o GLB já vem em metros; confirma a escala pelo entre-eixos real
      e.visual.tires = {"EXT_Tyre"};
      e.visual.headlights = {"EXT_Emissive_Light_Front"};
      e.visual.headlightGlow = 1.2f;
      e.visual.tailLights = {"EXT_Emissive_Light_Rear", "refractrear.002"};
      e.visual.tailLightUseMap = true;
      e.visual.discs = {"EXT_Disc"};
      // volante: aro, carbono, LEDs e botões; os adesivos dele usam um material compartilhado
      e.visual.steeringWheel = {"INT_Steer", "INT_Steer_Carbon_Forged", "LED_STEER_0", "BTN_STTER"};
      e.visual.steeringWheelNodes = {"Object_464"};
      // vidros e capas dos faróis vêm esverdeados no GLB
      e.visual.glass = {"EXT_Windows", "INT_Windows", "INT_Windshield", "EXT_Glass_Emissive_Front"};
      e.visual.paint = {"EXT_Carpaint_Inst"};
      // números medidos na própria simulação (sr_physics_test)
      e.figures = {{"Potência", "590 cv"}, {"Peso", "1.340 kg"}, {"0–100 km/h", "3,3 s"},
                   {"Velocidade máx.", "288 km/h"}, {"Motor", "6 em linha 3.0 biturbo"}, {"Tração", "Traseira"}};
      e.ratings = {{"Velocidade", 7.9f}, {"Aceleração", 8.0f}, {"Frenagem", 9.1f}, {"Curva", 8.6f}};
      c.push_back(e);
    }
    {
      CarEntry e;
      e.id = "honda-civic-type-r";
      e.maker = "Honda";
      e.name = "Civic Type R";
      e.year = 2024;
      e.category = "Rua";
      e.model = "civic type R/honda_civic_type_r_fl5_custom.glb";
      e.thumb = "assets/cars/honda-civic-type-r.jpg";
      e.logo = "assets/brands/honda.png";
      e.spec = &phys::HONDA_CIVIC_TYPE_R;
      e.visual.wheelbase = 2.735f;  // o GLB vem ~9% maior que o carro real
      // os 4 pneus e os 4 discos estão numa malha só: divide em esquerda/direita e frente/trás
      const std::vector<std::string> wheelParts = {"CSR2_TOYO_ProxesR888_A", "CSR2_BrakeDisc_ForgedDrilled"};
      e.visual.splitWheels = wheelParts;
      e.visual.splitWheelsZ = wheelParts;
      // faróis e lanternas também estão numa malha só: a metade de trás (+z) acende ao frear
      e.visual.splitWheelsZ.push_back("Honda_CivicTypeRRewardRecycled_2023LightA_Material");
      e.visual.tires = {"CSR2_TOYO_ProxesR888_A"};
      e.visual.frontHint = {"Honda_CivicTypeRRewardRecycled_2023EngineA_Material"};  // motor na frente
      e.visual.tailLightNodes = {"Object_14_zpos"};
      e.visual.tailLightUseMap = true;
      e.visual.discs = {"CSR2_BrakeDisc_ForgedDrilled"};
      e.visual.glass = {"Honda_CivicTypeRRewardRecycled_2023Window_Material", "Honda_CivicTypeRRewardRecycled_2023WindowInside_Material"};
      // o volante faz parte da malha do interior: olho do piloto (lado esquerdo) definido à mão
      e.visual.cockpit = VisualCfg::Cockpit{{-0.37f, 1.06f, 0.12f}, {0, -10, 0}};
      e.figures = {{"Potência", "329 cv"}, {"Peso", "1.430 kg"}, {"0–100 km/h", "6,2 s"},
                   {"Velocidade máx.", "272 km/h"}, {"Motor", "4 cil. 2.0 turbo"}, {"Tração", "Dianteira"}};
      e.ratings = {{"Velocidade", 6.4f}, {"Aceleração", 5.2f}, {"Frenagem", 6.6f}, {"Curva", 6.8f}};
      c.push_back(e);
    }
    return c;
  }();
  return cars;
}
