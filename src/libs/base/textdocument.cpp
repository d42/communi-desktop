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

#include "textdocument.h"
#include "messageformatter.h"
#include <QAbstractTextDocumentLayout>
#include <QTextDocumentFragment>
#include <QTextBlockUserData>
#include <IrcConnection>
#include <QStylePainter>
#include <QApplication>
#include <QStyleOption>
#include <QTextCursor>
#include <QTextBlock>
#include <IrcBuffer>
#include <QPalette>
#include <QPointer>
#include <QPainter>
#include <QFrame>
#include <qmath.h>

static int delay = 1000;

class TextBlockData : public QTextBlockUserData
{
public:
    QString message;
    QDateTime timestamp;
    IrcMessage::Type type;
};

class TextFrame : public QFrame
{
public:
    TextFrame(QWidget* parent = 0) : QFrame(parent)
    {
        setVisible(false);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
    }

    void paintEvent(QPaintEvent*)
    {
        QStyleOption option;
        option.init(this);
        QStylePainter painter(this);
        painter.drawPrimitive(QStyle::PE_Widget, option);
    }
};

class TextHighlight : public TextFrame
{
    Q_OBJECT
public:
    TextHighlight(QWidget* parent = 0) : TextFrame(parent) { }
};

class TextLowlight : public TextFrame
{
    Q_OBJECT
public:
    TextLowlight(QWidget* parent = 0) : TextFrame(parent) { }
};

TextDocument::TextDocument(IrcBuffer* buffer) : QTextDocument(buffer)
{
    qRegisterMetaType<TextDocument*>();

    d.uc = 0;
    d.dirty = -1;
    d.lowlight = -1;
    d.clone = false;
    d.buffer = buffer;
    d.visible = false;

    d.formatter = new MessageFormatter(this);
    d.formatter->setTimeStampFormat(QString());
    d.formatter->setBuffer(buffer);

    setUndoRedoEnabled(false);
    setMaximumBlockCount(1000);

    connect(buffer->connection(), SIGNAL(disconnected()), this, SLOT(lowlight()));
    connect(buffer, SIGNAL(messageReceived(IrcMessage*)), this, SLOT(receiveMessage(IrcMessage*)));
}

QString TextDocument::timeStampFormat() const
{
    return d.timeStampFormat;
}

void TextDocument::setTimeStampFormat(const QString& format)
{
    if (d.timeStampFormat != format) {
        d.timeStampFormat = format;
        rebuild();
    }
}

QString TextDocument::styleSheet() const
{
    return d.css;
}

void TextDocument::setStyleSheet(const QString& css)
{
    if (d.css != css) {
        d.css = css;
        setDefaultStyleSheet(css);
        rebuild();
    }
}

TextDocument* TextDocument::clone()
{
    if (d.dirty > 0)
        flushLines();

    TextDocument* doc = new TextDocument(d.buffer);
    doc->setDefaultStyleSheet(defaultStyleSheet());
    QTextCursor(doc).insertFragment(QTextDocumentFragment(this));
    doc->rootFrame()->setFrameFormat(rootFrame()->frameFormat());

    QTextBlock source = begin();
    while (source.isValid()) {
        QTextBlock target = doc->findBlockByNumber(source.blockNumber());
        if (target.isValid()) {
            TextBlockData* data = static_cast<TextBlockData*>(source.userData());
            if (data)
                target.setUserData(new TextBlockData(*data));
        }
        source = source.next();
    }

    // TODO:
    doc->d.uc = d.uc;
    doc->d.css = d.css;
    doc->d.lowlight = d.lowlight;
    doc->d.buffer = d.buffer;
    doc->d.highlights = d.highlights;
    doc->d.clone = true;

    return doc;
}

bool TextDocument::isClone() const
{
    return d.clone;
}

IrcBuffer* TextDocument::buffer() const
{
    return d.buffer;
}

MessageFormatter* TextDocument::formatter() const
{
    return d.formatter;
}

int TextDocument::totalCount() const
{
    int count = d.lines.count();
    if (!isEmpty())
        count += blockCount();
    return count;
}

bool TextDocument::isVisible() const
{
    return d.visible;
}

void TextDocument::setVisible(bool visible)
{
    if (d.visible != visible) {
        if (visible) {
            if (d.dirty > 0)
                flushLines();
        } else {
            d.uc = 0;
        }
        d.visible = visible;
    }
}

void TextDocument::lowlight(int block)
{
    if (block == -1)
        block = totalCount() - 1;
    if (d.lowlight != block) {
        d.lowlight = block;
        updateBlock(block);
    }
}

void TextDocument::addHighlight(int block)
{
    const int max = totalCount() - 1;
    if (block == -1)
        block = max;
    if (block >= 0 && block <= max) {
        QList<int>::iterator it = qLowerBound(d.highlights.begin(), d.highlights.end(), block);
        d.highlights.insert(it, block);
        updateBlock(block);
    }
}

void TextDocument::removeHighlight(int block)
{
    if (d.highlights.removeOne(block) && block >= 0 && block < totalCount())
        updateBlock(block);
}

void TextDocument::reset()
{
    d.uc = 0;
    d.lowlight = -1;
    d.highlights.clear();
}

void TextDocument::append(const QString& message, const QDateTime& timestamp, IrcMessage::Type type)
{
    if (!message.isEmpty()) {
        TextBlockData* data = new TextBlockData;
        data->timestamp = timestamp;
        data->message = message;
        data->type = type;
        if (d.dirty == 0 || d.visible) {
            QTextCursor cursor(this);
            cursor.beginEditBlock();
            appendLine(cursor, data);
            cursor.endEditBlock();
        } else {
            if (d.dirty <= 0) {
                d.dirty = startTimer(delay);
                delay += 1000;
            }
            d.lines += data;
        }
    }
}

