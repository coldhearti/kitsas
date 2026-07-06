/*
   Copyright (C) 2019 Arto Hyvättinen

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
#include "asiakkaatroute.h"

#include "model/tositevienti.h"

#include <QDate>

AsiakkaatRoute::AsiakkaatRoute(SQLiteModel *model) :
    SQLiteRoute(model, "/asiakkaat")
{

}

QVariant AsiakkaatRoute::get(const QString &/*polku*/, const QUrlQuery &/*urlquery*/)
{
        const QDate tanaan = QDate::currentDate();
        QSqlQuery kysely(db());

        struct Summat { QString nimi; qlonglong summa = 0; qlonglong avoin = 0; qlonglong eraantynyt = 0; };
        QMap<int, Summat> asiakkaat;   // kumppani id -> summat

        // Laskutettu yhteensä per asiakas (myyntisaatavan vastakirjausrivit)
        kysely.exec(QString("SELECT vienti.kumppani, kumppani.nimi, COALESCE(SUM(vienti.debetsnt),0) "
                            "FROM Vienti JOIN Tosite ON vienti.tosite=tosite.id "
                            "LEFT OUTER JOIN Kumppani ON vienti.kumppani=Kumppani.id "
                            "WHERE vienti.tyyppi=%1 AND tosite.tila > 0 AND vienti.kumppani IS NOT NULL "
                            "GROUP BY vienti.kumppani")
                    .arg( TositeVienti::MYYNTI + TositeVienti::VASTAKIRJAUS ));
        while( kysely.next() ) {
            const int kid = kysely.value(0).toInt();
            Summat& s = asiakkaat[kid];
            s.nimi = kysely.value(1).toString();
            s.summa = kysely.value(2).toLongLong();
        }

        // Avoin ja erääntynyt: eritellään erä kerrallaan (jaettu logiikka hoitaa
        // sekä tavalliset että synteettiset negatiiviset erät).
        kysely.exec(QString("SELECT DISTINCT vienti.eraid, vienti.kumppani "
                            "FROM Vienti JOIN Tosite ON vienti.tosite=tosite.id "
                            "WHERE vienti.tyyppi=%1 AND tosite.tila > 0 "
                            "AND vienti.eraid IS NOT NULL AND vienti.kumppani IS NOT NULL")
                    .arg( TositeVienti::MYYNTI + TositeVienti::VASTAKIRJAUS ));
        QList<QPair<int,int>> erat;   // (eraid, kumppani)
        while( kysely.next() )
            erat.append( qMakePair( kysely.value(0).toInt(), kysely.value(1).toInt() ) );

        for( const auto& e : erat ) {
            const EranTila t = eranTila( e.first, tanaan );
            Summat& s = asiakkaat[e.second];
            s.avoin += t.avoinSnt;
            s.eraantynyt += t.eraantynytSnt;
        }

        QVariantList lista;
        for( auto it = asiakkaat.constBegin(); it != asiakkaat.constEnd(); ++it ) {
            const Summat& s = it.value();
            QVariantMap map;
            map.insert("id", it.key());
            map.insert("nimi", s.nimi);
            map.insert("summa", s.summa / 100.0);
            map.insert("avoin", s.avoin / 100.0);
            map.insert("eraantynyt", s.eraantynyt / 100.0);
            lista.append(map);
        }
        return lista;
}
