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
#include "myyntilaskutroute.h"

#include "model/tositevienti.h"
#include "model/tosite.h"
#include "model/euro.h"

#include <QJsonDocument>
#include <QDate>
#include <QDebug>

MyyntilaskutRoute::MyyntilaskutRoute(SQLiteModel *model)
    : SQLiteRoute(model, "/myyntilaskut")
{

}



QVariant MyyntilaskutRoute::get(const QString &/*polku*/, const QUrlQuery &urlquery)
{
    // Toistuvat laskut, joiden uusintapaiva on koittanut
    if( urlquery.hasQueryItem("uusittavat"))
        return uusittavat( QDate::fromString( urlquery.queryItemValue("uusittavat"), Qt::ISODate) );

    // Laskutapa on json:n sisällä !
    QString ehdot = " AND ( tosite.tila ";

    if( urlquery.hasQueryItem("luonnos"))
        ehdot.append(QString(" = %1 ").arg( Tosite::LUONNOS ));
    else if( urlquery.hasQueryItem("lahetettava"))
        ehdot.append(QString(" = %1 OR tosite.tila = %2 OR tosite.tila = %3 ").arg( Tosite::VALMISLASKU ).arg(Tosite::LAHETETAAN).arg(Tosite::LAHETYSVIRHE));
    else
        ehdot.append(QString(" >= %1 ").arg( Tosite::KIRJANPIDOSSA ));
    ehdot.append(") ");

    if( urlquery.hasQueryItem("alkupvm"))
        ehdot.append(QString(" AND tosite.laskupvm >= '%1' ")
                       .arg(urlquery.queryItemValue("alkupvm")));
    if( urlquery.hasQueryItem("loppupvm"))
        ehdot.append(QString(" AND tosite.laskupvm <= '%1' ")
                       .arg(urlquery.queryItemValue("loppupvm")));

    if( urlquery.hasQueryItem("eraalkupvm"))
        ehdot.append(QString(" AND tosite.erapvm >= '%1' ")
                       .arg(urlquery.queryItemValue("eraalkupvm")));

    if( urlquery.hasQueryItem("eraloppupvm")) {
        if( urlquery.hasQueryItem("eraantynyt"))
            ehdot.append(QString(" AND tosite.erapvm < '%1' ")
                       .arg(urlquery.queryItemValue("eraloppupvm")));
        else
            ehdot.append(QString(" AND tosite.erapvm <= '%1' ")
                       .arg(urlquery.queryItemValue("eraloppupvm")));
    }


    QSqlQuery kysely( db());
    kysely.exec(sqlKysymys(urlquery, ehdot, false));
    QVariantList lista = resultList(kysely);

    if( urlquery.queryItemValue("avoin") == "maksut") {
        kysely.exec(sqlKysymys(urlquery, ehdot, true));
        lista.append( resultList(kysely) );
    }


    for(int i=0; i < lista.count(); i++) {
        QVariantMap map = lista.at(i).toMap();


        QVariantMap laskumap = map.take("lasku").toMap();
        if( laskumap.contains("laskutapa"))
            map.insert("laskutapa", laskumap.value("laskutapa"));
        if( laskumap.contains("numero"))
            map.insert("numero", laskumap.value("numero").toString());
        if( laskumap.contains("maksutapa"))
            map.insert("maksutapa", laskumap.value("maksutapa"));
        if( laskumap.contains("valvonta"))
            map.insert("valvonta", laskumap.value("valvonta"));

        lista[i] = map;
    }

    // Lisäksi haetaan valvomattomat laskut (Vakioviite ja Valvomaton)
    if( !urlquery.hasQueryItem("avoin") && !urlquery.hasQueryItem("eraantynyt")) {
        QString kysymys = "SELECT Tosite.id, Kumppani.id, Kumppani.nimi, Tosite.json, Tosite.tyyppi "
                   "FROM Tosite LEFT OUTER JOIN Kumppani ON Tosite.kumppani=Kumppani.id "
                  "WHERE Tosite.tyyppi >= 210 AND Tosite.tyyppi <= 219 " + ehdot;
        kysely.exec(kysymys);
        while( kysely.next()) {
            QVariantMap lasku = QJsonDocument::fromJson( kysely.value(3).toByteArray() ).toVariant().toMap().value("lasku").toMap();
            int valvonta = lasku.value("valvonta").toInt();
            if( valvonta == Lasku::VAKIOVIITE || valvonta == Lasku::VALVOMATON) {
                QVariantMap ulos;
                ulos.insert("tosite", kysely.value(0));
                ulos.insert("pvm", lasku.value("pvm"));
                ulos.insert("erapvm", lasku.value("erapvm"));
                ulos.insert("viite", lasku.value("viite"));
                ulos.insert("asiakas", kysely.value(2) );
                ulos.insert("asiakasid", kysely.value(1) );
                ulos.insert("tyyppi", kysely.value(4));
                ulos.insert("laskutapa", lasku.value("laskutapa"));
                ulos.insert("numero", lasku.value("numero"));
                ulos.insert("maksutapa", lasku.value("maksutapa"));
                ulos.insert("valvonta", lasku.value("valvonta"));
                ulos.insert("selite", lasku.value("otsikko"));
                ulos.insert("summa", lasku.value("summa"));
                lista.append(ulos);
            }
        }
    }

    // Synteettiset negatiiviset erät (huoneisto/asiakas): kuukausittaisen
    // laskun JOKAINEN kuukausiveloitus (vastavienti, tyyppi 202) palautetaan
    // omana rivinään omalla eräpäivällään, kuten pilvipalvelussa. Maksut
    // kohdennetaan vanhimmille kuukausille (FIFO), jolloin avoimet ja
    // erääntyneet kuukaudet näkyvät oikein. Otsikon erapvm on tyhjä.
    if( !urlquery.hasQueryItem("luonnos") && !urlquery.hasQueryItem("lahetettava")
        && urlquery.queryItemValue("avoin") != "maksut" ) {

        const bool avoinTab = urlquery.hasQueryItem("avoin");
        const bool eraantynytTab = urlquery.hasQueryItem("eraantynyt");
        QDate tanaan = QDate::currentDate();
        if( urlquery.hasQueryItem("eraloppupvm") )
            tanaan = QDate::fromString(urlquery.queryItemValue("eraloppupvm"), Qt::ISODate);
        const QString saldopvm = urlquery.queryItemValue("saldopvm");
        const QString rajaus = saldopvm.isEmpty() ? QString()
                : QString(" AND Vienti.pvm <= '%1' ").arg(saldopvm);
        QDate alkupvm, loppupvm;
        if( urlquery.hasQueryItem("alkupvm") )
            alkupvm = QDate::fromString(urlquery.queryItemValue("alkupvm"), Qt::ISODate);
        if( urlquery.hasQueryItem("loppupvm") )
            loppupvm = QDate::fromString(urlquery.queryItemValue("loppupvm"), Qt::ISODate);

        // Kaikki synteettisten erien kuukausiveloitukset aikajärjestyksessä.
        // Aikaväliä EI rajata SQL:ssä, jotta maksujen FIFO-kohdennus aiempiin
        // (mahdollisesti näyttövälin ulkopuolisiin) kuukausiin säilyy oikein;
        // näyttörajaus tehdään vasta kohdennuksen jälkeen.
        QString synteesi = QString(
            "SELECT tosite.id AS tosite, tosite.laskupvm AS laskupvm, tosite.viite AS viite, "
            "tosite.json AS json, kumppani.nimi AS asiakas, kumppani.id AS asiakasid, "
            "tosite.tyyppi AS tyyppi, tosite.tunniste AS tunniste, tosite.sarja AS sarja, "
            "tosite.tila AS tila, vienti.pvm AS kkpvm, vienti.eraid AS eraid, "
            "MIN(vienti.tili) AS tili, SUM(COALESCE(vienti.debetsnt,0)) AS velka "
            "FROM tosite JOIN Vienti ON vienti.tosite=tosite.id "
            "LEFT OUTER JOIN Kumppani ON vienti.kumppani=kumppani.id "
            "WHERE vienti.tyyppi=%1 AND vienti.eraid < 0 AND tosite.tila >= %2 %3 "
            "GROUP BY vienti.eraid, tosite.id, vienti.pvm "
            "ORDER BY vienti.eraid, vienti.pvm, tosite.id ")
            .arg( TositeVienti::MYYNTI + TositeVienti::VASTAKIRJAUS )
            .arg( Tosite::KIRJANPIDOSSA )
            .arg( rajaus );

        QSqlQuery synk( db() );
        synk.exec(synteesi);

        int nykyEra = 0;
        bool eraAloitettu = false;
        qlonglong maksukate = 0;   // FIFO: jäljellä oleva kohdentamaton maksukate
        while( synk.next() ) {
            const int eraid = synk.value("eraid").toInt();
            if( !eraAloitettu || eraid != nykyEra ) {
                eraAloitettu = true;
                nykyEra = eraid;
                // Erän maksut yhteensä (saldopäivään asti) FIFO-kohdennukseen.
                maksukate = 0;
                QSqlQuery mk( db() );
                mk.exec(QString("SELECT COALESCE(SUM(kreditsnt),0) FROM Vienti "
                                "JOIN Tosite ON Vienti.tosite=Tosite.id "
                                "WHERE Vienti.eraid=%1 AND Tosite.tila >= %2 %3")
                        .arg(eraid).arg(Tosite::KIRJANPIDOSSA).arg(rajaus));
                if( mk.next() ) maksukate = mk.value(0).toLongLong();
            }

            const qlonglong velka = synk.value("velka").toLongLong();
            const qlonglong kohdennus = qMin( maksukate, velka );  // FIFO vanhimmasta
            maksukate -= kohdennus;
            const qlonglong avoinSnt = velka - kohdennus;

            const QDate kkpvm = synk.value("kkpvm").toDate();

            // Näyttörajaus aikavälille kuukauden eräpäivän mukaan.
            if( alkupvm.isValid() && kkpvm < alkupvm ) continue;
            if( loppupvm.isValid() && kkpvm > loppupvm ) continue;

            // Välilehtien (Avoimet / Erääntyneet) suodatus per kuukausi.
            if( eraantynytTab ) {
                if( kkpvm >= tanaan || avoinSnt <= 0 ) continue;
            } else if( avoinTab ) {
                if( avoinSnt <= 0 ) continue;
            }

            const QVariantMap lasku = QJsonDocument::fromJson( synk.value("json").toByteArray() )
                                        .toVariant().toMap().value("lasku").toMap();

            QVariantMap ulos;
            ulos.insert("tosite", synk.value("tosite"));
            ulos.insert("pvm", kkpvm);
            ulos.insert("erapvm", kkpvm);
            ulos.insert("viite", synk.value("viite"));
            ulos.insert("summa", Euro(velka).toString());
            ulos.insert("avoin", Euro(avoinSnt).toString());
            ulos.insert("asiakas", synk.value("asiakas"));
            ulos.insert("asiakasid", synk.value("asiakasid"));
            ulos.insert("eraid", eraid);
            ulos.insert("tili", synk.value("tili"));
            ulos.insert("tyyppi", synk.value("tyyppi"));
            ulos.insert("tunniste", synk.value("tunniste"));
            ulos.insert("sarja", synk.value("sarja"));
            ulos.insert("tila", synk.value("tila"));
            ulos.insert("tositepvm", kkpvm);
            ulos.insert("selite", lasku.value("otsikko"));
            ulos.insert("laskutapa", lasku.value("laskutapa"));
            ulos.insert("numero", lasku.value("numero"));
            ulos.insert("maksutapa", lasku.value("maksutapa"));
            ulos.insert("valvonta", lasku.value("valvonta"));
            lista.append(ulos);
        }
    }

    return lista;
}

