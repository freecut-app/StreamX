#include "SourceDialogs.h"
#include "PropertiesView.h"
#include "../core/SceneManager.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace sx {

static const struct { const char *prefix; const char *name; const char *icon; } kTypes[] = {
	{"monitor_capture", "Captura de tela", "🖥"},
	{"window_capture", "Captura de janela", "🗔"},
	{"game_capture", "Captura de jogo", "🎮"},
	{"dshow_input", "Webcam / placa de captura", "📷"},
	{"wasapi_input_capture", "Microfone / entrada de áudio", "🎙"},
	{"wasapi_output_capture", "Áudio do PC", "🔊"},
	{"wasapi_process_output_capture", "Áudio de um aplicativo", "🎧"},
	{"image_source", "Imagem", "🖼"},
	{"color_source", "Cor sólida", "🟦"},
	{"text_gdiplus", "Texto", "T"},
	{"ffmpeg_source", "Vídeo / arquivo de mídia", "🎞"},
	{"slideshow", "Apresentação de imagens", "🗂"},
};

QString friendlyTypeName(const QString &id, const QString &fallback)
{
	for (const auto &t : kTypes)
		if (id.startsWith(t.prefix))
			return QString::fromUtf8(t.name);
	return fallback;
}

QString typeIcon(const QString &id)
{
	for (const auto &t : kTypes)
		if (id.startsWith(t.prefix))
			return QString::fromUtf8(t.icon);
	return "◆";
}

obs_source_t *showAddSourceDialog(QWidget *parent)
{
	QDialog dlg(parent);
	dlg.setWindowTitle("Adicionar fonte");
	dlg.resize(460, 520);
	auto *lay = new QVBoxLayout(&dlg);
	auto *list = new QListWidget(&dlg);
	auto types = SceneManager::inputTypes();
	// tipos principais primeiro, na ordem da tabela acima
	std::stable_sort(types.begin(), types.end(), [](const SourceType &a, const SourceType &b) {
		auto rank = [](const QString &id) {
			for (int i = 0; i < int(std::size(kTypes)); i++)
				if (id.startsWith(kTypes[i].prefix))
					return i;
			return 999;
		};
		return rank(a.id) < rank(b.id);
	});
	for (const auto &t : types) {
		auto *it = new QListWidgetItem(typeIcon(t.id) + "   " + friendlyTypeName(t.id, t.name));
		it->setData(Qt::UserRole, t.id);
		it->setData(Qt::UserRole + 1, friendlyTypeName(t.id, t.name));
		list->addItem(it);
	}
	list->setCurrentRow(0);
	auto *nameEdit = new QLineEdit(&dlg);
	nameEdit->setPlaceholderText("Nome da fonte");
	QObject::connect(list, &QListWidget::currentItemChanged, &dlg, [nameEdit](QListWidgetItem *it) {
		if (it)
			nameEdit->setText(SceneManager::get().uniqueSourceName(it->data(Qt::UserRole + 1).toString()));
	});
	if (list->currentItem())
		nameEdit->setText(SceneManager::get().uniqueSourceName(list->currentItem()->data(Qt::UserRole + 1).toString()));
	auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
	bb->button(QDialogButtonBox::Ok)->setText("Adicionar");
	bb->button(QDialogButtonBox::Cancel)->setText("Cancelar");
	QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	QObject::connect(list, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
	lay->addWidget(new QLabel("Tipo de fonte"));
	lay->addWidget(list, 1);
	lay->addWidget(new QLabel("Nome"));
	lay->addWidget(nameEdit);
	lay->addWidget(bb);
	if (dlg.exec() != QDialog::Accepted || !list->currentItem())
		return nullptr;
	const QString type = list->currentItem()->data(Qt::UserRole).toString();
	OBSDataAutoRelease settings = obs_data_create();
	// captura de tela já começa no monitor principal
	if (type.startsWith("monitor_capture")) {
		const auto mons = SceneManager::listOptions("monitor_capture", "monitor_id");
		if (!mons.isEmpty())
			obs_data_set_string(settings, "monitor_id", mons.first().value.toUtf8().constData());
	}
	obs_source_t *src = SceneManager::get().addSource(type.toUtf8().constData(), nameEdit->text().trimmed().isEmpty() ? "Fonte" : nameEdit->text().trimmed(), settings);
	if (!src) {
		QMessageBox::warning(parent, "Adicionar fonte", "Não foi possível criar a fonte.");
		return nullptr;
	}
	PropertiesDialog props(src, parent);
	props.exec();
	return src;
}

/* ---------------- propriedades ---------------- */

PropertiesDialog::PropertiesDialog(obs_source_t *source, QWidget *parent) : QDialog(parent), source_(source)
{
	setWindowTitle("Propriedades: " + QString::fromUtf8(obs_source_get_name(source)));
	resize(620, 560);
	OBSDataAutoRelease s = obs_source_get_settings(source);
	original_ = obs_data_get_json(s);
	auto *lay = new QVBoxLayout(this);
	lay->addWidget(new PropertiesView(source, this), 1);
	auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	bb->button(QDialogButtonBox::Ok)->setText("OK");
	bb->button(QDialogButtonBox::Cancel)->setText("Cancelar");
	connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	lay->addWidget(bb);
}

void PropertiesDialog::reject()
{
	// Cancelar desfaz as mudanças aplicadas ao vivo
	OBSDataAutoRelease orig = obs_data_create_from_json(original_.constData());
	OBSDataAutoRelease cur = obs_source_get_settings(source_);
	obs_data_clear(cur);
	obs_data_apply(cur, orig);
	obs_source_update(source_, cur);
	QDialog::reject();
}

/* ---------------- filtros ---------------- */

FiltersDialog::FiltersDialog(obs_source_t *source, QWidget *parent) : QDialog(parent), source_(source)
{
	setWindowTitle("Filtros: " + QString::fromUtf8(obs_source_get_name(source)));
	resize(860, 560);
	auto *lay = new QHBoxLayout(this);
	auto *left = new QVBoxLayout();
	list_ = new QListWidget(this);
	list_->setMinimumWidth(240);
	auto *btns = new QHBoxLayout();
	auto *add = new QPushButton("＋ Adicionar");
	auto *rem = new QPushButton("Remover");
	auto *up = new QPushButton("▲");
	auto *down = new QPushButton("▼");
	up->setToolTip("Mover para cima");
	down->setToolTip("Mover para baixo");
	btns->addWidget(add);
	btns->addWidget(rem);
	btns->addWidget(up);
	btns->addWidget(down);
	left->addWidget(list_, 1);
	left->addLayout(btns);
	pane_ = new QStackedWidget(this);
	lay->addLayout(left);
	lay->addWidget(pane_, 1);

	const uint32_t flags = obs_source_get_output_flags(source);
	connect(add, &QPushButton::clicked, this, [this, add, flags]() {
		QMenu menu;
		for (const auto &t : SceneManager::filterTypes(flags & OBS_SOURCE_VIDEO, flags & OBS_SOURCE_AUDIO)) {
			// filtros de áudio só para fontes com áudio; de vídeo, só para fontes com vídeo
			if ((t.audio && !(flags & OBS_SOURCE_AUDIO)) || (t.video && !t.audio && !(flags & OBS_SOURCE_VIDEO)))
				continue;
			menu.addAction(t.name, this, [this, t]() {
				QString base = t.name;
				QString n = base;
				for (int i = 2; ; i++) {
					OBSSourceAutoRelease ex = obs_source_get_filter_by_name(source_, n.toUtf8().constData());
					if (!ex) break;
					n = QString("%1 %2").arg(base).arg(i);
				}
				OBSSourceAutoRelease f = obs_source_create(t.id.toUtf8().constData(), n.toUtf8().constData(), nullptr, nullptr);
				if (f) {
					obs_source_filter_add(source_, f);
					refresh(list_->count());
				}
			});
		}
		menu.exec(add->mapToGlobal(QPoint(0, add->height())));
	});
	connect(rem, &QPushButton::clicked, this, [this]() {
		const int r = list_->currentRow();
		const auto fs = filters();
		if (r < 0 || r >= fs.size())
			return;
		obs_source_filter_remove(source_, fs[r]);
		refresh(qMax(0, r - 1));
	});
	auto move = [this](bool upDir) {
		const int r = list_->currentRow();
		const auto fs = filters();
		if (r < 0 || r >= fs.size())
			return;
		obs_source_filter_set_order(source_, fs[r], upDir ? OBS_ORDER_MOVE_UP : OBS_ORDER_MOVE_DOWN);
		refresh(qBound(0, r + (upDir ? -1 : 1), int(fs.size()) - 1));
	};
	connect(up, &QPushButton::clicked, this, [move]() { move(true); });
	connect(down, &QPushButton::clicked, this, [move]() { move(false); });
	connect(list_, &QListWidget::currentRowChanged, this, &FiltersDialog::showSelected);
	connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
		const auto fs = filters();
		const int r = list_->row(it);
		if (r >= 0 && r < fs.size())
			obs_source_set_enabled(fs[r], it->checkState() == Qt::Checked);
	});
	refresh(0);
}

