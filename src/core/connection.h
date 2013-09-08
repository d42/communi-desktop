/*
* Copyright (C) 2008-2013 The Communi Project
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#ifndef CONNECTION_H
#define CONNECTION_H

#include <QSet>
#include <QTimer>
#include <IrcConnection>
#include <IrcCommand>
#include <QStringList>
#include <IrcNetwork>
#include <QAbstractSocket>
#include <IrcMessageFilter>
#include "viewinfo.h"

class Connection : public IrcConnection, public IrcMessageFilter
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(int autoReconnectDelay READ autoReconnectDelay WRITE setAutoReconnectDelay)
    Q_PROPERTY(bool hasQuit READ hasQuit WRITE setHasQuit)
    Q_PROPERTY(bool reconnecting READ isReconnecting)
    Q_PROPERTY(bool bouncer READ isBouncer)

public:
    explicit Connection(QObject* parent = 0);
    ~Connection();

    QString name() const;
    void setName(const QString& name);

    int autoReconnectDelay() const;
    void setAutoReconnectDelay(int delay);

    bool hasQuit() const;
    void setHasQuit(bool quit);

    bool isBouncer() const;
    bool isReconnecting() const;

    ViewInfos views() const;
    void setViews(const ViewInfos& views);

    Q_INVOKABLE bool sendUiCommand(IrcCommand* command, const QString& identifier);

public slots:
    void reconnect();
    void quit();
    void destructLater();
    void stopReconnecting();
    void sleep();
    void wake();

signals:
    void nameChanged(const QString& name);

protected:
    IrcCommand* createCtcpReply(IrcPrivateMessage* request) const;

private slots:
    void onConnected();
    void onDisconnected();
    void onNickNameReserved(QString* alternate);

private:
    bool messageFilter(IrcMessage* message);
    void addChannel(const QString& channel);
    void removeChannel(const QString& channel);

    struct Private {
        bool quit;
        bool bouncer;
        QString name;
        QTimer reconnectTimer;
        ViewInfos views;
        QStringList alternateNicks;
        QHash<QString, IrcCommand*> commands;
    } d;
};

#endif // CONNECTION_H
