#ifndef ARMORYSERVERSWIDGET_H
#define ARMORYSERVERSWIDGET_H

#include <QDialog>
#include <ApplicationSettings.h>

#include "ArmoryServersViewModel.h"

namespace Ui {
class ArmoryServersWidget;
}

class ArmoryServersWidget : public QDialog
{
   Q_OBJECT

public:
   explicit ArmoryServersWidget(const std::shared_ptr<ApplicationSettings>& appSettings, QWidget *parent = nullptr);
   ~ArmoryServersWidget();

public slots:
   void onAddServer();

private:
   std::unique_ptr<Ui::ArmoryServersWidget> ui_; // The main widget object.
   std::shared_ptr<ApplicationSettings> appSettings_;
   ArmoryServersViewModel *armoryServersModel;
};

#endif // ARMORYSERVERSWIDGET_H
