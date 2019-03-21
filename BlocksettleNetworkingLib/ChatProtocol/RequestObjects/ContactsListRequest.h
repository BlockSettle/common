#ifndef CONTACTSLISTREQUEST_H
#define CONTACTSLISTREQUEST_H

#include "Request.h"
namespace Chat {

   class ContactsListRequest : public Request
   {
   public:
      ContactsListRequest(const std::string& clientId, const std::string& authId);

   public:
      QJsonObject toJson() const override;
      void handle(RequestHandler &) override;
      std::string getAuthId() const;
   private:
      std::string authId_;

   };

}

#endif // CONTACTSLISTREQUEST_H
