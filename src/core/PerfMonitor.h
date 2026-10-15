// StreamX — monitor de desempenho (CPU, RAM, GPU do sistema + estatísticas do libobs)
#pragma once
#include <QObject>
#include <QString>

namespace sx {

struct PerfSample {
	double cpuSystem = 0;     // % de CPU do sistema
	double cpuApp = 0;        // % de CPU do StreamX
	double ramUsedGB = 0, ramTotalGB = 0;
	double ramAppMB = 0;
	double gpu = -1;          // % de uso 3D da GPU (-1 = indisponível)
	double vramUsedGB = -1;
	double fps = 0;           // FPS de renderização do StreamX
	double renderMs = 0;
	uint32_t laggedFrames = 0, totalFrames = 0;   // atraso de renderização
	uint32_t skippedFrames = 0, encodedFrames = 0; // atraso de codificação
};

class PerfMonitor : public QObject {
	Q_OBJECT
public:
	static PerfMonitor &get();
	void start(int intervalMs = 1000);
	PerfSample last() const { return last_; }

signals:
	void sample(const sx::PerfSample &s);

private:
	PerfMonitor() = default;
	void tick();
	void initPdh();
	PerfSample last_;
	unsigned long long prevIdle_ = 0, prevKernel_ = 0, prevUser_ = 0;
	unsigned long long prevProcK_ = 0, prevProcU_ = 0, prevWall_ = 0;
	void *pdhQuery_ = nullptr, *gpuCounter_ = nullptr, *vramCounter_ = nullptr;
	bool pdhOk_ = false;
};

} // namespace sx
