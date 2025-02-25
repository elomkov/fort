#include "connlistmodel.h"

#include <QFont>
#include <QIcon>
#include <QLoggingCategory>

#include <sqlite/dbquery.h>
#include <sqlite/sqlitedb.h>
#include <sqlite/sqlitestmt.h>

#include <appinfo/appinfocache.h>
#include <fortmanager.h>
#include <hostinfo/hostinfocache.h>
#include <log/logentryconn.h>
#include <stat/statconnmanager.h>
#include <util/iconcache.h>
#include <util/ioc/ioccontainer.h>
#include <util/net/netformatutil.h>
#include <util/net/netutil.h>

namespace {

const QLoggingCategory LC("connListModel");

QString formatIpPort(const ip_addr_t ip, quint16 port, bool isIPv6, bool resolveAddress)
{
    QString address = NetFormatUtil::ipToText(ip, isIPv6);
    if (resolveAddress) {
        const QString hostName = IoC<HostInfoCache>()->hostName(address);
        if (!hostName.isEmpty()) {
            address = hostName;
        }
    }
    if (isIPv6) {
        address = '[' + address + ']';
    }
    return address + ':' + QString::number(port);
}

QString reasonIconPath(const ConnRow &connRow)
{
    static const char *const reasonIcons[] = {
        ":/icons/ip.png",
        ":/icons/arrow_refresh_small.png",
        ":/icons/application.png",
        ":/icons/application_double.png",
        ":/icons/lightbulb.png",
        ":/icons/hostname.png",
        ":/icons/ip_class.png",
        ":/icons/script.png",
        ":/icons/script_code.png",
        ":/icons/script_code_red.png",
        ":/icons/help.png",
    };

    if (connRow.reason >= FORT_CONN_REASON_IP_INET
            && connRow.reason <= FORT_CONN_REASON_ASK_LIMIT) {
        const int index = connRow.reason - FORT_CONN_REASON_IP_INET;
        return reasonIcons[index];
    }

    return ":/icons/error.png";
}

QString actionIconPath(const ConnRow &connRow)
{
    return connRow.blocked ? ":/icons/deny.png" : ":/icons/accept.png";
}

QString directionIconPath(const ConnRow &connRow)
{
    return connRow.inbound ? ":/icons/green_down.png" : ":/icons/blue_up.png";
}

QVariant dataDisplayAppName(const ConnRow &connRow, bool /*resolveAddress*/, int /*role*/)
{
    return IoC<AppInfoCache>()->appName(connRow.appPath);
}

QVariant dataDisplayProcessId(const ConnRow &connRow, bool /*resolveAddress*/, int /*role*/)
{
    return connRow.pid;
}

QVariant dataDisplayProtocolName(const ConnRow &connRow, bool /*resolveAddress*/, int /*role*/)
{
    return NetUtil::protocolName(connRow.ipProto);
}

QVariant dataDisplayLocalIpPort(const ConnRow &connRow, bool resolveAddress, int /*role*/)
{
    return formatIpPort(connRow.localIp, connRow.localPort, connRow.isIPv6, resolveAddress);
}

QVariant dataDisplayRemoteIpPort(const ConnRow &connRow, bool resolveAddress, int /*role*/)
{
    return formatIpPort(connRow.remoteIp, connRow.remotePort, connRow.isIPv6, resolveAddress);
}

QVariant dataDisplayDirection(const ConnRow &connRow, bool /*resolveAddress*/, int role)
{
    if (role == Qt::ToolTipRole) {
        return connRow.inbound ? ConnListModel::tr("In") : ConnListModel::tr("Out");
    }

    return {};
}

QVariant dataDisplayAction(const ConnRow &connRow, bool /*resolveAddress*/, int role)
{
    if (role == Qt::ToolTipRole) {
        return connRow.blocked ? ConnListModel::tr("Blocked") : ConnListModel::tr("Allowed");
    }

    return {};
}

QVariant dataDisplayReason(const ConnRow &connRow, bool /*resolveAddress*/, int role)
{
    if (role == Qt::ToolTipRole) {
        return ConnListModel::reasonText(FortConnReason(connRow.reason))
                + (connRow.inherited ? " (" + ConnListModel::tr("Inherited") + ")" : QString());
    }

    return {};
}

QVariant dataDisplayTime(const ConnRow &connRow, bool /*resolveAddress*/, int /*role*/)
{
    return connRow.connTime;
}

using dataDisplay_func = QVariant (*)(const ConnRow &connRow, bool resolveAddress, int role);

static const dataDisplay_func dataDisplay_funcList[] = {
    &dataDisplayAppName,
    &dataDisplayProcessId,
    &dataDisplayProtocolName,
    &dataDisplayLocalIpPort,
    &dataDisplayRemoteIpPort,
    &dataDisplayDirection,
    &dataDisplayAction,
    &dataDisplayReason,
    &dataDisplayTime,
};

}

