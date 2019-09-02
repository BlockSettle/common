#ifndef AUTOSQWIDGET_H
#define AUTOSQWIDGET_H

#include <QWidget>

class AutoSQProvider;

namespace Ui {
class AutoSQWidget;
}

class AutoSQWidget : public QWidget
{
   Q_OBJECT

public:
   explicit AutoSQWidget(QWidget *parent = nullptr);
   ~AutoSQWidget();

   void init();

public slots:
   void onAutoSignStateChanged(const std::string &walletId, bool active);

private slots:
   void aqFillHistory();
   void aqScriptChanged(int curIndex);

   void checkBoxAQClicked();

   void onSignerStateUpdated(bool autoSignAvailable);
   void onAutoSignActivated();

private:
   void validateGUI();
   QString askForAQScript();

private:
   std::unique_ptr<Ui::AutoSQWidget>      ui_;
   std::shared_ptr<AutoSQProvider>        autoSQProvider_;
};

#endif // AUTOSQWIDGET_H
