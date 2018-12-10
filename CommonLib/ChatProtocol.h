#ifndef CHATPROTOCOL_H
#define CHATPROTOCOL_H

#define PICOJSON_USE_INT64
#include "picojson.h"
#include <memory>

static const std::string CmdNames[] = {
    "er", // error
    "in", // login
    "ou", // logout
    "bm", // broadcast message
    "pm", // private message
    "cn", // get contacts
    "rm", // get rooms
    "cr", // create room
    "rr", // remove room
    "jr", // join room
    "lr", // leave room
    "cs"  // client status
};

class CommandBase {
public:

    enum class TypeCommand: int{
        ErROR = 0,
        LOGIN,
        LOGOUT,
        MESSAGE,
        PRIVATE_MESSAGE,
        CONTACTS,
        ROOMS,
        CREATE_ROOM,
        REMOVE_ROOM,
        JOIN_ROOM,
        LEAVE_ROOM,
        CLIENT_STATUS,
        NOT_COMPATIBLE
    };

private:
    picojson::object ret_obj;

protected:
    TypeCommand cmd_type;
    std::string err;
    int from_id;
    std::string from_name;
    virtual void request_pri(picojson::object &obj);
    virtual void response_pri(picojson::object &obj);
public:
    virtual void parse(picojson::object &obj);
    static CommandBase parse(int from_id, const std::string &from_name, const std::string &command);
    CommandBase(int from_id, const std::string &from_name, TypeCommand type = TypeCommand::ErROR)
        : cmd_type(type), err(""), from_id(from_id), from_name(from_name) {}
    CommandBase(int from_id, const std::string &from_name, const std::string &error, TypeCommand type = TypeCommand::ErROR)
        : cmd_type(type), err(error), from_id(from_id), from_name(from_name) {}
    CommandBase(int from_id, TypeCommand type = TypeCommand::ErROR)
        : CommandBase(from_id, "", type) {}
    CommandBase(TypeCommand type = TypeCommand::ErROR)
        : CommandBase(-1, "", type) {}
    virtual ~CommandBase() {}
    void set_from(int from_id, const std::string &from_name) { this->from_id = from_id; this->from_name = from_name; }
    void set_error(const std::string &err);
    std::string request();
    std::string response();
    bool valid() { return cmd_type != TypeCommand::ErROR; }
    bool is(TypeCommand type) { return type == cmd_type; }
    const std::string& error() { return err; }
    const std::string type() { return std::to_string(static_cast<int>(cmd_type)); }
    std::string cmd_name() const { return CmdNames[static_cast<int>(cmd_type)]; }
};

class CommandLogin: public CommandBase {
    void request_pri(picojson::object &obj) override;
    void response_pri(picojson::object &obj) override;
    void parse(picojson::object &obj) override;
public:
    CommandLogin(const std::string &login="")
        : CommandBase(-1, login, TypeCommand::LOGIN) {}
    std::string login() const { return from_name; }
};

class CommandLogout: public CommandBase {
    void request_pri(picojson::object &obj) override;
    void response_pri(picojson::object &obj) override;
    void parse(picojson::object &obj) override;
public:
    CommandLogout(const std::string &login="")
        : CommandBase(-1, login, TypeCommand::LOGOUT) {}
};

class CommandMessage: public CommandBase {
    void request_pri(picojson::object &obj) override;
    void response_pri(picojson::object &obj) override;
    void parse(picojson::object &obj) override;
public:
    std::string room, message;
    CommandMessage(const std::string &room="", const std::string &message="")
        : CommandBase(TypeCommand::MESSAGE), room(room), message(message) {}
};

class CommandPrivate: public CommandBase {
    std::vector<int> to_vi;
    void request_pri(picojson::object &obj) override;
    void response_pri(picojson::object &obj) override;
    void parse(picojson::object &obj) override;
public:
    std::string room, message;
    CommandPrivate(const std::string &room="", const std::string &message="", const std::vector<int> &toid=std::vector<int>())
        : CommandBase(TypeCommand::PRIVATE_MESSAGE), to_vi(toid), room(room), message(message) {}
    bool has_id(int id) const;
    void add_id(int id);
    void clear_id();
};

class CommandContacts: public CommandBase {
    std::map<int, std::string> contacts;
    void request_pri(picojson::object &obj) override;
    void response_pri(picojson::object &obj) override;
    void parse(picojson::object &obj) override;
public:
    CommandContacts()
        : CommandBase(TypeCommand::CONTACTS) {}
    void add_contact(int id, const std::string &name);
};

class CommandRooms: public CommandBase {
    std::map<int, std::pair<std::string, std::string>> rooms;
    void request_pri(picojson::object &obj) override;
    void response_pri(picojson::object &obj) override;
    void parse(picojson::object &obj) override;
public:
    CommandRooms()
        : CommandBase(TypeCommand::ROOMS) {}
    void add_room(int id, const std::string &type, const std::string &name);
};

class CommandCreateRoom: public CommandBase {
    std::string room;
    int id_room;
    void request_pri(picojson::object &obj) override;
    void response_pri(picojson::object &obj) override;
    void parse(picojson::object &obj) override;
public:
    CommandCreateRoom(const std::string &room="")
        : CommandBase(TypeCommand::CREATE_ROOM), room(room), id_room(-1) {}
};

class CommandRemoveRoom: public CommandBase {
protected:
    int id_room;
    void request_pri(picojson::object &obj) override;
    void response_pri(picojson::object &obj) override;
    void parse(picojson::object &obj) override;
public:
    CommandRemoveRoom(int id_room = -1)
        : CommandBase(TypeCommand::REMOVE_ROOM), id_room(id_room) {}
};

class CommandJoinRoom: public CommandBase {
protected:
    int id_room;
    void request_pri(picojson::object &obj) override;
    void response_pri(picojson::object &obj) override;
    void parse(picojson::object &obj) override;
public:
    CommandJoinRoom(int id_room = -1)
        : CommandBase(TypeCommand::JOIN_ROOM), id_room(id_room) {}
};

class CommandLeaveRoom: public CommandBase {
protected:
    int id_room;
    void request_pri(picojson::object &obj) override;
    void response_pri(picojson::object &obj) override;
    void parse(picojson::object &obj) override;
public:
    CommandLeaveRoom(int id_room = -1)
        : CommandBase(TypeCommand::LEAVE_ROOM), id_room(id_room) {}
};

class CommandClientStatus: public CommandBase {
protected:
    std::vector<std::pair<std::string, std::string>> statuslist;
    void request_pri(picojson::object &obj) override;
    void response_pri(picojson::object &obj) override;
    void parse(picojson::object &obj) override;
public:
    CommandClientStatus()
        : CommandBase(TypeCommand::CLIENT_STATUS) {}
    void add_status(const std::string &type, const std::string &value);
};

using CommandError = CommandBase;

class Command {
    std::shared_ptr<CommandBase> cmd;
public:
    Command(): cmd(nullptr) {}
    Command(CommandBase *command): cmd(std::shared_ptr<CommandBase>(command)) {}
    static Command error_cmd(int from_id, const std::string &error);
    static bool self_test();
    Command(int from_id, const std::string &from_name, const std::string &json);
    Command(const std::string &json)
        : Command(-1, "", json) {}
    bool is(CommandBase::TypeCommand type);
    template <typename T>
    T *get() {
        if(!cmd) return nullptr;
        return dynamic_cast<T*>(cmd.get());
    }
    std::string request();
    std::string response();
    bool valid();
    const std::string error();
    const std::string type();
};

#endif //CHATPROTOCOL_H
