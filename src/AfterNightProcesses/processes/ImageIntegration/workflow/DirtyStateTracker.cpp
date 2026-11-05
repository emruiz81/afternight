// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ezequiel Ruiz

#include "stdafx.hpp"
#include "DirtyStateTracker.h"

namespace
{
QString biasId(const GroupKey& key)
{
    return QStringLiteral("Bias|%1").arg(key.toString());
}
QString darkId(const GroupKey& key)
{
    return QStringLiteral("Dark|%1").arg(key.toString());
}
QString flatId(const GroupKey& key)
{
    return QStringLiteral("Flat|%1").arg(key.toString());
}
QString registrationId(const GroupKey& key)
{
    return QStringLiteral("Reg|%1").arg(key.toString());
}
QString integrationId(const IntegrationGroupKey& key)
{
    return QStringLiteral("Int|%1").arg(key.toString());
}
} // namespace

void DirtyStateTracker::markBiasGroupDirty(const GroupKey& key, DirtyReason reason, const QString& details)
{
    markDirty(biasId(key), reason, details);
}

void DirtyStateTracker::markDarkGroupDirty(const GroupKey& key, DirtyReason reason, const QString& details)
{
    markDirty(darkId(key), reason, details);
}

void DirtyStateTracker::markFlatGroupDirty(const GroupKey& key, DirtyReason reason, const QString& details)
{
    markDirty(flatId(key), reason, details);
}

void DirtyStateTracker::markRegistrationGroupDirty(const GroupKey& key, DirtyReason reason, const QString& details)
{
    markDirty(registrationId(key), reason, details);
}

void DirtyStateTracker::markIntegrationGroupDirty(const IntegrationGroupKey& key, DirtyReason reason,
                                                  const QString& details)
{
    markDirty(integrationId(key), reason, details);
}

void DirtyStateTracker::propagateBiasDirtyToConsumers(const GroupKey& biasKey, const std::vector<GroupKey>& darkGroups,
                                                      const std::vector<GroupKey>& flatGroups,
                                                      const std::vector<GroupKey>& registrationGroups)
{
    const QString cause = biasId(biasKey);
    const QString details = QStringLiteral("Bias master changed: %1").arg(biasKey.toString());

    for (const auto& key : darkGroups)
        markDirty(darkId(key), DirtyReason::UpstreamDirty, details, cause);
    for (const auto& key : flatGroups)
        markDirty(flatId(key), DirtyReason::UpstreamDirty, details, cause);
    for (const auto& key : registrationGroups)
        markDirty(registrationId(key), DirtyReason::UpstreamDirty, details, cause);
}

void DirtyStateTracker::propagateDarkDirtyToConsumers(const GroupKey& darkKey, const std::vector<GroupKey>& flatGroups,
                                                      const std::vector<GroupKey>& registrationGroups)
{
    const QString cause = darkId(darkKey);
    const QString details = QStringLiteral("Dark master changed: %1").arg(darkKey.toString());

    for (const auto& key : flatGroups)
        markDirty(flatId(key), DirtyReason::UpstreamDirty, details, cause);
    for (const auto& key : registrationGroups)
        markDirty(registrationId(key), DirtyReason::UpstreamDirty, details, cause);
}

void DirtyStateTracker::propagateFlatDirtyToConsumers(const GroupKey& flatKey,
                                                      const std::vector<GroupKey>& registrationGroups)
{
    const QString cause = flatId(flatKey);
    const QString details = QStringLiteral("Flat master changed: %1").arg(flatKey.toString());

    for (const auto& key : registrationGroups)
        markDirty(registrationId(key), DirtyReason::UpstreamDirty, details, cause);
}

void DirtyStateTracker::propagateRegistrationDirtyToIntegration(
    const GroupKey& registrationKey, const std::vector<IntegrationGroupKey>& integrationGroups)
{
    const QString cause = registrationId(registrationKey);
    const QString details = QStringLiteral("Registration dependency changed: %1").arg(registrationKey.toString());

    for (const auto& key : integrationGroups)
        markDirty(integrationId(key), DirtyReason::UpstreamDirty, details, cause);
}

bool DirtyStateTracker::isBiasGroupDirty(const GroupKey& key) const
{
    return isDirty(biasId(key));
}
bool DirtyStateTracker::isDarkGroupDirty(const GroupKey& key) const
{
    return isDirty(darkId(key));
}
bool DirtyStateTracker::isFlatGroupDirty(const GroupKey& key) const
{
    return isDirty(flatId(key));
}
bool DirtyStateTracker::isRegistrationGroupDirty(const GroupKey& key) const
{
    return isDirty(registrationId(key));
}
bool DirtyStateTracker::isIntegrationGroupDirty(const IntegrationGroupKey& key) const
{
    return isDirty(integrationId(key));
}

std::vector<GroupKey> DirtyStateTracker::dirtyBiasGroups() const
{
    std::vector<GroupKey> keys;
    for (auto it = m_dirtyComponents.constBegin(); it != m_dirtyComponents.constEnd(); ++it)
        if (it.key().startsWith(QStringLiteral("Bias|")))
            keys.emplace_back(it.key().mid(5));
    return keys;
}

