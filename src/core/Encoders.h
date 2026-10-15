// StreamX — codificadores compartilhados entre gravação e Clip It
#pragma once
#include <QString>
#include <obs.hpp>

namespace sx {

class EncoderPool {
public:
	static EncoderPool &get();

	// Encoders de gravação (vídeo + uma faixa de áudio AAC por faixa habilitada).
	// Se já existirem e estiverem em uso, são reaproveitados; caso contrário são recriados
	// com as configurações atuais do perfil.
	bool acquireRecording(QString *error);
	obs_encoder_t *video() const { return video_; }
	obs_encoder_t *audio(int track) const { return (track >= 0 && track < 6) ? audio_[track].Get() : nullptr; }
	int tracks() const { return tracks_; }
	QString videoId() const { return videoId_; }
	void releaseIfIdle();

private:
	EncoderPool() = default;
	bool inUse() const;
	OBSEncoderAutoRelease video_;
	OBSEncoderAutoRelease audio_[6];
	int tracks_ = 0;
	QString videoId_;
};

obs_data_t *videoEncoderSettings(const QString &encoderId, bool constantQuality, int quality, int bitrateKbps);

} // namespace sx
