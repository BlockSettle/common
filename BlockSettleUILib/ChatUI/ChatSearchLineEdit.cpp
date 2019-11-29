/*

***********************************************************************************
* Copyright (C) 2016 - 2019, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "ChatSearchLineEdit.h"

#include <QKeyEvent>
ChatSearchLineEdit::ChatSearchLineEdit(QWidget *parent)
   : QLineEdit(parent)
   , resetOnNextInput_(false)
{
   connect(this, &QLineEdit::textChanged, this, &ChatSearchLineEdit::onTextChanged);
}

/*
void ChatSearchLineEdit::setActionsHandler(std::shared_ptr<ChatSearchActionsHandler> handler)
{
   handler_ = handler;
}
*/

void ChatSearchLineEdit::setResetOnNextInput(bool value)
{
   resetOnNextInput_ = value;
}

void ChatSearchLineEdit::onTextChanged(const QString &text)
{
/*
   if (text.isEmpty() && handler_) {
      handler_->onActionResetSearch();
   }
*/
}


ChatSearchLineEdit::~ChatSearchLineEdit() = default;

void ChatSearchLineEdit::keyPressEvent(QKeyEvent * e)
{
   switch (e->key()) {
   case Qt::Key_Enter:     //Qt::Key_Enter   - Numpad Enter key
   case Qt::Key_Return:    //Qt::Key_Return  - Main Enter key
   {
      qDebug("Return/Enter search press %d", e->key());
      emit keyEnterPressed();
      return e->ignore();
   }
   case Qt::Key_Down:     //Qt::Key_Down     - For both standalone and Numpad arrow down keys
      emit keyDownPressed();
      break;
   case Qt::Key_Escape:
      emit keyEscapePressed();
      break;
   default:
      break;
   }
   return QLineEdit::keyPressEvent(e);
}
