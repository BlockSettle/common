#include <QClipboard>
#include <QFileDialog>
#include <QStandardPaths>

#include "NetworkSettingsPage.h"
#include "ui_NetworkSettingsPage.h"
#include "ApplicationSettings.h"
#include "ArmoryServersWidget.h"

enum EnvConfiguration
{
   CustomConfiguration,
   StagingConfiguration,
   UATConfiguration,
   PRODConfiguration
};

struct EnvSettings
{
   QString  pubHost;
   int      pubPort;
};

NetworkSettingsPage::NetworkSettingsPage(QWidget* parent)
   : SettingsPage{parent}
   , ui_{new Ui::NetworkSettingsPage}
{
   ui_->setupUi(this);

   connect(ui_->runArmoryDBLocallyCheckBox, &QCheckBox::clicked
      , this, &NetworkSettingsPage::onRunArmoryLocallyChecked);

   connect(ui_->checkBoxTestnet, &QCheckBox::clicked
      , this, &NetworkSettingsPage::onNetworkClicked);

   connect(ui_->armoryDBHostLineEdit, &QLineEdit::editingFinished, this, &NetworkSettingsPage::onArmoryHostChanged);
   connect(ui_->armoryDBPortLineEdit, &QLineEdit::editingFinished, this, &NetworkSettingsPage::onArmoryPortChanged);
   connect(ui_->pushButtonManageArmory, &QPushButton::clicked, this, [this](){
      ArmoryServersWidget armoryServersWidget(appSettings_, this);
      connect(&armoryServersWidget, &ArmoryServersWidget::reconnectArmory, this, [this](){
         emit reconnectArmory();
      });
      armoryServersWidget.exec();


      // todo: refresh current selected server
   });

   connect(ui_->pushButtonArmoryServerKeyCopy, &QPushButton::clicked, this, [this](){
      qApp->clipboard()->setText(ui_->labelArmoryServerKey->text());
   });
   connect(ui_->pushButtonArmoryServerKeySave, &QPushButton::clicked, this, [this](){
      QString fileName = QFileDialog::getSaveFileName(this
                                   , tr("Save Armory Public Key")
                                   , QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                   , tr("Key files (*.pub)"));

      QFile file(fileName);
      if (file.open(QIODevice::WriteOnly)) {
         file.write(ui_->labelArmoryServerKey->text().toLatin1());
      }
   });

   connect(ui_->pushButtonArmoryTerminalKeyCopy, &QPushButton::clicked, this, [this](){
      qApp->clipboard()->setText(ui_->labelArmoryTerminalKey->text());
   });
   connect(ui_->pushButtonArmoryTerminalKeySave, &QPushButton::clicked, this, [this](){
      QString fileName = QFileDialog::getSaveFileName(this
                                   , tr("Save Armory Public Key")
                                   , QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                   , tr("Key files (*.pub)"));

      QFile file(fileName);
      if (file.open(QIODevice::WriteOnly)) {
         file.write(ui_->labelArmoryTerminalKey->text().toLatin1());
      }
   });


   ui_->ArmorySettingsGroupBoxOld->hide();
   ui_->PublicBridgeSettingsGroup->hide();
}

NetworkSettingsPage::~NetworkSettingsPage() = default;

void NetworkSettingsPage::display()
{
   armoryServerComboBoxModel = new ArmoryServersViewModel(appSettings_);
   ui_->checkBoxTestnet->setChecked(appSettings_->get<NetworkType>(ApplicationSettings::netType) == NetworkType::TestNet);

   if (appSettings_->get<bool>(ApplicationSettings::runArmoryLocally)) {
      ui_->runArmoryDBLocallyCheckBox->setChecked(true);
      DisplayRunArmorySettings(true);
   } else {
      ui_->runArmoryDBLocallyCheckBox->setChecked(false);
      DisplayRunArmorySettings(false);
   }

#ifdef PRODUCTION_BUILD
   ui_->PublicBridgeSettingsGroup->hide();
#endif

   ui_->lineEditPublicBridgeHost->setText(appSettings_->get<QString>(ApplicationSettings::pubBridgeHost));
   ui_->spinBoxPublicBridgePort->setValue(appSettings_->get<int>(ApplicationSettings::pubBridgePort));
   ui_->lineEditPublicBridgeHost->setText(appSettings_->get<QString>(ApplicationSettings::pubBridgeHost));
   ui_->spinBoxPublicBridgePort->setValue(appSettings_->get<int>(ApplicationSettings::pubBridgePort));

   ui_->comboBoxArmoryServer->setModel(armoryServerComboBoxModel);
}

