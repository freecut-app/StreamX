// StreamX — integração com o editor FreeCut ("Editar no FreeCut")
// Contrato documentado em docs/FREECUT_INTEGRATION.md
#pragma once
#include <QString>
#include <QStringList>

namespace sx {

struct FreeCutLaunch {
	bool launched = false;
	QString exe;
	QString message; // o que aconteceu, para mostrar ao usuário
};

class FreeCutBridge {
public:
	// Procura o FreeCut: caminho configurado, instalação por usuário, registro do Windows
	static QString findExecutable();
	static void setCustomPath(const QString &exe);
	static QString customPath();
	// Abre o FreeCut com os arquivos (contrato: FreeCut.exe --import "<arquivo>" ...),
	// grava o arquivo de entrega (handoff) e seleciona o arquivo no Explorer
	static FreeCutLaunch open(const QStringList &files);
	static QString handoffFile();
};

} // namespace sx
