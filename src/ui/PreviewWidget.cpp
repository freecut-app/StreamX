#include "PreviewWidget.h"
#include "../core/SceneManager.h"
#include <graphics/matrix4.h>
#include <graphics/vec3.h>

#include <QMouseEvent>
#include <QResizeEvent>
#include <QWindow>
#include <algorithm>
#include <cmath>

namespace sx {

PreviewWidget::PreviewWidget(QWidget *parent) : QWidget(parent)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_NativeWindow);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setMouseTracking(true);
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

void PreviewWidget::setRenderingEnabled(bool on)
{
	if (display_)
		obs_display_set_enabled(display_, on);
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

void PreviewWidget::setSelected(int64_t id)
{
	selected_ = id;
}

/* ---------------- geometria ---------------- */

// área (em pixels do widget) onde a composição é desenhada, com letterbox
static void fitRect(float cx, float cy, float bw, float bh, float *x, float *y, float *s)
{
	*s = std::min(cx / bw, cy / bh);
	*x = (cx - bw * *s) / 2.0f;
	*y = (cy - bh * *s) / 2.0f;
}

bool PreviewWidget::toScene(const QPointF &wp, vec2 *out) const
{
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return false;
	const qreal dpr = devicePixelRatioF();
	float x, y, s;
	fitRect(float(width() * dpr), float(height() * dpr), float(ovi.base_width), float(ovi.base_height), &x, &y, &s);
	vec2_set(out, (float(wp.x() * dpr) - x) / s, (float(wp.y() * dpr) - y) / s);
	return true;
}

bool PreviewWidget::itemCorners(int64_t id, vec3 out[4]) const
{
	obs_sceneitem_t *it = SceneManager::get().item(id);
	if (!it)
		return false;
	matrix4 m;
	obs_sceneitem_get_box_transform(it, &m);
	const float pts[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	for (int i = 0; i < 4; i++) {
		vec3 p;
		vec3_set(&p, pts[i][0], pts[i][1], 0);
		vec3_transform(&out[i], &p, &m);
	}
	return true;
}

static bool insideQuad(const vec3 q[4], const vec2 &p)
{
	bool pos = false, neg = false;
	for (int i = 0; i < 4; i++) {
		const vec3 &a = q[i], &b = q[(i + 1) % 4];
		const float c = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
		pos |= c > 0;
		neg |= c < 0;
	}
	return !(pos && neg);
}

int64_t PreviewWidget::hitTest(const vec2 &p) const
{
	for (const auto &info : SceneManager::get().items()) { // do topo para o fundo
		if (!info.visible)
			continue;
		vec3 q[4];
		if (itemCorners(info.id, q) && insideQuad(q, p))
			return info.id;
	}
	return 0;
}

/* ---------------- mouse ---------------- */

void PreviewWidget::mousePressEvent(QMouseEvent *e)
{
	if (e->button() != Qt::LeftButton)
		return;
	vec2 p;
	if (!toScene(e->position(), &p))
		return;
	// alça de redimensionar: canto inferior direito do item selecionado
	vec3 q[4];
	if (selected_ && itemCorners(selected_, q)) {
		obs_video_info ovi;
		obs_get_video_info(&ovi);
		const float tolerance = 14.0f * float(ovi.base_width) / float(std::max(1, width()));
		if (std::hypot(p.x - q[2].x, p.y - q[2].y) < tolerance) {
			obs_sceneitem_t *it = SceneManager::get().item(selected_);
			if (it && !obs_sceneitem_locked(it)) {
				drag_ = Drag::Resize;
				dragStart_ = p;
				itemUsesBounds_ = obs_sceneitem_get_bounds_type(it) != OBS_BOUNDS_NONE;
				if (itemUsesBounds_) obs_sceneitem_get_bounds(it, &itemStartSize_);
				else obs_sceneitem_get_scale(it, &itemStartSize_);
				startDist_ = std::max(1.0f, std::hypot(p.x - q[0].x, p.y - q[0].y));
				return;
			}
		}
	}
	const int64_t hit = hitTest(p);
	if (hit != selected_) {
		selected_ = hit;
		emit itemSelected(hit);
	}
	obs_sceneitem_t *it = hit ? SceneManager::get().item(hit) : nullptr;
	if (it && !obs_sceneitem_locked(it)) {
		drag_ = Drag::Move;
		dragStart_ = p;
		obs_sceneitem_get_pos(it, &itemStartPos_);
	}
}

void PreviewWidget::mouseMoveEvent(QMouseEvent *e)
{
	vec2 p;
	if (!toScene(e->position(), &p))
		return;
	if (drag_ == Drag::None) {
		vec3 q[4];
		bool onHandle = false;
		if (selected_ && itemCorners(selected_, q)) {
			obs_video_info ovi;
			obs_get_video_info(&ovi);
			onHandle = std::hypot(p.x - q[2].x, p.y - q[2].y) < 14.0f * float(ovi.base_width) / float(std::max(1, width()));
		}
		setCursor(onHandle ? Qt::SizeFDiagCursor : Qt::ArrowCursor);
		return;
	}
	obs_sceneitem_t *it = SceneManager::get().item(selected_);
	if (!it)
		return;
	if (drag_ == Drag::Move) {
		vec2 np;
		vec2_set(&np, std::round(itemStartPos_.x + p.x - dragStart_.x), std::round(itemStartPos_.y + p.y - dragStart_.y));
		obs_sceneitem_set_pos(it, &np);
	} else {
		vec3 q[4];
		itemCorners(selected_, q);
		const float k = std::max(0.02f, std::hypot(p.x - q[0].x, p.y - q[0].y) / startDist_);
		vec2 ns;
		vec2_set(&ns, itemStartSize_.x * k, itemStartSize_.y * k);
		if (itemUsesBounds_) obs_sceneitem_set_bounds(it, &ns);
		else obs_sceneitem_set_scale(it, &ns);
	}
	emit itemTransformed();
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent *)
{
	drag_ = Drag::None;
}

/* ---------------- desenho ---------------- */

static void drawSelection(const vec3 q[4], float lineScale)
{
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
	vec4 c;
	vec4_set(&c, 0.12f, 0.55f, 1.0f, 1.0f); // azul elétrico
	gs_effect_set_vec4(color, &c);
	const float t = 2.0f * lineScale;
	while (gs_effect_loop(solid, "Solid")) {
		for (int i = 0; i < 4; i++) { // bordas como quadriláteros finos
			const vec3 &a = q[i], &b = q[(i + 1) % 4];
			float dx = b.x - a.x, dy = b.y - a.y;
			const float len = std::max(0.001f, std::hypot(dx, dy));
			const float nx = -dy / len * t / 2, ny = dx / len * t / 2;
			gs_render_start(true);
			gs_vertex2f(a.x + nx, a.y + ny);
			gs_vertex2f(a.x - nx, a.y - ny);
			gs_vertex2f(b.x + nx, b.y + ny);
			gs_vertex2f(b.x - nx, b.y - ny);
			gs_render_stop(GS_TRISTRIP);
		}
		const float h = 6.0f * lineScale; // alças nos cantos
		for (int i = 0; i < 4; i++) {
			gs_render_start(true);
			gs_vertex2f(q[i].x - h, q[i].y - h);
			gs_vertex2f(q[i].x + h, q[i].y - h);
			gs_vertex2f(q[i].x - h, q[i].y + h);
			gs_vertex2f(q[i].x + h, q[i].y + h);
			gs_render_stop(GS_TRISTRIP);
		}
	}
}

void PreviewWidget::draw(void *data, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<PreviewWidget *>(data);
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;
	const float bw = float(ovi.base_width), bh = float(ovi.base_height);
	float x, y, s;
	fitRect(float(cx), float(cy), bw, bh, &x, &y, &s);
	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, bw, 0.0f, bh, -100.0f, 100.0f);
	gs_set_viewport(int(x), int(y), int(bw * s), int(bh * s));
	obs_render_main_texture();
	vec3 q[4];
	if (self->selected_ && self->itemCorners(self->selected_, q))
		drawSelection(q, 1.0f / s);
	gs_projection_pop();
	gs_viewport_pop();
}

} // namespace sx