void NetworkSettingsPage::reset()
{
   for (const auto &setting : { ApplicationSettings::runArmoryLocally, ApplicationSettings::netType
      , ApplicationSettings::pubBridgeHost, ApplicationSettings::pubBridgePort
      , ApplicationSettings::armoryDbIp, ApplicationSettings::armoryDbPort}) {
      appSettings_->reset(setting, false);
   }
   display();
}

void NetworkSettingsPage::onArmoryHostChanged()
{
   appSettings_->set(ApplicationSettings::armoryDbIp, ui_->armoryDBHostLineEdit->text());
}

void NetworkSettingsPage::onArmoryPortChanged()
{
   appSettings_->set(ApplicationSettings::armoryDbPort, ui_->armoryDBPortLineEdit->text());
}

void NetworkSettingsPage::DisplayRunArmorySettings(bool runLocally)
{
   auto networkType = ui_->checkBoxTestnet->isChecked()
      ? NetworkType::TestNet
      : NetworkType::MainNet;

   if (runLocally) {
      ui_->armoryDBHostLineEdit->setText(QString::fromStdString("localhost"));

      ui_->armoryDBPortLineEdit->setText(QString::number(appSettings_->GetDefaultArmoryLocalPort(networkType)));
      ui_->armoryDBHostLineEdit->setEnabled(false);
      ui_->armoryDBPortLineEdit->setEnabled(false);
   } else {
      ui_->armoryDBHostLineEdit->setEnabled(true);
      ui_->armoryDBPortLineEdit->setEnabled(true);

      const QString &host = appSettings_->get<QString>(ApplicationSettings::armoryDbIp);
      const QString &port = appSettings_->GetArmoryRemotePort(networkType);
      ui_->armoryDBHostLineEdit->setText(host);
      ui_->armoryDBPortLineEdit->setText(port);
      ui_->comboBoxArmoryServer->addItem(host + QStringLiteral(":") + port);
      ui_->comboBoxArmoryServer->setCurrentIndex(1);

      ui_->labelArmoryServerNetwork->setText(appSettings_->get<int>(ApplicationSettings::netType) == 0 ? tr("MainNet") : tr("TestNet"));
      ui_->labelArmoryServerAddress->setText(host);
      ui_->labelArmoryServerPort->setText(port);
   }
}

void NetworkSettingsPage::apply()
{
//   appSettings_->set(ApplicationSettings::netType, ui_->checkBoxTestnet->isChecked()
//      ? (int)NetworkType::TestNet : (int)NetworkType::MainNet);

   if (ui_->runArmoryDBLocallyCheckBox->isChecked()) {
      appSettings_->set(ApplicationSettings::runArmoryLocally, true);
   } else {
      appSettings_->set(ApplicationSettings::runArmoryLocally, false);
//      appSettings_->set(ApplicationSettings::armoryDbIp, ui_->armoryDBHostLineEdit->text());
//      appSettings_->set(ApplicationSettings::armoryDbPort, ui_->armoryDBPortLineEdit->text());

      const QStringList &servers = appSettings_->get<QStringList>(ApplicationSettings::armoryServers);
      if (servers.size() > ui_->comboBoxArmoryServer->currentIndex()) {
         const QString &server = servers.at(ui_->comboBoxArmoryServer->currentIndex());
         if (server.split(QStringLiteral(":")).size() == 5) {
            const QStringList &serverData = server.split(QStringLiteral(":"));
            appSettings_->set(ApplicationSettings::armoryDbIp, serverData.at(2));
            appSettings_->set(ApplicationSettings::armoryDbPort, serverData.at(3));

            appSettings_->set(ApplicationSettings::netType, serverData.at(1) == QStringLiteral("1")
               ? (int)NetworkType::TestNet : (int)NetworkType::MainNet);
         }
      }

   }

   appSettings_->set(ApplicationSettings::pubBridgeHost, ui_->lineEditPublicBridgeHost->text());
   appSettings_->set(ApplicationSettings::pubBridgePort, ui_->spinBoxPublicBridgePort->value());
}

void NetworkSettingsPage::onRunArmoryLocallyChecked(bool checked)
{
   DisplayRunArmorySettings(checked);
}

void NetworkSettingsPage::onNetworkClicked(bool)
{
   DisplayRunArmorySettings(ui_->runArmoryDBLocallyCheckBox->isChecked());
}
