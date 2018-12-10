#include "ChatWidget.h"
#include "ui_ChatWidget.h"
#include <iostream>
#include <QAction>

static ChatWidget *static_this;

void ChatWidget::log_emit_func(int level, const char *line)
{
    auto l = strlen(line);
    if(l > 0 && line[l-1] == '\n') --l;
    auto msg = std::string(line, l);
    if(level == LLL_ERR) std::cerr << msg << std::endl;
    else std::cout << msg << std::endl;
}

bool ChatWidget::connect_client()
{
    struct lws_client_connect_info i;
    memset(&i, 0, sizeof(i));
    i.context = static_this->context;
    i.port = static_this->port;
    i.address = static_this->host.c_str();
    i.path = "/";
    i.host = i.address;
    i.origin = i.address;
    i.ssl_connection = static_this->ssl_connection;
    i.protocol = "";
    i.local_protocol_name = "";
    i.pwsi = &static_this->client_wsi;
    return !lws_client_connect_via_info(&i);
}

int ChatWidget::callback_main(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len)
{
    switch (reason) {
    case LWS_CALLBACK_PROTOCOL_INIT:
        if(!connect_client()) {
            static_this->stop();
            return -1;
        }
        break;
    }

    return lws_callback_http_dummy(wsi, reason, user, in, len);

}

ChatWidget::ChatWidget(QWidget *parent, const std::string &host, const int &port, const std::string &name, const std::string &cert_path)
    : QWidget(parent), host(host), name(name), cert_path(cert_path), port(port), ui(new Ui::ChatWidget)
{
    ui->setupUi(this);
    model = new QStringListModel(this);
    ui->messages->setModel(model);

    static_this = this;
    context = nullptr;
    client_wsi = nullptr;
    forcestop = true;

    protocols[0].id = 0;
    protocols[0].name = "";
    protocols[0].per_session_data_size = 0;
    protocols[0].callback = callback_main;
    protocols[0].user = nullptr;

    protocols[1].id = 0;
    protocols[1].name = nullptr;
    protocols[1].per_session_data_size = 0;
    protocols[1].callback = nullptr;
    protocols[1].user = nullptr;

    int logs = LLL_ERR | LLL_WARN | LLL_NOTICE;
    lws_set_log_level(logs, log_emit_func);
    memset(&info, 0, sizeof info);
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    if(cert_path.empty()) ssl_connection = 0;
    else {
        ssl_connection = LCCSCF_USE_SSL|LCCSCF_ALLOW_SELFSIGNED;
        info.client_ssl_ca_filepath = this->cert_path.c_str();
        info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    }
    info.gid = -1;
    info.uid = -1;
    context = lws_create_context(&info);
    if(!context) {
        std::cerr << "libwebsocket init failed" << std::endl;
        return;
    }
    forcestop = false;
    mainloop = std::thread([this]()->void{
       int n = 0;
       while (!forcestop && n >= 0)
           n = lws_service(context, 100);
       forcestop = true;
    });
    addLine("Websocket run.");
}

ChatWidget::~ChatWidget()
{
    if(!forcestop) {
        forcestop = true;
        mainloop.join();
        addLine("Websocket error.");
    }
    delete ui;
    if(context) lws_context_destroy(context);
}

void ChatWidget::stop()
{
    forcestop = true;
}

void ChatWidget::addLine(const QString &txt)
{
    int index = model->rowCount();
    model->insertRow(index);
    model->setData(model->index(index), txt);
}
