#ifndef CHATHANDLEINTERFACES_H
#define CHATHANDLEINTERFACES_H
#include <memory>

namespace Chat {
   class ContactRecordData;
}

class CategoryElement;
class ViewItemWatcher {
public:
   virtual void onElementSelected(CategoryElement* element) = 0;
   virtual ~ViewItemWatcher() = default;
};

class ChatItemActionsHandler {
public:
   virtual ~ChatItemActionsHandler() = default;
   virtual void onActionAddToContacts(std::shared_ptr<Chat::ContactRecordData> crecord) = 0;
   virtual void onActionRemoveFromContacts(std::shared_ptr<Chat::ContactRecordData> crecord) = 0;
   virtual void onActionAcceptContactRequest(std::shared_ptr<Chat::ContactRecordData> crecord) = 0;
   virtual void onActionRejectContactRequest(std::shared_ptr<Chat::ContactRecordData> crecord) = 0;
};


#endif // CHATHANDLEINTERFACES_H
