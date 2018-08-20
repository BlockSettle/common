#include "SelectWalletDialog.h"
#include "ui_SelectWalletDialog.h"
#include "WalletsManager.h"
#include "WalletsViewModel.h"
#include "ApplicationSettings.h"
#include <QPushButton>


SelectWalletDialog::SelectWalletDialog(const std::shared_ptr<WalletsManager> &walletsManager, const std::string &selWalletId, QWidget* parent)
   : QDialog(parent)
   , ui_(new Ui::SelectWalletDialog)
   , walletsManager_(walletsManager)
{
   ui_->setupUi(this);

   ui_->buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Select"));

   walletsModel_ = new WalletsViewModel(walletsManager, selWalletId, nullptr, ui_->treeViewWallets, true);
   ui_->treeViewWallets->setModel(walletsModel_);
   ui_->treeViewWallets->setItemsExpandable(true);
   ui_->treeViewWallets->setRootIsDecorated(true);
   walletsModel_->LoadWallets();
   ui_->treeViewWallets->header()->setSectionResizeMode(QHeaderView::ResizeToContents);

   connect(ui_->treeViewWallets->selectionModel(), &QItemSelectionModel::selectionChanged
           , this, &SelectWalletDialog::onSelectionChanged);
   connect(ui_->treeViewWallets, &QTreeView::doubleClicked
           , this, &SelectWalletDialog::onDoubleClicked);

   connect(ui_->buttonBox, &QDialogButtonBox::accepted, this, &SelectWalletDialog::accept);
   connect(ui_->buttonBox, &QDialogButtonBox::rejected, this, &SelectWalletDialog::reject);

   onSelectionChanged();
}

SelectWalletDialog::~SelectWalletDialog()
{}

void SelectWalletDialog::onSelectionChanged()
{
   auto selectedRows = ui_->treeViewWallets->selectionModel()->selectedRows();
   if (selectedRows.size() == 1) {
      selectedWallet_ = walletsModel_->getWallet(selectedRows[0]);
   }
   else {
      selectedWallet_ = nullptr;
   }
   walletsModel_->setSelectedWallet(selectedWallet_);

   auto okButton = ui_->buttonBox->button(QDialogButtonBox::Ok);
   okButton->setEnabled(selectedWallet_ != nullptr);
}

std::shared_ptr<bs::Wallet> SelectWalletDialog::getSelectedWallet() const
{
   return selectedWallet_;
}

void SelectWalletDialog::onDoubleClicked(const QModelIndex& index)
{
   selectedWallet_ = walletsModel_->getWallet(index);
   QDialog::accept();
}

bool SelectWalletDialog::isNestedSegWitAddress() const
{
   return ui_->radioButtonAddrNestedSegWit->isChecked();
}