ConnListModel::ConnListModel(QObject *parent) : TableSqlModel(parent) { }

void ConnListModel::setResolveAddress(bool v)
{
    if (m_resolveAddress != v) {
        m_resolveAddress = v;
        refresh();
    }
}

FortManager *ConnListModel::fortManager() const
{
    return IoC<FortManager>();
}

StatConnManager *ConnListModel::statConnManager() const
{
    return IoC<StatConnManager>();
}

SqliteDb *ConnListModel::sqliteDb() const
{
    return statConnManager()->roSqliteDb();
}

AppInfoCache *ConnListModel::appInfoCache() const
{
    return IoC<AppInfoCache>();
}

HostInfoCache *ConnListModel::hostInfoCache() const
{
    return IoC<HostInfoCache>();
}

void ConnListModel::initialize()
{
    connect(appInfoCache(), &AppInfoCache::cacheChanged, this, &ConnListModel::refresh);
    connect(hostInfoCache(), &HostInfoCache::cacheChanged, this, &ConnListModel::refresh);
    connect(statConnManager(), &StatConnManager::connChanged, this,
            &ConnListModel::updateConnIdRange);

    updateConnIdRange();
}

int ConnListModel::columnCount(const QModelIndex & /*parent*/) const
{
    return 9;
}

QVariant ConnListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};

    switch (role) {
    // Label
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
        return headerDataDisplay(section, role);

    // Icon
    case Qt::DecorationRole:
        return headerDataDecoration(section);
    }

    return {};
}

QVariant ConnListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    switch (role) {
    // Label
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
        return dataDisplay(index, role);

    // Icon
    case Qt::DecorationRole:
        return dataDecoration(index);
    }

    return {};
}

QVariant ConnListModel::headerDataDisplay(int section, int role) const
{
    static const char *const headerTexts[] = {
        QT_TR_NOOP("Program"),
        QT_TR_NOOP("Proc. ID"),
        QT_TR_NOOP("Protocol"),
        QT_TR_NOOP("Local IP and Port"),
        QT_TR_NOOP("Remote IP and Port"),
        nullptr,
        nullptr,
        nullptr,
        QT_TR_NOOP("Time"),
    };

    static const char *const headerTooltips[] = {
        QT_TR_NOOP("Program"),
        QT_TR_NOOP("Process ID"),
        QT_TR_NOOP("Protocol"),
        QT_TR_NOOP("Local IP and Port"),
        QT_TR_NOOP("Remote IP and Port"),
        QT_TR_NOOP("Direction"),
        QT_TR_NOOP("Action"),
        QT_TR_NOOP("Reason"),
        QT_TR_NOOP("Time"),
    };

    if (section >= 0 && section <= 8) {
        const char *const *arr = (role == Qt::ToolTipRole) ? headerTooltips : headerTexts;
        const char *text = arr[section];

        if (text != nullptr) {
            return tr(text);
        }
    }

    return {};
}

QVariant ConnListModel::headerDataDecoration(int section) const
{
    switch (section) {
    case 5:
        return IconCache::icon(":/icons/green_down.png");
    case 6:
        return IconCache::icon(":/icons/accept.png");
    case 7:
        return IconCache::icon(":/icons/help.png");
    }

    return {};
}

QVariant ConnListModel::dataDisplay(const QModelIndex &index, int role) const
{
    const int row = index.row();
    const int column = index.column();

    const auto &connRow = connRowAt(row);
    if (connRow.isNull())
        return {};

    const dataDisplay_func func = dataDisplay_funcList[column];

    return func(connRow, resolveAddress(), role);
}

QVariant ConnListModel::dataDecoration(const QModelIndex &index) const
{
    const int column = index.column();
    const int row = index.row();

    const auto &connRow = connRowAt(row);

    switch (column) {
    case 0:
        return appInfoCache()->appIcon(connRow.appPath);
    case 5:
        return IconCache::icon(directionIconPath(connRow));
    case 6:
        return IconCache::icon(actionIconPath(connRow));
    case 7:
        return IconCache::icon(reasonIconPath(connRow));
    }

    return {};
}

const ConnRow &ConnListModel::connRowAt(int row) const
{
    updateRowCache(row);

    return m_connRow;
}

void ConnListModel::updateConnIdRange()
{
    const qint64 oldIdMin = connIdMin();
    const qint64 oldIdMax = connIdMax();

    qint64 idMin, idMax;
    statConnManager()->getConnIdRange(sqliteDb(), idMin, idMax);

    if (idMin == oldIdMin && idMax == oldIdMax)
        return;

    if (idMax == 0) {
        hostInfoCache()->clear();
    }

    updateConnRows(oldIdMin, oldIdMax, idMin, idMax);
}