std::vector<GroupKey> DirtyStateTracker::dirtyDarkGroups() const
{
    std::vector<GroupKey> keys;
    for (auto it = m_dirtyComponents.constBegin(); it != m_dirtyComponents.constEnd(); ++it)
        if (it.key().startsWith(QStringLiteral("Dark|")))
            keys.emplace_back(it.key().mid(5));
    return keys;
}

std::vector<GroupKey> DirtyStateTracker::dirtyFlatGroups() const
{
    std::vector<GroupKey> keys;
    for (auto it = m_dirtyComponents.constBegin(); it != m_dirtyComponents.constEnd(); ++it)
        if (it.key().startsWith(QStringLiteral("Flat|")))
            keys.emplace_back(it.key().mid(5));
    return keys;
}

std::vector<GroupKey> DirtyStateTracker::dirtyRegistrationGroups() const
{
    std::vector<GroupKey> keys;
    for (auto it = m_dirtyComponents.constBegin(); it != m_dirtyComponents.constEnd(); ++it)
        if (it.key().startsWith(QStringLiteral("Reg|")))
            keys.emplace_back(it.key().mid(4));
    return keys;
}

std::vector<IntegrationGroupKey> DirtyStateTracker::dirtyIntegrationGroups() const
{
    std::vector<IntegrationGroupKey> keys;
    for (auto it = m_dirtyComponents.constBegin(); it != m_dirtyComponents.constEnd(); ++it)
        if (it.key().startsWith(QStringLiteral("Int|")))
            keys.emplace_back(GroupKey{it.key().mid(4)});
    return keys;
}

std::optional<DirtyComponent> DirtyStateTracker::getDirtyDetails(const QString& componentId) const
{
    auto it = m_dirtyComponents.find(componentId);
    if (it != m_dirtyComponents.end())
        return *it;
    return std::nullopt;
}

std::vector<DirtyComponent> DirtyStateTracker::allDirtyComponents() const
{
    std::vector<DirtyComponent> all;
    all.reserve(m_dirtyComponents.size());
    for (const auto& comp : m_dirtyComponents)
        all.push_back(comp);
    return all;
}

void DirtyStateTracker::clear()
{
    m_dirtyComponents.clear();
}

namespace
{
void removeWithPrefix(QMap<QString, DirtyComponent>& map, const QString& prefix)
{
    auto it = map.begin();
    while (it != map.end())
    {
        if (it.key().startsWith(prefix))
        {
            it = map.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
} // namespace

void DirtyStateTracker::clearBiasGroups()
{
    removeWithPrefix(m_dirtyComponents, QStringLiteral("Bias|"));
}
void DirtyStateTracker::clearDarkGroups()
{
    removeWithPrefix(m_dirtyComponents, QStringLiteral("Dark|"));
}
void DirtyStateTracker::clearFlatGroups()
{
    removeWithPrefix(m_dirtyComponents, QStringLiteral("Flat|"));
}
void DirtyStateTracker::clearRegistrationGroups()
{
    removeWithPrefix(m_dirtyComponents, QStringLiteral("Reg|"));
}
void DirtyStateTracker::clearIntegrationGroups()
{
    removeWithPrefix(m_dirtyComponents, QStringLiteral("Int|"));
}

QString DirtyStateTracker::toSummaryString() const
{
    if (m_dirtyComponents.isEmpty())
        return QStringLiteral("No dirty components");

    int bias = 0, dark = 0, flat = 0, reg = 0, integration = 0;
    for (auto it = m_dirtyComponents.constBegin(); it != m_dirtyComponents.constEnd(); ++it)
    {
        if (it.key().startsWith(QStringLiteral("Bias|")))
            ++bias;
        else if (it.key().startsWith(QStringLiteral("Dark|")))
            ++dark;
        else if (it.key().startsWith(QStringLiteral("Flat|")))
            ++flat;
        else if (it.key().startsWith(QStringLiteral("Reg|")))
            ++reg;
        else if (it.key().startsWith(QStringLiteral("Int|")))
            ++integration;
    }

    return QStringLiteral("Dirty: %1 bias, %2 dark, %3 flat, %4 registration, %5 integration groups")
        .arg(bias)
        .arg(dark)
        .arg(flat)
        .arg(reg)
        .arg(integration);
}

void DirtyStateTracker::markDirty(const QString& id, DirtyReason reason, const QString& details,
                                  const std::optional<QString>& upstream)
{
    if (m_dirtyComponents.contains(id))
        return; // Already marked

    DirtyComponent comp;
    comp.componentId = id;
    comp.reason = reason;
    comp.details = details;
    comp.upstreamCause = upstream;
    m_dirtyComponents.insert(id, comp);
}

bool DirtyStateTracker::isDirty(const QString& id) const
{
    return m_dirtyComponents.contains(id);
}
