#ifndef CHATPROTOCOL_H
#define CHATPROTOCOL_H

#define PICOJSON_USE_INT64
#include "picojson.h"

class ChatProtocol {
public:
    enum class TypeCommand: int{
        ERROR = -1,
        LOGIN = 0,
        LOGOUT = 1,
        MESSAGE = 2,
        PRIVATE_MESSAGE = 3,
        CONTACTS = 4
    };
private:
    std::vector<int> to_vi;
    std::string message, err;
    TypeCommand cmd_type;
    void set_type(picojson::object &obj);
    picojson::array split_message(char split_char = '\n');
public:
    ChatProtocol(const std::string &command);
    ChatProtocol(const std::string &message, TypeCommand command_type,
                 const std::vector<int> &to_vi = std::vector<int>())
        : to_vi(to_vi), message(message), cmd_type(command_type) {}
    ChatProtocol(): ChatProtocol("", TypeCommand::ERROR) {}
    void copy(const ChatProtocol &cmd);
    bool has_id(int id);
    void clear_ids();
    void add_id(int id);
    void set_message(const std::string &msg);
    void set_error(const std::string &err);
    std::string request();
    std::string response(const std::string &name = "", const int &id = -1);
    std::string error_response(const std::string &message);
    bool valid() { return cmd_type != TypeCommand::ERROR; }
    bool is(TypeCommand type) { return type == cmd_type; }
    const std::vector<int>& to() { return to_vi; }
    const std::string& msg() { return message; }
    const std::string& error() { return err; }
    const std::string type() { return std::to_string(static_cast<int>(cmd_type)); }
};

#endif //CHATPROTOCOL_H
