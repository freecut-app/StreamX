// StreamX — prévia em tempo real (display do libobs desenhado direto na GPU)
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

protected:
	void showEvent(QShowEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *) override {}

private:
	void createDisplay();
	static void draw(void *data, uint32_t cx, uint32_t cy);
	OBSDisplay display_;
};

} // namespace sx
