#include "renderer.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {

const char* PBR_VS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv0;
layout(location = 3) in vec2 aUv1;
layout(location = 4) in vec4 aTangent;
uniform mat4 uModel, uView, uProj, uNormalMat;
uniform vec4 uClipPlane;
out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vTangent;
out vec2 vUv0;
out vec2 vUv1;
out float vViewDepth;
void main() {
  vec4 wp = uModel * vec4(aPos, 1.0);
  vWorldPos = wp.xyz;
  vNormal = mat3(uNormalMat) * aNormal;
  vTangent = vec4(mat3(uModel) * aTangent.xyz, aTangent.w);
  vUv0 = aUv0;
  vUv1 = aUv1;
  vec4 vp = uView * wp;
  vViewDepth = -vp.z;
  gl_Position = uProj * vp;
  gl_ClipDistance[0] = dot(wp, uClipPlane);
}
)";

const char* PBR_FS = R"(
in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vTangent;
in vec2 vUv0;
in vec2 vUv1;
in float vViewDepth;
out vec4 fragColor;

uniform vec3 uCamPos;
// material
uniform int uKind;  // 0 = standard/physical, 1 = basic
uniform vec3 uColor;
uniform float uOpacity, uAlphaTest;
uniform bool uTransparent, uAlphaToCoverage;
uniform float uMetalness, uRoughness;
uniform vec3 uEmissive;
uniform float uEnvIntensity;
uniform bool uPhysical;
uniform float uClearcoat, uClearcoatRoughness, uSpecularIntensity, uIor;
uniform vec3 uSpecularColor;
uniform vec2 uNormalScale, uCcNormalScale, uMapRepeat;
uniform float uAoIntensity;
uniform bool uDoubleSided, uFlipNormalY, uHasTangents;
uniform int uMapUv, uMrUv, uNormalUv, uAoUv, uEmissiveUv, uCcUv, uCcRoughUv, uCcNormalUv, uSpecIntUv, uSpecColorUv;
uniform sampler2D tMap, tMr, tNormal, tAo, tEmissive, tCc, tCcRough, tCcNormal, tSpecInt, tSpecColor;
uniform float uAsphaltTile;
uniform sampler2D tDetail;
uniform int uHasDetail;
uniform float uDetailScale, uDetailGain;
// adesivos da oficina (projetados no espaço do carro)
uniform int uDecalCount;
uniform sampler2DArray tDecal;
uniform mat4 uDecalInv;
uniform mat4 uDecalMat[8];
uniform vec4 uDecalInfo[8];
// luzes
uniform bool uHasLight;
uniform vec3 uLightDir, uLightColor;
uniform bool uReceiveShadow, uHasShadow;
uniform sampler2DShadow tShadow;
uniform mat4 uShadowMatrix;
uniform float uShadowBias, uShadowNormalBias;
uniform vec2 uShadowMapSize;
uniform bool uHasHemi;
uniform vec3 uHemiSky, uHemiGround;
uniform bool uHasEnv;
uniform samplerCube tEnvSpec, tEnvIrr;
uniform float uEnvMips, uSceneEnvIntensity;
// neblina
uniform int uFogType;
uniform vec3 uFogColor;
uniform float uFogNear, uFogFar, uFogDensity;
uniform float uFogBase, uFogFalloff;  // neblina atmosférica: altura de referência e queda com a altura
uniform vec3 uFogSunColor;
uniform int uDebug;

vec2 uvc(int ch) { return ch == 1 ? vUv1 : vUv0; }

// ------------------------------------------------ asfalto (onBeforeCompile do world.js)
float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * .1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p); vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}
float fbm(vec2 p) { float s = 0.0, a = 0.5; for (int i = 0; i < 4; i++) { s += a * vnoise(p); p *= 2.03; a *= 0.5; } return s; }
vec2 uvA() { return vUv0 * uAsphaltTile; }
vec2 uvB() { mat2 r = mat2(0.788, -0.616, 0.616, 0.788); return r * vUv0 * uAsphaltTile * 0.37 + 0.31; }
float blendW() { return smoothstep(0.35, 0.65, fbm(vUv0 * 0.045)); }

// ------------------------------------------------ BRDF (bsdfs.glsl do three)
vec3 F_Schlick(vec3 f0, float f90, float dotVH) {
  float fresnel = exp2((-5.55473 * dotVH - 6.98316) * dotVH);
  return f0 * (1.0 - fresnel) + (f90 * fresnel);
}
float V_GGX_SmithCorrelated(float alpha, float dotNL, float dotNV) {
  float a2 = pow2(alpha);
  float gv = dotNL * sqrt(a2 + (1.0 - a2) * pow2(dotNV));
  float gl = dotNV * sqrt(a2 + (1.0 - a2) * pow2(dotNL));
  return 0.5 / max(gv + gl, EPSILON);
}
float D_GGX(float alpha, float dotNH) {
  float a2 = pow2(alpha);
  float denom = pow2(dotNH) * (a2 - 1.0) + 1.0;
  return RECIPROCAL_PI * a2 / pow2(denom);
}
vec3 BRDF_GGX(vec3 L, vec3 V, vec3 N, vec3 f0, float f90, float roughness) {
  float alpha = pow2(roughness);
  vec3 H = normalize(L + V);
  float dotNL = clamp(dot(N, L), 0.0, 1.0), dotNV = clamp(dot(N, V), 0.0, 1.0);
  float dotNH = clamp(dot(N, H), 0.0, 1.0), dotVH = clamp(dot(V, H), 0.0, 1.0);
  return F_Schlick(f0, f90, dotVH) * (V_GGX_SmithCorrelated(alpha, dotNL, dotNV) * D_GGX(alpha, dotNH));
}
vec2 DFGApprox(vec3 N, vec3 V, float roughness) {
  float dotNV = clamp(dot(N, V), 0.0, 1.0);
  const vec4 c0 = vec4(-1, -0.0275, -0.572, 0.022);
  const vec4 c1 = vec4(1, 0.0425, 1.04, -0.04);
  vec4 r = roughness * c0 + c1;
  float a004 = min(r.x * r.x, exp2(-9.28 * dotNV)) * r.x + r.y;
  return vec2(-1.04, 1.04) * a004 + r.zw;
}

