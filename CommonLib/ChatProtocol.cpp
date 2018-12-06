#include "ChatProtocol.h"
#include <sstream>

void ChatProtocol::set_type(picojson::object &obj)
{

    switch (cmd_type) {
    case TypeCommand::LOGIN:
        obj["type"] = picojson::value("in");
        break;
    case TypeCommand::LOGOUT:
        obj["type"] = picojson::value("out");
        break;
    case TypeCommand::MESSAGE:
        obj["type"] = picojson::value("msg");
        break;
    case TypeCommand::PRIVATE_MESSAGE:
        obj["type"] = picojson::value("pmsg");
        break;
    case TypeCommand::CONTACTS:
        obj["type"] = picojson::value("cnt");
        break;
    default:
        obj["type"] = picojson::value("error");
        break;
    }
}

picojson::array ChatProtocol::split_message(char split_char)
{
    picojson::array ret;
    std::istringstream f(message);
    std::string s;
    while(getline(f, s, split_char)) {
        if(s.length() > 0)
            ret.push_back(picojson::value(s));
    }
    return ret;
}

ChatProtocol::ChatProtocol(const std::string &command)
{
    try {
        picojson::value cmd;
        std::string picoerr = picojson::parse(cmd, command);
        cmd_type = TypeCommand::ERROR;
        if(!err.empty()) {
            err = picoerr;
            return;
        }
        auto &obj = cmd.get<picojson::object>();

        auto type = obj["type"].to_str();
        if(type == "msg") cmd_type = TypeCommand::MESSAGE;
        else if(type == "pmsg") cmd_type = TypeCommand::PRIVATE_MESSAGE;
        else if(type == "in") cmd_type = TypeCommand::LOGIN;
        else if(type == "cnt") cmd_type = TypeCommand::CONTACTS;
        else if(type == "out") cmd_type = TypeCommand::LOGOUT;

        if(cmd_type == TypeCommand::ERROR) {
            err = "Type not compatible";
            return;
        }

        switch (cmd_type) {
        case TypeCommand::LOGIN:
        case TypeCommand::LOGOUT:
            message = obj["name"].get<std::string>();
            break;
        case TypeCommand::MESSAGE:
        case TypeCommand::PRIVATE_MESSAGE:
            message = obj["message"].get<std::string>();
            break;
        case TypeCommand::CONTACTS:
            break;
        default:
            break;
        }

    } catch (const std::exception &e) {
        cmd_type = TypeCommand::ERROR;
        err = e.what();
    }
}

void ChatProtocol::copy(const ChatProtocol &cmd)
{
    to_vi = cmd.to_vi;
    message = cmd.message;
    err = cmd.err;
    cmd_type = cmd.cmd_type;
}

bool ChatProtocol::has_id(int id)
{
    if(cmd_type == TypeCommand::MESSAGE) return true;
    for(const auto &i: to_vi)
        if(i == id) return true;
    return false;
}

void ChatProtocol::clear_ids()
{
    to_vi.clear();
}

void ChatProtocol::add_id(int id)
{
    if(!has_id(id))
        to_vi.push_back(id);
}

void ChatProtocol::set_message(const std::string &msg)
{
    message = msg;
}

void ChatProtocol::set_error(const std::string &err)
{
    this->err = err;
}

std::string ChatProtocol::request()
{
    picojson::object ret_obj;
    set_type(ret_obj);
    switch (cmd_type) {
    case TypeCommand::LOGIN:
    case TypeCommand::LOGOUT:
        ret_obj["name"] = picojson::value(message);
        break;
    case TypeCommand::MESSAGE:
    case TypeCommand::PRIVATE_MESSAGE:
        {
            picojson::array arr;
            for(const auto &v: to_vi)
                arr.push_back(picojson::value(static_cast<int64_t>(v)));
            ret_obj["id"] = picojson::value(arr);
        }
        ret_obj["message"] = picojson::value(message);
        break;
    case TypeCommand::CONTACTS:
        break;
    default:
        ret_obj["text"] = picojson::value("Error type");
        break;
    }
    return picojson::value(ret_obj).serialize();
}

std::string ChatProtocol::response(const std::string &name, const int &id)
{
    picojson::object ret_obj;
    set_type(ret_obj);
    switch (cmd_type) {
    case TypeCommand::LOGIN:
    case TypeCommand::LOGOUT:
        break;
    case TypeCommand::MESSAGE:
    case TypeCommand::PRIVATE_MESSAGE:
        ret_obj["from"] = picojson::value(name);
        ret_obj["fromid"] = picojson::value(static_cast<int64_t>(id));
        ret_obj["message"] = picojson::value(message);
        break;
    case TypeCommand::CONTACTS:
        {
            picojson::array arr1;
            for(const auto &v: to_vi)
                arr1.push_back(picojson::value(static_cast<int64_t>(v)));
            picojson::array arr2 = split_message();
            ret_obj["id"] = picojson::value(arr1);
            ret_obj["name"] = picojson::value(arr2);
        }
        break;
    default:
        if(err.empty())
            ret_obj["error"] = picojson::value(message);
        else
            ret_obj["error"] = picojson::value(err);
    }
    return picojson::value(ret_obj).serialize();
}

std::string ChatProtocol::error_response(const std::string &message)
{
    cmd_type = TypeCommand::ERROR;
    err = message;
    return response();
}
