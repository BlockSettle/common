#include "ArmoryServersWidget.h"
#include "ui_ArmoryServersWidget.h"
#include <QDebug>

ArmoryServersWidget::ArmoryServersWidget(const std::shared_ptr<ApplicationSettings> &appSettings, QWidget *parent) :
   QDialog(parent)
   , appSettings_(appSettings)
   , ui_(new Ui::ArmoryServersWidget)
   , armoryServersModel(new ArmoryServersViewModel(appSettings))
{
   ui_->setupUi(this);
   ui_->lineEditKey->setVisible(false);
   ui_->labelKey->setVisible(false);

   //ui_->tableViewArmory->horizontalHeader()->setStretchLastSection(true);
   ui_->tableViewArmory->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
   ui_->tableViewArmory->setModel(armoryServersModel);
   ui_->buttonBox->setStandardButtons(QDialogButtonBox::Ok);

   ui_->lineEditKey->setVisible(false);
   connect(ui_->comboBoxPrivacy, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index){
      ui_->lineEditKey->setVisible(index == 1);
      ui_->labelKey->setVisible(index == 1);
   });

   connect(ui_->pushButtonAddServer, &QPushButton::clicked, this, &ArmoryServersWidget::onAddServer);
   connect(ui_->pushButtonDeleteServer, &QPushButton::clicked, this, &ArmoryServersWidget::onDeleteServer);
   connect(ui_->pushButtonConnect, &QPushButton::clicked, this, &ArmoryServersWidget::onConnect);

   connect(ui_->tableViewArmory->selectionModel(), &QItemSelectionModel::selectionChanged, this,
           [this](const QItemSelection &selected, const QItemSelection &deselected){
      ui_->pushButtonDeleteServer->setDisabled(ui_->tableViewArmory->selectionModel()->selectedIndexes().isEmpty());
      ui_->pushButtonConnect->setDisabled(ui_->tableViewArmory->selectionModel()->selectedIndexes().isEmpty());
   });
}

ArmoryServersWidget::~ArmoryServersWidget() = default;

void ArmoryServersWidget::onAddServer()
{
   if (ui_->lineEditName->text().isEmpty() || ui_->lineEditAddress->text().isEmpty())
      return;

   if (ui_->lineEditKey->text().isEmpty() && ui_->comboBoxNetworkType->currentIndex() == 1)
      return;

   QStringList servers = appSettings_->get<QStringList>(ApplicationSettings::armoryServers);
   QString server = QString(QStringLiteral("%1:%2:%3:%4:%5"))
         .arg(ui_->lineEditName->text())
         .arg(ui_->comboBoxNetworkType->currentIndex())
         .arg(ui_->lineEditAddress->text())
         .arg(ui_->spinBoxPort->value())
         .arg(ui_->lineEditKey->text());
   servers.append(server);
   appSettings_->set(ApplicationSettings::armoryServers, servers);
   armoryServersModel->reloadServers();
}

void ArmoryServersWidget::onDeleteServer()
{
   if (ui_->tableViewArmory->selectionModel()->selectedRows(0).isEmpty()) {
      return;
   }
   QStringList servers = appSettings_->get<QStringList>(ApplicationSettings::armoryServers);
   servers.removeAt(ui_->tableViewArmory->selectionModel()->selectedRows(0).first().row());

   appSettings_->set(ApplicationSettings::armoryServers, servers);
   armoryServersModel->reloadServers();
}

void ArmoryServersWidget::onConnect()
{
   emit reconnectArmory();
}