// ------------------------------------------------ sombra (PCFSoftShadowMap do three)
float cmp(vec2 uv, float z) { return texture(tShadow, vec3(uv, z)); }
float getShadow(vec3 n) {
  vec4 sc = uShadowMatrix * vec4(vWorldPos + n * uShadowNormalBias, 1.0);
  sc.xyz /= sc.w;
  sc.z += uShadowBias;
  bool inFrustum = sc.x >= 0.0 && sc.x <= 1.0 && sc.y >= 0.0 && sc.y <= 1.0;
  if (!(inFrustum && sc.z <= 1.0)) return 1.0;
  vec2 texelSize = 1.0 / uShadowMapSize;
  float dx = texelSize.x, dy = texelSize.y;
  vec2 uv = sc.xy;
  vec2 f = fract(uv * uShadowMapSize + 0.5);
  uv -= f * texelSize;
  float z = sc.z;
  // perto da borda do quadrado coberto pelo mapa a sombra some aos poucos (sem corte seco)
  float edge = min(min(sc.x, 1.0 - sc.x), min(sc.y, 1.0 - sc.y));
  float fade = smoothstep(0.0, 0.08, edge);
  return mix(1.0, (cmp(uv, z) + cmp(uv + vec2(dx, 0.0), z) + cmp(uv + vec2(0.0, dy), z) + cmp(uv + texelSize, z) +
    mix(cmp(uv + vec2(-dx, 0.0), z), cmp(uv + vec2(2.0 * dx, 0.0), z), f.x) +
    mix(cmp(uv + vec2(-dx, dy), z), cmp(uv + vec2(2.0 * dx, dy), z), f.x) +
    mix(cmp(uv + vec2(0.0, -dy), z), cmp(uv + vec2(0.0, 2.0 * dy), z), f.y) +
    mix(cmp(uv + vec2(dx, -dy), z), cmp(uv + vec2(dx, 2.0 * dy), z), f.y) +
    mix(mix(cmp(uv + vec2(-dx, -dy), z), cmp(uv + vec2(2.0 * dx, -dy), z), f.x),
        mix(cmp(uv + vec2(-dx, 2.0 * dy), z), cmp(uv + vec2(2.0 * dx, 2.0 * dy), z), f.x), f.y)) * (1.0 / 9.0), fade);
}

// quadro tangente por derivadas (getTangentFrame do three)
mat3 tangentFrame(vec3 eye, vec3 N, vec2 uv) {
  vec3 q0 = dFdx(eye), q1 = dFdy(eye);
  vec2 st0 = dFdx(uv), st1 = dFdy(uv);
  vec3 q1perp = cross(q1, N), q0perp = cross(N, q0);
  vec3 T = q1perp * st0.x + q0perp * st1.x;
  vec3 B = q1perp * st0.y + q0perp * st1.y;
  float det = max(dot(T, T), dot(B, B));
  float scale = det == 0.0 ? 0.0 : inversesqrt(det);
  return mat3(T * scale, B * scale, N);
}

vec3 envSpec(vec3 dir, float roughness) { return textureLod(tEnvSpec, dir, roughness * (uEnvMips - 1.0)).rgb; }

