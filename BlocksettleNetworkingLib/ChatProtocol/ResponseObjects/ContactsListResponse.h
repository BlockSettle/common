#ifndef ContactsListResponse_h__
#define ContactsListResponse_h__

#include "ListResponse.h"

namespace Chat {

   class ContactsListResponse : public ListResponse
   {
   public:
      ContactsListResponse(std::vector<std::string> dataList);
      ContactsListResponse(std::vector<std::shared_ptr<ContactRecordData>> contactList);
      static std::shared_ptr<Response> fromJSON(const std::string& jsonData);
      void handle(ResponseHandler&) override;
      const std::vector<std::shared_ptr<ContactRecordData>>& getContactsList() const;
   private:
      std::vector<std::shared_ptr<ContactRecordData>> contactsList_;
   };
}

#endif // ContactsListResponse_h__
