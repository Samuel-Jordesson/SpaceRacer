// Ícones de botões (teclado e controles Xbox, PlayStation e genéricos), desenhados em vetor.
#pragma once
#include <string>

#include "ui.h"

enum class PadStyle { Keyboard, Xbox, PlayStation, Generic };

// botões lógicos usados nas dicas da interface
enum class PadButton { Confirm, Back, West, North, LB, RB, LT, RT, Start, Select, DPadH, DPadV, DPadUp, LStick };

struct PromptTheme {
  bool dark = false;  // fundo escuro (texto/contornos claros)
};

// desenha o ícone com o centro vertical em cy; devolve a largura usada.
// Para o teclado, `key` é o texto da tecla ("Enter", "Esc"...); "space" desenha a barra de espaço.
float drawPrompt(UI& ui, PadStyle style, PadButton b, float x, float cy, float size, const std::string& key, PromptTheme th = {});
float promptWidth(UI& ui, PadStyle style, PadButton b, float size, const std::string& key);
