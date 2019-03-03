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

bool operator == (const EnvSettings& l, const EnvSettings& r)
{
   return l.pubHost == r.pubHost
      && l.pubPort == r.pubPort;
}

static const EnvSettings StagingEnvSettings {
   QLatin1String("185.213.153.45"), 9091 };

static const EnvSettings UATEnvSettings{
   QLatin1String("185.213.153.44"), 9091 };

static const EnvSettings PRODEnvSettings{
   QLatin1String("185.213.153.36"), 9091 };

NetworkSettingsPage::NetworkSettingsPage(QWidget* parent)
   : SettingsPage{parent}
   , ui_{new Ui::NetworkSettingsPage}
{
   ui_->setupUi(this);

   ui_->comboBoxEnvironment->addItem(tr("Custom"));
   ui_->comboBoxEnvironment->addItem(tr("Staging"));
   ui_->comboBoxEnvironment->addItem(tr("UAT"));
   ui_->comboBoxEnvironment->addItem(tr("PROD"));

   connect(ui_->pushButtonManageArmory, &QPushButton::clicked, this, [this](){
      QDialog *d = new QDialog(this);
      QVBoxLayout *l = new QVBoxLayout(d);
      d->setLayout(l);

      ArmoryServersWidget *armoryServersWidget = new ArmoryServersWidget(armoryServersProvider_, this);

      // TODO: fix stylesheet to support popup widgets
//      armoryServersWidget->setWindowModality(Qt::ApplicationModal);
//      armoryServersWidget->setWindowFlags(Qt::Dialog);
      l->addWidget(armoryServersWidget);
      //connect(armoryServersWidget, &ArmoryServersWidget::destroyed, d, &QDialog::deleteLater);

      connect(armoryServersWidget, &ArmoryServersWidget::reconnectArmory, this, [this](){
         emit reconnectArmory();
      });
      connect(armoryServersWidget, &ArmoryServersWidget::needClose, this, [d](){
         d->reject();
      });

      d->exec();

      //armoryServersWidget->show();
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

   connect(armoryServersProvider_.get(), &ArmoryServersProvider::dataChanged, this, &NetworkSettingsPage::display);
}

void NetworkSettingsPage::initSettings()
{
   armoryServerModel_ = new ArmoryServersViewModel(armoryServersProvider_);
   armoryServerModel_->setHighLightSelectedServer(false);
   ui_->comboBoxArmoryServer->setModel(armoryServerModel_);

   connect(armoryServersProvider_.get(), &ArmoryServersProvider::dataChanged, this, &NetworkSettingsPage::displayArmorySettings);
   connect(ui_->comboBoxEnvironment, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NetworkSettingsPage::onEnvSelected);
}

NetworkSettingsPage::~NetworkSettingsPage() = default;

void NetworkSettingsPage::display()
{
#ifdef PRODUCTION_BUILD
   ui_->PublicBridgeSettingsGroup->hide();
#endif

   displayArmorySettings();
   displayEnvironmentSettings();
}

void NetworkSettingsPage::DetectEnvironmentSettings()
{
   int index = CustomConfiguration;

   EnvSettings currentConfiguration{
      ui_->lineEditPublicBridgeHost->text(),
      ui_->spinBoxPublicBridgePort->value()
   };

   if (currentConfiguration == StagingEnvSettings) {
      index = StagingConfiguration;
   } else if (currentConfiguration == UATEnvSettings) {
      index = UATConfiguration;
   } else if (currentConfiguration == PRODEnvSettings) {
      index = PRODConfiguration;
   }

   ui_->comboBoxEnvironment->setCurrentIndex(index);
}

void NetworkSettingsPage::displayArmorySettings()
{
   int serverIndex = armoryServersProvider_->indexOf(appSettings_->get<QString>(ApplicationSettings::armoryDbName));
   if (serverIndex >= 0) {
      ArmoryServer server = armoryServersProvider_->servers().at(serverIndex);

      int port = appSettings_->GetArmoryRemotePort(server.netType);
      ui_->comboBoxArmoryServer->setCurrentIndex(serverIndex);
      ui_->labelArmoryServerNetwork->setText(server.netType == NetworkType::MainNet ? tr("MainNet") : tr("TestNet"));
      ui_->labelArmoryServerAddress->setText(server.armoryDBIp);
      ui_->labelArmoryServerPort->setText(QString::number(port));
      ui_->labelArmoryServerKey->setText(server.armoryDBKey);
   }

}

void NetworkSettingsPage::displayEnvironmentSettings()
{
   ui_->lineEditPublicBridgeHost->setText(appSettings_->get<QString>(ApplicationSettings::pubBridgeHost));
   ui_->spinBoxPublicBridgePort->setValue(appSettings_->get<int>(ApplicationSettings::pubBridgePort));
   ui_->spinBoxPublicBridgePort->setEnabled(false);

   DetectEnvironmentSettings();
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

void NetworkSettingsPage::apply()
{
   armoryServersProvider_->setupServer(ui_->comboBoxArmoryServer->currentIndex());

   appSettings_->set(ApplicationSettings::pubBridgeHost, ui_->lineEditPublicBridgeHost->text());
   appSettings_->set(ApplicationSettings::pubBridgePort, ui_->spinBoxPublicBridgePort->value());
}

void NetworkSettingsPage::onEnvSelected(int index)
{
   if (index != CustomConfiguration) {
      const EnvSettings *settings = nullptr;
      if (index == StagingConfiguration) {
         settings = &StagingEnvSettings;
      } else if (index == UATConfiguration) {
         settings = &UATEnvSettings;
      } else  {
         settings = &PRODEnvSettings;
      }

      ui_->lineEditPublicBridgeHost->setText(settings->pubHost);
      ui_->lineEditPublicBridgeHost->setEnabled(false);
      ui_->spinBoxPublicBridgePort->setValue(settings->pubPort);
      ui_->spinBoxPublicBridgePort->setEnabled(false);
   } else {
      ui_->lineEditPublicBridgeHost->setEnabled(true);
      ui_->spinBoxPublicBridgePort->setEnabled(true);
   }
}
