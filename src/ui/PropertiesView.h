// StreamX — editor de propriedades gerado a partir das propriedades reais do plugin (obs_properties)
#pragma once
#include <QScrollArea>
#include <obs.hpp>

class QFormLayout;

namespace sx {

class PropertiesView : public QScrollArea {
	Q_OBJECT
public:
	// Edita as configurações de uma fonte/filtro/transição; as mudanças são aplicadas ao vivo
	explicit PropertiesView(obs_source_t *source, QWidget *parent = nullptr);
	~PropertiesView() override;

signals:
	void changed();

private:
	void rebuild();
	void addProperty(obs_property_t *p, QFormLayout *form);
	void apply(obs_property_t *p);
	OBSSource source_;
	OBSData settings_;
	obs_properties_t *props_ = nullptr;
	bool rebuilding_ = false;
};

} // namespace sx
