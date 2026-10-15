// StreamX — diálogos de fontes: adicionar, propriedades e filtros
#pragma once
#include <QDialog>
#include <obs.hpp>

class QListWidget;
class QLineEdit;
class QStackedWidget;

namespace sx {

QString friendlyTypeName(const QString &typeId, const QString &fallback);
QString typeIcon(const QString &typeId);

// Escolher tipo + nome e criar a fonte na cena atual. Retorna a fonte criada ou nullptr.
obs_source_t *showAddSourceDialog(QWidget *parent);

class PropertiesDialog : public QDialog {
	Q_OBJECT
public:
	PropertiesDialog(obs_source_t *source, QWidget *parent = nullptr);
	void reject() override;

private:
	OBSSource source_;
	QByteArray original_; // JSON das configurações originais (para Cancelar)
};

class FiltersDialog : public QDialog {
	Q_OBJECT
public:
	FiltersDialog(obs_source_t *source, QWidget *parent = nullptr);

private:
	void refresh(int select = -1);
	void showSelected();
	QList<OBSSource> filters() const;
	OBSSource source_;
	QListWidget *list_ = nullptr;
	QStackedWidget *pane_ = nullptr;
};

} // namespace sx
