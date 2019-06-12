#ifndef MessagesResponse_h__
#define MessagesResponse_h__

#include "ListResponse.h"

namespace Chat {

   class MessagesResponse : public ListResponse
   {
   public:
      MessagesResponse(std::vector<std::string> dataList);
      static std::shared_ptr<Response> fromJSON(const std::string& jsonData);
      void handle(ResponseHandler &) override;
   };
}

#endif // MessagesResponse_h__
