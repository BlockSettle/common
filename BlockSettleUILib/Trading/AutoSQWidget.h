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

   void init(const std::shared_ptr<AutoSQProvider> &autoSQProvider);

public slots:
   void onAutoSignStateChanged(const std::string &walletId, bool active);
   void onAutoSQAvailChanged();

   void onAqScriptLoaded();
   void onAqScriptUnloaded();

private slots:
   void aqFillHistory();
   void aqScriptChanged(int curIndex);

   void onAutoQuoteToggled();
   void onAutoSignToggled();

private:
   QString askForAQScript();
   void validateGUI();

private:
   std::unique_ptr<Ui::AutoSQWidget>      ui_;
   std::shared_ptr<AutoSQProvider>        autoSQProvider_;
};

#endif // AUTOSQWIDGET_H