void main() {
  bool asphalt = uAsphaltTile > 0.0;
  vec4 diffuseColor = vec4(uColor, uOpacity);
  if (asphalt) {
    vec4 cA = texture(tMap, uvA()), cB = texture(tMap, uvB());
    vec4 s = mix(cA, cB, blendW() * 0.6);
    float macro = fbm(vUv0 * 0.012);
    float patches = smoothstep(0.55, 0.75, fbm(vUv0 * 0.03 + 7.0));
    s.rgb *= mix(0.82, 1.12, macro) * mix(1.0, 0.86, patches);
    diffuseColor *= s;
  } else if (uMapUv >= 0) {
    vec2 muv = uvc(uMapUv) * uMapRepeat;
    diffuseColor *= texture(tMap, muv);
    // recortes (folhas, cercas): a média dos mipmaps derrete o alfa e a vegetação some/pisca
    // ao longe; compensa conforme o nível de mip usado
    if (uAlphaTest > 0.0) diffuseColor.a *= 1.0 + max(0.0, textureQueryLod(tMap, muv).x) * 0.25;
  }
  if (uHasDetail == 1) diffuseColor.rgb *= texture(tDetail, vWorldPos.xz / uDetailScale).rgb * uDetailGain;
  if (uDecalCount > 0) {
    // cada adesivo é projetado ao longo do eixo z da sua matriz; só pinta faces viradas para ele
    vec3 lp = (uDecalInv * vec4(vWorldPos, 1.0)).xyz;
    vec3 ln = normalize(mat3(uDecalInv) * normalize(vNormal));
    for (int i = 0; i < 8; i++) {
      if (i >= uDecalCount) break;
      for (int m = 0; m < 2; m++) {
        if (m == 1 && uDecalInfo[i].y < 0.5) break;
        vec3 p = m == 1 ? vec3(-lp.x, lp.y, lp.z) : lp;
        vec3 nn = m == 1 ? vec3(-ln.x, ln.y, ln.z) : ln;
        vec3 q = (uDecalMat[i] * vec4(p, 1.0)).xyz;
        vec3 ax = normalize(vec3(uDecalMat[i][0][2], uDecalMat[i][1][2], uDecalMat[i][2][2]));
        if (q.x <= 0.0 || q.x >= 1.0 || q.y <= 0.0 || q.y >= 1.0 || abs(q.z) > 1.0 || dot(nn, ax) > -0.15) continue;
        float u = m == 1 ? 1.0 - q.x : q.x;  // lado espelhado: a imagem continua legível
        vec4 dc = texture(tDecal, vec3(u, 1.0 - q.y, uDecalInfo[i].x));
        diffuseColor.rgb = mix(diffuseColor.rgb, dc.rgb, dc.a);
      }
    }
  }
  if (uAlphaTest > 0.0 && uAlphaToCoverage) {
    // alpha-to-coverage com borda nítida: a cobertura do MSAA suaviza o recorte (folhas, cercas)
    float a = (diffuseColor.a - uAlphaTest) / max(fwidth(diffuseColor.a), 1e-4) + 0.5;
    if (a <= 0.0) discard;
    diffuseColor.a = clamp(a, 0.0, 1.0);
  } else {
    if (diffuseColor.a < uAlphaTest) discard;
    if (!uTransparent) diffuseColor.a = 1.0;
  }

  vec3 outgoing;
  if (uKind == 1) {
    outgoing = diffuseColor.rgb;
  } else {
    float faceDirection = gl_FrontFacing ? 1.0 : -1.0;
    vec3 normal = normalize(vNormal);
    if (uDoubleSided) normal *= faceDirection;
    vec3 geometryNormalRaw = normalize(vNormal);
    mat3 tbn;
    bool needTbn = asphalt || uNormalUv >= 0 || uCcNormalUv >= 0;
    if (needTbn) {
      if (uHasTangents) {
        vec3 T = normalize(vTangent.xyz);
        vec3 B = normalize(cross(normalize(vNormal), T) * vTangent.w);
        tbn = mat3(T, B, normal);
      } else {
        vec2 tuv = asphalt ? vUv0 : uvc(uNormalUv >= 0 ? uNormalUv : uCcNormalUv);
        tbn = tangentFrame(vWorldPos - uCamPos, normal, tuv);
      }
      if (uDoubleSided) { tbn[0] *= faceDirection; tbn[1] *= faceDirection; }
    }
    vec3 nonPerturbedNormal = normal;

    float roughnessFactor = uRoughness, metalnessFactor = uMetalness;
    float normalVariance = 0.0;
    if (asphalt) {
      float rA = texture(tMr, uvA()).g, rB = texture(tMr, uvB()).g;
      roughnessFactor *= clamp(mix(rA, rB, blendW() * 0.6) * mix(0.85, 1.05, fbm(vUv0 * 0.02 + 3.0)), 0.35, 1.0);
      vec3 mapN = texture(tNormal, uvA()).xyz * 2.0 - 1.0;
      mapN.xy *= uNormalScale;
      normal = normalize(tbn * mapN);
    } else {
      if (uMrUv >= 0) {
        vec4 mr = texture(tMr, uvc(uMrUv));
        roughnessFactor *= mr.g;
        metalnessFactor *= mr.b;
      }
      if (uNormalUv >= 0) {
        vec3 mapN = texture(tNormal, uvc(uNormalUv)).xyz * 2.0 - 1.0;
        // Toksvig: normal média encurtada no mipmap = relevo que não cabe no pixel → mais rugosidade
        float nlen = clamp(length(mapN), 0.0, 1.0);
        normalVariance = (1.0 - nlen) / max(nlen, 1e-3);
        vec2 ns = uNormalScale;
        if (uFlipNormalY && !uHasTangents) ns.y = -ns.y;
        mapN.xy *= ns;
        normal = normalize(tbn * mapN);
      }
    }
    vec3 ccNormal = nonPerturbedNormal;
    if (uCcNormalUv >= 0) {
      vec3 ccN = texture(tCcNormal, uvc(uCcNormalUv)).xyz * 2.0 - 1.0;
      vec2 ns = uCcNormalScale;
      if (uFlipNormalY && !uHasTangents) ns.y = -ns.y;
      ccN.xy *= ns;
      ccNormal = normalize(tbn * ccN);
    }
    vec3 emissive = uEmissive;
    if (uEmissiveUv >= 0) emissive *= texture(tEmissive, uvc(uEmissiveUv)).rgb;

    // ---- material físico (lights_physical_fragment)
    vec3 diffuse = diffuseColor.rgb * (1.0 - metalnessFactor);
    vec3 dxy = max(abs(dFdx(nonPerturbedNormal)), abs(dFdy(nonPerturbedNormal)));
    float geometryRoughness = max(max(dxy.x, dxy.y), dxy.z);
    float roughness = min(max(roughnessFactor, 0.0525) + geometryRoughness, 1.0);
    // anti-serrilhado de reflexo (Kaplanyan/Tokuyoshi): variação da normal final no pixel vira
    // rugosidade extra, então o brilho do sol em bordas e frisos não "cintila" ao longe
    {
      vec3 ndx = dFdx(normal), ndy = dFdy(normal);
      float variance = 0.25 * (dot(ndx, ndx) + dot(ndy, ndy)) + normalVariance * 0.5;
      float kernel = min(2.0 * variance, 0.18);
      float a2 = clamp(pow2(pow2(roughness)) + kernel, 0.0, 1.0);
      roughness = max(roughness, sqrt(sqrt(a2)));
    }
    vec3 specularColor;
    float specularF90;
    if (uPhysical) {
      float si = uSpecularIntensity;
      vec3 sc = uSpecularColor;
      if (uSpecColorUv >= 0) sc *= texture(tSpecColor, uvc(uSpecColorUv)).rgb;
      if (uSpecIntUv >= 0) si *= texture(tSpecInt, uvc(uSpecIntUv)).a;
      specularF90 = mix(si, 1.0, metalnessFactor);
      specularColor = mix(min(pow2((uIor - 1.0) / (uIor + 1.0)) * sc, vec3(1.0)) * si, diffuseColor.rgb, metalnessFactor);
    } else {
      specularColor = mix(vec3(0.04), diffuseColor.rgb, metalnessFactor);
      specularF90 = 1.0;
    }
    float clearcoat = uClearcoat, ccRoughness = uClearcoatRoughness;
    bool useCc = uPhysical && uClearcoat > 0.0;
    if (useCc) {
      if (uCcUv >= 0) clearcoat *= texture(tCc, uvc(uCcUv)).x;
      if (uCcRoughUv >= 0) ccRoughness *= texture(tCcRough, uvc(uCcRoughUv)).y;
      clearcoat = clamp(clearcoat, 0.0, 1.0);
      ccRoughness = min(max(ccRoughness, 0.0525) + geometryRoughness, 1.0);
    }

    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 directDiffuse = vec3(0.0), directSpecular = vec3(0.0), indirectDiffuse = vec3(0.0), indirectSpecular = vec3(0.0);
    vec3 ccSpecDirect = vec3(0.0), ccSpecIndirect = vec3(0.0);

    // luz direcional (com sombra)
    if (uHasLight) {
      vec3 lc = uLightColor;
      if (uHasShadow && uReceiveShadow) lc *= getShadow(geometryNormalRaw);
      vec3 L = uLightDir;
      float dotNL = clamp(dot(normal, L), 0.0, 1.0);
      vec3 irradiance = dotNL * lc;
      if (useCc) {
        float dotNLcc = clamp(dot(ccNormal, L), 0.0, 1.0);
        ccSpecDirect += dotNLcc * lc * BRDF_GGX(L, V, ccNormal, vec3(0.04), 1.0, ccRoughness);
      }
      directSpecular += irradiance * BRDF_GGX(L, V, normal, specularColor, specularF90, roughness);
      directDiffuse += irradiance * RECIPROCAL_PI * diffuse;
    }
    // hemisférica
    vec3 irradiance = vec3(0.0);
    if (uHasHemi) {
      float w = 0.5 * normal.y + 0.5;
      irradiance += mix(uHemiGround, uHemiSky, w);
    }
    indirectDiffuse += irradiance * RECIPROCAL_PI * diffuse;

    // ambiente (IBL)
    if (uHasEnv) {
      float envI = uEnvIntensity * uSceneEnvIntensity;
      vec3 iblIrradiance = PI * texture(tEnvIrr, normal).rgb * envI;
      vec3 r = reflect(-V, normal);
      r = normalize(mix(r, normal, roughness * roughness));
      vec3 radiance = envSpec(r, roughness) * envI;
      if (useCc) {
        vec3 rc = reflect(-V, ccNormal);
        rc = normalize(mix(rc, ccNormal, ccRoughness * ccRoughness));
        vec3 ccRadiance = envSpec(rc, ccRoughness) * envI;
        vec2 fabc = DFGApprox(ccNormal, V, ccRoughness);
        ccSpecIndirect += ccRadiance * (vec3(0.04) * fabc.x + 1.0 * fabc.y);
      }
      vec2 fab = DFGApprox(normal, V, roughness);
      vec3 FssEss = specularColor * fab.x + specularF90 * fab.y;
      float Ess = fab.x + fab.y, Ems = 1.0 - Ess;
      vec3 Favg = specularColor + (1.0 - specularColor) * 0.047619;
      vec3 Fms = FssEss * Favg / (1.0 - Ems * Favg);
      vec3 single = FssEss, multi = Fms * Ems;
      vec3 total = single + multi;
      vec3 diffuseIbl = diffuse * (1.0 - max(max(total.r, total.g), total.b));
      vec3 cosIrr = iblIrradiance * RECIPROCAL_PI;
      indirectSpecular += radiance * single + multi * cosIrr;
      indirectDiffuse += diffuseIbl * cosIrr;
    }
    // oclusão do aoMap
    if (uAoUv >= 0) {
      float ao = (texture(tAo, uvc(uAoUv)).r - 1.0) * uAoIntensity + 1.0;
      indirectDiffuse *= ao;
      if (uHasEnv) {
        float dotNV = clamp(dot(normal, V), 0.0, 1.0);
        indirectSpecular *= clamp(pow(dotNV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
      }
    }
    outgoing = directDiffuse + indirectDiffuse + directSpecular + indirectSpecular + emissive;
    if (uDebug == 3) outgoing = texture(tEnvIrr, normal).rgb;
    if (uDebug == 4) outgoing = envSpec(reflect(-V, normal), 0.0);
    if (uDebug == 5) outgoing = directDiffuse + indirectDiffuse;
    if (uDebug == 6) outgoing = directSpecular + indirectSpecular;
    if (useCc) {
      float dotNVcc = clamp(dot(ccNormal, V), 0.0, 1.0);
      vec3 Fcc = F_Schlick(vec3(0.04), 1.0, dotNVcc);
      if (uDebug == 7) outgoing = (ccSpecDirect + ccSpecIndirect) * clearcoat;
      else if (uDebug == 8) outgoing = vec3(clearcoat, ccRoughness, 0.0);
      else if (uDebug != 9) outgoing = outgoing * (1.0 - clearcoat * Fcc) + (ccSpecDirect + ccSpecIndirect) * clearcoat;
    }
  }
  vec4 col = vec4(outgoing, diffuseColor.a);
  if (uDebug == 1) col.rgb = diffuseColor.rgb;
  if (uDebug == 2) { vec3 nn = normalize(vNormal) * (uDoubleSided && !gl_FrontFacing ? -1.0 : 1.0); col.rgb = nn * 0.5 + 0.5; }
  if (uFogType == 1) col.rgb = mix(col.rgb, uFogColor, smoothstep(uFogNear, uFogFar, vViewDepth));
  else if (uFogType == 2) col.rgb = mix(col.rgb, uFogColor, 1.0 - exp(-uFogDensity * uFogDensity * vViewDepth * vViewDepth));
  else if (uFogType == 3) {
    // neblina atmosférica: densidade cai exponencialmente com a altura (vales mais enevoados),
    // integrada ao longo do raio câmera → ponto; perto do sol a névoa fica dourada (dispersão)
    vec3 rd = vWorldPos - uCamPos;
    float dist = length(rd);
    float k = uFogFalloff;
    float h0 = uCamPos.y - uFogBase;
    float ky = k * rd.y;
    float integ = abs(ky) > 1e-4 ? (1.0 - exp(-ky)) / ky : 1.0;
    float amount = uFogDensity * exp(-k * h0) * dist * integ;
    float f = 1.0 - exp(-max(amount, 0.0));
    vec3 dir = rd / max(dist, 1e-4);
    float sunAmt = uHasLight ? pow(max(dot(dir, uLightDir), 0.0), 6.0) : 0.0;
    vec3 fogCol = mix(uFogColor, uFogSunColor, sunAmt);
    col.rgb = mix(col.rgb, fogCol, f);
  }
  fragColor = col;
}
)";

const char* DEPTH_VS = R"(
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUv0;
layout(location = 3) in vec2 aUv1;
uniform mat4 uModel, uViewProj;
out vec2 vUv0;
out vec2 vUv1;
void main() { vUv0 = aUv0; vUv1 = aUv1; gl_Position = uViewProj * uModel * vec4(aPos, 1.0); }
)";
const char* DEPTH_FS = R"(
in vec2 vUv0;
in vec2 vUv1;
uniform sampler2D tMap;
uniform int uMapUv;
uniform float uAlphaTest, uOpacity;
out vec4 fragColor;
void main() {
  if (uAlphaTest > 0.0) {
    float a = uOpacity;
    if (uMapUv >= 0) a *= texture(tMap, uMapUv == 1 ? vUv1 : vUv0).a;
    if (a < uAlphaTest) discard;
  }
  fragColor = vec4(1.0);
}
)";

