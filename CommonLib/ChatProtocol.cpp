#include "ChatProtocol.h"
#include <sstream>

void CommandBase::request_pri(picojson::object &obj)
{
    obj["text"] = picojson::value("Error type");
}

void CommandBase::response_pri(picojson::object &obj)
{
    obj["text"] = picojson::value(err);
}

void CommandBase::parse(picojson::object &)
{

}

void CommandBase::set_error(const std::string &err)
{
    this->err = err;
}

std::string CommandBase::request()
{
    picojson::object ret_obj;
    ret_obj["type"] = picojson::value(cmd_name());
    request_pri(ret_obj);
    return picojson::value(ret_obj).serialize();
}

std::string CommandBase::response()
{
    picojson::object ret_obj;
    ret_obj["type"] = picojson::value(cmd_name());
    response_pri(ret_obj);
    return picojson::value(ret_obj).serialize();
}

void CommandLogin::request_pri(picojson::object &obj)
{
    obj["name"] = picojson::value(from_name);
}

void CommandLogin::response_pri(picojson::object &obj)
{
    obj["name"] = picojson::value(from_name);
    obj["id"] = picojson::value(static_cast<int64_t>(from_id));
}

void CommandLogin::parse(picojson::object &obj)
{
    from_name = obj["name"].get<std::string>();
}

void CommandPrivate::request_pri(picojson::object &obj)
{
    obj["room"] = picojson::value(room);
    obj["message"] = picojson::value(message);
    obj["from"] = picojson::value(from_name);
    obj["fromid"] = picojson::value(static_cast<int64_t>(from_id));
    picojson::array ids;
    for(const auto &v: to_vi)
        ids.push_back(picojson::value(static_cast<int64_t>(v)));
    obj["toid"] = picojson::value(ids);
}

void CommandPrivate::response_pri(picojson::object &obj)
{
    obj["room"] = picojson::value(room);
    obj["message"] = picojson::value(message);
    obj["from"] = picojson::value(from_name);
    obj["fromid"] = picojson::value(static_cast<int64_t>(from_id));
}

void CommandPrivate::parse(picojson::object &obj)
{
    message = obj["message"].get<std::string>();
    room = obj["room"].get<std::string>();
}

bool CommandPrivate::has_id(int id) const
{
    for(const auto &i: to_vi)
        if(i == id) return true;
    return false;
}

void CommandPrivate::add_id(int id)
{
    if(!has_id(id)) to_vi.push_back(id);
}

void CommandPrivate::clear_id()
{
    to_vi.clear();
}

void CommandLogout::request_pri(picojson::object &obj)
{
    obj["name"] = picojson::value(from_name);
}

void CommandLogout::response_pri(picojson::object &obj)
{
    obj["name"] = picojson::value(from_name);
}

void CommandLogout::parse(picojson::object &obj)
{
    from_name = obj["name"].get<std::string>();
}

void CommandMessage::request_pri(picojson::object &obj)
{
    obj["room"] = picojson::value(room);
    obj["message"] = picojson::value(message);
    obj["from"] = picojson::value(from_name);
    obj["fromid"] = picojson::value(static_cast<int64_t>(from_id));
}

void CommandMessage::response_pri(picojson::object &obj)
{
    obj["room"] = picojson::value(room);
    obj["message"] = picojson::value(message);
    obj["from"] = picojson::value(from_name);
    obj["fromid"] = picojson::value(static_cast<int64_t>(from_id));
}

void CommandMessage::parse(picojson::object &obj)
{
    message = obj["message"].get<std::string>();
    room = obj["room"].get<std::string>();
}

void CommandContacts::request_pri(picojson::object &)
{

}

void CommandContacts::response_pri(picojson::object &obj)
{
    picojson::array contacts_arr;
    for(const auto &c: contacts) {
        picojson::array cont;
        cont.push_back(picojson::value(static_cast<int64_t>(c.first)));
        cont.push_back(picojson::value(c.second));
        contacts_arr.push_back(picojson::value(cont));
    }
    obj["contacts"] = picojson::value(contacts_arr);
}

void CommandContacts::parse(picojson::object &obj)
{
    auto cont_it = obj.find("contacts");
    if(cont_it != obj.end()) {
        auto cont_js = cont_it->second.get<picojson::array>();
        for(auto &c_js: cont_js) {
            picojson::array &c_arr = c_js.get<picojson::array>();
            int id = static_cast<int>(c_arr[0].get<int64_t>());
            add_contact(id, c_arr[1].get<std::string>());
        }
    }
}

void CommandContacts::add_contact(int id, const std::string &name)
{
    contacts[id] = name;
}

Command Command::error_cmd(int from_id, const std::string &error)
{
    Command ret(new CommandError(from_id, "", error));
    return ret;
}

