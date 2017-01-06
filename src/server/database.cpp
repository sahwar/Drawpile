/*
   Drawpile - a collaborative drawing program.

   Copyright (C) 2016 Calle Laakkonen

   Drawpile is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Drawpile is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Drawpile.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "database.h"
#include "../shared/util/logger.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>
#include <QUrl>
#include <QRegularExpression>
#include <QDateTime>
#include <QHostAddress>
#include <QJsonObject>
#include <QJsonArray>

namespace server {

struct Database::Private {
	QSqlDatabase db;
};

static bool initDatabase(QSqlDatabase db)
{
	QSqlQuery q(db);

	// Settings key/value table
	if(!q.exec(
		"CREATE TABLE IF NOT EXISTS settings (key PRIMARY KEY, value);"
		))
		return false;

	// Listing server URL whitelist (regular expressions)
	if(!q.exec(
		"CREATE TABLE IF NOT EXISTS listingservers (url);"
		))
		return false;

	// List of serverwide IP address bans
	if(!q.exec(
		"CREATE TABLE IF NOT EXISTS ipbans (ip, subnet, expires, comment, added);"
		))
		return false;

	// Registered user accounts
	if(!q.exec(
		"CREATE TABLE IF NOT EXISTS users ("
			"username PRIMARY KEY," // the username
			"password,"             // hashed password
			"locked,"               // is this username locked/banned
			"flags"                 // comma separated list of extra features (e.g. "mod")
			");"
		))
		return false;

	return true;
}

Database::Database(QObject *parent)
	: ServerConfig(parent), d(new Private)
{
}

Database::~Database()
{
	delete d;
}

bool Database::openFile(const QString &path)
{
	d->db = QSqlDatabase::addDatabase("QSQLITE");
	d->db.setDatabaseName(path);
	if(!d->db.open()) {
		logger::warning() << "Unable to open database:" << path;
		return false;
	}

	if(!initDatabase(d->db)) {
		logger::warning() << "Database initialization failed:" << path;
		return false;
	}

	logger::info() << "Opened configuration database:" << path;
	return true;
}

void Database::setConfigValue(ConfigKey key, const QString &value)
{
	QSqlQuery q(d->db);
	q.prepare("INSERT OR REPLACE INTO settings VALUES (?, ?)");
	q.bindValue(0, key.name);
	q.bindValue(1, value);
	q.exec();
}

QString Database::getConfigValue(const ConfigKey key, bool &found) const
{
	QSqlQuery q(d->db);
	q.prepare("SELECT value FROM settings WHERE key=?");
	q.bindValue(0, key.name);
	q.exec();
	if(q.next()) {
		found = true;
		return q.value(0).toString();
	} else {
		found = false;
		return QString();
	}
}

bool Database::isAllowedAnnouncementUrl(const QUrl &url) const
{
	if(!url.isValid())
		return false;

	// If whitelisting is not enabled, allow all URLs
	if(!getConfigBool(config::AnnounceWhiteList))
		return true;

	const QString urlStr = url.toString();

	QSqlQuery q(d->db);
	q.exec("SELECT url FROM listingservers");
	while(q.next()) {
		const QString serverUrl = q.value(0).toString();
		const QRegularExpression re(serverUrl);
		if(!re.isValid()) {
			logger::warning() << "Invalid listingserver whitelist regular expression:" << serverUrl;
		} else {
			if(re.match(urlStr).hasMatch())
				return true;
		}
	}

	return false;
}

bool Database::isAddressBanned(const QHostAddress &addr) const
{
	QSqlQuery q(d->db);
	q.exec("SELECT ip, subnet FROM ipbans WHERE expires > datetime('now')");

	while(q.next()) {
		const QHostAddress a(q.value(0).toString());
		int subnet = q.value(1).toInt();
		if(subnet==0) {
			switch(a.protocol()) {
			case QAbstractSocket::IPv4Protocol: subnet=32; break;
			case QAbstractSocket::IPv6Protocol: subnet=128; break;
			default: break;
			}
		}

		if(addr.isInSubnet(a, subnet))
			return true;
	}

	return false;
}

static QJsonObject banResultToJson(const QSqlQuery &q)
{
	QJsonObject b;
	b["id"] = q.value(0).toInt();
	b["ip"] = q.value(1).toString();
	b["subnet"] = q.value(2).toInt();
	b["expires"] = q.value(3).toString();
	b["comment"] = q.value(4).toString();
	b["added"] = q.value(5).toString();
	return b;
}

QJsonArray Database::getBanlist() const
{
	QJsonArray result;
	QSqlQuery q(d->db);
	q.exec("SELECT rowid, ip, subnet, expires, comment, added FROM ipbans");

	while(q.next()) {
		result.append(banResultToJson(q));
	}
	return result;
}

QJsonObject Database::addBan(const QHostAddress &ip, int subnet, const QDateTime &expiration, const QString &comment)
{
	QSqlQuery q(d->db);
	q.prepare("SELECT rowid, ip, subnet, expires, comment, added FROM ipbans WHERE ip=? AND subnet=?");
	q.bindValue(0, ip.toString());
	q.bindValue(1, subnet);
	q.exec();
	if(q.next()) {
		// Matching entry already in database
		return banResultToJson(q);
	} else {
		const QString datefmt = "yyyy-MM-dd HH:mm:ss";
		QString datestr = expiration.toString(datefmt);
		QString now = QDateTime::currentDateTime().toString(datefmt);

		q.prepare("INSERT INTO ipbans (ip, subnet, expires, comment, added) VALUES (?, ?, ?, ?, ?)");
		q.bindValue(0, ip.toString());
		q.bindValue(1, subnet);
		q.bindValue(2, datestr);
		q.bindValue(3, comment);
		q.bindValue(4, now);
		q.exec();

		QJsonObject b;
		b["id"] = q.lastInsertId().toInt();
		b["ip"] = ip.toString();
		b["subnet"] = subnet;
		b["expires"] = datestr;
		b["comment"] = comment;
		b["added"] = now;
		return b;
	}
}

bool Database::deleteBan(int entryId)
{
	QSqlQuery q(d->db);
	q.prepare("DELETE FROM ipbans WHERE rowid=?");
	q.bindValue(0, entryId);
	q.exec();
	return q.numRowsAffected()>0;
}

RegisteredUser Database::getUserAccount(const QString &username, const QString &password) const
{
	QSqlQuery q(d->db);
	q.prepare("SELECT password, locked, flags FROM users WHERE username=?");
	q.bindValue(0, username);
	q.exec();
	if(q.next()) {
		const QString passwordHash = q.value(0).toString();
		const int locked = q.value(1).toInt();
		const QStringList flags = q.value(2).toString().split(',',QString::SkipEmptyParts);

		if(locked) {
			return RegisteredUser {
				RegisteredUser::Banned,
				username,
				QStringList()
			};
		}

		// TODO password hashing
		if(passwordHash != password) {
			return RegisteredUser {
				RegisteredUser::BadPass,
				username,
				QStringList()
			};
		}

		return RegisteredUser {
			RegisteredUser::Ok,
			username,
			flags
		};
	} else {
		return RegisteredUser {
			RegisteredUser::NotFound,
			username,
			QStringList()
		};
	}
}

}
