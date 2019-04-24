#ifndef ARMORYSERVERSWIDGET_H
#define ARMORYSERVERSWIDGET_H

#include <QWidget>
#include <ApplicationSettings.h>

#include "ArmoryServersProvider.h"
#include "ArmoryServersViewModel.h"

namespace Ui {
class ArmoryServersWidget;
}

class ArmoryServersWidget : public QWidget
{
   Q_OBJECT

public:
   explicit ArmoryServersWidget(const std::shared_ptr<ArmoryServersProvider>& armoryServersProvider
                                , std::shared_ptr<ApplicationSettings> appSettings
                                , QWidget *parent = nullptr);
   ~ArmoryServersWidget();

   void adaptForStartupDialog();

public slots:
   void onAddServer();
   void onDeleteServer();
   void onEdit();
   void onSelect();
   void onSave();
   void onConnect();

signals:
   void reconnectArmory();
   void needClose();

private slots:
   void resetForm();

private:
   std::unique_ptr<Ui::ArmoryServersWidget> ui_; // The main widget object.
   std::shared_ptr<ArmoryServersProvider> armoryServersProvider_;
   std::shared_ptr<ApplicationSettings> appSettings_;

   ArmoryServersViewModel *armoryServersModel;
   bool isStartupDialog;
};

#endif // ARMORYSERVERSWIDGET_H
