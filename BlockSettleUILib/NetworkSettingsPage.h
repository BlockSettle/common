#ifndef __NETWORK_SETTINGS_PAGE_H__
#define __NETWORK_SETTINGS_PAGE_H__

#include <memory>
#include "ConfigDialog.h"
#include "ArmoryServersViewModel.h"

namespace Ui {
   class NetworkSettingsPage;
}

class ApplicationSettings;

class NetworkSettingsPage : public SettingsPage
{
   Q_OBJECT

public:
   NetworkSettingsPage(QWidget* parent = nullptr);
   ~NetworkSettingsPage() override;

   void display() override;
   void reset() override;
   void apply() override;

private slots:
   void onRunArmoryLocallyChecked(bool checked);
   void onNetworkClicked(bool checked);

   void onArmoryHostChanged();
   void onArmoryPortChanged();

signals:
   void reconnectArmory();

private:
   void DisplayRunArmorySettings(bool runLocally);

private:
   std::unique_ptr<Ui::NetworkSettingsPage> ui_;
   ArmoryServersViewModel *armoryServerComboBoxModel;
};

#endif // __NETWORK_SETTINGS_PAGE_H__
