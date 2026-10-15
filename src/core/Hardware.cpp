#include "Hardware.h"
#include "Engine.h"

#include <QGuiApplication>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <thread>

#include <windows.h>
#include <dxgi.h>

namespace sx {

HardwareInfo HardwareInfo::detect()
{
	HardwareInfo hw;
	QSettings cpu(R"(HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0)", QSettings::NativeFormat);
	hw.cpuName = cpu.value("ProcessorNameString").toString().trimmed();
	hw.cpuThreads = int(std::thread::hardware_concurrency());
	MEMORYSTATUSEX ms = {sizeof(ms)};
	if (GlobalMemoryStatusEx(&ms))
		hw.ramGB = double(ms.ullTotalPhys) / 1073741824.0;

	IDXGIFactory1 *f = nullptr;
	if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&f)))) {
		IDXGIAdapter1 *a = nullptr;
		for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
			DXGI_ADAPTER_DESC1 d;
			if (SUCCEEDED(a->GetDesc1(&d)) && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
				hw.gpus.append({QString::fromWCharArray(d.Description), d.VendorId, double(d.DedicatedVideoMemory) / 1073741824.0});
			a->Release();
		}
		f->Release();
	}
	if (QScreen *s = QGuiApplication::primaryScreen()) {
		const QSize px = s->size() * s->devicePixelRatio();
		hw.screenW = px.width();
		hw.screenH = px.height();
		hw.refreshHz = qRound(s->refreshRate());
	}
	for (const auto &e : Engine::get().videoEncoders())
		hw.encoders << e.id;
	return hw;
}

Recommendation recommend(const HardwareInfo &hw, const QString &goal)
{
	Recommendation r;
	const auto encs = Engine::get().videoEncoders();
	const bool nvenc = hw.encoders.contains("obs_nvenc_h264_tex");
	const bool amf = hw.encoders.contains("h264_texture_amf");
	const bool qsv = hw.encoders.contains("obs_qsv11_v2");
	const GpuInfo gpu = hw.gpus.isEmpty() ? GpuInfo{} : hw.gpus.first();

	// Codificador: hardware sempre que existir, para não roubar CPU do jogo
	if (nvenc) { r.encoderId = "obs_nvenc_h264_tex"; r.reasons << "NVENC disponível: a codificação fica no chip dedicado da GPU NVIDIA, sem pesar no jogo."; }
	else if (amf) { r.encoderId = "h264_texture_amf"; r.reasons << "AMD AMF disponível: codificação por hardware da Radeon."; }
	else if (qsv) { r.encoderId = "obs_qsv11_v2"; r.reasons << "Intel Quick Sync disponível: codificação por hardware da Intel."; }
	else { r.encoderId = "obs_x264"; r.reasons << "Nenhum codificador de hardware encontrado: usando x264 (CPU)."; }
	for (const auto &e : encs)
		if (e.id == r.encoderId)
			r.encoderName = e.name;

	// Classe do equipamento
	const bool strongGpu = gpu.vramGB >= 7.5 || gpu.name.contains(QRegularExpression("RTX (30|40|50)|RX (6[789]|7[6-9]|9)"));
	const bool weakCpu = hw.cpuThreads < 8;
	if (strongGpu && hw.ramGB >= 15) r.tier = "alto";
	else if (gpu.vramGB >= 3.5 && hw.ramGB >= 8) r.tier = "médio";
	else r.tier = "básico";

	// Resolução: nunca acima do monitor; transmissão limitada a 1080p (limite das plataformas)
	const int sh = hw.screenH > 0 ? hw.screenH : 1080, sw = hw.screenW > 0 ? hw.screenW : 1920;
	int targetH = goal == "transmitir" ? 1080 : (r.tier == "alto" ? qMin(sh, 1440) : 1080);
	if (r.tier == "básico" || (r.encoderId == "obs_x264" && weakCpu))
		targetH = 720;
	targetH = qMin(targetH, sh);
	r.outH = targetH & ~1;
	r.outW = int(double(sw) * targetH / sh) & ~1;
	r.reasons << QString("Saída %1×%2: mantém a proporção do seu monitor (%3×%4)%5.")
			     .arg(r.outW).arg(r.outH).arg(sw).arg(sh)
			     .arg(goal == "transmitir" ? " e respeita o limite de 1080p das plataformas" : "");

	// FPS
	r.fps = (r.tier == "básico" || (r.encoderId == "obs_x264" && weakCpu)) ? 30 : 60;
	r.reasons << (r.fps == 60 ? "60 fps: o equipamento aguenta movimento fluido (bom para jogos)."
				  : "30 fps: reduz a carga em equipamentos mais modestos.");

	// Qualidade de gravação e bitrate de transmissão
	r.recQuality = r.tier == "alto" ? 18 : (r.tier == "médio" ? 20 : 23);
	r.reasons << QString("Gravação em qualidade constante %1 (quanto menor, melhor e maior o arquivo).").arg(r.recQuality);
	if (r.outH >= 1080)
		r.streamBitrate = r.fps == 60 ? 6000 : 4500;
	else
		r.streamBitrate = r.fps == 60 ? 4500 : 3000;
	r.reasons << QString("Transmissão a %1 kbps: dentro do recomendado por Twitch e YouTube para %2p%3. Garanta uma conexão de upload de pelo menos %4 Mbps.")
			     .arg(r.streamBitrate).arg(r.outH).arg(r.fps).arg(qRound(r.streamBitrate * 1.5 / 1000.0));
	r.recFormat = "hybrid_mp4";
	r.reasons << "Formato MP4 híbrido: abre em qualquer player e não corrompe se o PC desligar no meio da gravação.";
	return r;
}

} // namespace sx
