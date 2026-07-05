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
#ifndef HUONEISTOROUTE_H
#define HUONEISTOROUTE_H

#include "../sqliteroute.h"

/**
 * @brief Local SQLite backend for apartment (huoneisto) tracking.
 *
 * Mirrors the cloud /huoneistot endpoints so the desktop client works
 * unchanged on a local .kitsas file:
 *   GET    /huoneistot        list (with laskutettu/maksettu balances)
 *   GET    /huoneistot/{id}   single (with muistiinpanot + laskutus config)
 *   POST   /huoneistot        create
 *   PUT    /huoneistot/{id}   update
 *   DELETE /huoneistot/{id}   delete
 *
 * Apartment balances use the synthetic negative era id defined in
 * ViiteNumero::eraId(): era = -(huoneistoId * 10 + ViiteNumero::HUONEISTO).
 * Invoices post debit, payments post credit, both keyed to that era, so a
 * running per-apartment balance falls out of a simple SUM.
 */
class HuoneistoRoute : public SQLiteRoute
{
public:
    HuoneistoRoute(SQLiteModel *model);

    QVariant get(const QString &polku, const QUrlQuery &urlquery = QUrlQuery()) override;
    QVariant post(const QString &polku, const QVariant &data) override;
    QVariant put(const QString &polku, const QVariant &data) override;
    QVariant doDelete(const QString &polku) override;

private:
    // Synthetic era id for an apartment row id (matches ViiteNumero::eraId()).
    static int eraId(int huoneistoId);
    // Fills "laskutettu" and "maksettu" (decimal euro strings) for one apartment.
    void taytaSaldot(QVariantMap &map, int huoneistoId);
};

#endif // HUONEISTOROUTE_H
