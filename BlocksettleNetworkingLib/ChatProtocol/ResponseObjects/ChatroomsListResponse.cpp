#include "ChatroomsListResponse.h"

namespace Chat {
   ChatroomsListResponse::ChatroomsListResponse(std::vector<std::string> dataList)
      : ListResponse (ResponseType::ResponseChatroomsList, dataList)
   {
      roomList_.reserve(dataList_.size());
      for (const auto& roomData: dataList_){
         auto room = RoomData::fromJSON(roomData);
         if (room->getId() == QLatin1String("global_chat"))
            room->setDisplayTrayNotification(false);
         roomList_.push_back(room);
      }
   }
   
   ChatroomsListResponse::ChatroomsListResponse(std::vector<std::shared_ptr<RoomData>> roomList)
      : ListResponse (ResponseType::ResponseChatroomsList, {})
      , roomList_(roomList)
   {
      std::vector<std::string> rooms;
      for (const auto& room: roomList_){
         if (room->getId() == QLatin1String("global_chat"))
            room->setDisplayTrayNotification(false);
         rooms.push_back(room->toJsonString());
      }
      dataList_ = std::move(rooms);
      
   }
   
   std::shared_ptr<Response> ChatroomsListResponse::fromJSON(const std::string& jsonData)
   {
      QJsonObject data = QJsonDocument::fromJson(QString::fromStdString(jsonData).toUtf8()).object();
      return std::make_shared<ChatroomsListResponse>(ListResponse::fromJSON(jsonData));
   }
   
   void ChatroomsListResponse::handle(ResponseHandler& handler)
   {
      handler.OnChatroomsList(*this);
   }
   
   const std::vector<std::shared_ptr<RoomData> >&ChatroomsListResponse::getChatRoomList() const
   {
      return roomList_;
   }
}