bool Command::self_test()
{
    int typeindx = 0;
    Command cmd_from_req, cmd_from_resp;
    int from_id = 10;
    std::string from_name("Client name");
    bool ret = false;
    for(auto i = std::begin(CmdNames); i != std::end(CmdNames); ++i) {
        CommandBase::TypeCommand type = static_cast<CommandBase::TypeCommand>(typeindx);
        switch (type) {
        case CommandBase::TypeCommand::ErROR:
            cmd_from_req = Command(CommandError(from_id, from_name, "Error string").request());
            break;
        case CommandBase::TypeCommand::LOGIN:
            cmd_from_req = Command(CommandLogin("Login string").request());
            break;
        case CommandBase::TypeCommand::LOGOUT:
            cmd_from_req = Command(CommandLogout("Login string").request());
            break;
        case CommandBase::TypeCommand::MESSAGE:
            cmd_from_req = Command(CommandMessage("Room string", "Message string").request());
            break;
        case CommandBase::TypeCommand::PRIVATE_MESSAGE:
            cmd_from_req = Command(CommandPrivate("Room string", "Message string", {0,1,2,3}).request());
            break;
        case CommandBase::TypeCommand::CONTACTS:
            cmd_from_req = Command(CommandContacts().request());
            break;
        case CommandBase::TypeCommand::ROOMS:
            cmd_from_req = Command(CommandRooms().request());
            break;
        case CommandBase::TypeCommand::CREATE_ROOM:
            cmd_from_req = Command(CommandCreateRoom("Room name string").request());
            break;
        case CommandBase::TypeCommand::REMOVE_ROOM:
            cmd_from_req = Command(CommandRemoveRoom(297).request());
            break;
        case CommandBase::TypeCommand::JOIN_ROOM:
            cmd_from_req = Command(CommandJoinRoom(297).request());
            break;
        case CommandBase::TypeCommand::LEAVE_ROOM:
            cmd_from_req = Command(CommandLeaveRoom(297).request());
            break;
        case CommandBase::TypeCommand::CLIENT_STATUS:
            cmd_from_req = Command(CommandClientStatus().request());
            break;
        default:
            ++typeindx;
            continue;
        }
        ret = cmd_from_req.valid();
        if(!ret) return false;
        cmd_from_req.get<CommandBase>()->set_from(from_id, from_name);
        ++typeindx;
    }
    return ret;
}

Command::Command(int from_id, const std::string &from_name, const std::string &json)
    : cmd(nullptr)
{
    try {
        picojson::value cmdjs;
        std::string picoerr = picojson::parse(cmdjs, json);
        if(!picoerr.empty()) {
            cmd = std::shared_ptr<CommandBase>(new CommandError(from_id, from_name, picoerr));
            return;
        }
        auto &obj = cmdjs.get<picojson::object>();

        auto type = obj["type"].to_str();

        int typeindx = 0;
        for(auto i = std::begin(CmdNames); i != std::end(CmdNames); ++i) {
            if(*i == type) break;
            ++typeindx;
        }
        CommandBase::TypeCommand cmd_type = static_cast<CommandBase::TypeCommand>(typeindx);
        switch (cmd_type) {
        case CommandBase::TypeCommand::ErROR:
            cmd = std::shared_ptr<CommandBase>(new CommandError(from_id, from_name));
            break;
        case CommandBase::TypeCommand::LOGIN:
            cmd = std::shared_ptr<CommandBase>(new CommandLogin());
            break;
        case CommandBase::TypeCommand::LOGOUT:
            cmd = std::shared_ptr<CommandBase>(new CommandLogout());
            break;
        case CommandBase::TypeCommand::MESSAGE:
            cmd = std::shared_ptr<CommandBase>(new CommandMessage());
            break;
        case CommandBase::TypeCommand::PRIVATE_MESSAGE:
            cmd = std::shared_ptr<CommandBase>(new CommandPrivate());
            break;
        case CommandBase::TypeCommand::CONTACTS:
            cmd = std::shared_ptr<CommandBase>(new CommandContacts());
            break;
        case CommandBase::TypeCommand::ROOMS:
            cmd = std::shared_ptr<CommandBase>(new CommandRooms());
            break;
        case CommandBase::TypeCommand::CREATE_ROOM:
            cmd = std::shared_ptr<CommandBase>(new CommandCreateRoom());
            break;
        case CommandBase::TypeCommand::REMOVE_ROOM:
            cmd = std::shared_ptr<CommandBase>(new CommandRemoveRoom());
            break;
        case CommandBase::TypeCommand::JOIN_ROOM:
            cmd = std::shared_ptr<CommandBase>(new CommandJoinRoom());
            break;
        case CommandBase::TypeCommand::LEAVE_ROOM:
            cmd = std::shared_ptr<CommandBase>(new CommandLeaveRoom());
            break;
        case CommandBase::TypeCommand::CLIENT_STATUS:
            cmd = std::shared_ptr<CommandBase>(new CommandClientStatus());
            break;
        default:
            cmd = std::shared_ptr<CommandBase>(new CommandError(from_id, from_name, "Type not compatible"));
            break;
        }
        cmd->set_from(from_id, from_name);
        cmd->parse(obj);
    } catch (const std::exception &e) {
        cmd = std::shared_ptr<CommandBase>(new CommandError(from_id, from_name, e.what()));
    }
}