bool ConnListModel::updateTableRow(const QVariantHash & /*vars*/, int row) const
{
    const qint64 connId = connIdMin() + row;

    SqliteStmt stmt;
    if (!DbQuery(sqliteDb()).sql(sql()).vars({ connId }).prepareRow(stmt))
        return false;

    m_connRow.connId = stmt.columnInt64(0);
    m_connRow.appId = stmt.columnInt64(1);
    m_connRow.connTime = stmt.columnUnixTime(2);
    m_connRow.pid = stmt.columnInt(3);
    m_connRow.reason = stmt.columnInt(4);
    m_connRow.blocked = stmt.columnBool(5);
    m_connRow.inherited = stmt.columnBool(6);
    m_connRow.inbound = stmt.columnBool(7);
    m_connRow.ipProto = stmt.columnInt(8);
    m_connRow.localPort = stmt.columnInt(9);
    m_connRow.remotePort = stmt.columnInt(10);

    m_connRow.isIPv6 = stmt.columnIsNull(11);
    if (!m_connRow.isIPv6) {
        m_connRow.localIp.v4 = stmt.columnInt(11);
        m_connRow.remoteIp.v4 = stmt.columnInt(12);
    } else {
        m_connRow.localIp.v6 = NetUtil::arrayViewToIp6(stmt.columnBlob(13, /*isView=*/true));
        m_connRow.remoteIp.v6 = NetUtil::arrayViewToIp6(stmt.columnBlob(14, /*isView=*/true));
    }

    m_connRow.appPath = stmt.columnText(15);

    return true;
}

int ConnListModel::doSqlCount() const
{
    return connIdMax() <= 0 ? 0 : int(connIdMax() - connIdMin()) + 1;
}

QString ConnListModel::sqlBase() const
{
    return "SELECT"
           "    t.conn_id,"
           "    t.app_id,"
           "    t.conn_time,"
           "    t.process_id,"
           "    t.reason,"
           "    t.blocked,"
           "    t.inherited,"
           "    t.inbound,"
           "    t.ip_proto,"
           "    t.local_port,"
           "    t.remote_port,"
           "    t.local_ip,"
           "    t.remote_ip,"
           "    t.local_ip6,"
           "    t.remote_ip6,"
           "    a.path"
           "  FROM conn t"
           "    JOIN app a ON a.app_id = t.app_id";
}

QString ConnListModel::sqlWhere() const
{
    return " WHERE t.conn_id = ?1";
}

QString ConnListModel::sqlLimitOffset() const
{
    return QString();
}

void ConnListModel::updateConnRows(qint64 oldIdMin, qint64 oldIdMax, qint64 idMin, qint64 idMax)
{
    const bool isIdMinOut = (idMin < oldIdMin || idMin >= oldIdMax);
    const bool isIdMaxOut = (idMax < oldIdMax || oldIdMax == 0);

    if (isIdMinOut || isIdMaxOut) {
        resetConnRows(idMin, idMax);
        return;
    }

    const int removedCount = idMin - oldIdMin;
    if (removedCount > 0) {
        removeConnRows(idMin, removedCount);
    }

    const int addedCount = idMax - oldIdMax;
    if (addedCount > 0) {
        const int endRow = oldIdMax - idMin + 1;
        insertConnRows(idMax, endRow, addedCount);
    }
}

void ConnListModel::resetConnRows(qint64 idMin, qint64 idMax)
{
    m_connIdMin = idMin;
    m_connIdMax = idMax;
    reset();
}

void ConnListModel::removeConnRows(qint64 idMin, int count)
{
    beginRemoveRows({}, 0, count - 1);
    m_connIdMin = idMin;
    invalidateRowCache();
    endRemoveRows();
}

void ConnListModel::insertConnRows(qint64 idMax, int endRow, int count)
{
    beginInsertRows({}, endRow, endRow + count - 1);
    m_connIdMax = idMax;
    invalidateRowCache();
    endInsertRows();
}

QString ConnListModel::reasonText(FortConnReason reason)
{
    static const char *const reasonTexts[] = {
        QT_TR_NOOP("Internet address"),
        QT_TR_NOOP("Old connection"),
        QT_TR_NOOP("Program's action"),
        QT_TR_NOOP("App. Group"),
        QT_TR_NOOP("Filter Mode"),
        QT_TR_NOOP("LAN only"),
        QT_TR_NOOP("Zone"),
        QT_TR_NOOP("Rule"),
        QT_TR_NOOP("Global Rule before App Rules"),
        QT_TR_NOOP("Global Rule after App Rules"),
        QT_TR_NOOP("Limit of Ask to Connect"),
    };

    if (reason >= FORT_CONN_REASON_IP_INET && reason <= FORT_CONN_REASON_ASK_LIMIT) {
        const int index = reason - FORT_CONN_REASON_IP_INET;
        return tr(reasonTexts[index]);
    }

    return tr("Unknown");
}
