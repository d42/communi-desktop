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

#ifndef CONNECTIONPLUGIN_H
#define CONNECTIONPLUGIN_H

#include <QtPlugin>

class IrcConnection;

class ConnectionPlugin
{
public:
    virtual ~ConnectionPlugin() {}
    virtual void initialize(IrcConnection*) {}
    virtual void uninitialize(IrcConnection*) {}
};

Q_DECLARE_INTERFACE(ConnectionPlugin, "com.github.communi.ConnectionPlugin")

#endif // CONNECTIONPLUGIN_H