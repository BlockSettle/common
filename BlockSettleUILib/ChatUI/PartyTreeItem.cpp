/*

***********************************************************************************
* Copyright (C) 2016 - 2019, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "PartyTreeItem.h"

PartyTreeItem::PartyTreeItem(const QVariant& data, UI::ElementType modelType, PartyTreeItem* parent /*= nullptr*/)
   : itemData_(data)
   , modelType_(modelType)
   , parentItem_(parent)
{
}

PartyTreeItem::~PartyTreeItem()
{
}

PartyTreeItem* PartyTreeItem::child(int number)
{
   Q_ASSERT(number >= 0 && number < childItems_.size());
   return childItems_[number].get();
}

int PartyTreeItem::childCount() const
{
   return childItems_.size();
}

int PartyTreeItem::columnCount() const
{
   return 1;
}

QVariant PartyTreeItem::data() const
{
   return itemData_;
}

bool PartyTreeItem::insertChildren(std::unique_ptr<PartyTreeItem>&& item)
{
   childItems_.push_back(std::move(item));
   return true;
}

PartyTreeItem* PartyTreeItem::parent()
{
   return parentItem_;
}

void PartyTreeItem::removeAll()
{
   childItems_.clear();
}

int PartyTreeItem::childNumber() const
{
   if (parentItem_) {
      for (int iChild = 0; iChild < parentItem_->childCount(); ++iChild) {
         if (parentItem_->childItems_[iChild].get() != this) {
            continue;
         }

         return iChild;
      }
   }

   Q_ASSERT(false);
   return 0;
}

bool PartyTreeItem::setData(const QVariant& value)
{
   itemData_ = value;
   return true;
}

UI::ElementType PartyTreeItem::modelType() const
{
   return modelType_;
}

void PartyTreeItem::increaseUnseenCounter(int newMessageCount)
{
   Q_ASSERT(newMessageCount > 0);
   unseenCounter_ += newMessageCount;
}

void PartyTreeItem::decreaseUnseenCounter(int seenMessageCount)
{
   unseenCounter_ -= seenMessageCount;
   unseenCounter_ = std::max(unseenCounter_, 0);
}

bool PartyTreeItem::hasNewMessages() const
{
   return unseenCounter_ > 0;
}

int PartyTreeItem::unseenCount() const
{
   return unseenCounter_;
}

void PartyTreeItem::enableOTCToggling(bool otcToggling)
{
   otcTogglingMode_ = otcToggling;
}

bool PartyTreeItem::isOTCTogglingMode() const
{
   // #flickeringOTC Flickering otc awaiting messages - disabled for now
   return false; //  otcTogglingMode_;
}

void PartyTreeItem::changeOTCToggleState()
{
   currentOTCToggleState_ = !currentOTCToggleState_;
}

bool PartyTreeItem::activeOTCToggleState() const
{
   return currentOTCToggleState_;
}

void PartyTreeItem::applyReusableData(const ReusableItemData& data)
{
   unseenCounter_ = data.unseenCount_;
   otcTogglingMode_ = data.otcTogglingMode_;
}

ReusableItemData PartyTreeItem::generateReusableData() const
{
   return { unseenCounter_ , otcTogglingMode_ };
}
