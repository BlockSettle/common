#ifndef SEARCHUSERSRESPONSE_H
#define SEARCHUSERSRESPONSE_H

#include "ListResponse.h"
namespace Chat {
    class SearchUsersResponse : public ListResponse
    {
    public:
       SearchUsersResponse(std::vector<std::string> dataList);
       SearchUsersResponse(std::vector<std::shared_ptr<ChatUserData>> usersList);
       static std::shared_ptr<Response> fromJSON(const std::string& jsonData);
       void handle(ResponseHandler&) override;
       const std::vector<std::shared_ptr<ChatUserData>>& getUsersList() const;
    private:
       std::vector<std::shared_ptr<ChatUserData>> usersList_;
    };
   }

#endif // SEARCHUSERSRESPONSE_H
