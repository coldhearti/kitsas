/*
   Copyright (C) 2019 Arto Hyvättinen
   Local huoneisto (apartment) support added in fork.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include "huoneistoroute.h"
#include "laskutus/viitenumero.h"

#include <QJsonDocument>
#include <QJsonObject>

HuoneistoRoute::HuoneistoRoute(SQLiteModel *model) :
    SQLiteRoute(model, "/huoneistot")
{

}

int HuoneistoRoute::eraId(int huoneistoId)
{
    // Matches ViiteNumero::eraId(): -(base*10 + tyyppi), tyyppi HUONEISTO == 4.
    return 0 - ( huoneistoId * 10 + ViiteNumero::HUONEISTO );
}

void HuoneistoRoute::taytaSaldot(QVariantMap &map, int huoneistoId)
{
    QSqlQuery kysely(db());
    kysely.exec(QString(
        "SELECT COALESCE(SUM(v.debetsnt),0), COALESCE(SUM(v.kreditsnt),0) "
        "FROM Vienti v JOIN Tosite t ON v.tosite=t.id "
        "WHERE v.eraid=%1 AND t.tila>=100").arg( eraId(huoneistoId) ));
    if( kysely.next() ) {
        const double laskutettu = kysely.value(0).toLongLong() / 100.0;
        const double maksettu   = kysely.value(1).toLongLong() / 100.0;
        map.insert("laskutettu", QString::number(laskutettu, 'f', 2));
        map.insert("maksettu",   QString::number(maksettu, 'f', 2));
    }

    // Avoin saldo, laskennallinen eräpäivä ja erääntynyt osuus (jaettu logiikka).
    const EranTila t = eranTila( eraId(huoneistoId), QDate::currentDate() );
    map.insert("avoin", t.avoinSnt / 100.0);
    map.insert("eraantynyt", t.eraantynytSnt / 100.0);
    if( t.erapvm.isValid() )
        map.insert("erapvm", t.erapvm);
}

QVariant HuoneistoRoute::get(const QString &polku, const QUrlQuery &/*urlquery*/)
{
    QSqlQuery kysely(db());

    if( polku.isEmpty() ) {
        // List view: id, nimi, asiakas + per-apartment invoiced/paid totals.
        kysely.exec("SELECT id, asiakas, nimi FROM Huoneisto ORDER BY id");
        QVariantList lista;
        while( kysely.next() ) {
            QVariantMap map;
            const int id = kysely.value("id").toInt();
            map.insert("id", id);
            map.insert("nimi", kysely.value("nimi"));
            if( kysely.value("asiakas").toInt() )
                map.insert("asiakas", kysely.value("asiakas"));
            taytaSaldot(map, id);
            lista.append(map);
        }
        return lista;
    }

    // Single apartment (with muistiinpanot + laskutus config from json).
    const int id = polku.toInt();
    kysely.exec(QString("SELECT id, asiakas, nimi, json FROM Huoneisto WHERE id=%1").arg(id));
    if( !kysely.next() )
        return QVariantMap();

    QVariantMap map;
    map.insert("id", kysely.value("id").toInt());
    map.insert("nimi", kysely.value("nimi"));
    if( kysely.value("asiakas").toInt() )
        map.insert("asiakas", kysely.value("asiakas"));

    const QByteArray json = kysely.value("json").toByteArray();
    if( !json.isEmpty() ) {
        const QVariantMap extra = QJsonDocument::fromJson(json).object().toVariantMap();
        if( extra.contains("muistiinpanot") )
            map.insert("muistiinpanot", extra.value("muistiinpanot"));
        if( extra.contains("laskutus") )
            map.insert("laskutus", extra.value("laskutus"));
    }
    taytaSaldot(map, id);
    return map;
}

QVariant HuoneistoRoute::post(const QString &/*polku*/, const QVariant &data)
{
    QVariantMap map = data.toMap();

    // nimi + asiakas live in columns; everything else (muistiinpanot, laskutus) in json.
    QVariantMap extra(map);
    extra.remove("id");
    extra.remove("nimi");
    extra.remove("asiakas");

    QSqlQuery kysely(db());
    kysely.prepare("INSERT INTO Huoneisto(asiakas, nimi, json) VALUES(?,?,?)");
    kysely.addBindValue( map.value("asiakas").toInt() ? map.value("asiakas") : QVariant() );
    kysely.addBindValue( map.value("nimi") );
    kysely.addBindValue( mapToJson(extra) );

    if( !kysely.exec() )
        throw SQLiteVirhe(kysely);

    map.insert("id", kysely.lastInsertId().toInt());
    return map;
}

QVariant HuoneistoRoute::put(const QString &polku, const QVariant &data)
{
    const int id = polku.toInt();
    QVariantMap map = data.toMap();

    QVariantMap extra(map);
    extra.remove("id");
    extra.remove("nimi");
    extra.remove("asiakas");

    QSqlQuery kysely(db());
    kysely.prepare("UPDATE Huoneisto SET asiakas=?, nimi=?, json=? WHERE id=?");
    kysely.addBindValue( map.value("asiakas").toInt() ? map.value("asiakas") : QVariant() );
    kysely.addBindValue( map.value("nimi") );
    kysely.addBindValue( mapToJson(extra) );
    kysely.addBindValue( id );

    if( !kysely.exec() )
        throw SQLiteVirhe(kysely);

    map.insert("id", id);
    return map;
}

QVariant HuoneistoRoute::doDelete(const QString &polku)
{
    QSqlQuery kysely(db());
    kysely.exec(QString("DELETE FROM Huoneisto WHERE id=%1").arg(polku.toInt()));
    return QVariant();
}
