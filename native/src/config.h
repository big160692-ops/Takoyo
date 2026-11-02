#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

class Config {
public:
    Config();

    QJsonValue value(const QString &key) const;
    QString str(const QString &key) const;
    int integer(const QString &key) const;
    double number(const QString &key) const;
    bool flag(const QString &key) const;

    void set(const QString &key, const QJsonValue &value, bool save = true);
    void save();

    QString path() const { return path_; }

    QString modelFor(const QString &provider) const;
    QString apiKey(const QString &provider) const;
    QString providerLabel(const QString &provider) const;

private:
    QString path_;
    QJsonObject data_;
};
