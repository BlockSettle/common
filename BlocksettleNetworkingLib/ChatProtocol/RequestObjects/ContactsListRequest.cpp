#include "ContactsListRequest.h"

using namespace Chat;

ContactsListRequest::ContactsListRequest(const std::string &clientId, const std::string &authId)
    :Request(RequestType::RequestContactsList, clientId)
    , authId_(authId)
{

}

QJsonObject ContactsListRequest::toJson() const
{
    QJsonObject data = Request::toJson();

    data[AuthIdKey] = QString::fromStdString(authId_);

    return data;
}

void ContactsListRequest::handle(RequestHandler & handler)
{
   handler.OnRequestContactsList(*this);
}

std::string ContactsListRequest::getAuthId() const { return authId_; }