bool Command::is(CommandBase::TypeCommand type)
{
    if(cmd) return cmd->is(type);
    return false;
}

std::string Command::request()
{
    if(cmd) return cmd->request();
    return "{}";
}

std::string Command::response()
{
    if(cmd) return cmd->response();
    return "{}";
}

bool Command::valid()
{
    if(cmd) return cmd->valid();
    return false;
}

const std::string Command::error()
{
    if(cmd) return cmd->error();
    return "null";
}

const std::string Command::type()
{
    if(cmd) return cmd->type();
    return "null";
}

void CommandRooms::request_pri(picojson::object &)
{
}

void CommandRooms::response_pri(picojson::object &obj)
{
    picojson::array rooms_js;
    for(const auto &room: rooms) {
        picojson::object rm;
        rm["id"] = picojson::value(static_cast<int64_t>(room.first));
        rm["type"] = picojson::value(room.second.first);
        rm["name"] = picojson::value(room.second.second);
        rooms_js.push_back(picojson::value(rm));
    }
    obj["rooms"] = picojson::value(rooms_js);
}

void CommandRooms::parse(picojson::object &obj)
{
    auto rooms_it = obj.find("rooms");
    if(rooms_it != obj.end()) {
        auto rooms_js = rooms_it->second.get<picojson::array>();
        for(auto &room_js: rooms_js) {
            picojson::object &room_obj = room_js.get<picojson::object>();
            int id = static_cast<int>(room_obj["id"].get<int64_t>());
            add_room(id, room_obj["type"].get<std::string>(), room_obj["name"].get<std::string>());
        }
    }
}

void CommandRooms::add_room(int id, const std::string &type, const std::string &name)
{
    rooms[id] = {type, name};
}

void CommandCreateRoom::request_pri(picojson::object &obj)
{
    obj["name"] = picojson::value(room);
}

void CommandCreateRoom::response_pri(picojson::object &obj)
{
    obj["id"] = picojson::value(static_cast<int64_t>(id_room));
    obj["name"] = picojson::value(room);
}

void CommandCreateRoom::parse(picojson::object &obj)
{
    room = obj["name"].get<std::string>();
    auto id_it = obj.find("id");
    if(id_it != obj.end()) id_room = static_cast<int>(id_it->second.get<int64_t>());
}

void CommandRemoveRoom::request_pri(picojson::object &obj)
{
    obj["id"] = picojson::value(static_cast<int64_t>(id_room));
}

void CommandRemoveRoom::response_pri(picojson::object &obj)
{
    request_pri(obj);
}

void CommandRemoveRoom::parse(picojson::object &obj)
{
    id_room = static_cast<int>(obj["id"].get<int64_t>());
}

void CommandJoinRoom::request_pri(picojson::object &obj)
{
    obj["id"] = picojson::value(static_cast<int64_t>(id_room));
}

void CommandJoinRoom::response_pri(picojson::object &obj)
{
    request_pri(obj);
}

void CommandJoinRoom::parse(picojson::object &obj)
{
    id_room = static_cast<int>(obj["id"].get<int64_t>());
}

void CommandLeaveRoom::request_pri(picojson::object &obj)
{
    obj["id"] = picojson::value(static_cast<int64_t>(id_room));
}

void CommandLeaveRoom::response_pri(picojson::object &obj)
{
    request_pri(obj);
}

void CommandLeaveRoom::parse(picojson::object &obj)
{
    id_room = static_cast<int>(obj["id"].get<int64_t>());
}

void CommandClientStatus::request_pri(picojson::object &)
{

}

void CommandClientStatus::response_pri(picojson::object &obj)
{
    obj["from"] = picojson::value(from_name);
    obj["fromid"] = picojson::value(static_cast<int64_t>(from_id));
    picojson::array statuses;
    for(auto &status: statuslist) {
        picojson::array status_js;
        status_js.push_back(picojson::value(status.first));
        status_js.push_back(picojson::value(status.second));
        statuses.push_back(picojson::value(status_js));
    }
    obj["status"] = picojson::value(statuses);
}

void CommandClientStatus::parse(picojson::object &obj)
{
    auto stat_it = obj.find("status");
    if(stat_it != obj.end()) {
        from_name = obj["from"].get<std::string>();
        from_id = static_cast<int>(obj["fromid"].get<int64_t>());
        picojson::array &stat_arr = obj["status"].get<picojson::array>();
        for(auto &stat_it: stat_arr) {
            picojson::array &stat_it_arr = stat_it.get<picojson::array>();
            add_status(stat_it_arr[0].get<std::string>(), stat_it_arr[1].get<std::string>());
        }
    }
}

void CommandClientStatus::add_status(const std::string &type, const std::string &value)
{
    statuslist.emplace_back(type, value);
}
