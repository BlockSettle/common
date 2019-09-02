#include "AutoSQWidget.h"
#include "ui_AutoSQWidget.h"

#include "AutoSQProvider.h"
#include "BSMessageBox.h"

#include <QFileDialog>
#include <QFileInfo>

constexpr int kSelectAQFileItemIndex = 1;

namespace {

} // namespace


AutoSQWidget::AutoSQWidget(QWidget *parent) :
   QWidget(parent),
   ui_(new Ui::AutoSQWidget)
{
   ui_->setupUi(this);

   connect(ui_->checkBoxAQ, &ToggleSwitch::clicked, this, &AutoSQWidget::onAutoQuoteToggled);
   connect(ui_->comboBoxAQScript, SIGNAL(activated(int)), this, SLOT(aqScriptChanged(int)));
   connect(ui_->checkBoxAutoSign, &ToggleSwitch::clicked, this, &AutoSQWidget::onAutoSignToggled);

   ui_->comboBoxAQScript->setFirstItemHidden(true);
}

void AutoSQWidget::init(const std::shared_ptr<AutoSQProvider> &autoSQProvider)
{
   autoSQProvider_ = autoSQProvider;
   aqFillHistory();
   onAutoSQAvailChanged();

   connect(autoSQProvider_.get(), &AutoSQProvider::autoSQAvailabilityChanged, this, &AutoSQWidget::onAutoSQAvailChanged);
   connect(autoSQProvider_.get(), &AutoSQProvider::autoSignStateChanged, this, &AutoSQWidget::onAutoSignStateChanged);

   connect(autoSQProvider_.get(), &AutoSQProvider::aqScriptLoaded, this, &AutoSQWidget::onAqScriptLoaded);
   connect(autoSQProvider_.get(), &AutoSQProvider::aqScriptUnLoaded, this, &AutoSQWidget::onAqScriptUnloaded);
   connect(autoSQProvider_.get(), &AutoSQProvider::aqHistoryChanged, this, &AutoSQWidget::aqFillHistory);
}

AutoSQWidget::~AutoSQWidget() = default;

void AutoSQWidget::onAutoQuoteToggled()
{
   bool isValidScript = (ui_->comboBoxAQScript->currentIndex() > kSelectAQFileItemIndex);
   if (ui_->checkBoxAQ->isChecked() && !isValidScript) {
      BSMessageBox question(BSMessageBox::question
         , tr("Try to enable Auto Quoting")
         , tr("Auto Quoting Script is not specified. Do you want to select a script from file?"));
      const bool answerYes = (question.exec() == QDialog::Accepted);
      if (answerYes) {
         const auto scriptFileName = askForAQScript();
         if (scriptFileName.isEmpty()) {
            ui_->checkBoxAQ->setChecked(false);
         } else {
            autoSQProvider_->initAQ(scriptFileName);
         }
      } else {
         ui_->checkBoxAQ->setChecked(false);
      }
   }

   if (autoSQProvider_->aqLoaded()) {
      autoSQProvider_->setAqLoaded(false);
   } else {
      autoSQProvider_->initAQ(ui_->comboBoxAQScript->currentData().toString());
   }

   validateGUI();
}

void AutoSQWidget::onAutoSignStateChanged(const std::string &walletId, bool active)
{
   ui_->checkBoxAutoSign->setChecked(active);
}

void AutoSQWidget::onAutoSQAvailChanged()
{
   ui_->groupBoxAutoSign->setEnabled(autoSQProvider_->autoSQAvailable());
   ui_->groupBoxAutoQuote->setEnabled(autoSQProvider_->autoSQAvailable());

   ui_->checkBoxAutoSign->setChecked(false);
   ui_->checkBoxAQ->setChecked(false);
}

void AutoSQWidget::onAqScriptLoaded()
{
   ui_->checkBoxAQ->setChecked(true);
}

void AutoSQWidget::onAqScriptUnloaded()
{
   ui_->checkBoxAQ->setChecked(false);
}

void AutoSQWidget::aqFillHistory()
{
   ui_->comboBoxAQScript->clear();
   int curIndex = 0;
   ui_->comboBoxAQScript->addItem(tr("Select script..."));
   ui_->comboBoxAQScript->addItem(tr("Load new AQ script"));
   const auto scripts = autoSQProvider_->getAQScripts();
   if (!scripts.isEmpty()) {
      const auto lastScript = autoSQProvider_->getAQLastScript();
      for (int i = 0; i < scripts.size(); i++) {
         QFileInfo fi(scripts[i]);
         ui_->comboBoxAQScript->addItem(fi.fileName(), scripts[i]);
         if (scripts[i] == lastScript) {
            curIndex = i + kSelectAQFileItemIndex + 1; // note the "Load" row in the head
         }
      }
   }
   ui_->comboBoxAQScript->setCurrentIndex(curIndex);
}

void AutoSQWidget::aqScriptChanged(int curIndex)
{
   if (curIndex < kSelectAQFileItemIndex) {
      return;
   }

   if (curIndex == kSelectAQFileItemIndex) {
      const auto scriptFileName = askForAQScript();

      if (scriptFileName.isEmpty()) {
         aqFillHistory();
         return;
      }

      // comboBoxAQScript will be updated later from onAqScriptLoaded
      //newLoaded_ = true;
      autoSQProvider_->initAQ(scriptFileName);
   } else {
      if (autoSQProvider_->aqLoaded()) {
         autoSQProvider_->deinitAQ();
      }
   }
}

void AutoSQWidget::onAutoSignToggled()
{
   if (ui_->checkBoxAutoSign->isChecked()) {
      autoSQProvider_->tryEnableAutoSign();
   } else {
      autoSQProvider_->disableAutoSign();
   }
   ui_->checkBoxAutoSign->setChecked(autoSQProvider_->autoSignState());
}

void AutoSQWidget::validateGUI()
{
   ui_->checkBoxAQ->setChecked(autoSQProvider_->aqLoaded());
}

QString AutoSQWidget::askForAQScript()
{
   auto lastDir = autoSQProvider_->getAQLastDir();
   if (lastDir.isEmpty()) {
      lastDir = autoSQProvider_->getDefaultScriptsDir();
   }

   auto path = QFileDialog::getOpenFileName(this, tr("Open Auto-quoting script file")
      , lastDir, tr("QML files (*.qml)"));

   if (!path.isEmpty()) {
      autoSQProvider_->setAQLastDir(path);
   }

   return path;
}