const char* SKY_VS = R"(
uniform mat4 uInvProj;
uniform mat4 uInvViewRot;
out vec3 vDir;

void main() {
  // Triangulo em tela cheia.
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;

  // Reconstrói um raio no espaço da câmera usando um ponto no meio
  // do espaço de recorte, evitando depender do plano distante.
  vec4 viewPoint = uInvProj * vec4(p, 0.0, 1.0);
  // sem normalizar: o ponto é linear na tela e pode ser interpolado; normalizar nos vértices
  // (que ficam bem fora da tela) entortava o céu e ele parecia girar quando a câmera virava
  vec3 viewDir = viewPoint.xyz / viewPoint.w;

  // O céu depende apenas da orientação da câmera, nunca da posição.
  vDir = mat3(uInvViewRot) * viewDir;

  gl_Position = vec4(p, 1.0, 1.0);
}
)";
const char* SKY_FS = R"(
in vec3 vDir;
uniform sampler2D tSky;
uniform float uIntensity;
out vec4 fragColor;

void main() {
  vec3 d = normalize(vDir);

  // Filtragem baseada no tamanho angular do pixel para reduzir
  // cintilação nos detalhes da textura HDRI quando a câmera se move.
  float ang = max(length(dFdx(d)), length(dFdy(d)));
  float lod = max(
    0.0,
    log2(ang * float(textureSize(tSky, 0).x) * 0.15915494)
  );

  fragColor = vec4(textureLod(tSky, equirectUv(d), lod).rgb * uIntensity, 1.0);
}
)";

