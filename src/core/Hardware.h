// StreamX — análise do hardware e Assistente Inteligente (regras locais, sem serviços pagos)
#pragma once
#include <QString>
#include <QStringList>
#include <QList>

namespace sx {

struct GpuInfo {
	QString name;
	quint32 vendorId = 0;   // 0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel
	double vramGB = 0;
};

struct HardwareInfo {
	QString cpuName;
	int cpuThreads = 0;
	double ramGB = 0;
	QList<GpuInfo> gpus;
	int screenW = 0, screenH = 0, refreshHz = 60;
	QStringList encoders;        // IDs disponíveis no libobs
	static HardwareInfo detect();
};

struct Recommendation {
	QString encoderId, encoderName;
	int outW = 1920, outH = 1080;
	int fps = 60;
	int recQuality = 20;        // CQP/CRF
	int streamBitrate = 6000;   // kbps
	QString recFormat = "hybrid_mp4";
	QStringList reasons;        // explicação de cada escolha
	QString tier;               // "alto", "médio" ou "básico"
};

Recommendation recommend(const HardwareInfo &hw, const QString &goal /* "gravar" | "transmitir" | "jogos" */);

} // namespace sx
