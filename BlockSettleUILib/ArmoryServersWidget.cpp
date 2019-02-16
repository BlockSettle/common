#include "ArmoryServersWidget.h"
#include "ui_ArmoryServersWidget.h"


ArmoryServersWidget::ArmoryServersWidget(const std::shared_ptr<ApplicationSettings> &appSettings, QWidget *parent) :
   QDialog(parent)
   , appSettings_(appSettings)
   , ui_(new Ui::ArmoryServersWidget)
   , armoryServersModel(new ArmoryServersViewModel(appSettings))
{
   ui_->setupUi(this);
   ui_->tableViewArmory->setModel(armoryServersModel);
   ui_->buttonBox->setStandardButtons(QDialogButtonBox::Ok);
   ui_->tableViewArmory->horizontalHeader()->setStretchLastSection(true);

   connect(ui_->pushButtonAddServer, &QPushButton::clicked, this, &ArmoryServersWidget::onAddServer);
}

ArmoryServersWidget::~ArmoryServersWidget() = default;

void ArmoryServersWidget::onAddServer()
{
   QStringList servers = appSettings_->get<QStringList>(ApplicationSettings::armoryServers);
   QString server = QString(QStringLiteral("%1:%2:%3"))
         .arg(ui_->lineEditAddress->text())
         .arg(ui_->spinBoxPort->value())
         .arg(ui_->lineEditKey->text());
   servers.append(server);
   appSettings_->set(ApplicationSettings::armoryServers, servers);
   armoryServersModel->reloadServers();
}