enum Unit { U_MAP, U_MR, U_NORMAL, U_AO, U_EMISSIVE, U_CC, U_CCR, U_CCN, U_SPI, U_SPC, U_ENVS, U_ENVI, U_SHADOW, U_DETAIL, U_DECAL };

}  // namespace

DirectionalLight::~DirectionalLight() {
  if (fbo) glDeleteFramebuffers(1, &fbo);
  if (depthTex) glDeleteTextures(1, &depthTex);
}

Renderer::Renderer() {
  pbrShader = std::make_unique<Shader>(PBR_VS, std::string(GLSL_COMMON) + PBR_FS);
  Shader& s = *pbrShader;
  s.use();
  const char* names[] = {"tMap", "tMr", "tNormal", "tAo", "tEmissive", "tCc", "tCcRough", "tCcNormal", "tSpecInt", "tSpecColor", "tEnvSpec", "tEnvIrr", "tShadow", "tDetail", "tDecal"};
  for (int i = 0; i < 15; i++) s.set(names[i], i);
  depthShader = std::make_unique<Shader>(DEPTH_VS, DEPTH_FS);
  depthShader->use();
  depthShader->set("tMap", 0);
  skyShader = std::make_unique<Shader>(SKY_VS, std::string(GLSL_COMMON) + SKY_FS);
  skyShader->use();
  skyShader->set("tSky", 0);
  glUseProgram(0);
}

// planos do frustum de uma matriz view·proj e teste de esfera
struct Frustum {
  vec4 p[6];
  explicit Frustum(const mat4& vp) {
    auto row = [&](int r) { return vec4(vp(r, 0), vp(r, 1), vp(r, 2), vp(r, 3)); };
    vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    auto add = [](vec4 a, vec4 b, float s) { return vec4(a.x + b.x * s, a.y + b.y * s, a.z + b.z * s, a.w + b.w * s); };
    p[0] = add(r3, r0, 1); p[1] = add(r3, r0, -1); p[2] = add(r3, r1, 1);
    p[3] = add(r3, r1, -1); p[4] = add(r3, r2, 1); p[5] = add(r3, r2, -1);
  }
  bool sees(const Node& n) const {
    const Geometry& g = *n.geometry;
    vec3 c = n.matrixWorld.point(g.sphereCenter);
    float sc = std::max({length(n.matrixWorld.col(0)), length(n.matrixWorld.col(1)), length(n.matrixWorld.col(2))});
    float rad = g.sphereRadius * sc;
    for (auto& q : p) {
      float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
      if ((q.x * c.x + q.y * c.y + q.z * c.z + q.w) / l < -rad) return false;
    }
    return true;
  }
};

