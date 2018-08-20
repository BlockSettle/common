#include "ChangeLogDialog.h"
#include "ui_ChangeLogDialog.h"

ChangeLogDialog::ChangeLogDialog(const bs::VersionChecker &verChecker, QWidget* parent)
  : QDialog(parent)
  , ui_(new Ui::ChangeLogDialog())
{
   ui_->setupUi(this);
   connect(ui_->pushButtonOk, &QPushButton::clicked, this, &ChangeLogDialog::accept);

   const auto& changeLog = verChecker.getChangeLog();
   QString changeLogText;

   for (const auto& versionLog : changeLog) {
      changeLogText += tr("Version: ") + versionLog.versionString + QLatin1String("\n");

      if (!versionLog.newFeatures.empty()) {
         changeLogText += tr("  New features:\n");
         for (const auto& newFeature : versionLog.newFeatures) {
            changeLogText += tr("      ") + newFeature + QLatin1String("\n");
         }
      }

      if (!versionLog.bugFixes.empty()) {
         changeLogText += tr("  Bug fixes:\n");
         for (const auto& bugFix : versionLog.bugFixes) {
            changeLogText += tr("      ") + bugFix + QLatin1String("\n");
         }
      }
   }

   ui_->textEditChangeLog->setText(changeLogText);
}

ChangeLogDialog::~ChangeLogDialog()
{
}