void TextDocument::drawForeground(QPainter* painter, const QRect& bounds)
{
    const int num = blockCount() - d.uc;
    if (num > 0) {
        const QPen oldPen = painter->pen();
        const QBrush oldBrush = painter->brush();
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QPalette().color(QPalette::Mid), 1, Qt::DashLine));
        QTextBlock block = findBlockByNumber(num);
        if (block.isValid()) {
            QRect br = documentLayout()->blockBoundingRect(block).toAlignedRect();
            if (bounds.intersects(br)) {
                QLine line(br.topLeft(), br.topRight());
                line.translate(0, -2);
                painter->drawLine(line);
            }
        }
        painter->setPen(oldPen);
        painter->setBrush(oldBrush);
    }
}

void TextDocument::drawBackground(QPainter* painter, const QRect& bounds)
{
    if (d.highlights.isEmpty() && d.lowlight == -1)
        return;

    const int margin = qCeil(documentMargin());
    const QAbstractTextDocumentLayout* layout = documentLayout();

    static QPointer<TextLowlight> lowlightFrame = 0;
    if (!lowlightFrame)
        lowlightFrame = new TextLowlight(static_cast<QWidget*>(painter->device()));

    static QPointer<TextHighlight> highlightFrame = 0;
    if (!highlightFrame)
        highlightFrame = new TextHighlight(static_cast<QWidget*>(painter->device()));

    if (d.lowlight != -1) {
        const QAbstractTextDocumentLayout* layout = documentLayout();
        const int margin = qCeil(documentMargin());
        const QTextBlock to = findBlockByNumber(d.lowlight);
        if (to.isValid()) {
            QRect br = layout->blockBoundingRect(to).toAlignedRect();
            br.setTop(0);
            if (bounds.intersects(br)) {
                br.adjust(-margin - 1, 0, margin + 1, 2);
                painter->translate(br.topLeft());
                lowlightFrame->setGeometry(br);
                lowlightFrame->render(painter);
                painter->translate(-br.topLeft());
            }
        }
    }

    foreach (int highlight, d.highlights) {
        QTextBlock block = findBlockByNumber(highlight);
        if (block.isValid()) {
            QRect br = layout->blockBoundingRect(block).toAlignedRect();
            if (bounds.intersects(br)) {
                br.adjust(-margin - 1, 0, margin + 1, 2);
                painter->translate(br.topLeft());
                highlightFrame->setGeometry(br);
                highlightFrame->render(painter);
                painter->translate(-br.topLeft());
            }
        }
    }
}

void TextDocument::updateBlock(int number)
{
    if (d.visible) {
        QTextBlock block = findBlockByNumber(number);
        if (block.isValid())
            QMetaObject::invokeMethod(documentLayout(), "updateBlock", Q_ARG(QTextBlock, block));
    }
}

void TextDocument::timerEvent(QTimerEvent* event)
{
    QTextDocument::timerEvent(event);
    if (event->timerId() == d.dirty) {
        delay -= 1000;
        flushLines();
    }
}

void TextDocument::flushLines()
{
    if (!d.lines.isEmpty()) {
        QTextCursor cursor(this);
        cursor.beginEditBlock();
        foreach (TextBlockData* line, d.lines)
            appendLine(cursor, line);
        cursor.endEditBlock();
        d.lines.clear();
    }

    if (d.dirty > 0) {
        killTimer(d.dirty);
        d.dirty = 0;
    }
}

void TextDocument::receiveMessage(IrcMessage* message)
{
    append(d.formatter->formatMessage(message), message->timeStamp(), message->type());
    emit messageReceived(message);

    if (message->type() == IrcMessage::Private || message->type() == IrcMessage::Notice) {
        if (!message->isOwn()) {
            const bool contains = message->property("content").toString().contains(message->connection()->nickName(), Qt::CaseInsensitive);
            if (contains) {
                addHighlight(totalCount() - 1);
                emit messageHighlighted(message);
            } else if (message->property("private").toBool()) {
                emit privateMessageReceived(message);
            }
        }
    }
}

void TextDocument::rebuild()
{
    flushLines();
    QTextBlock block = begin();
    while (block.isValid()) {
        TextBlockData* data = static_cast<TextBlockData*>(block.userData());
        if (data)
            d.lines += new TextBlockData(*data);
        block = block.next();
    }
    clear();
    flushLines();
}

void TextDocument::appendLine(QTextCursor& cursor, TextBlockData* line)
{
    cursor.movePosition(QTextCursor::End);

    if (!isEmpty()) {
        const int count = blockCount();
        const int max = maximumBlockCount();
        const QRectF br = documentLayout()->blockBoundingRect(findBlockByNumber(0));
        cursor.insertBlock();

        if (count >= max) {
            emit lineRemoved(qRound(br.bottom()));

            const int diff = max - count + 1;
            QList<int>::iterator it = d.highlights.begin();
            while (it != d.highlights.end()) {
                *it -= diff;
                if (*it < 0)
                    it = d.highlights.erase(it);
                else
                    ++it;
            }
            d.lowlight -= diff;
        }
    }

    QString message = tr("<span class='timestamp'>%1</span> %2").arg(line->timestamp.time().toString(d.timeStampFormat)).arg(line->message);
    cursor.insertHtml(message);

    QTextBlock block = cursor.block();
    block.setUserData(line);

    QTextBlockFormat format = cursor.blockFormat();
    format.setLineHeight(125, QTextBlockFormat::ProportionalHeight);
    cursor.setBlockFormat(format);

    ++d.uc;
}

#include "textdocument.moc"
