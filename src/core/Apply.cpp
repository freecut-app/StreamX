#include "Apply.h"
#include "ClipIt.h"
#include "Engine.h"
#include "Hotkeys.h"
#include "Log.h"
#include "Recorder.h"
#include "SceneManager.h"
#include "Settings.h"
#include "Streamer.h"
#include "../control/ControlServer.h"

#include <QGuiApplication>
#include <QScreen>

namespace sx {

bool anyOutputActive() { return Recorder::get().isActive() || Streamer::get().isActive() || ClipIt::get().isActive(); }

QString applyProfile(bool startup)
{
	QStringList warn;
	const auto &p = Settings::get().p();

	// Vídeo: só pode mudar com tudo parado
	VideoSettings vs = Engine::get().video();
	VideoSettings want = vs;
	if (QScreen *s = QGuiApplication::primaryScreen()) {
		const QSize px = s->size() * s->devicePixelRatio();
		want.baseWidth = uint32_t(p.baseW > 0 ? p.baseW : px.width()) & ~1u;
		want.baseHeight = uint32_t(p.baseH > 0 ? p.baseH : px.height()) & ~1u;
	}
	if (p.outW > 0 && p.outH > 0) {
		want.outWidth = uint32_t(p.outW) & ~1u;
		want.outHeight = uint32_t(p.outH) & ~1u;
	} else {
		want.outWidth = want.baseWidth;
		want.outHeight = want.baseHeight;
		if (want.outHeight > 1080) {
			want.outWidth = (want.baseWidth * 1080 / want.baseHeight) & ~1u;
			want.outHeight = 1080;
		}
	}
	want.fps = uint32_t(qBound(10, p.fps, 240));
	const bool videoChanged = want.baseWidth != vs.baseWidth || want.baseHeight != vs.baseHeight || want.outWidth != vs.outWidth ||
				  want.outHeight != vs.outHeight || want.fps != vs.fps;
	if (videoChanged && !startup) {
		if (anyOutputActive())
			warn << "A nova resolução/FPS será aplicada quando a gravação, a transmissão e o Clip It estiverem parados.";
		else {
			QString e;
			if (!Engine::get().resetVideo(want, &e))
				warn << e;
			else
				logInfo(QString("Vídeo: base %1x%2, saída %3x%4 @ %5").arg(want.baseWidth).arg(want.baseHeight).arg(want.outWidth).arg(want.outHeight).arg(want.fps));
		}
	}

	SceneManager::get().setAudioDevices(p.desktopDevice, p.micDevice);
	Hotkeys::get().loadFromProfile();

	auto &ctl = ControlServer::get();
	if (p.controlEnabled) {
		QString e;
		// reinicia sempre: porta, rede local ou token podem ter mudado
		if (!ctl.start(quint16(p.controlPort), p.controlLan, p.controlToken, &e))
			warn << "StreamX Control: " + e;
	} else if (ctl.isRunning()) {
		ctl.stop();
	}

	if (startup && p.clipEnabled) {
		QString e;
		if (!ClipIt::get().start(&e))
			warn << "Clip It: " + e;
	}
	return warn.join("\n");
}

} // namespace sx
