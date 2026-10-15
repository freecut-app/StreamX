#include "PropertiesView.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>

namespace sx {

PropertiesView::PropertiesView(obs_source_t *source, QWidget *parent) : QScrollArea(parent), source_(source)
{
	setWidgetResizable(true);
	setFrameShape(QFrame::NoFrame);
	settings_ = OBSData(obs_source_get_settings(source));
	obs_data_release(settings_); // OBSData já guardou a referência
	rebuild();
}

PropertiesView::~PropertiesView()
{
	if (props_)
		obs_properties_destroy(props_);
}

void PropertiesView::rebuild()
{
	rebuilding_ = true;
	const int scroll = verticalScrollBar() ? verticalScrollBar()->value() : 0;
	if (props_)
		obs_properties_destroy(props_);
	props_ = obs_source_properties(source_);
	auto *w = new QWidget();
	auto *form = new QFormLayout(w);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
	if (props_) {
		obs_property_t *p = obs_properties_first(props_);
		while (p) {
			addProperty(p, form);
			obs_property_next(&p);
		}
	}
	if (form->rowCount() == 0)
		form->addRow(new QLabel("Esta fonte não tem opções."));
	setWidget(w);
	QTimer::singleShot(0, this, [this, scroll]() { verticalScrollBar()->setValue(scroll); });
	rebuilding_ = false;
}

// Aplica a mudança na fonte; se o plugin pedir (callback "modified"), reconstrói a tela
void PropertiesView::apply(obs_property_t *p)
{
	if (rebuilding_)
		return;
	obs_source_update(source_, settings_);
	emit changed();
	if (p && obs_property_modified(p, settings_))
		QTimer::singleShot(0, this, &PropertiesView::rebuild);
}

static QString name(obs_property_t *p) { return QString::fromUtf8(obs_property_name(p)); }
static QString desc(obs_property_t *p) { return QString::fromUtf8(obs_property_description(p)); }

void PropertiesView::addProperty(obs_property_t *p, QFormLayout *form)
{
	if (!obs_property_visible(p))
		return;
	const QByteArray key = name(p).toUtf8();
	const char *k = key.constData();
	QWidget *w = nullptr;
	QString label = desc(p);
	const bool enabled = obs_property_enabled(p);
	const QString tip = QString::fromUtf8(obs_property_long_description(p));

	switch (obs_property_get_type(p)) {
	case OBS_PROPERTY_BOOL: {
		auto *c = new QCheckBox(label);
		c->setChecked(obs_data_get_bool(settings_, k));
		connect(c, &QCheckBox::toggled, this, [this, p, key](bool v) { obs_data_set_bool(settings_, key.constData(), v); apply(p); });
		w = c;
		label.clear();
		break;
	}
	case OBS_PROPERTY_INT: {
		const int mn = obs_property_int_min(p), mx = obs_property_int_max(p), st = obs_property_int_step(p);
		auto *spin = new QSpinBox();
		spin->setRange(mn, mx);
		spin->setSingleStep(st);
		spin->setValue(int(obs_data_get_int(settings_, k)));
		spin->setSuffix(QString::fromUtf8(obs_property_int_suffix(p)));
		if (obs_property_int_type(p) == OBS_NUMBER_SLIDER) {
			auto *box = new QWidget();
			auto *h = new QHBoxLayout(box);
			h->setContentsMargins(0, 0, 0, 0);
			auto *sl = new QSlider(Qt::Horizontal);
			sl->setRange(mn, mx);
			sl->setSingleStep(st);
			sl->setValue(spin->value());
			h->addWidget(sl, 1);
			h->addWidget(spin);
			connect(sl, &QSlider::valueChanged, spin, &QSpinBox::setValue);
			connect(spin, &QSpinBox::valueChanged, sl, &QSlider::setValue);
			w = box;
		} else
			w = spin;
		connect(spin, &QSpinBox::valueChanged, this, [this, p, key](int v) { obs_data_set_int(settings_, key.constData(), v); apply(p); });
		break;
	}
	case OBS_PROPERTY_FLOAT: {
		auto *spin = new QDoubleSpinBox();
		spin->setRange(obs_property_float_min(p), obs_property_float_max(p));
		spin->setSingleStep(obs_property_float_step(p));
		spin->setDecimals(obs_property_float_step(p) < 0.1 ? 2 : 1);
		spin->setValue(obs_data_get_double(settings_, k));
		spin->setSuffix(QString::fromUtf8(obs_property_float_suffix(p)));
		connect(spin, &QDoubleSpinBox::valueChanged, this, [this, p, key](double v) { obs_data_set_double(settings_, key.constData(), v); apply(p); });
		w = spin;
		break;
	}
	case OBS_PROPERTY_TEXT: {
		const auto type = obs_property_text_type(p);
		if (type == OBS_TEXT_INFO) {
			auto *l = new QLabel(QString::fromUtf8(obs_data_get_string(settings_, k)).isEmpty() ? label : QString::fromUtf8(obs_data_get_string(settings_, k)));
			l->setWordWrap(true);
			l->setObjectName("hint");
			w = l;
			if (QString::fromUtf8(obs_data_get_string(settings_, k)).isEmpty())
				label.clear();
		} else if (type == OBS_TEXT_MULTILINE) {
			auto *t = new QPlainTextEdit(QString::fromUtf8(obs_data_get_string(settings_, k)));
			t->setMinimumHeight(80);
			connect(t, &QPlainTextEdit::textChanged, this, [this, p, key, t]() { obs_data_set_string(settings_, key.constData(), t->toPlainText().toUtf8().constData()); apply(p); });
			w = t;
		} else {
			auto *e = new QLineEdit(QString::fromUtf8(obs_data_get_string(settings_, k)));
			if (type == OBS_TEXT_PASSWORD)
				e->setEchoMode(QLineEdit::Password);
			connect(e, &QLineEdit::editingFinished, this, [this, p, key, e]() { obs_data_set_string(settings_, key.constData(), e->text().toUtf8().constData()); apply(p); });
			w = e;
		}
		break;
	}
	case OBS_PROPERTY_PATH: {
		auto *box = new QWidget();
		auto *h = new QHBoxLayout(box);
		h->setContentsMargins(0, 0, 0, 0);
		auto *e = new QLineEdit(QString::fromUtf8(obs_data_get_string(settings_, k)));
		auto *b = new QPushButton("Procurar...");
		h->addWidget(e, 1);
		h->addWidget(b);
		const auto ptype = obs_property_path_type(p);
		const QString filter = QString::fromUtf8(obs_property_path_filter(p));
		const QString def = QString::fromUtf8(obs_property_path_default_path(p));
		connect(e, &QLineEdit::editingFinished, this, [this, p, key, e]() { obs_data_set_string(settings_, key.constData(), e->text().toUtf8().constData()); apply(p); });
		connect(b, &QPushButton::clicked, this, [this, p, key, e, ptype, filter, def, label]() {
			QString r = ptype == OBS_PATH_DIRECTORY ? QFileDialog::getExistingDirectory(this, label, def)
				  : ptype == OBS_PATH_FILE_SAVE ? QFileDialog::getSaveFileName(this, label, def, filter)
								  : QFileDialog::getOpenFileName(this, label, def, filter);
			if (r.isEmpty())
				return;
			e->setText(r);
			obs_data_set_string(settings_, key.constData(), r.toUtf8().constData());
			apply(p);
		});
		w = box;
		break;
	}
	case OBS_PROPERTY_LIST: {
		auto *c = new QComboBox();
		const auto fmt = obs_property_list_format(p);
		const bool editable = obs_property_list_type(p) == OBS_COMBO_TYPE_EDITABLE;
		c->setEditable(editable);
		int current = -1;
		for (size_t i = 0; i < obs_property_list_item_count(p); i++) {
			QVariant v;
			if (fmt == OBS_COMBO_FORMAT_INT) {
				v = qlonglong(obs_property_list_item_int(p, i));
				if (obs_data_get_int(settings_, k) == v.toLongLong()) current = int(i);
			} else if (fmt == OBS_COMBO_FORMAT_FLOAT) {
				v = obs_property_list_item_float(p, i);
				if (qFuzzyCompare(obs_data_get_double(settings_, k), v.toDouble())) current = int(i);
			} else if (fmt == OBS_COMBO_FORMAT_BOOL) {
				v = obs_property_list_item_bool(p, i);
				if (obs_data_get_bool(settings_, k) == v.toBool()) current = int(i);
			} else {
				v = QString::fromUtf8(obs_property_list_item_string(p, i));
				if (v.toString() == QString::fromUtf8(obs_data_get_string(settings_, k))) current = int(i);
			}
			c->addItem(QString::fromUtf8(obs_property_list_item_name(p, i)), v);
			if (obs_property_list_item_disabled(p, i))
				if (auto *m = qobject_cast<QStandardItemModel *>(c->model()))
					m->item(c->count() - 1)->setEnabled(false);
		}
		if (current < 0 && fmt == OBS_COMBO_FORMAT_STRING && editable)
			c->setEditText(QString::fromUtf8(obs_data_get_string(settings_, k)));
		c->setCurrentIndex(current);
		auto set = [this, p, key, c, fmt](int idx) {
			const QVariant v = c->itemData(idx);
			if (fmt == OBS_COMBO_FORMAT_INT) obs_data_set_int(settings_, key.constData(), v.toLongLong());
			else if (fmt == OBS_COMBO_FORMAT_FLOAT) obs_data_set_double(settings_, key.constData(), v.toDouble());
			else if (fmt == OBS_COMBO_FORMAT_BOOL) obs_data_set_bool(settings_, key.constData(), v.toBool());
			else obs_data_set_string(settings_, key.constData(), v.toString().toUtf8().constData());
			apply(p);
		};
		connect(c, &QComboBox::activated, this, set);
		if (editable)
			connect(c->lineEdit(), &QLineEdit::editingFinished, this, [this, p, key, c]() {
				obs_data_set_string(settings_, key.constData(), c->currentText().toUtf8().constData());
				apply(p);
			});
		w = c;
		break;
	}
	case OBS_PROPERTY_COLOR:
	case OBS_PROPERTY_COLOR_ALPHA: {
		const bool alpha = obs_property_get_type(p) == OBS_PROPERTY_COLOR_ALPHA;
		auto *b = new QPushButton();
		auto paint = [b](uint32_t abgr) {
			const QColor col(abgr & 0xff, (abgr >> 8) & 0xff, (abgr >> 16) & 0xff, (abgr >> 24) & 0xff);
			b->setText(col.name(QColor::HexRgb).toUpper());
			b->setStyleSheet(QString("background:%1; color:%2;").arg(col.name(), col.lightness() > 128 ? "#000" : "#fff"));
		};
		paint(uint32_t(obs_data_get_int(settings_, k)) | (alpha ? 0 : 0xff000000u));
		connect(b, &QPushButton::clicked, this, [this, p, key, paint, alpha, label]() {
			const uint32_t cur = uint32_t(obs_data_get_int(settings_, key.constData()));
			QColor init(cur & 0xff, (cur >> 8) & 0xff, (cur >> 16) & 0xff, alpha ? ((cur >> 24) & 0xff) : 255);
			const QColor c = QColorDialog::getColor(init, this, label, alpha ? QColorDialog::ShowAlphaChannel : QColorDialog::ColorDialogOptions());
			if (!c.isValid())
				return;
			const uint32_t v = uint32_t(c.red()) | (uint32_t(c.green()) << 8) | (uint32_t(c.blue()) << 16) | (uint32_t(alpha ? c.alpha() : 255) << 24);
			obs_data_set_int(settings_, key.constData(), v);
			paint(v);
			apply(p);
		});
		w = b;
		break;
	}
	case OBS_PROPERTY_FONT: {
		OBSDataAutoRelease f = obs_data_get_obj(settings_, k);
		auto *b = new QPushButton(QString("%1 %2").arg(QString::fromUtf8(obs_data_get_string(f, "face"))).arg(obs_data_get_int(f, "size")));
		connect(b, &QPushButton::clicked, this, [this, p, key, b]() {
			OBSDataAutoRelease cur = obs_data_get_obj(settings_, key.constData());
			QFont init(QString::fromUtf8(obs_data_get_string(cur, "face")), int(obs_data_get_int(cur, "size")));
			bool ok = false;
			const QFont f = QFontDialog::getFont(&ok, init, this);
			if (!ok)
				return;
			OBSDataAutoRelease nf = obs_data_create();
			obs_data_set_string(nf, "face", f.family().toUtf8().constData());
			obs_data_set_string(nf, "style", f.styleName().toUtf8().constData());
			obs_data_set_int(nf, "size", f.pointSize() > 0 ? f.pointSize() : 48);
			obs_data_set_int(nf, "flags", (f.bold() ? 1 : 0) | (f.italic() ? 2 : 0) | (f.underline() ? 4 : 0) | (f.strikeOut() ? 8 : 0));
			obs_data_set_obj(settings_, key.constData(), nf);
			b->setText(QString("%1 %2").arg(f.family()).arg(f.pointSize()));
			apply(p);
		});
		w = b;
		break;
	}
	case OBS_PROPERTY_BUTTON: {
		auto *b = new QPushButton(label);
		connect(b, &QPushButton::clicked, this, [this, p]() {
			if (obs_property_button_clicked(p, source_.Get()))
				QTimer::singleShot(0, this, &PropertiesView::rebuild);
		});
		w = b;
		label.clear();
		break;
	}
	case OBS_PROPERTY_GROUP: {
		auto *g = new QGroupBox(label);
		auto *gf = new QFormLayout(g);
		obs_properties_t *content = obs_property_group_content(p);
		if (obs_property_group_type(p) == OBS_GROUP_CHECKABLE) {
			g->setCheckable(true);
			g->setChecked(obs_data_get_bool(settings_, k));
			connect(g, &QGroupBox::toggled, this, [this, p, key](bool v) { obs_data_set_bool(settings_, key.constData(), v); apply(p); });
		}
		obs_property_t *c = obs_properties_first(content);
		while (c) {
			addProperty(c, gf);
			obs_property_next(&c);
		}
		form->addRow(g);
		g->setEnabled(enabled);
		return;
	}
	default: // listas editáveis e taxa de quadros: raros nas fontes do StreamX
		w = new QLabel("(opção avançada não suportada nesta versão)");
		break;
	}
	w->setEnabled(enabled);
	if (!tip.isEmpty())
		w->setToolTip(tip);
	if (label.isEmpty())
		form->addRow(w);
	else
		form->addRow(label, w);
}

} // namespace sx