QList<OBSSource> FiltersDialog::filters() const
{
	QList<OBSSource> out;
	obs_source_enum_filters(source_, [](obs_source_t *, obs_source_t *f, void *p) {
		static_cast<QList<OBSSource> *>(p)->append(OBSSource(f));
	}, &out);
	return out;
}

void FiltersDialog::refresh(int select)
{
	list_->blockSignals(true);
	list_->clear();
	for (const auto &f : filters()) {
		auto *it = new QListWidgetItem(QString::fromUtf8(obs_source_get_name(f)));
		it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
		it->setCheckState(obs_source_enabled(f) ? Qt::Checked : Qt::Unchecked);
		list_->addItem(it);
	}
	list_->blockSignals(false);
	if (list_->count())
		list_->setCurrentRow(qBound(0, select, list_->count() - 1));
	showSelected();
}

void FiltersDialog::showSelected()
{
	while (pane_->count()) {
		QWidget *w = pane_->widget(0);
		pane_->removeWidget(w);
		w->deleteLater();
	}
	const int r = list_->currentRow();
	const auto fs = filters();
	if (r < 0 || r >= fs.size()) {
		auto *l = new QLabel("Sem filtros. Clique em “＋ Adicionar” para aplicar, por exemplo, redução de ruído no microfone ou chroma key na webcam.");
		l->setWordWrap(true);
		l->setAlignment(Qt::AlignCenter);
		pane_->addWidget(l);
		return;
	}
	pane_->addWidget(new PropertiesView(fs[r], pane_));
}

} // namespace sx
