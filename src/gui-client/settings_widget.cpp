#include "settings_widget.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace veil::gui {

SettingsWidget::SettingsWidget(QWidget* parent) : QWidget(parent) {
  setupUi();
}

void SettingsWidget::setupUi() {
  auto* layout = new QVBoxLayout(this);
  layout->setSpacing(20);
  layout->setContentsMargins(40, 20, 40, 20);

  // Back button
  auto* backButton = new QPushButton("← Back", this);
  backButton->setStyleSheet("background: #252932; padding: 8px;");
  connect(backButton, &QPushButton::clicked, this, &backRequested);
  layout->addWidget(backButton);

  // Title
  auto* titleLabel = new QLabel("Settings", this);
  titleLabel->setStyleSheet("font-size: 24px; font-weight: 700;");
  layout->addWidget(titleLabel);

  // Server Configuration
  auto* serverGroup = new QGroupBox("Server Configuration", this);
  auto* serverLayout = new QVBoxLayout(serverGroup);
  serverLayout->addWidget(new QLabel("Server Address:"));
  auto* serverEdit = new QLineEdit("vpn.example.com", serverGroup);
  serverLayout->addWidget(serverEdit);
  serverLayout->addWidget(new QLabel("Port:"));
  auto* portSpin = new QSpinBox(serverGroup);
  portSpin->setRange(1, 65535);
  portSpin->setValue(4433);
  serverLayout->addWidget(portSpin);
  layout->addWidget(serverGroup);

  // Routing
  auto* routingGroup = new QGroupBox("Routing", this);
  auto* routingLayout = new QVBoxLayout(routingGroup);
  routingLayout->addWidget(new QCheckBox("Route all traffic through VPN"));
  routingLayout->addWidget(new QCheckBox("Split tunnel mode"));
  layout->addWidget(routingGroup);

  // Connection
  auto* connGroup = new QGroupBox("Connection", this);
  auto* connLayout = new QVBoxLayout(connGroup);
  connLayout->addWidget(new QCheckBox("Auto-reconnect on disconnect"));
  layout->addWidget(connGroup);

  // Advanced
  auto* advGroup = new QGroupBox("Advanced", this);
  auto* advLayout = new QVBoxLayout(advGroup);
  advLayout->addWidget(new QCheckBox("Enable obfuscation"));
  advLayout->addWidget(new QCheckBox("Verbose logging"));
  advLayout->addWidget(new QCheckBox("Developer mode"));
  layout->addWidget(advGroup);

  layout->addStretch();

  // Save button
  auto* saveButton = new QPushButton("Save Changes", this);
  layout->addWidget(saveButton);
}

}  // namespace veil::gui
