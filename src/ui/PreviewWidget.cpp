#include "PreviewWidget.h"
#include <QResizeEvent>
#include <QWindow>
#include <algorithm>

namespace sx {

PreviewWidget::PreviewWidget(QWidget *parent) : QWidget(parent)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_NativeWindow);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setMinimumSize(320, 180);
}

PreviewWidget::~PreviewWidget()
{
	if (display_)
		obs_display_remove_draw_callback(display_, draw, this);
	display_ = nullptr;
}

void PreviewWidget::createDisplay()
{
	if (display_ || !windowHandle())
		return;
	const qreal dpr = devicePixelRatioF();
	gs_init_data info = {};
	info.cx = uint32_t(width() * dpr);
	info.cy = uint32_t(height() * dpr);
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;
	info.window.hwnd = reinterpret_cast<void *>(winId());
	display_ = obs_display_create(&info, 0x0b0d12);
	obs_display_add_draw_callback(display_, draw, this);
}

void PreviewWidget::showEvent(QShowEvent *e)
{
	QWidget::showEvent(e);
	createDisplay();
}

void PreviewWidget::resizeEvent(QResizeEvent *e)
{
	QWidget::resizeEvent(e);
	createDisplay();
	if (display_) {
		const qreal dpr = devicePixelRatioF();
		obs_display_resize(display_, uint32_t(width() * dpr), uint32_t(height() * dpr));
	}
}

// Desenha a composição principal ajustada ao tamanho do widget (letterbox)
void PreviewWidget::draw(void *, uint32_t cx, uint32_t cy)
{
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;
	const float bw = float(ovi.base_width), bh = float(ovi.base_height);
	const float scale = std::min(float(cx) / bw, float(cy) / bh);
	const int w = int(bw * scale), h = int(bh * scale);
	const int x = (int(cx) - w) / 2, y = (int(cy) - h) / 2;

	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, bw, 0.0f, bh, -100.0f, 100.0f);
	gs_set_viewport(x, y, w, h);
	obs_render_main_texture();
	gs_projection_pop();
	gs_viewport_pop();
}

} // namespace sx
