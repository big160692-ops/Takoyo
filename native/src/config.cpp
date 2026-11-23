#include "config.h"

#include "util.h"

#include <QJsonArray>
#include <QRandomGenerator>

namespace {
QJsonObject defaults()
{
    QJsonObject o;
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("provider"), QStringLiteral("local"));
    o.insert(QStringLiteral("local_model"), QStringLiteral("qwen2.5-1.5b"));
    o.insert(QStringLiteral("openai_model"), QStringLiteral("gpt-4o-mini"));
    o.insert(QStringLiteral("anthropic_model"), QStringLiteral("claude-sonnet-4-5"));
    o.insert(QStringLiteral("google_model"), QStringLiteral("gemini-2.5-flash"));
    o.insert(QStringLiteral("ollama_model"), QStringLiteral("qwen2.5-coder:1.5b"));
    o.insert(QStringLiteral("openai_api_key"), QString());
    o.insert(QStringLiteral("anthropic_api_key"), QString());
    o.insert(QStringLiteral("google_api_key"), QString());
    o.insert(QStringLiteral("openai_base_url"), QStringLiteral("https://api.openai.com/v1"));
    o.insert(QStringLiteral("ollama_base_url"), QStringLiteral("http://127.0.0.1:11434"));
    o.insert(QStringLiteral("temperature"), 0.7);
    o.insert(QStringLiteral("max_tokens"), 2048);
    o.insert(QStringLiteral("image_provider"), QStringLiteral("takoyo"));
    o.insert(QStringLiteral("image_size"), QStringLiteral("1024x1280"));
    o.insert(QStringLiteral("palette"), QStringLiteral("dark"));
    o.insert(QStringLiteral("allow_code_execution"), true);
    o.insert(QStringLiteral("auto_apply_edits"), false);
    o.insert(QStringLiteral("api_host"), QStringLiteral("127.0.0.1"));
    o.insert(QStringLiteral("api_port"), 8765);
    o.insert(QStringLiteral("api_token"), QString());
    o.insert(QStringLiteral("api_allowed_origins"), QJsonArray());
    o.insert(QStringLiteral("api_auto_start"), false);
    o.insert(QStringLiteral("local_port"), 8766);
    o.insert(QStringLiteral("sidebar_open"), false);
    o.insert(QStringLiteral("window_geometry"), QStringLiteral("1280x800"));
    o.insert(QStringLiteral("last_conversation"), QString());
    o.insert(QStringLiteral("last_project"), QString());
    o.insert(QStringLiteral("working_folder"), QString());
    o.insert(QStringLiteral("voice_enabled"), true);
    return o;
}

QString defaultModelKey(const QString &provider)
{
    return provider + QStringLiteral("_model");
}
} // namespace

Config::Config()
    : path_(util::configPath())
{
    data_ = defaults();
    const QJsonObject stored = util::readJson(path_);
    for (auto it = stored.begin(); it != stored.end(); ++it)
        data_.insert(it.key(), it.value());
    if (data_.value(QStringLiteral("api_token")).toString().isEmpty()) {
        QString token;
        for (int i = 0; i < 4; ++i)
            token += QString::number(QRandomGenerator::system()->generate64(), 16).rightJustified(16, QChar('0'));
        data_.insert(QStringLiteral("api_token"), token);
        save();
    }
}

QJsonValue Config::value(const QString &key) const
{
    return data_.value(key);
}

QString Config::str(const QString &key) const
{
    return data_.value(key).toString();
}

int Config::integer(const QString &key) const
{
    return data_.value(key).toInt();
}

double Config::number(const QString &key) const
{
    return data_.value(key).toDouble();
}

bool Config::flag(const QString &key) const
{
    return data_.value(key).toBool();
}

void Config::set(const QString &key, const QJsonValue &value, bool saveNow)
{
    data_.insert(key, value);
    if (saveNow)
        save();
}

void Config::save()
{
    util::writeJsonAtomic(path_, data_);
}

QString Config::modelFor(const QString &provider) const
{
    return data_.value(defaultModelKey(provider)).toString();
}

QString Config::apiKey(const QString &provider) const
{
    if (provider == QLatin1String("local") || provider == QLatin1String("ollama"))
        return QStringLiteral("local");
    return data_.value(provider + QStringLiteral("_api_key")).toString();
}

QString Config::providerLabel(const QString &provider) const
{
    if (provider == QLatin1String("local"))
        return QStringLiteral("Built-in AI");
    if (provider == QLatin1String("openai"))
        return QStringLiteral("OpenAI");
    if (provider == QLatin1String("anthropic"))
        return QStringLiteral("Anthropic");
    if (provider == QLatin1String("google"))
        return QStringLiteral("Google");
    if (provider == QLatin1String("ollama"))
        return QStringLiteral("Ollama");
    return provider.left(1).toUpper() + provider.mid(1);
}