QVariant MyyntilaskutRoute::uusittavat(const QDate &pvm)
{
    QVariantList lista;
    QSqlQuery kysely( db());
    // Esisuodatus SQL:llä; toistoehto luetaan json:sta (kuten muuallakin
    // tassa reitissa), jottei olla riippuvaisia SQLiten json-laajennoksesta.
    kysely.exec("SELECT id, json FROM Tosite WHERE tyyppi >= 210 AND tyyppi <= 219 AND tila >= "
                + QString::number(Tosite::VALMISLASKU));
    while( kysely.next()) {
        const QVariantMap lasku = QJsonDocument::fromJson( kysely.value(1).toByteArray() )
                                    .toVariant().toMap().value("lasku").toMap();
        const QVariantMap toisto = lasku.value("toisto").toMap();
        if( toisto.isEmpty())
            continue;
        const QDate toistoPvm = toisto.value("pvm").toDate();
        if( !toistoPvm.isValid() || toistoPvm > pvm)
            continue;
        QVariantMap ulos;
        ulos.insert("id", kysely.value(0).toInt());
        lista.append(ulos);
    }
    return lista;
}

QString MyyntilaskutRoute::sqlKysymys(const QUrlQuery &urlquery, const QString &ehdot, bool hyvitys) const
{

    QString kysymys("select tosite.id as tosite, tosite.laskupvm as pvm, tosite.erapvm as erapvm, tosite.viite, tosite.json as json, "
                        "COALESCE(debetsnt,0) - COALESCE(kreditsnt,0) AS summasnt, avoinsnt, kumppani.nimi as asiakas, kumppani.id as asiakasid, vienti.eraid as eraid, vienti.tili as tili,"
                        "tosite.tyyppi as tyyppi, vienti.selite as selite, tosite.tunniste as tunniste, tosite.sarja as sarja, tosite.tila as tila, tosite.pvm as tositepvm  "
                        "FROM tosite JOIN Vienti ON vienti.tosite=tosite.id ");

    if( !urlquery.hasQueryItem("avoin") && !urlquery.hasQueryItem("eraantynyt"))
        kysymys.append("LEFT OUTER ");

    kysymys.append("JOIN (select eraid,  COALESCE(SUM(debetsnt),0) - COALESCE(SUM(kreditsnt),0) AS avoinsnt FROM Vienti JOIN Tosite ON Vienti.tosite=Tosite.id WHERE Tosite.tila >= 100 ");

    if( urlquery.hasQueryItem("saldopvm"))
        kysymys.append(QString(" AND Vienti.pvm <= '%1' ").arg(urlquery.queryItemValue("saldopvm")));

    kysymys.append(" GROUP BY eraid ");
    if( urlquery.hasQueryItem("avoin") || urlquery.hasQueryItem("eraantynyt"))
        kysymys.append(QString(" HAVING COALESCE(SUM(kreditsnt),0) %1 COALESCE(SUM(debetsnt),0) ")
                .arg( urlquery.queryItemValue("avoin")=="maksut" ? "<" : "<>" ));

    kysymys.append(QString(") as q ON vienti.eraid=q.eraid LEFT OUTER JOIN "
            "Kumppani ON vienti.kumppani=kumppani.id WHERE vienti.tyyppi = %1")
            .arg( hyvitys ? TositeVienti::OSTO + TositeVienti::VASTAKIRJAUS : TositeVienti::MYYNTI + TositeVienti::VASTAKIRJAUS) );

    // Synteettiset negatiiviset erät (huoneisto/asiakas) käsitellään erikseen
    // yhtenä rivinä per lasku, joten ne suljetaan pois täältä (muuten yksi
    // kuukausittainen lasku toistuisi 12 rivillä).
    kysymys.append(" AND vienti.eraid > 0 ");


    kysymys.append(ehdot);

    if( urlquery.hasQueryItem("kitsaslaskut"))
        kysymys.append(" AND tosite.tyyppi >= 210 AND tosite.tyyppi <= 219 ");
    if( urlquery.queryItemValue("avoin") == "myynnit")
        kysymys.append(" AND Vienti.eraid=Vienti.id ");

    kysymys.append(" ORDER BY tosite.laskupvm, tosite.viite");

    return kysymys;
}
