/*
* Copyright (C) 2008-2014 The Communi Project
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

#include "textinput.h"
#include <QStyleOptionFrame>
#include <IrcCommandParser>
#include <IrcBufferModel>
#include <IrcConnection>
#include <IrcCompleter>
#include <QMessageBox>
#include <QSettings>
#include <QCheckBox>
#include <IrcBuffer>
#include <QKeyEvent>
#include <QPainter>

TextInput::TextInput(QWidget* parent) : QLineEdit(parent)
{
    setAttribute(Qt::WA_MacShowFocusRect, false);

    d.hint = "...";
    d.index = 0;
    d.buffer = 0;
    d.parser = 0;

    d.completer = new IrcCompleter(this);
    connect(this, SIGNAL(bufferChanged(IrcBuffer*)), d.completer, SLOT(setBuffer(IrcBuffer*)));
    connect(this, SIGNAL(parserChanged(IrcCommandParser*)), d.completer, SLOT(setParser(IrcCommandParser*)));
    connect(d.completer, SIGNAL(completed(QString,int)), this, SLOT(doComplete(QString,int)));
    connect(this, SIGNAL(textEdited(QString)), d.completer, SLOT(reset()));

    connect(this, SIGNAL(returnPressed()), this, SLOT(sendInput()));
    connect(this, SIGNAL(textChanged(QString)), this, SLOT(updateHint(QString)));
}

IrcBuffer* TextInput::buffer() const
{
    return d.buffer;
}

IrcCommandParser* TextInput::parser() const
{
    return d.parser;
}

static void bind(IrcBuffer* buffer, IrcCommandParser* parser)
{
    if (buffer && parser) {
        IrcBufferModel* model = buffer->model();
        QObject::connect(model, SIGNAL(channelsChanged(QStringList)), parser, SLOT(setChannels(QStringList)));
        QObject::connect(buffer, SIGNAL(titleChanged(QString)), parser, SLOT(setTarget(QString)));

        parser->setTarget(buffer->title());
        parser->setChannels(buffer->model()->channels());
    } else if (parser) {
        parser->reset();
    }
}

static void unbind(IrcBuffer* buffer, IrcCommandParser* parser)
{
    if (buffer && parser) {
        IrcBufferModel* model = buffer->model();
        QObject::disconnect(model, SIGNAL(channelsChanged(QStringList)), parser, SLOT(setChannels(QStringList)));
        QObject::disconnect(buffer, SIGNAL(titleChanged(QString)), parser, SLOT(setTarget(QString)));
    }
}

void TextInput::setBuffer(IrcBuffer* buffer)
{
    if (d.buffer != buffer) {
        unbind(d.buffer, d.parser);
        bind(buffer, d.parser);
        if (d.buffer)
            d.states.insert(d.buffer, saveState());
        d.buffer = buffer;
        if (buffer)
            restoreState(d.states.value(buffer));
        emit bufferChanged(buffer);
    }
}

void TextInput::setParser(IrcCommandParser* parser)
{
    if (d.parser != parser) {
        unbind(d.buffer, d.parser);
        bind(d.buffer, parser);
        d.parser = parser;
        emit parserChanged(parser);
    }
}

bool TextInput::event(QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        switch (static_cast<QKeyEvent*>(event)->key()) {
        case Qt::Key_Tab:
            tryComplete();
            return true;
        case Qt::Key_Up:
            goBackward();
            return true;
        case Qt::Key_Down:
            goForward();
            return true;
        default:
            break;
        }
    }
    return QLineEdit::event(event);
}

// copied from qlineedit.cpp:
#define vMargin 1
#define hMargin 2

void TextInput::paintEvent(QPaintEvent* event)
{
    QLineEdit::paintEvent(event);

    if (!d.hint.isEmpty()) {
        QStyleOptionFrameV2 option;
        initStyleOption(&option);

        QRect r = style()->subElementRect(QStyle::SE_LineEditContents, &option, this);
        int left, top, right, bottom;
        getTextMargins(&left, &top, &right, &bottom);
        left += qMax(0, -fontMetrics().minLeftBearing());
        r.adjust(left, top, -right, -bottom);
        r.adjust(hMargin, vMargin, -hMargin, -vMargin);

        QString txt = text();
        if (!txt.isEmpty()) {
            if (!txt.endsWith(" "))
                txt += " ";
            r.adjust(fontMetrics().width(txt), 0, 0, 0);
        }

        QPainter painter(this);
        QColor color = palette().text().color();
        color.setAlpha(128);
        painter.setPen(color);

        QString hint = fontMetrics().elidedText(d.hint, Qt::ElideRight, r.width());
        painter.drawText(r, alignment(), hint);
    }
}

void TextInput::updateHint(const QString& text)
{
    QString match;
    QStringList params;
    QStringList suggestions;
    if (d.parser) {
        if (text.startsWith('/')) {
            QStringList words = text.mid(1).split(" ");
            QString command = words.value(0);
            params = words.mid(1);
            foreach (const QString& available, d.parser->commands()) {
                if (!command.compare(available, Qt::CaseInsensitive)) {
                    match = available;
                    break;
                } else if (params.isEmpty() && available.startsWith(command, Qt::CaseInsensitive)) {
                    suggestions += available;
                }
            }
        }
    }

    if (!match.isEmpty()) {
        QStringList syntax = d.parser->syntax(match).split(" ", QString::SkipEmptyParts).mid(1);
        if (!params.isEmpty())
            d.hint = QStringList(syntax.mid(params.count() - 1)).join(" ");
        else
            d.hint = syntax.join(" ");
    } else if (suggestions.isEmpty()) {
        d.hint = text.isEmpty() ? "..." : "";
    } else if (suggestions.count() == 1) {
        d.hint = d.parser->syntax(suggestions.first());
    } else {
        d.hint = suggestions.join(" ");
    }
}

void TextInput::goBackward()
{
    if (!text().isEmpty() && !d.history.contains(text()))
        d.current = text();
    if (d.index > 0)
        setText(d.history.value(--d.index));
}

void TextInput::goForward()
{
    if (d.index < d.history.count())
        setText(d.history.value(++d.index));
    if (text().isEmpty())
        setText(d.current);
}

void TextInput::sendInput()
{
    IrcBuffer* b = buffer();
    IrcCommandParser* p = parser();
    IrcConnection* c = b ? b->connection() : 0;
    if (!c || !p)
        return;

    const QStringList lines = text().split(QRegExp("[\\r\\n]"), QString::SkipEmptyParts);
#if QT_VERSION >= 0x050200
    if (lines.count() > 2) {
        QSettings settings;
        bool warn = settings.value("warn", true).toBool();
        if (warn) {
            QMessageBox msgBox;
            msgBox.setText(tr("The input contains more than two lines."));
            msgBox.setInformativeText(tr("IRC is not a suitable medium for pasting multiple lines of text. Consider using a pastebin site instead.\n\n"
                                         "Do you still want to proceed and send %1 lines of text?\n").arg(lines.count()));
            msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            msgBox.setDefaultButton(QMessageBox::No);
            QCheckBox* checkBox = new QCheckBox(tr("Do not show again"), &msgBox);
            msgBox.setCheckBox(checkBox);
            int res = msgBox.exec();
            settings.setValue("warn", !checkBox->isChecked());
            if (res != QMessageBox::Yes)
                return;
        }
    }
#endif

    if (!text().isEmpty()) {
        d.current.clear();
        d.history.append(text());
        d.index = d.history.count();
    }

    bool error = false;
    foreach (const QString& line, lines) {
        if (!line.trimmed().isEmpty()) {
            IrcCommand* cmd = p->parse(line);
            if (cmd) {
                cmd->setProperty("TextInput", true);
                b->sendCommand(cmd);
                if (cmd->type() == IrcCommand::Message || cmd->type() == IrcCommand::Notice || cmd->type() == IrcCommand::CtcpAction)
                    b->receiveMessage(cmd->toMessage(c->nickName(), c));
            } else {
                error = true;
            }
        }
    }
    if (!error)
        clear();
}

void TextInput::tryComplete()
{
    d.completer->complete(text(), cursorPosition());
}

void TextInput::doComplete(const QString& text, int cursor)
{
    setText(text);
    setCursorPosition(cursor);
}

QByteArray TextInput::saveState() const
{
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out << d.index << d.current << d.history;
    out << text() << cursorPosition() << selectionStart() << selectedText().length();
    return data;
}

void TextInput::restoreState(const QByteArray& state)
{
    QDataStream in(state);
    in >> d.index >> d.current >> d.history;
    QString txt;
    int pos, start, len;
    in >> txt >> pos >> start >> len;
    setText(txt);
    setCursorPosition(pos);
    if (start != -1)
        setSelection(start, len);
}
