#ifndef CHATWIDGET_H
#define CHATWIDGET_H

#include <QWidget>
#include <QStringListModel>
#include <libwebsockets.h>
#include <random>
#include <thread>

namespace Ui {
class ChatWidget;
}

class ChatWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChatWidget(QWidget *parent = nullptr,
                        const std::string &host = "127.0.0.1",
                        const int &port = 7681,
                        const std::string &name = "chat_client_name" + std::to_string(rand()),
                        const std::string &cert_path = ""
                        );
    ~ChatWidget();
    std::string host, name, cert_path;
    int port;
    struct lws_protocols protocols[2];
    struct lws_context_creation_info info;
    struct lws_context *context;
    struct lws *client_wsi;
    int ssl_connection;
    void stop();
    void addLine(const QString &txt);

private:
    static int callback_main(lws *wsi, lws_callback_reasons reason, void *user, void *in, size_t len);
    static void log_emit_func(int level, const char *line);
    static bool connect_client();
    std::thread mainloop;
    bool forcestop;
    Ui::ChatWidget *ui;
    QStringListModel *model;
};

#endif // CHATWIDGET_H
