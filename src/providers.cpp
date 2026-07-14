#include "providers.h"

const QList<ProviderInfo> &providerCatalog()
{
    static const QList<ProviderInfo> catalog{
        {"codex", "Codex", "oauth", "5-hour limit", "Weekly", "Additional",
         ":/assets/provider-codex.svg", 0x49A3B0,
         "https://chatgpt.com/codex/settings/usage", "https://status.openai.com",
         "https://chatgpt.com/#settings/Account"},
        {"claude", "Claude", "oauth", "Session", "Weekly", "Additional",
         ":/assets/provider-claude.svg", 0xCC7C5E,
         "https://claude.ai/settings/usage", "https://status.claude.com",
         "https://claude.ai/settings/profile"},
        {"cursor", "Cursor", "web", "Total", "Auto", "API",
         ":/assets/provider-cursor.svg", 0x00BFA5,
         "https://cursor.com/dashboard?tab=usage", "https://status.cursor.com",
         "https://cursor.com/settings"},
        {"factory", "Droid", "api", "Standard", "Premium", "Additional",
         ":/assets/provider-factory.svg", 0xFF6B35,
         "https://app.factory.ai/settings/billing", "https://status.factory.ai",
         "https://app.factory.ai/settings"},
        {"gemini", "Gemini", "api", "Pro", "Flash", "Flash Lite",
         ":/assets/provider-gemini.svg", 0xAB87EA,
         "https://gemini.google.com",
         "https://www.google.com/appsstatus/dashboard/products/npdyhgECDJ6tB66MxXyo/history",
         "https://myaccount.google.com"},
        {"copilot", "Copilot", "api", "Premium", "Chat", "Additional",
         ":/assets/provider-copilot.svg", 0xA855F7,
         "https://github.com/settings/copilot", "https://www.githubstatus.com/",
         "https://github.com/settings/copilot"},
    };
    return catalog;
}

const ProviderInfo *providerInfo(const QString &id)
{
    for (const auto &provider : providerCatalog()) {
        if (provider.id == id)
            return &provider;
    }
    return nullptr;
}
