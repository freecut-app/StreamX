// StreamX — aplica o perfil atual ao motor (vídeo, áudio, controle remoto, Clip It)
#pragma once
#include <QString>

namespace sx {
// Retorna avisos para o usuário (ex.: "pare a gravação para mudar a resolução"), vazio se tudo foi aplicado.
QString applyProfile(bool startup = false);
bool anyOutputActive();
} // namespace sx