static void collect(Node& n, std::vector<Node*>& out, const std::function<bool(const Node&)>& exclude) {
  if (!n.visible) return;
  if (exclude && exclude(n)) return;
  if (n.isMesh()) out.push_back(&n);
  for (auto& c : n.children) collect(*c, out, exclude);
}

void Renderer::renderShadows(Scene& scene) {
  DirectionalLight* L = scene.light;
  if (!L || !L->castShadow) return;
  if (L->allocated != L->mapSize) {
    if (!L->fbo) glGenFramebuffers(1, &L->fbo);
    if (L->depthTex) glDeleteTextures(1, &L->depthTex);
    glGenTextures(1, &L->depthTex);
    glBindTexture(GL_TEXTURE_2D, L->depthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, L->mapSize, L->mapSize, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindFramebuffer(GL_FRAMEBUFFER, L->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, L->depthTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    L->allocated = L->mapSize;
  }
  mat4 view = mat4::compose(L->position, quatFromMatrix(mat4::lookAt(L->position, L->target, {0, 1, 0})), vec3(1)).inverse();
  mat4 proj = mat4::ortho(L->left, L->right, L->top, L->bottom, L->near, L->far);
  mat4 vp = proj * view;
  mat4 bias;
  bias.e[0] = 0.5f; bias.e[5] = 0.5f; bias.e[10] = 0.5f;
  bias.e[12] = 0.5f; bias.e[13] = 0.5f; bias.e[14] = 0.5f;
  L->shadowMatrix = bias * vp;

  glBindFramebuffer(GL_FRAMEBUFFER, L->fbo);
  glViewport(0, 0, L->mapSize, L->mapSize);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glClear(GL_DEPTH_BUFFER_BIT);
  std::vector<Node*> list;
  collect(*scene.root, list, nullptr);
  Shader& d = *depthShader;
  d.use();
  d.set("uViewProj", vp);
  Frustum fr(vp);
  for (Node* n : list) {
    if (!n->castShadow || !fr.sees(*n)) continue;
    Geometry& g = *n->geometry;
    g.upload();
    Material& m = *n->material;
    d.set("uModel", n->matrixWorld);
    d.set("uAlphaTest", m.alphaTest);
    d.set("uOpacity", m.opacity);
    int mapUv = -1;
    if (m.map && m.alphaTest > 0) {
      m.map.tex->upload();
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, m.map.tex->id);
      mapUv = m.map.uv;
    }
    d.set("uMapUv", mapUv);
    glBindVertexArray(g.vao);
    glDrawElements(GL_TRIANGLES, g.count, GL_UNSIGNED_INT, nullptr);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::setSceneUniforms(Scene& scene, const RenderContext& ctx) {
  Shader& s = *pbrShader;
  s.use();
  s.set("uView", ctx.view);
  s.set("uProj", ctx.proj);
  s.set("uCamPos", ctx.camPos);
  s.set("uClipPlane", ctx.clip ? ctx.clipPlane : vec4(0, 0, 0, 1));
  DirectionalLight* L = scene.light;
  s.set("uHasLight", L != nullptr);
  if (L) {
    s.set("uLightDir", normalize(L->position - L->target));
    s.set("uLightColor", L->color * L->intensity);
    bool sh = L->castShadow && L->depthTex;
    s.set("uHasShadow", sh);
    if (sh) {
      glActiveTexture(GL_TEXTURE0 + U_SHADOW);
      glBindTexture(GL_TEXTURE_2D, L->depthTex);
      s.set("uShadowMatrix", L->shadowMatrix);
      s.set("uShadowBias", L->bias);
      s.set("uShadowNormalBias", L->normalBias);
      // suavidade: o filtro trabalha numa grade de texels "maiores" (raio maior = borda mais suave)
      float eff = (float)L->mapSize / std::max(0.25f, L->softness);
      s.set("uShadowMapSize", vec2(eff, eff));
    }
  } else {
    s.set("uHasShadow", false);
  }
  s.set("uHasHemi", scene.hemi);
  s.set("uHemiSky", scene.hemiSky * scene.hemiIntensity);
  s.set("uHemiGround", scene.hemiGround * scene.hemiIntensity);
  s.set("uHasEnv", scene.env != nullptr);
  if (scene.env) {
    glActiveTexture(GL_TEXTURE0 + U_ENVS);
    glBindTexture(GL_TEXTURE_CUBE_MAP, scene.env->specular);
    glActiveTexture(GL_TEXTURE0 + U_ENVI);
    glBindTexture(GL_TEXTURE_CUBE_MAP, scene.env->irradiance);
    s.set("uEnvMips", (float)scene.env->specularMips);
    s.set("uSceneEnvIntensity", scene.envIntensity);
  }
  static int dbg = std::getenv("SR_DEBUG_ALBEDO") ? 1 : std::getenv("SR_DEBUG_NORMAL") ? 2 : std::getenv("SR_DEBUG") ? std::atoi(std::getenv("SR_DEBUG")) : 0;
  s.set("uDebug", dbg);
  s.set("uFogType", (int)ctx.fog.type);
  s.set("uFogColor", ctx.fog.color);
  s.set("uFogNear", ctx.fog.near);
  s.set("uFogFar", ctx.fog.far);
  s.set("uFogDensity", ctx.fog.density);
  s.set("uFogBase", ctx.fog.base);
  s.set("uFogFalloff", ctx.fog.falloff);
  s.set("uFogSunColor", ctx.fog.sunColor);
}

static int bindTex(const TexRef& t, int unit) {
  if (!t) return -1;
  t.tex->upload();
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, t.tex->id);
  return t.uv;
}

// pass: 0 = opaco, 1 = transparente lado de trás, 2 = transparente lado da frente/único
void Renderer::drawMesh(Node& n, const RenderContext& ctx, Scene&, int pass) {
  Geometry& g = *n.geometry;
  g.upload();
  Material& m = *n.material;
  Shader& s = *pbrShader;
  s.set("uModel", n.matrixWorld);
  s.set("uNormalMat", normalMatrix(n.matrixWorld));
  s.set("uHasTangents", g.hasTangents);
  s.set("uReceiveShadow", n.receiveShadow);
  // o material só é reenviado quando muda (as listas vêm ordenadas por material)
  if (&m != lastMaterial) {
    lastMaterial = &m;
    s.set("uKind", m.kind == Material::Kind::Basic ? 1 : 0);
    s.set("uColor", m.color);
    s.set("uOpacity", m.opacity);
    s.set("uAlphaTest", m.alphaTest);
    s.set("uTransparent", m.transparent);
    s.set("uMetalness", m.metalness);
    s.set("uRoughness", m.roughness);
    s.set("uEmissive", m.emissive * m.emissiveIntensity);
    s.set("uEnvIntensity", m.envMapIntensity);
    s.set("uPhysical", m.physical);
    s.set("uClearcoat", m.clearcoat);
    s.set("uClearcoatRoughness", m.clearcoatRoughness);
    s.set("uSpecularIntensity", m.specularIntensity);
    s.set("uSpecularColor", m.specularColor);
    s.set("uIor", m.ior);
    s.set("uNormalScale", m.normalScale);
    s.set("uCcNormalScale", m.clearcoatNormalScale);
    s.set("uMapRepeat", m.mapRepeat);
    s.set("uAoIntensity", m.aoMapIntensity);
    s.set("uDoubleSided", m.doubleSided);
    s.set("uFlipNormalY", m.gltf);
    s.set("uAsphaltTile", m.asphaltTile);
    s.set("uMapUv", bindTex(m.map, U_MAP));
    s.set("uMrUv", bindTex(m.roughnessMap ? m.roughnessMap : m.metalnessMap, U_MR));
    s.set("uNormalUv", bindTex(m.normalMap, U_NORMAL));
    s.set("uAoUv", bindTex(m.aoMap, U_AO));
    s.set("uEmissiveUv", bindTex(m.emissiveMap, U_EMISSIVE));
    bool phys = m.physical;
    s.set("uCcUv", phys ? bindTex(m.clearcoatMap, U_CC) : -1);
    s.set("uCcRoughUv", phys ? bindTex(m.clearcoatRoughnessMap, U_CCR) : -1);
    s.set("uCcNormalUv", phys && m.clearcoat > 0 ? bindTex(m.clearcoatNormalMap, U_CCN) : -1);
    s.set("uSpecIntUv", phys ? bindTex(m.specularIntensityMap, U_SPI) : -1);
    s.set("uSpecColorUv", phys ? bindTex(m.specularColorMap, U_SPC) : -1);
    s.set("uHasDetail", bindTex(m.detailMap, U_DETAIL) >= 0 ? 1 : 0);
    s.set("uDetailScale", m.detailScale);
    s.set("uDetailGain", m.detailGain);
    const DecalSet* ds = m.decals.get();
    bool decal = ds && ds->array && ds->count > 0 && ds->space;
    s.set("uDecalCount", decal ? ds->count : 0);
    if (decal) {
      glActiveTexture(GL_TEXTURE0 + U_DECAL);
      glBindTexture(GL_TEXTURE_2D_ARRAY, ds->array);
      // espaço do carro sem escala (o adesivo é medido em metros)
      vec3 dp, dsc;
      quat dq;
      ds->space->matrixWorld.decompose(dp, dq, dsc);
      s.set("uDecalInv", mat4::compose(dp, dq, vec3(1)).inverse());
      // nomes fixos: o cache de uniforms usa o ponteiro do nome
      static const char* MATN[8] = {"uDecalMat[0]", "uDecalMat[1]", "uDecalMat[2]", "uDecalMat[3]",
                                    "uDecalMat[4]", "uDecalMat[5]", "uDecalMat[6]", "uDecalMat[7]"};
      static const char* INFN[8] = {"uDecalInfo[0]", "uDecalInfo[1]", "uDecalInfo[2]", "uDecalInfo[3]",
                                    "uDecalInfo[4]", "uDecalInfo[5]", "uDecalInfo[6]", "uDecalInfo[7]"};
      for (int i = 0; i < ds->count; i++) {
        s.set(MATN[i], ds->mats[i]);
        s.set(INFN[i], ds->info[i]);
      }
    }
    // estado
    if (m.transparent) {
      glEnable(GL_BLEND);
      if (m.additive) glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
      else glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else {
      glDisable(GL_BLEND);
    }
    glDepthMask(m.depthWrite ? GL_TRUE : GL_FALSE);
    if (m.decal) {
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(-1, -4);
    } else {
      glDisable(GL_POLYGON_OFFSET_FILL);
    }
    bool a2c = m.alphaTest > 0 && msaaTarget && !m.transparent;
    if (a2c) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    else glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    s.set("uAlphaToCoverage", a2c);
    lastPass = -1;
  }
  if (pass != lastPass) {
    lastPass = pass;
    if (pass == 1) {
      glEnable(GL_CULL_FACE);
      glCullFace(GL_FRONT);
    } else if (m.doubleSided) {
      if (m.transparent) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
      } else {
        glDisable(GL_CULL_FACE);
      }
    } else {
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
    }
  }
  bool flip = n.matrixWorld.determinant() < 0;
  glFrontFace((ctx.mirrored != flip) ? GL_CW : GL_CCW);
  glBindVertexArray(g.vao);
  glDrawElements(GL_TRIANGLES, g.count, GL_UNSIGNED_INT, nullptr);
}

void Renderer::render(Scene& scene, const Camera& camera, int vw, int vh, const RenderOptions& opt) {
  RenderContext ctx;
  ctx.view = camera.view;
  ctx.proj = camera.proj;
  ctx.camPos = camera.position;
  ctx.fov = camera.fov;
  ctx.fog = scene.fog;
  ctx.viewportW = vw;
  ctx.viewportH = vh;
  ctx.mirrored = opt.mirrored;
  ctx.clip = opt.clip;
  ctx.clipPlane = opt.clipPlane;
  if (opt.mirrored) {
    // vista refletida no plano do espelho
    vec4 p = opt.clipPlane;
    vec3 n(p.x, p.y, p.z);
    float d = p.w;
    mat4 R;
    R(0, 0) = 1 - 2 * n.x * n.x; R(0, 1) = -2 * n.x * n.y; R(0, 2) = -2 * n.x * n.z; R(0, 3) = -2 * n.x * d;
    R(1, 0) = -2 * n.y * n.x; R(1, 1) = 1 - 2 * n.y * n.y; R(1, 2) = -2 * n.y * n.z; R(1, 3) = -2 * n.y * d;
    R(2, 0) = -2 * n.z * n.x; R(2, 1) = -2 * n.z * n.y; R(2, 2) = 1 - 2 * n.z * n.z; R(2, 3) = -2 * n.z * d;
    ctx.view = camera.view * R;
    ctx.camPos = R.point(camera.position);
  }

  glFrontFace(opt.mirrored ? GL_CW : GL_CCW);
  {
    GLint samples = 0;
    glGetIntegerv(GL_SAMPLES, &samples);
    msaaTarget = samples > 1;
  }
  if (opt.clip) glEnable(GL_CLIP_DISTANCE0);
  glClearColor(scene.background.x, scene.background.y, scene.background.z, 1);
  glClearDepth(1);
  glDepthMask(GL_TRUE);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);

  // fundo HDRI
  if (scene.backgroundTex) {
    scene.backgroundTex->upload();
    Shader& sk = *skyShader;
    sk.use();
    // O céu usa somente a orientação da câmera. Separar a inversa da
    // projeção da inversa da rotação evita reconstruir um ponto distante,
    // reduzindo erros de precisão que podem fazer a textura tremer.
    mat4 viewRot = ctx.view;
    viewRot.e[12] = viewRot.e[13] = viewRot.e[14] = 0;
    sk.set("uInvProj", ctx.proj.inverse());
    sk.set("uInvViewRot", viewRot.inverse());
    sk.set("uIntensity", scene.backgroundIntensity);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene.backgroundTex->id);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    drawFullscreen();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
  }

  // listas com recorte por frustum (esferas de contorno)
  std::vector<Node*> all;
  collect(*scene.root, all, opt.exclude);
  mat4 vp = ctx.proj * ctx.view;
  vec4 planes[6];
  auto row = [&](int r) { return vec4(vp(r, 0), vp(r, 1), vp(r, 2), vp(r, 3)); };
  vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
  auto add = [](vec4 a, vec4 b, float s) { return vec4(a.x + b.x * s, a.y + b.y * s, a.z + b.z * s, a.w + b.w * s); };
  planes[0] = add(r3, r0, 1); planes[1] = add(r3, r0, -1); planes[2] = add(r3, r1, 1);
  planes[3] = add(r3, r1, -1); planes[4] = add(r3, r2, 1); planes[5] = add(r3, r2, -1);
  std::vector<Item> opaque, transp;
  for (Node* n : all) {
    Geometry& g = *n->geometry;
    vec3 c = n->matrixWorld.point(g.sphereCenter);
    float sc = std::max({length(n->matrixWorld.col(0)), length(n->matrixWorld.col(1)), length(n->matrixWorld.col(2))});
    float rad = g.sphereRadius * sc;
    bool inside = true;
    for (auto& p : planes) {
      float l = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
      if ((p.x * c.x + p.y * c.y + p.z * c.z + p.w) / l < -rad) { inside = false; break; }
    }
    if (!inside) continue;
    float depth = -ctx.view.point(c).z;
    (n->material->transparent ? transp : opaque).push_back({n, depth});
  }
  // de frente para trás em faixas de 100 m (a GPU descarta o que fica escondido) e, dentro da
  // faixa, agrupado por material; recortes por transparência (árvores, cercas) por último
  std::stable_sort(opaque.begin(), opaque.end(), [](const Item& a, const Item& b) {
    if (a.node->renderOrder != b.node->renderOrder) return a.node->renderOrder < b.node->renderOrder;
    bool ta = a.node->material->alphaTest > 0, tb = b.node->material->alphaTest > 0;
    if (ta != tb) return tb;
    int ba = (int)(a.depth / 100), bb = (int)(b.depth / 100);
    if (ba != bb) return ba < bb;
    if (a.node->material.get() != b.node->material.get()) return a.node->material.get() < b.node->material.get();
    return a.depth < b.depth;
  });
  std::stable_sort(transp.begin(), transp.end(), [](const Item& a, const Item& b) {
    if (a.node->renderOrder != b.node->renderOrder) return a.node->renderOrder < b.node->renderOrder;
    return a.depth > b.depth;
  });

  setSceneUniforms(scene, ctx);
  lastMaterial = nullptr;
  for (auto& it : opaque) drawMesh(*it.node, ctx, scene, 0);
  if (opt.customs)
    for (auto* c : scene.opaqueCustom) { c->draw(ctx); setSceneUniforms(scene, ctx); }
  if (opt.customs)
    for (auto* c : scene.transparentFirst) { c->draw(ctx); setSceneUniforms(scene, ctx); }
  pbrShader->use();
  lastMaterial = nullptr;
  for (auto& it : transp) {
    lastMaterial = nullptr;  // transparentes: costas/frente mudam o estado
    // transparente de dupla face: costas primeiro, depois a frente (como o three)
    if (it.node->material->doubleSided) drawMesh(*it.node, ctx, scene, 1);
    drawMesh(*it.node, ctx, scene, 2);
  }
  if (opt.customs)
    for (auto* c : scene.transparentLast) { c->draw(ctx); setSceneUniforms(scene, ctx); }

  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glDisable(GL_POLYGON_OFFSET_FILL);
  glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
  glDisable(GL_CLIP_DISTANCE0);
  glFrontFace(GL_CCW);
  glBindVertexArray(0);
}