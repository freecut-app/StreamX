// StreamX — prévia em tempo real (display do libobs desenhado direto na GPU) com seleção,
// movimento e redimensionamento das fontes
#pragma once
#include <QWidget>
#include <obs.hpp>

namespace sx {

class PreviewWidget : public QWidget {
	Q_OBJECT
public:
	explicit PreviewWidget(QWidget *parent = nullptr);
	~PreviewWidget() override;
	QPaintEngine *paintEngine() const override { return nullptr; }

	void setSelected(int64_t itemId);
	int64_t selected() const { return selected_; }
	void setRenderingEnabled(bool on); // desligar economiza GPU durante jogos

signals:
	void itemSelected(int64_t itemId);
	void itemTransformed();

protected:
	void showEvent(QShowEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *) override {}
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	void createDisplay();
	static void draw(void *data, uint32_t cx, uint32_t cy);
	bool toScene(const QPointF &widgetPos, vec2 *out) const;
	int64_t hitTest(const vec2 &p) const;
	bool itemCorners(int64_t id, vec3 out[4]) const;

	OBSDisplay display_;
	int64_t selected_ = 0;
	enum class Drag { None, Move, Resize } drag_ = Drag::None;
	vec2 dragStart_ = {};
	vec2 itemStartPos_ = {};
	vec2 itemStartSize_ = {};  // bounds ou escala no início do redimensionamento
	bool itemUsesBounds_ = false;
	float startDist_ = 1.0f;
};

} // namespace sx
