#include "PerfMonitor.h"
#include "Log.h"

#include <QTimer>
#include <obs.h>

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <vector>

namespace sx {

static unsigned long long ft(const FILETIME &f) { return (unsigned long long(f.dwHighDateTime) << 32) | f.dwLowDateTime; }

PerfMonitor &PerfMonitor::get()
{
	static PerfMonitor p;
	return p;
}

void PerfMonitor::initPdh()
{
	PDH_HQUERY q = nullptr;
	if (PdhOpenQueryW(nullptr, 0, &q) != ERROR_SUCCESS)
		return;
	PDH_HCOUNTER gpu = nullptr, vram = nullptr;
	// Contadores do Windows 10+ que funcionam com qualquer fabricante de GPU
	const bool g = PdhAddEnglishCounterW(q, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &gpu) == ERROR_SUCCESS;
	const bool v = PdhAddEnglishCounterW(q, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &vram) == ERROR_SUCCESS;
	pdhQuery_ = q;
	gpuCounter_ = g ? gpu : nullptr;
	vramCounter_ = v ? vram : nullptr;
	pdhOk_ = g || v;
	PdhCollectQueryData(q);
	if (!pdhOk_)
		logWarn("Contadores de GPU do Windows indisponíveis");
}

static double sumCounter(void *counter, bool maxOnly)
{
	DWORD size = 0, count = 0;
	PDH_STATUS st = PdhGetFormattedCounterArrayW(PDH_HCOUNTER(counter), PDH_FMT_DOUBLE, &size, &count, nullptr);
	if (st != PDH_MORE_DATA)
		return -1;
	std::vector<unsigned char> buf(size);
	auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buf.data());
	if (PdhGetFormattedCounterArrayW(PDH_HCOUNTER(counter), PDH_FMT_DOUBLE, &size, &count, items) != ERROR_SUCCESS)
		return -1;
	double total = 0;
	for (DWORD i = 0; i < count; i++) {
		if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA)
			continue;
		total = maxOnly ? qMax(total, items[i].FmtValue.doubleValue) : total + items[i].FmtValue.doubleValue;
	}
	return total;
}

void PerfMonitor::start(int intervalMs)
{
	static bool started = false;
	if (started)
		return;
	started = true;
	initPdh();
	auto *t = new QTimer(this);
	connect(t, &QTimer::timeout, this, &PerfMonitor::tick);
	t->start(intervalMs);
	tick();
}

void PerfMonitor::tick()
{
	PerfSample s;
	// CPU do sistema
	FILETIME idle, kernel, user;
	if (GetSystemTimes(&idle, &kernel, &user)) {
		const auto i = ft(idle), k = ft(kernel), u = ft(user);
		const auto di = i - prevIdle_, dk = k - prevKernel_, du = u - prevUser_;
		if (prevKernel_ && dk + du > 0)
			s.cpuSystem = 100.0 * double(dk + du - di) / double(dk + du);
		prevIdle_ = i; prevKernel_ = k; prevUser_ = u;
	}
	// CPU do processo (normalizada pelo número de núcleos)
	FILETIME c, e, pk, pu, now;
	GetSystemTimeAsFileTime(&now);
	if (GetProcessTimes(GetCurrentProcess(), &c, &e, &pk, &pu)) {
		const auto k = ft(pk), u = ft(pu), w = ft(now);
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		if (prevWall_ && w > prevWall_)
			s.cpuApp = 100.0 * double((k - prevProcK_) + (u - prevProcU_)) / double(w - prevWall_) / double(si.dwNumberOfProcessors);
		prevProcK_ = k; prevProcU_ = u; prevWall_ = w;
	}
	// RAM
	MEMORYSTATUSEX ms = {sizeof(ms)};
	if (GlobalMemoryStatusEx(&ms)) {
		s.ramTotalGB = double(ms.ullTotalPhys) / 1073741824.0;
		s.ramUsedGB = double(ms.ullTotalPhys - ms.ullAvailPhys) / 1073741824.0;
	}
	PROCESS_MEMORY_COUNTERS_EX pmc = {};
	if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc)))
		s.ramAppMB = double(pmc.PrivateUsage) / 1048576.0;
	// GPU
	if (pdhOk_ && PdhCollectQueryData(PDH_HQUERY(pdhQuery_)) == ERROR_SUCCESS) {
		if (gpuCounter_)
			s.gpu = qMin(100.0, sumCounter(gpuCounter_, false));
		if (vramCounter_) {
			const double b = sumCounter(vramCounter_, true);
			s.vramUsedGB = b >= 0 ? b / 1073741824.0 : -1;
		}
	}
	// libobs
	s.fps = obs_get_active_fps();
	s.renderMs = double(obs_get_average_frame_time_ns()) / 1e6;
	s.laggedFrames = obs_get_lagged_frames();
	s.totalFrames = obs_get_total_frames();
	if (video_t *v = obs_get_video()) {
		s.skippedFrames = video_output_get_skipped_frames(v);
		s.encodedFrames = video_output_get_total_frames(v);
	}
	last_ = s;
	emit sample(s);
}

} // namespace sx
